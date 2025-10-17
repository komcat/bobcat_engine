// MotionGraphicsView.cpp
#include "MotionGraphicsView.h"
#include <QMouseEvent>
#include <QScrollBar>
#include <QtMath>
#include <QPainter>

// MotionGraphicsView.cpp - Add to constructor
MotionGraphicsView::MotionGraphicsView(QWidget* parent)
  : QGraphicsView(parent)
  , m_scene(nullptr)
  , m_liveMarker(nullptr)
  , m_zoomFactor(1.0)
  , m_zMin(0.0)
  , m_zMax(0.0)
  , m_isPanning(false)
{
  setupScene();

  // Enable antialiasing for smooth graphics
  setRenderHint(QPainter::Antialiasing);
  setRenderHint(QPainter::SmoothPixmapTransform);

  // Set drag mode for panning
  setDragMode(QGraphicsView::NoDrag);

  // Set background color (Veritis dark theme)
  setBackgroundBrush(QBrush(QColor(26, 31, 46)));

  // Set viewport update mode for better performance
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

  // Enable custom background drawing
  setCacheMode(QGraphicsView::CacheBackground);

  // NEW - Disable scroll bar auto-range to allow infinite panning
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  // NEW - Allow panning beyond scene boundaries
  setSceneRect(-10000, -10000, 20000, 20000); // Large but finite rect

  // Create live marker (hidden by default)
  m_liveMarker = new DevicePositionMarker("");
  m_scene->addItem(m_liveMarker);
  m_liveMarker->setVisible(false);
}


void MotionGraphicsView::setupScene()
{
  m_scene = new QGraphicsScene(this);

  // NEW - Don't set a fixed scene rect, make it infinite
  // The scene will automatically expand as needed
  // m_scene->setSceneRect(-200, -200, 400, 400); // REMOVE THIS LINE

  setScene(m_scene);

  // Connect selection signal
  connect(m_scene, &QGraphicsScene::selectionChanged,
    this, &MotionGraphicsView::onSelectionChanged);
}


void MotionGraphicsView::drawBackground(QPainter* painter, const QRectF& rect)
{
  // Draw dark background
  painter->fillRect(rect, QColor(26, 31, 46));

  // Draw grid
  drawGrid(painter, rect);
}

void MotionGraphicsView::drawGrid(QPainter* painter, const QRectF& rect)
{
  // Calculate appropriate grid spacing based on zoom level
  qreal viewScale = transform().m11();
  double gridSpacing = calculateGridSpacing(viewScale);

  // Set grid pen
  QPen gridPen(QColor(60, 70, 90), 1);
  gridPen.setCosmetic(true); // Keep line width constant regardless of zoom
  painter->setPen(gridPen);

  // Calculate grid boundaries
  double left = std::floor(rect.left() / gridSpacing) * gridSpacing;
  double right = std::ceil(rect.right() / gridSpacing) * gridSpacing;
  double top = std::floor(rect.top() / gridSpacing) * gridSpacing;
  double bottom = std::ceil(rect.bottom() / gridSpacing) * gridSpacing;

  // Draw vertical lines
  for (double x = left; x <= right; x += gridSpacing) {
    painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
  }

  // Draw horizontal lines
  for (double y = top; y <= bottom; y += gridSpacing) {
    painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
  }
}

double MotionGraphicsView::calculateGridSpacing(double viewScale)
{
  // Base grid spacing in scene units
  double baseSpacing = 10.0;

  // Calculate how many pixels one grid unit takes up
  double pixelsPerUnit = baseSpacing * viewScale;

  // Adjust grid spacing to keep visual density reasonable
  // We want roughly 20-50 pixels between grid lines

  if (pixelsPerUnit < 20) {
    // Zoomed out too far - increase spacing
    while (pixelsPerUnit < 20 && baseSpacing < 1000) {
      baseSpacing *= 2;
      pixelsPerUnit = baseSpacing * viewScale;
    }
  }
  else if (pixelsPerUnit > 100) {
    // Zoomed in too far - decrease spacing
    while (pixelsPerUnit > 100 && baseSpacing > 0.1) {
      baseSpacing /= 2;
      pixelsPerUnit = baseSpacing * viewScale;
    }
  }

  return baseSpacing;
}

void MotionGraphicsView::addPosition(const QString& name, double x, double y, double z)
{
  // Check if position already exists
  if (m_dots.contains(name)) {
    return;
  }

  // Create new dot
  PositionDot* dot = new PositionDot(name, x, y, z, m_zMin, m_zMax);
  m_scene->addItem(dot);
  m_dots[name] = dot;

  // Recalculate Z range and update colors
  calculateZRange();
  updateDotColors();
}

void MotionGraphicsView::clearPositions()
{
  // Remove all dots
  for (auto* dot : m_dots) {
    m_scene->removeItem(dot);
    delete dot;
  }
  m_dots.clear();
}

void MotionGraphicsView::updatePositions(const QMap<QString, QVector<double>>& positions)
{
  clearPositions();

  // Add all positions
  for (auto it = positions.begin(); it != positions.end(); ++it) {
    const QString& name = it.key();
    const QVector<double>& pos = it.value();

    if (pos.size() >= 3) {
      addPosition(name, pos[0], pos[1], pos[2]);
    }
  }

  // Fit view to show all positions
  if (!m_dots.isEmpty()) {
    fitInView(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    m_zoomFactor = transform().m11();
  }
}

void MotionGraphicsView::calculateZRange()
{
  if (m_dots.isEmpty()) {
    m_zMin = 0.0;
    m_zMax = 0.0;
    return;
  }

  m_zMin = m_dots.first()->getZ();
  m_zMax = m_zMin;

  for (auto* dot : m_dots) {
    double z = dot->getZ();
    if (z < m_zMin) m_zMin = z;
    if (z > m_zMax) m_zMax = z;
  }
}

void MotionGraphicsView::updateDotColors()
{
  for (auto* dot : m_dots) {
    dot->updateColor(m_zMin, m_zMax);
  }
}

void MotionGraphicsView::zoomIn()
{
  scale(1.2, 1.2);
  m_zoomFactor *= 1.2;
  viewport()->update(); // Trigger grid redraw
}

void MotionGraphicsView::zoomOut()
{
  scale(1.0 / 1.2, 1.0 / 1.2);
  m_zoomFactor /= 1.2;
  viewport()->update(); // Trigger grid redraw
}

void MotionGraphicsView::resetZoom()
{
  resetTransform();
  m_zoomFactor = 1.0;

  if (!m_dots.isEmpty()) {
    fitInView(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    m_zoomFactor = transform().m11();
  }

  viewport()->update(); // Trigger grid redraw
}


// MotionGraphicsView.cpp - Replace the wheelEvent method

void MotionGraphicsView::wheelEvent(QWheelEvent* event)
{
  // Get zoom direction
  bool zoomIn = event->angleDelta().y() > 0;
  double scaleFactor = zoomIn ? 1.2 : (1.0 / 1.2);

  // Get the position of the mouse in scene coordinates BEFORE zoom
  QPointF oldPos = mapToScene(event->position().toPoint());

  // Apply zoom
  scale(scaleFactor, scaleFactor);
  m_zoomFactor *= scaleFactor;

  // Get the position of the mouse in scene coordinates AFTER zoom
  QPointF newPos = mapToScene(event->position().toPoint());

  // Calculate the difference and adjust the view
  QPointF delta = newPos - oldPos;
  translate(delta.x(), delta.y());

  viewport()->update(); // Trigger grid redraw
  event->accept();
}


void MotionGraphicsView::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::MiddleButton) {
    // Start panning
    m_isPanning = true;
    m_lastPanPoint = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
  }
  else {
    QGraphicsView::mousePressEvent(event);
  }
}

void MotionGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::MiddleButton) {
    // Stop panning
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    event->accept();
  }
  else {
    QGraphicsView::mouseReleaseEvent(event);
  }
}

void MotionGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
  if (m_isPanning) {
    // Pan the view
    QPoint delta = event->pos() - m_lastPanPoint;
    m_lastPanPoint = event->pos();

    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());

    event->accept();
  }
  else {
    QGraphicsView::mouseMoveEvent(event);
  }
}

void MotionGraphicsView::onSelectionChanged()
{
  QList<QGraphicsItem*> selected = m_scene->selectedItems();

  if (selected.isEmpty()) {
    emit positionDeselected();
    return;
  }

  // Get first selected item
  PositionDot* dot = dynamic_cast<PositionDot*>(selected.first());
  if (dot) {
    emit positionSelected(dot->getName(), dot->getX(), dot->getY(), dot->getZ());
  }
}


// NEW - Add these methods at the end of the file
void MotionGraphicsView::updateLiveMarker(const QString& deviceName, double x, double y)
{
  if (m_liveMarker) {
    m_liveMarker->setDeviceName(deviceName);
    m_liveMarker->updatePosition(x, y);
    m_liveMarker->setVisible(true);
  }
}

void MotionGraphicsView::showLiveMarker(bool show)
{
  if (m_liveMarker) {
    m_liveMarker->setVisible(show);
  }
}
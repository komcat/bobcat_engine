// MotionGraphicsView.cpp
#include "MotionGraphicsView.h"
#include <QMouseEvent>
#include <QScrollBar>
#include <QtMath>
#include <QPainter>

MotionGraphicsView::MotionGraphicsView(QWidget* parent)
  : QGraphicsView(parent)
  , m_scene(nullptr)
  , m_liveMarker(nullptr)
  , m_pathPreview(nullptr)  // ✅ NEW
  , m_zoomFactor(1.0)
  , m_zMin(0.0)
  , m_zMax(0.0)
  , m_isPanning(false)
{
  setupScene();

  setRenderHint(QPainter::Antialiasing);
  setRenderHint(QPainter::SmoothPixmapTransform);
  setDragMode(QGraphicsView::NoDrag);
  setBackgroundBrush(QBrush(QColor(26, 31, 46)));
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  setCacheMode(QGraphicsView::CacheBackground);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setSceneRect(-10000, -10000, 20000, 20000);

  m_liveMarker = new DevicePositionMarker("");
  m_scene->addItem(m_liveMarker);
  m_liveMarker->setVisible(false);


  // ✅ NEW - Initialize path preview
  m_pathPreview = new PathPreview(m_scene);
}

// Destructor:
// ✅ NEW - Implement destructor
MotionGraphicsView::~MotionGraphicsView()
{
  delete m_pathPreview;
  m_pathPreview = nullptr;
}

void MotionGraphicsView::setupScene()
{
  m_scene = new QGraphicsScene(this);
  setScene(m_scene);
  connect(m_scene, &QGraphicsScene::selectionChanged,
    this, &MotionGraphicsView::onSelectionChanged);
}

void MotionGraphicsView::addPosition(const QString& name, double x, double y, double z,
  double labelOffsetX, double labelOffsetY)
{
  // Check if position already exists
  if (m_anchors.contains(name)) {
    return;
  }

  // Create anchor (small fixed dot at actual position)
  PositionAnchor* anchor = new PositionAnchor(name, x, y, z);
  m_scene->addItem(anchor);
  m_anchors[name] = anchor;

  // Create connection line
  QGraphicsLineItem* line = new QGraphicsLineItem();
  line->setPen(QPen(QColor(149, 165, 166, 150), 1, Qt::DashLine));  // Gray dashed line
  line->setZValue(1);  // Below everything except grid
  m_scene->addItem(line);
  m_connectionLines[name] = line;

  // Create label (draggable circle)
  PositionLabel* label = new PositionLabel(name, x, y, labelOffsetX, labelOffsetY);
  m_scene->addItem(label);
  m_labels[name] = label;

  // Set connection line for the label
  label->setConnectionLine(line);

  // Connect label position changes to save offset
  connect(label, &PositionLabel::positionChanged,
    this, &MotionGraphicsView::labelOffsetChanged);

  // Recalculate Z range and update colors
  calculateZRange();
  updateColors();
}

void MotionGraphicsView::clearPositions()
{
  // Block signals during cleanup
  if (m_scene) {
    m_scene->blockSignals(true);
    m_scene->clearSelection();
  }

  // Clear path preview
  if (m_pathPreview) {
    m_pathPreview->hidePath();
  }

  // Remove all anchors
  for (auto* anchor : m_anchors) {
    if (anchor && m_scene) {
      m_scene->removeItem(anchor);
      delete anchor;
    }
  }
  m_anchors.clear();

  // Remove all labels
  for (auto* label : m_labels) {
    if (label && m_scene) {
      m_scene->removeItem(label);
      delete label;
    }
  }
  m_labels.clear();

  // Remove all lines
  for (auto* line : m_connectionLines) {
    if (line && m_scene) {
      m_scene->removeItem(line);
      delete line;
    }
  }
  m_connectionLines.clear();

  // ✅ Unblock signals after cleanup
  if (m_scene) {
    m_scene->blockSignals(false);
  }
}

void MotionGraphicsView::updatePositions(const QMap<QString, QVector<double>>& positions,
  const QMap<QString, QPointF>& labelOffsets)
{
  clearPositions();

  // Add all positions with their label offsets
  for (auto it = positions.begin(); it != positions.end(); ++it) {
    const QString& name = it.key();
    const QVector<double>& pos = it.value();

    if (pos.size() >= 3) {
      QPointF offset = labelOffsets.value(name, QPointF(0, 0));
      addPosition(name, pos[0], pos[1], pos[2], offset.x(), offset.y());
    }
  }

  // Fit view to show all positions
  if (!m_anchors.isEmpty()) {
    fitInView(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    m_zoomFactor = transform().m11();
  }
}

QMap<QString, QPointF> MotionGraphicsView::getLabelOffsets() const
{
  QMap<QString, QPointF> offsets;

  for (auto it = m_labels.begin(); it != m_labels.end(); ++it) {
    const QString& name = it.key();
    PositionLabel* label = it.value();

    QPointF offset(label->getOffsetX(), label->getOffsetY());

    // Only save non-zero offsets
    if (offset.x() != 0.0 || offset.y() != 0.0) {
      offsets[name] = offset;
    }
  }

  return offsets;
}

void MotionGraphicsView::calculateZRange()
{
  if (m_anchors.isEmpty()) {
    m_zMin = 0.0;
    m_zMax = 0.0;
    return;
  }

  m_zMin = m_anchors.first()->getZ();
  m_zMax = m_zMin;

  for (auto* anchor : m_anchors) {
    double z = anchor->getZ();
    if (z < m_zMin) m_zMin = z;
    if (z > m_zMax) m_zMax = z;
  }
}

void MotionGraphicsView::updateColors()
{
  for (auto* anchor : m_anchors) {
    if (anchor) {  // ✅ Safety check
      anchor->updateColor(m_zMin, m_zMax);
    }
  }
}

void MotionGraphicsView::onSelectionChanged()
{
  // ✅ Safety check - don't process if scene is invalid
  if (!m_scene) {
    return;
  }

  QList<QGraphicsItem*> selected = m_scene->selectedItems();

  // Deselect all labels first
  for (auto* label : m_labels) {
    if (label) {  // ✅ Safety check
      label->setSelected(false);
    }
  }

  if (selected.isEmpty()) {
    emit positionDeselected();
    return;
  }

  // Check if a label was selected
  PositionLabel* selectedLabel = dynamic_cast<PositionLabel*>(selected.first());
  if (selectedLabel) {
    selectedLabel->setSelected(true);

    // Find corresponding anchor to get actual position
    QString name = selectedLabel->getName();
    if (m_anchors.contains(name)) {
      PositionAnchor* anchor = m_anchors[name];
      if (anchor) {  // ✅ Safety check
        emit positionSelected(name, anchor->getX(), anchor->getY(), anchor->getZ());
      }
    }
  }
}

void MotionGraphicsView::drawBackground(QPainter* painter, const QRectF& rect)
{
  painter->fillRect(rect, QColor(26, 31, 46));
  drawGrid(painter, rect);
}

void MotionGraphicsView::drawGrid(QPainter* painter, const QRectF& rect)
{
  qreal viewScale = transform().m11();
  double gridSpacing = calculateGridSpacing(viewScale);

  QPen gridPen(QColor(60, 70, 90), 1);
  gridPen.setCosmetic(true);
  painter->setPen(gridPen);

  double left = std::floor(rect.left() / gridSpacing) * gridSpacing;
  double right = std::ceil(rect.right() / gridSpacing) * gridSpacing;
  double top = std::floor(rect.top() / gridSpacing) * gridSpacing;
  double bottom = std::ceil(rect.bottom() / gridSpacing) * gridSpacing;

  for (double x = left; x <= right; x += gridSpacing) {
    painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
  }

  for (double y = top; y <= bottom; y += gridSpacing) {
    painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
  }
}

double MotionGraphicsView::calculateGridSpacing(double viewScale)
{
  double baseSpacing = 10.0;
  double pixelsPerUnit = baseSpacing * viewScale;

  if (pixelsPerUnit < 20) {
    while (pixelsPerUnit < 20 && baseSpacing < 1000) {
      baseSpacing *= 2;
      pixelsPerUnit = baseSpacing * viewScale;
    }
  }
  else if (pixelsPerUnit > 100) {
    while (pixelsPerUnit > 100 && baseSpacing > 0.1) {
      baseSpacing /= 2;
      pixelsPerUnit = baseSpacing * viewScale;
    }
  }

  return baseSpacing;
}

void MotionGraphicsView::zoomIn()
{
  scale(1.2, 1.2);
  m_zoomFactor *= 1.2;
  viewport()->update();
}

void MotionGraphicsView::zoomOut()
{
  scale(1.0 / 1.2, 1.0 / 1.2);
  m_zoomFactor /= 1.2;
  viewport()->update();
}

void MotionGraphicsView::resetZoom()
{
  resetTransform();
  m_zoomFactor = 1.0;

  if (!m_anchors.isEmpty()) {
    fitInView(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    m_zoomFactor = transform().m11();
  }

  viewport()->update();
}

void MotionGraphicsView::wheelEvent(QWheelEvent* event)
{
  bool zoomIn = event->angleDelta().y() > 0;
  double scaleFactor = zoomIn ? 1.2 : (1.0 / 1.2);

  QPointF oldPos = mapToScene(event->position().toPoint());
  scale(scaleFactor, scaleFactor);
  m_zoomFactor *= scaleFactor;
  QPointF newPos = mapToScene(event->position().toPoint());
  QPointF delta = newPos - oldPos;
  translate(delta.x(), delta.y());

  viewport()->update();
  event->accept();
}

void MotionGraphicsView::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::MiddleButton) {
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


// Simplified path preview methods:
void MotionGraphicsView::showPathPreview(const QString& targetPosition,
  const std::vector<std::string>& waypoints)
{
  if (!m_pathPreview || !m_liveMarker || !m_liveMarker->isVisible()) {
    return;
  }

  // Get current device position
  QPointF currentPos(m_liveMarker->x(), m_liveMarker->y());

  // Get target position
  QPointF targetPos = getPositionPoint(targetPosition);
  if (targetPos.isNull()) {
    return;
  }

  // Get waypoint positions
  std::vector<QPointF> waypointPositions;
  for (const auto& waypoint : waypoints) {
    QString waypointName = QString::fromStdString(waypoint);
    QPointF waypointPos = getPositionPoint(waypointName);
    if (!waypointPos.isNull()) {
      waypointPositions.push_back(waypointPos);
    }
  }

  // Show path
  m_pathPreview->showPath(currentPos, targetPos, waypointPositions);
}

void MotionGraphicsView::hidePathPreview()
{
  if (m_pathPreview) {
    m_pathPreview->hidePath();
  }
}

// Add this method to MotionGraphicsView.cpp
QPointF MotionGraphicsView::getPositionPoint(const QString& positionName) const
{
  // Try to get from anchors first (actual position)
  if (m_anchors.contains(positionName)) {
    PositionAnchor* anchor = m_anchors[positionName];
    if (anchor) {
      return QPointF(anchor->getX(), anchor->getY());
    }
  }

  // Fallback to labels
  if (m_labels.contains(positionName)) {
    PositionLabel* label = m_labels[positionName];
    if (label) {
      return label->pos();
    }
  }

  return QPointF(); // Null point if not found
}
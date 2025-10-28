// MotionGraphicsView.cpp - FIXED connection lines for zoom
#include "MotionGraphicsView.h"
#include <QMouseEvent>
#include <QScrollBar>
#include <QtMath>
#include <QPainter>

MotionGraphicsView::MotionGraphicsView(QWidget* parent)
  : QGraphicsView(parent)
  , m_scene(nullptr)
  , m_liveMarker(nullptr)
  , m_pathPreview(nullptr)
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

  m_pathPreview = new PathPreview(m_scene);
}

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
  if (m_anchors.contains(name)) {
    return;
  }

  // Create anchor (small fixed dot at actual position)
  PositionAnchor* anchor = new PositionAnchor(name, x, y, z);
  m_scene->addItem(anchor);
  m_anchors[name] = anchor;

  // Create connection line
  QGraphicsLineItem* line = new QGraphicsLineItem();

  // ✅ FIXED: Use cosmetic pen to keep line width constant
  QPen linePen(QColor(149, 165, 166, 150), 1, Qt::DashLine);
  linePen.setCosmetic(true);
  line->setPen(linePen);

  line->setZValue(1);
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
  if (m_scene) {
    m_scene->blockSignals(true);
    m_scene->clearSelection();
  }

  if (m_pathPreview) {
    m_pathPreview->hidePath();
  }

  for (auto* anchor : m_anchors) {
    if (anchor && m_scene) {
      m_scene->removeItem(anchor);
      delete anchor;
    }
  }
  m_anchors.clear();

  for (auto* label : m_labels) {
    if (label && m_scene) {
      m_scene->removeItem(label);
      delete label;
    }
  }
  m_labels.clear();

  for (auto* line : m_connectionLines) {
    if (line && m_scene) {
      m_scene->removeItem(line);
      delete line;
    }
  }
  m_connectionLines.clear();

  // ✅ NEW - Clear stored positions
  m_positions.clear();

  if (m_scene) {
    m_scene->blockSignals(false);
  }
}

void MotionGraphicsView::updatePositions(const QMap<QString, QVector<double>>& positions,
  const QMap<QString, QPointF>& labelOffsets)
{
  clearPositions();

  // ✅ NEW - Store positions for waypoint lookup
  m_positions = positions;

  for (auto it = positions.begin(); it != positions.end(); ++it) {
    const QString& name = it.key();
    const QVector<double>& pos = it.value();

    if (pos.size() >= 3) {
      QPointF offset = labelOffsets.value(name, QPointF(0, 0));
      addPosition(name, pos[0], pos[1], pos[2], offset.x(), offset.y());
    }
  }

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
    if (anchor) {
      anchor->updateColor(m_zMin, m_zMax);
    }
  }
}

void MotionGraphicsView::onSelectionChanged()
{
  if (!m_scene) {
    return;
  }

  QList<QGraphicsItem*> selected = m_scene->selectedItems();

  for (auto* label : m_labels) {
    if (label) {
      label->setSelected(false);
    }
  }

  if (selected.isEmpty()) {
    emit positionDeselected();
    return;
  }

  PositionLabel* selectedLabel = dynamic_cast<PositionLabel*>(selected.first());
  if (selectedLabel) {
    selectedLabel->setSelected(true);

    QString name = selectedLabel->getName();
    if (m_anchors.contains(name)) {
      PositionAnchor* anchor = m_anchors[name];
      if (anchor) {
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

void MotionGraphicsView::fitAllPositions()
{
  if (m_anchors.isEmpty()) {
    return;
  }

  // Get bounding rect of all position anchors
  QRectF bounds;
  for (auto* anchor : m_anchors) {
    if (anchor) {
      bounds = bounds.united(anchor->sceneBoundingRect());
    }
  }

  // Add padding around the bounds
  bounds = bounds.adjusted(-50, -50, 50, 50);

  // Fit the view to show all positions
  resetTransform();
  fitInView(bounds, Qt::KeepAspectRatio);
  m_zoomFactor = transform().m11();

  viewport()->update();
}

void MotionGraphicsView::centerOnDeviceMarker()
{
  if (!m_liveMarker || !m_liveMarker->isVisible()) {
    return;
  }

  // Center view on the device marker position
  centerOn(m_liveMarker);
  viewport()->update();
}

void MotionGraphicsView::autoFitView()
{
  if (!m_anchors.isEmpty()) {
    // If positions exist, zoom to fit all positions
    fitAllPositions();
  } else if (m_liveMarker && m_liveMarker->isVisible()) {
    // If no positions but device marker is visible, center on marker
    centerOnDeviceMarker();
  }
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

// ✅ UPDATED - showPathPreview with entry and exit waypoints
void MotionGraphicsView::showPathPreview(const QString& targetPositionName,
  const std::vector<std::string>& entryWaypoints,
  const std::vector<std::string>& exitWaypoints)
{
  if (!m_pathPreview) {
    return;
  }

  // Get current device position from live marker
  QPointF currentPos = m_liveMarker && m_liveMarker->isVisible()
    ? m_liveMarker->pos()
    : QPointF(0, 0);

  // Get target position coordinates from stored positions
  auto targetIt = m_positions.find(targetPositionName);
  if (targetIt == m_positions.end()) {
    qWarning() << "Target position not found:" << targetPositionName;
    return;
  }

  QPointF targetPos(targetIt.value()[0], targetIt.value()[1]);

  // ✅ Convert exit waypoint names to positions
  std::vector<QPointF> exitWaypointPositions;
  for (const auto& waypointName : exitWaypoints) {
    QString qName = QString::fromStdString(waypointName);
    auto wIt = m_positions.find(qName);
    if (wIt != m_positions.end()) {
      exitWaypointPositions.push_back(QPointF(wIt.value()[0], wIt.value()[1]));
    }
    else {
      qWarning() << "Exit waypoint position not found:" << qName;
    }
  }

  // ✅ Convert entry waypoint names to positions
  std::vector<QPointF> entryWaypointPositions;
  for (const auto& waypointName : entryWaypoints) {
    QString qName = QString::fromStdString(waypointName);
    auto wIt = m_positions.find(qName);
    if (wIt != m_positions.end()) {
      entryWaypointPositions.push_back(QPointF(wIt.value()[0], wIt.value()[1]));
    }
    else {
      qWarning() << "Entry waypoint position not found:" << qName;
    }
  }

  // ✅ Show path with both entry and exit waypoints
  m_pathPreview->showPath(currentPos, targetPos, entryWaypointPositions, exitWaypointPositions);
}

void MotionGraphicsView::hidePathPreview()
{
  if (m_pathPreview) {
    m_pathPreview->hidePath();
  }
}

QPointF MotionGraphicsView::getPositionPoint(const QString& positionName) const
{
  if (m_anchors.contains(positionName)) {
    PositionAnchor* anchor = m_anchors[positionName];
    if (anchor) {
      return QPointF(anchor->getX(), anchor->getY());
    }
  }

  if (m_labels.contains(positionName)) {
    PositionLabel* label = m_labels[positionName];
    if (label) {
      return label->pos();
    }
  }

  return QPointF();
}
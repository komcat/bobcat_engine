// MotionGraphicsView.cpp - Isometric 3D projection view
#include "MotionGraphicsView.h"
#include <QMouseEvent>
#include <QScrollBar>
#include <QResizeEvent>
#include <QFrame>
#include <QtMath>
#include <QPainter>
#include <QSettings>
#include <QDebug>
#include <cmath>

// Default isometric angles for reference
// static constexpr double ISO_COS30 = 0.866025;
// static constexpr double ISO_SIN30 = 0.5;

MotionGraphicsView::MotionGraphicsView(QWidget* parent)
  : QGraphicsView(parent)
  , m_scene(nullptr)
  , m_liveMarker(nullptr)
  // PathPreview removed
  , m_zoomFactor(1.0)
  , m_zMin(0.0)
  , m_zMax(0.0)
  , m_isPanning(false)
  , m_isRotating(false)
  , m_hasSelectedPositionInfo(false)
  , m_selectedPosX(0.0), m_selectedPosY(0.0), m_selectedPosZ(0.0)
  , m_selectedPosU(0.0), m_selectedPosV(0.0), m_selectedPosW(0.0)
  , m_hasLivePositionInfo(false)
  , m_livePosX(0.0), m_livePosY(0.0), m_livePosZ(0.0)
  , m_livePosU(0.0), m_livePosV(0.0), m_livePosW(0.0)
  , m_zScaleFactor(5.0)
  , m_azimuthDeg(45.0)
  , m_elevationDeg(35.264)
  , m_viewButtonBar(nullptr)
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

  setupViewButtons();
}

void MotionGraphicsView::setupViewButtons()
{
  m_viewButtonBar = new QWidget(this);
  QHBoxLayout* layout = new QHBoxLayout(m_viewButtonBar);
  layout->setContentsMargins(4, 2, 4, 2);
  layout->setSpacing(4);

  QString btnStyle =
    "QPushButton {"
    "  background-color: rgba(30, 30, 30, 200);"
    "  color: #d4d4d4;"
    "  border: 1px solid #555;"
    "  border-radius: 3px;"
    "  font-size: 11px;"
    "  font-weight: bold;"
    "  padding: 4px 8px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #0e639c;"
    "  color: white;"
    "}";

  auto addBtn = [&](const QString& text) -> QPushButton* {
    QPushButton* btn = new QPushButton(text);
    btn->setStyleSheet(btnStyle);
    btn->setFixedHeight(24);
    layout->addWidget(btn);
    return btn;
  };

  // Preset views
  QPushButton* xyBtn = addBtn("XY");
  QPushButton* yzBtn = addBtn("YZ");
  QPushButton* xzBtn = addBtn("XZ");

  // Separator
  QFrame* sep = new QFrame();
  sep->setFrameShape(QFrame::VLine);
  sep->setStyleSheet("QFrame { color: #555; }");
  layout->addWidget(sep);

  // Rotation buttons
  QPushButton* rotCCW = addBtn("< CCW");
  QPushButton* rotCW = addBtn("CW >");

  // Separator
  QFrame* sep2 = new QFrame();
  sep2->setFrameShape(QFrame::VLine);
  sep2->setStyleSheet("QFrame { color: #555; }");
  layout->addWidget(sep2);

  // Reset
  QPushButton* resetBtn = addBtn("Reset");

  layout->addStretch();

  // Connect buttons
  connect(xyBtn, &QPushButton::clicked, this, [this]() {
    setCamera(0.0, 90.0);  // Top-down XY view (elevation 90 = looking straight down, clamped to 80)
    setCamera(0.0, 80.0);
    autoFitView();
  });
  connect(yzBtn, &QPushButton::clicked, this, [this]() {
    setCamera(0.0, 10.0);  // Front view: Y horizontal, Z vertical
    autoFitView();
  });
  connect(xzBtn, &QPushButton::clicked, this, [this]() {
    setCamera(90.0, 10.0);  // Side view: X horizontal, Z vertical
    autoFitView();
  });
  connect(rotCCW, &QPushButton::clicked, this, [this]() {
    rotate(-45.0);  // Rotate viewport CCW around screen center
    viewport()->update();
  });
  connect(rotCW, &QPushButton::clicked, this, [this]() {
    rotate(45.0);   // Rotate viewport CW around screen center
    viewport()->update();
  });
  connect(resetBtn, &QPushButton::clicked, this, [this]() {
    resetCamera();
    resetZoom();  // Also resets viewport rotation
  });

  m_viewButtonBar->setFixedHeight(30);
  m_viewButtonBar->show();
}

void MotionGraphicsView::resizeEvent(QResizeEvent* event)
{
  QGraphicsView::resizeEvent(event);
  if (m_viewButtonBar) {
    int barWidth = qMin(width(), 500);
    m_viewButtonBar->setGeometry(
      (width() - barWidth) / 2, height() - 34,
      barWidth, 30);
  }
}

MotionGraphicsView::~MotionGraphicsView()
{
  disconnect();
  // PathPreview removed
  qDebug() << "[MotionGraphicsView] Destroyed";
}

void MotionGraphicsView::setupScene()
{
  m_scene = new QGraphicsScene(this);
  setScene(m_scene);
}

// ============================================================================
// ISOMETRIC PROJECTION
// ============================================================================

QPointF MotionGraphicsView::toIsometric(double x, double y, double z) const
{
  double azRad = qDegreesToRadians(m_azimuthDeg);
  double elRad = qDegreesToRadians(m_elevationDeg);

  // Rotate XY plane by azimuth
  double rotX = x * std::cos(azRad) - y * std::sin(azRad);
  double rotY = x * std::sin(azRad) + y * std::cos(azRad);

  // Project with elevation
  double isoX = rotX;
  double isoY = rotY * std::sin(elRad) - z * m_zScaleFactor * std::cos(elRad);
  return QPointF(isoX, isoY);
}

// ============================================================================
// POSITION MANAGEMENT
// ============================================================================

void MotionGraphicsView::addPosition(const QString& name, double x, double y, double z)
{
  if (m_anchors.contains(name)) {
    return;
  }

  // Compute isometric positions
  QPointF elevatedPt = toIsometric(x, y, z);
  QPointF groundPt = toIsometric(x, y, 0.0);

  // Create elevated anchor (main position dot, colored by Z)
  PositionAnchor* anchor = new PositionAnchor(name, x, y, z);
  anchor->setPos(elevatedPt);
  anchor->setZValue(5);
  m_scene->addItem(anchor);
  m_anchors[name] = anchor;

  // Text labels are drawn in drawForeground() for pixel-perfect alignment

  // Update Z range and colors
  calculateZRange();
  updateColors();
}

void MotionGraphicsView::clearPositions()
{
  // Block scene signals during cleanup
  bool blocked = m_scene->blockSignals(true);

  // Remove elevated anchors
  for (auto* anchor : m_anchors) {
    if (anchor && m_scene) {
      m_scene->removeItem(anchor);
      delete anchor;
    }
  }
  m_anchors.clear();

  // Remove edge lines
  for (auto* line : m_edgeLines) {
    if (line && m_scene) {
      m_scene->removeItem(line);
      delete line;
    }
  }
  m_edgeLines.clear();

  m_positions.clear();
  m_scene->blockSignals(blocked);
}

void MotionGraphicsView::updatePositions(const QMap<QString, QVector<double>>& positions)
{
  clearPositions();
  m_positions = positions;

  for (auto it = positions.begin(); it != positions.end(); ++it) {
    const QString& name = it.key();
    const QVector<double>& pos = it.value();
    if (pos.size() >= 3) {
      addPosition(name, pos[0], pos[1], pos[2]);
    }
  }

  if (!m_anchors.isEmpty()) {
    fitInView(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20), Qt::KeepAspectRatio);
    m_zoomFactor = transform().m11();
  }
}

void MotionGraphicsView::setGraphEdges(const QList<GraphEdge>& edges)
{
  // Remove old edge lines
  for (auto* line : m_edgeLines) {
    if (line && m_scene) {
      m_scene->removeItem(line);
      delete line;
    }
  }
  m_edgeLines.clear();
  m_graphEdges.clear();

  // Draw edge lines between positions (keep parallel lists)
  for (const auto& edge : edges) {
    if (!m_positions.contains(edge.from) || !m_positions.contains(edge.to)) {
      continue;
    }

    const auto& fromPos = m_positions[edge.from];
    const auto& toPos = m_positions[edge.to];
    if (fromPos.size() < 3 || toPos.size() < 3) continue;

    QPointF fromPt = toIsometric(fromPos[0], fromPos[1], fromPos[2]);
    QPointF toPt = toIsometric(toPos[0], toPos[1], toPos[2]);

    QGraphicsLineItem* line = new QGraphicsLineItem(
      fromPt.x(), fromPt.y(), toPt.x(), toPt.y());
    QPen edgePen(QColor(255, 255, 255, 60), 1, Qt::SolidLine);
    edgePen.setCosmetic(true);
    line->setPen(edgePen);
    line->setZValue(1);
    m_scene->addItem(line);
    m_edgeLines.append(line);
    m_graphEdges.append(edge);
  }
}

void MotionGraphicsView::highlightEdgesForPosition(const QString& positionName)
{
  m_highlightedPosition = positionName;
  for (int i = 0; i < m_graphEdges.size() && i < m_edgeLines.size(); ++i) {
    bool connected = (m_graphEdges[i].from == positionName ||
                      m_graphEdges[i].to == positionName);
    QPen pen = m_edgeLines[i]->pen();
    if (connected) {
      pen.setColor(QColor(46, 204, 113, 200));  // Bright green
      pen.setWidth(2);
    } else {
      pen.setColor(QColor(255, 255, 255, 30));   // Dimmed
      pen.setWidth(1);
    }
    m_edgeLines[i]->setPen(pen);
  }
}

void MotionGraphicsView::clearEdgeHighlight()
{
  m_highlightedPosition.clear();
  for (auto* line : m_edgeLines) {
    if (line) {
      QPen pen = line->pen();
      pen.setColor(QColor(255, 255, 255, 60));  // Default
      pen.setWidth(1);
      line->setPen(pen);
    }
  }
}

void MotionGraphicsView::rebuildScene()
{
  if (m_positions.isEmpty()) return;

  // Save state
  QMap<QString, QVector<double>> savedPositions = m_positions;
  QList<GraphEdge> savedEdges = m_graphEdges;
  QString savedHighlight = m_highlightedPosition;

  // Rebuild positions (clearPositions + re-add)
  clearPositions();
  m_positions = savedPositions;

  for (auto it = m_positions.begin(); it != m_positions.end(); ++it) {
    const QVector<double>& pos = it.value();
    if (pos.size() >= 3) {
      addPosition(it.key(), pos[0], pos[1], pos[2]);
    }
  }

  // Rebuild edge lines
  setGraphEdges(savedEdges);

  // Re-apply highlight
  if (!savedHighlight.isEmpty()) {
    highlightEdgesForPosition(savedHighlight);
  }

  // Update live marker with current projection
  if (m_liveMarker && m_liveMarker->isVisible() && m_hasLivePositionInfo) {
    // Live marker uses transformed coords, not raw — just trigger viewport repaint
  }

  viewport()->update();
}

// ============================================================================
// Z RANGE AND COLOR CODING
// ============================================================================

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

// ============================================================================
// BACKGROUND AND GRID
// ============================================================================

void MotionGraphicsView::drawBackground(QPainter* painter, const QRectF& rect)
{
  painter->fillRect(rect, QColor(26, 31, 46));
  drawGrid(painter, rect);
}

void MotionGraphicsView::drawGrid(QPainter* painter, const QRectF& rect)
{
  qreal viewScale = transform().m11();
  double gridSpacing = calculateGridSpacing(viewScale);

  QPen gridPen(QColor(60, 70, 90, 80), 1, Qt::SolidLine);
  gridPen.setCosmetic(true);
  painter->setPen(gridPen);

  // Compute grid directions from dynamic projection
  QPointF origin = toIsometric(0, 0, 0);
  QPointF xUnit = toIsometric(gridSpacing, 0, 0) - origin;
  QPointF yUnit = toIsometric(0, gridSpacing, 0) - origin;

  double dirXx = xUnit.x();
  double dirXy = xUnit.y();
  double dirYx = yUnit.x();
  double dirYy = yUnit.y();

  // Calculate how many grid lines we need to cover the visible area
  double maxDim = qMax(rect.width(), rect.height());
  int numLines = static_cast<int>(maxDim / gridSpacing) + 10;

  // Draw lines parallel to isometric X-axis (along Y-axis direction)
  for (int i = -numLines; i <= numLines; ++i) {
    double baseX = i * dirXx;
    double baseY = i * dirXy;
    // Draw line from (baseX - N*dirYx, baseY - N*dirYy) to (baseX + N*dirYx, baseY + N*dirYy)
    painter->drawLine(
      QPointF(baseX - numLines * dirYx, baseY - numLines * dirYy),
      QPointF(baseX + numLines * dirYx, baseY + numLines * dirYy));
  }

  // Draw lines parallel to isometric Y-axis (along X-axis direction)
  for (int i = -numLines; i <= numLines; ++i) {
    double baseX = i * dirYx;
    double baseY = i * dirYy;
    painter->drawLine(
      QPointF(baseX - numLines * dirXx, baseY - numLines * dirXy),
      QPointF(baseX + numLines * dirXx, baseY + numLines * dirXy));
  }
}

double MotionGraphicsView::calculateGridSpacing(double viewScale)
{
  // Aim for ~50 pixels between grid lines
  double targetPixels = 50.0;
  double rawSpacing = targetPixels / viewScale;

  // Snap to nice values: 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, ...
  double magnitude = std::pow(10.0, std::floor(std::log10(rawSpacing)));
  double normalized = rawSpacing / magnitude;

  double baseSpacing;
  if (normalized < 1.5) {
    baseSpacing = 1.0 * magnitude;
  } else if (normalized < 3.5) {
    baseSpacing = 2.0 * magnitude;
  } else if (normalized < 7.5) {
    baseSpacing = 5.0 * magnitude;
  } else {
    baseSpacing = 10.0 * magnitude;
  }

  return baseSpacing;
}

// ============================================================================
// FOREGROUND OVERLAY (drawn in viewport coordinates)
// ============================================================================

void MotionGraphicsView::drawForeground(QPainter* painter, const QRectF& /*rect*/)
{
  // Calculate zoom percentage
  int zoomPercent = static_cast<int>(m_zoomFactor * 100.0);
  QString zoomText = QString("Zoom: %1%").arg(zoomPercent);

  painter->save();
  painter->resetTransform();  // Draw in viewport coordinates

  int viewportWidth = viewport()->width();

  // === Zoom indicator (top-right) ===
  QFont font("Arial", 11, QFont::Bold);
  painter->setFont(font);
  QFontMetrics fm(font);
  QRect textRect = fm.boundingRect(zoomText);
  int textWidth = textRect.width();
  int textHeight = textRect.height();

  int padding = 10;
  int x = viewportWidth - textWidth - padding - 10;
  int y = padding + textHeight;

  QRect bgRect(x - 5, y - textHeight - 2, textWidth + 10, textHeight + 6);
  painter->setBrush(QColor(0, 0, 0, 150));
  painter->setPen(Qt::NoPen);
  painter->drawRoundedRect(bgRect, 3, 3);
  painter->setPen(QColor(255, 255, 255));
  painter->drawText(x, y, zoomText);

  // === Device name label (top-left) ===
  if (!m_deviceName.isEmpty()) {
    QColor deviceColor;
    if (m_deviceName.contains("gantry", Qt::CaseInsensitive)) {
      deviceColor = QColor(52, 152, 219);
    } else if (m_deviceName.contains("hex-left", Qt::CaseInsensitive)) {
      deviceColor = QColor(46, 204, 113);
    } else if (m_deviceName.contains("hex-right", Qt::CaseInsensitive)) {
      deviceColor = QColor(231, 76, 60);
    } else if (m_deviceName.contains("hex-bottom", Qt::CaseInsensitive)) {
      deviceColor = QColor(155, 89, 182);
    } else if (m_deviceName.contains("stage", Qt::CaseInsensitive)) {
      deviceColor = QColor(241, 196, 15);
    } else {
      deviceColor = QColor(149, 165, 166);
    }

    QFont deviceFont("Arial", 24, QFont::Bold);
    painter->setFont(deviceFont);
    QFontMetrics dfm(deviceFont);
    QRect deviceTextRect = dfm.boundingRect(m_deviceName);
    int dPadding = 15;

    QRect dBgRect(dPadding - 8, dPadding - 4,
                  deviceTextRect.width() + 24, deviceTextRect.height() + 12);
    painter->setBrush(QColor(0, 0, 0, 160));
    painter->setPen(QPen(deviceColor, 2));
    painter->drawRoundedRect(dBgRect, 6, 6);

    painter->setPen(deviceColor);
    painter->drawText(dPadding + 4, dPadding + deviceTextRect.height() + 1, m_deviceName);

    // === Current device position overlay (below device name) ===
    int nextOverlayY = dBgRect.bottom() + 10;

    if (m_hasLivePositionInfo) {
      QFont posFont("RobotoMono", 11, QFont::Normal);
      painter->setFont(posFont);
      QFontMetrics pfm(posFont);
      int lineHeight = pfm.height() + 2;

      QStringList lines;
      lines << "Current Position";
      lines << QString("X: %1").arg(m_livePosX, 0, 'f', 3);
      lines << QString("Y: %1").arg(m_livePosY, 0, 'f', 3);
      lines << QString("Z: %1").arg(m_livePosZ, 0, 'f', 3);
      lines << QString("U: %1").arg(m_livePosU, 0, 'f', 3);
      lines << QString("V: %1").arg(m_livePosV, 0, 'f', 3);
      lines << QString("W: %1").arg(m_livePosW, 0, 'f', 3);

      int maxWidth = 0;
      for (const auto& line : lines) {
        maxWidth = qMax(maxWidth, pfm.horizontalAdvance(line));
      }

      int boxPad = 8;
      QRect liveBg(dPadding - 8, nextOverlayY,
                   maxWidth + boxPad * 2, static_cast<int>(lines.size()) * lineHeight + boxPad * 2);
      painter->setBrush(QColor(0, 0, 0, 160));
      painter->setPen(QPen(QColor(80, 80, 80), 1));
      painter->drawRoundedRect(liveBg, 5, 5);

      int textY = nextOverlayY + boxPad + pfm.ascent();
      for (int i = 0; i < lines.size(); ++i) {
        if (i == 0) {
          QFont boldFont = posFont;
          boldFont.setBold(true);
          painter->setFont(boldFont);
          painter->setPen(QColor(255, 215, 0));  // Gold for "Current Position"
          painter->drawText(dPadding - 8 + boxPad, textY, lines[i]);
          painter->setFont(posFont);
        } else {
          painter->setPen(QColor(180, 180, 180));
          painter->drawText(dPadding - 8 + boxPad, textY, lines[i]);
        }
        textY += lineHeight;
      }

      nextOverlayY = liveBg.bottom() + 8;
    }

    // === Selected position info overlay ===
    if (m_hasSelectedPositionInfo) {
      int overlayY = nextOverlayY;

      QFont posFont("RobotoMono", 12, QFont::Normal);
      painter->setFont(posFont);
      QFontMetrics pfm(posFont);
      int lineHeight = pfm.height() + 2;

      QStringList lines;
      lines << QString("%1 >> %2").arg(m_deviceName, m_selectedPosName);
      lines << QString("X: %1").arg(m_selectedPosX, 0, 'f', 3);
      lines << QString("Y: %1").arg(m_selectedPosY, 0, 'f', 3);
      lines << QString("Z: %1").arg(m_selectedPosZ, 0, 'f', 3);
      lines << QString("U: %1").arg(m_selectedPosU, 0, 'f', 3);
      lines << QString("V: %1").arg(m_selectedPosV, 0, 'f', 3);
      lines << QString("W: %1").arg(m_selectedPosW, 0, 'f', 3);

      int maxWidth = 0;
      for (const auto& line : lines) {
        maxWidth = qMax(maxWidth, pfm.horizontalAdvance(line));
      }

      int boxPad = 8;
      QRect posInfoBg(dPadding - 8, overlayY,
                      maxWidth + boxPad * 2, static_cast<int>(lines.size()) * lineHeight + boxPad * 2);
      painter->setBrush(QColor(0, 0, 0, 180));
      painter->setPen(QPen(QColor(100, 100, 100), 1));
      painter->drawRoundedRect(posInfoBg, 5, 5);

      painter->setPen(QColor(220, 220, 220));
      int textY = overlayY + boxPad + pfm.ascent();
      for (int i = 0; i < lines.size(); ++i) {
        if (i == 0) {
          QFont boldFont = posFont;
          boldFont.setBold(true);
          painter->setFont(boldFont);
          painter->setPen(deviceColor);
          painter->drawText(dPadding - 8 + boxPad, textY, lines[i]);
          painter->setFont(posFont);
          painter->setPen(QColor(220, 220, 220));
        } else {
          painter->drawText(dPadding - 8 + boxPad, textY, lines[i]);
        }
        textY += lineHeight;
      }
    }
  }

  // === Position name labels (drawn in viewport coords for pixel-perfect alignment) ===
  {
    QFont labelFont("RobotoMono", 8);
    painter->setFont(labelFont);
    QFontMetrics lfm(labelFont);
    painter->setPen(QColor(210, 210, 210));

    for (auto it = m_anchors.constBegin(); it != m_anchors.constEnd(); ++it) {
      PositionAnchor* anchor = it.value();
      if (!anchor) continue;

      // Map anchor scene position to viewport pixel position
      QPoint screenPos = mapFromScene(anchor->scenePos());
      QString name = it.key();
      int tw = lfm.horizontalAdvance(name);

      // Draw centered above the marker
      painter->drawText(screenPos.x() - tw / 2, screenPos.y() - 8, name);
    }
  }

  // === Isometric axis indicator (bottom-left) ===
  {
    int axisLen = 40;
    int axisOriginX = 50;
    int axisOriginY = viewport()->height() - 50;

    // Compute axis directions from current projection
    QPointF origin = toIsometric(0, 0, 0);
    QPointF xDir = toIsometric(1, 0, 0) - origin;
    QPointF yDir = toIsometric(0, 1, 0) - origin;
    QPointF zDir = toIsometric(0, 0, 1) - origin;

    // Normalize and scale to axisLen
    auto normalize = [](QPointF p, double len) -> QPointF {
      double mag = std::sqrt(p.x() * p.x() + p.y() * p.y());
      if (mag < 0.001) return QPointF(0, 0);
      return QPointF(p.x() / mag * len, p.y() / mag * len);
    };

    QPointF xEnd(axisOriginX + normalize(xDir, axisLen).x(),
                 axisOriginY + normalize(xDir, axisLen).y());
    QPointF yEnd(axisOriginX + normalize(yDir, axisLen).x(),
                 axisOriginY + normalize(yDir, axisLen).y());
    QPointF zEnd(axisOriginX + normalize(zDir, axisLen).x(),
                 axisOriginY + normalize(zDir, axisLen).y());

    QPen axisPen;
    axisPen.setWidth(2);

    // X axis - red
    axisPen.setColor(QColor(231, 76, 60));
    painter->setPen(axisPen);
    painter->drawLine(QPointF(axisOriginX, axisOriginY), xEnd);
    painter->setFont(QFont("Arial", 10, QFont::Bold));
    painter->drawText(static_cast<int>(xEnd.x()) + 3, static_cast<int>(xEnd.y()) + 4, "X");

    // Y axis - green
    axisPen.setColor(QColor(46, 204, 113));
    painter->setPen(axisPen);
    painter->drawLine(QPointF(axisOriginX, axisOriginY), yEnd);
    painter->drawText(static_cast<int>(yEnd.x()) - 14, static_cast<int>(yEnd.y()) + 4, "Y");

    // Z axis - blue
    axisPen.setColor(QColor(52, 152, 219));
    painter->setPen(axisPen);
    painter->drawLine(QPointF(axisOriginX, axisOriginY), zEnd);
    painter->drawText(static_cast<int>(zEnd.x()) - 4, static_cast<int>(zEnd.y()) - 6, "Z");
  }

  painter->restore();
}

// ============================================================================
// ZOOM AND PAN
// ============================================================================

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

  QRectF bounds;
  for (auto* anchor : m_anchors) {
    if (anchor) {
      bounds = bounds.united(anchor->sceneBoundingRect());
    }
  }
  bounds = bounds.adjusted(-50, -50, 50, 50);
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
  centerOn(m_liveMarker);
  viewport()->update();
}

void MotionGraphicsView::autoFitView()
{
  if (!m_anchors.isEmpty()) {
    fitAllPositions();
  } else if (m_liveMarker && m_liveMarker->isVisible()) {
    centerOnDeviceMarker();
  }
}

void MotionGraphicsView::centerOnPosition(const QString& positionName, double zoomLevel)
{
  if (!m_anchors.contains(positionName)) {
    qWarning() << "Position" << positionName << "not found in graphics view";
    return;
  }

  PositionAnchor* anchor = m_anchors[positionName];
  if (!anchor) {
    return;
  }

  QPointF positionPoint = anchor->scenePos();
  resetTransform();
  scale(zoomLevel, zoomLevel);
  m_zoomFactor = zoomLevel;
  centerOn(positionPoint);
  viewport()->update();
}

void MotionGraphicsView::wheelEvent(QWheelEvent* event)
{
  bool isZoomIn = event->angleDelta().y() > 0;
  double scaleFactor = isZoomIn ? 1.2 : (1.0 / 1.2);

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
    // Middle = Pan (default)
    m_isPanning = true;
    m_lastPanPoint = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
  }
  else if (event->button() == Qt::RightButton) {
    // Right = Rotate camera
    m_isRotating = true;
    m_lastRotatePoint = event->pos();
    setCursor(Qt::SizeAllCursor);
    event->accept();
  }
  else {
    QGraphicsView::mousePressEvent(event);
  }
}

void MotionGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::MiddleButton && m_isPanning) {
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    event->accept();
  }
  else if (event->button() == Qt::RightButton && m_isRotating) {
    m_isRotating = false;
    setCursor(Qt::ArrowCursor);
    event->accept();
  }
  else {
    QGraphicsView::mouseReleaseEvent(event);
  }
}

void MotionGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
  if (m_isRotating) {
    QPoint delta = event->pos() - m_lastRotatePoint;
    m_lastRotatePoint = event->pos();

    // Horizontal drag = azimuth rotation (0.3 deg per pixel)
    m_azimuthDeg += delta.x() * 0.3;
    if (m_azimuthDeg < 0.0) m_azimuthDeg += 360.0;
    if (m_azimuthDeg >= 360.0) m_azimuthDeg -= 360.0;

    // Vertical drag = elevation change (0.3 deg per pixel)
    m_elevationDeg -= delta.y() * 0.3;
    if (m_elevationDeg < 10.0) m_elevationDeg = 10.0;
    if (m_elevationDeg > 80.0) m_elevationDeg = 80.0;

    rebuildScene();
    event->accept();
  }
  else if (m_isPanning) {
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

void MotionGraphicsView::keyPressEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_R) {
    // Reset camera to default isometric view
    resetCamera();
    autoFitView();
    event->accept();
  }
  else {
    QGraphicsView::keyPressEvent(event);
  }
}

// ============================================================================
// LIVE MARKER AND PATH PREVIEW
// ============================================================================

void MotionGraphicsView::updateLiveMarker(const QString& deviceName, double x, double y, double z)
{
  if (m_liveMarker) {
    m_liveMarker->setDeviceName(deviceName);
    // Project live position to isometric with Z
    QPointF isoPt = toIsometric(x, y, z);
    m_liveMarker->updatePosition(isoPt.x(), isoPt.y());
    m_liveMarker->setVisible(true);
  }
}

void MotionGraphicsView::setLivePositionInfo(double x, double y, double z,
  double u, double v, double w)
{
  m_hasLivePositionInfo = true;
  m_livePosX = x;
  m_livePosY = y;
  m_livePosZ = z;
  m_livePosU = u;
  m_livePosV = v;
  m_livePosW = w;
  viewport()->update();
}

void MotionGraphicsView::showLiveMarker(bool show)
{
  if (m_liveMarker) {
    m_liveMarker->setVisible(show);
  }
  if (!show) {
    m_hasLivePositionInfo = false;
  }
}

// ============================================================================
// POSITION LOOKUP
// ============================================================================

QPointF MotionGraphicsView::getPositionPoint(const QString& positionName) const
{
  if (m_anchors.contains(positionName)) {
    PositionAnchor* anchor = m_anchors[positionName];
    if (anchor) {
      return anchor->scenePos();
    }
  }
  return QPointF();
}

// ============================================================================
// SETTINGS AND STATE
// ============================================================================

void MotionGraphicsView::setZScaleFactor(double factor)
{
  if (std::abs(m_zScaleFactor - factor) > 0.001) {
    m_zScaleFactor = factor;
    rebuildScene();
  }
}

void MotionGraphicsView::setCamera(double azimuthDeg, double elevationDeg)
{
  m_azimuthDeg = azimuthDeg;
  m_elevationDeg = qBound(10.0, elevationDeg, 80.0);
  rebuildScene();
}

void MotionGraphicsView::resetCamera()
{
  setCamera(45.0, 35.264);
}

void MotionGraphicsView::setDeviceName(const QString& deviceName)
{
  m_deviceName = deviceName;
  viewport()->update();
}

void MotionGraphicsView::setSelectedPositionInfo(const QString& name, double x, double y, double z,
  double u, double v, double w)
{
  m_hasSelectedPositionInfo = true;
  m_selectedPosName = name;
  m_selectedPosX = x;
  m_selectedPosY = y;
  m_selectedPosZ = z;
  m_selectedPosU = u;
  m_selectedPosV = v;
  m_selectedPosW = w;
  viewport()->update();
}

void MotionGraphicsView::clearSelectedPositionInfo()
{
  m_hasSelectedPositionInfo = false;
  m_selectedPosName.clear();
  viewport()->update();
}

void MotionGraphicsView::saveViewState()
{
  if (m_deviceName.isEmpty()) {
    return;
  }

  QSettings settings;
  QString prefix = QString("MotionPage/%1/").arg(m_deviceName);
  settings.setValue(prefix + "zoomFactor", m_zoomFactor);
  settings.setValue(prefix + "azimuth", m_azimuthDeg);
  settings.setValue(prefix + "elevation", m_elevationDeg);

  QPointF centerPoint = mapToScene(viewport()->rect().center());
  settings.setValue(prefix + "centerX", centerPoint.x());
  settings.setValue(prefix + "centerY", centerPoint.y());
}

void MotionGraphicsView::restoreViewState()
{
  if (m_deviceName.isEmpty()) {
    return;
  }

  QSettings settings;
  QString prefix = QString("MotionPage/%1/").arg(m_deviceName);

  if (!settings.contains(prefix + "zoomFactor")) {
    return;
  }

  double savedZoomFactor = settings.value(prefix + "zoomFactor", 1.0).toDouble();
  double savedCenterX = settings.value(prefix + "centerX", 0.0).toDouble();
  double savedCenterY = settings.value(prefix + "centerY", 0.0).toDouble();
  m_azimuthDeg = settings.value(prefix + "azimuth", 45.0).toDouble();
  m_elevationDeg = settings.value(prefix + "elevation", 35.264).toDouble();

  // Rebuild scene with restored camera angles
  rebuildScene();

  resetTransform();
  m_zoomFactor = 1.0;

  double scaleFactor = savedZoomFactor / m_zoomFactor;
  scale(scaleFactor, scaleFactor);
  m_zoomFactor = savedZoomFactor;

  centerOn(QPointF(savedCenterX, savedCenterY));
  viewport()->update();
}

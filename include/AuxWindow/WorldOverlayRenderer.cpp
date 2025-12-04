// WorldOverlayRenderer.cpp
#include "WorldOverlayRenderer.h"
#include "WorldOverlay.h"
#include <QPen>
#include <QBrush>
#include <QFont>
#include <cmath>

WorldOverlayRenderer::WorldOverlayRenderer(QObject* parent)
  : QObject(parent)
{
}

WorldOverlayRenderer::~WorldOverlayRenderer()
{
}

QImage WorldOverlayRenderer::renderOverlay(const QImage& frame,
                                            const WorldOverlay::OverlayFile* overlay,
                                            double gantryX, double gantryY,
                                            double viewCenterX, double viewCenterY,
                                            double pxPerMm, double zoomFactor,
                                            const QString& selectedMarkerId)
{
  // Create a copy to draw on
  QImage result = frame.copy();

  if (!overlay) {
    return result;
  }

  QPainter painter(&result);
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Render each visible layer
  for (const auto& layer : overlay->layers) {
    if (!layer.visible) {
      continue;
    }

    QColor layerColor = layer.color;
    double layerOpacity = layer.opacity;

    // Render each marker in the layer
    for (const auto& marker : layer.markers) {
      QPointF screenPos = worldToScreen(marker.worldX, marker.worldY,
                                         gantryX, gantryY,
                                         viewCenterX, viewCenterY,
                                         pxPerMm, zoomFactor);

      // Check if marker is within frame bounds (with margin)
      double margin = 100;
      if (screenPos.x() < -margin || screenPos.x() > result.width() + margin ||
          screenPos.y() < -margin || screenPos.y() > result.height() + margin) {
        continue;  // Skip markers outside visible area
      }

      // Render based on marker type
      switch (marker.type) {
        case WorldOverlay::MarkerType::Circle:
          renderCircle(painter, screenPos, marker, layerColor, layerOpacity, zoomFactor);
          break;

        case WorldOverlay::MarkerType::Crosshair:
          renderCrosshair(painter, screenPos, marker, layerColor, layerOpacity, zoomFactor);
          break;

        case WorldOverlay::MarkerType::Rectangle:
          renderRectangle(painter, screenPos, marker, layerColor, layerOpacity, zoomFactor);
          break;

        case WorldOverlay::MarkerType::Diamond:
          renderDiamond(painter, screenPos, marker, layerColor, layerOpacity, zoomFactor);
          break;

        case WorldOverlay::MarkerType::Point:
          renderPoint(painter, screenPos, marker, layerColor, layerOpacity, zoomFactor);
          break;

        case WorldOverlay::MarkerType::Line: {
          QPointF endPos = worldToScreen(marker.endWorldX, marker.endWorldY,
                                          gantryX, gantryY,
                                          viewCenterX, viewCenterY,
                                          pxPerMm, zoomFactor);
          renderLine(painter, screenPos, endPos, marker, layerColor, layerOpacity, zoomFactor);
          break;
        }

        case WorldOverlay::MarkerType::Text:
          renderText(painter, screenPos, marker, layerColor, layerOpacity, zoomFactor);
          break;
      }

      // Render label if enabled
      if (marker.showLabel && !marker.label.isEmpty() &&
          marker.type != WorldOverlay::MarkerType::Text) {
        renderLabel(painter, screenPos, marker.label, marker.fontSize,
                    layerColor, layerOpacity);
      }

      // Render selection highlight
      if (marker.id == selectedMarkerId) {
        renderSelectionHighlight(painter, screenPos, marker, zoomFactor);
      }
    }
  }

  painter.end();
  return result;
}

QPointF WorldOverlayRenderer::worldToScreen(double worldX, double worldY,
                                             double gantryX, double gantryY,
                                             double viewCenterX, double viewCenterY,
                                             double pxPerMm, double zoomFactor) const
{
  // Calculate where marker appears on screen relative to gantry position
  // When gantry moves, the camera view shifts - objects appear to move opposite to gantry motion
  //
  // If gantry moves RIGHT (gantryX increases), objects appear to move LEFT in camera
  // If gantry moves UP (gantryY increases), objects appear to move DOWN in camera (screen Y increases)
  //
  // deltaX/Y = where marker is relative to current gantry position
  double deltaX = worldX - gantryX;  // Positive = marker is to the right of gantry
  double deltaY = worldY - gantryY;  // Positive = marker is above gantry (higher Y in world)

  // Convert to pixels and apply zoom
  double screenOffsetX = deltaX * pxPerMm * zoomFactor;
  double screenOffsetY = deltaY * pxPerMm * zoomFactor;

  // Add to view center
  // X: positive delta -> marker to the right -> add to X
  // Y: positive delta -> marker above gantry -> appears LOWER on screen (higher screen Y)
  return QPointF(viewCenterX + screenOffsetX,
                 viewCenterY + screenOffsetY);
}

void WorldOverlayRenderer::renderCircle(QPainter& painter, const QPointF& center,
                                          const WorldOverlay::Marker& marker,
                                          const QColor& color, double opacity, double zoomFactor)
{
  QColor drawColor = color;
  drawColor.setAlphaF(opacity);

  QPen pen(drawColor, marker.lineWidthPx * zoomFactor);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  double radius = marker.radiusPx * zoomFactor;
  painter.drawEllipse(center, radius, radius);
}

void WorldOverlayRenderer::renderCrosshair(QPainter& painter, const QPointF& center,
                                            const WorldOverlay::Marker& marker,
                                            const QColor& color, double opacity, double zoomFactor)
{
  QColor drawColor = color;
  drawColor.setAlphaF(opacity);

  QPen pen(drawColor, marker.lineWidthPx * zoomFactor);
  painter.setPen(pen);

  double halfSize = (marker.sizePx * zoomFactor) / 2.0;

  // Horizontal line
  painter.drawLine(QPointF(center.x() - halfSize, center.y()),
                   QPointF(center.x() + halfSize, center.y()));

  // Vertical line
  painter.drawLine(QPointF(center.x(), center.y() - halfSize),
                   QPointF(center.x(), center.y() + halfSize));
}

void WorldOverlayRenderer::renderRectangle(QPainter& painter, const QPointF& center,
                                            const WorldOverlay::Marker& marker,
                                            const QColor& color, double opacity, double zoomFactor)
{
  QColor drawColor = color;
  drawColor.setAlphaF(opacity);

  QPen pen(drawColor, marker.lineWidthPx * zoomFactor);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  double halfWidth = (marker.widthPx * zoomFactor) / 2.0;
  double halfHeight = (marker.heightPx * zoomFactor) / 2.0;

  painter.drawRect(QRectF(center.x() - halfWidth, center.y() - halfHeight,
                          halfWidth * 2, halfHeight * 2));
}

void WorldOverlayRenderer::renderDiamond(QPainter& painter, const QPointF& center,
                                          const WorldOverlay::Marker& marker,
                                          const QColor& color, double opacity, double zoomFactor)
{
  QColor drawColor = color;
  drawColor.setAlphaF(opacity);

  QPen pen(drawColor, marker.lineWidthPx * zoomFactor);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  double halfSize = (marker.sizePx * zoomFactor) / 2.0;

  QPolygonF diamond;
  diamond << QPointF(center.x(), center.y() - halfSize)        // Top
          << QPointF(center.x() + halfSize, center.y())        // Right
          << QPointF(center.x(), center.y() + halfSize)        // Bottom
          << QPointF(center.x() - halfSize, center.y());       // Left

  painter.drawPolygon(diamond);
}

void WorldOverlayRenderer::renderPoint(QPainter& painter, const QPointF& center,
                                        const WorldOverlay::Marker& marker,
                                        const QColor& color, double opacity, double zoomFactor)
{
  QColor drawColor = color;
  drawColor.setAlphaF(opacity);

  painter.setPen(Qt::NoPen);
  painter.setBrush(QBrush(drawColor));

  double radius = (marker.sizePx * zoomFactor) / 2.0;
  painter.drawEllipse(center, radius, radius);
}

void WorldOverlayRenderer::renderLine(QPainter& painter, const QPointF& start, const QPointF& end,
                                       const WorldOverlay::Marker& marker,
                                       const QColor& color, double opacity, double zoomFactor)
{
  QColor drawColor = color;
  drawColor.setAlphaF(opacity);

  QPen pen(drawColor, marker.lineWidthPx * zoomFactor);
  painter.setPen(pen);

  painter.drawLine(start, end);
}

void WorldOverlayRenderer::renderText(QPainter& painter, const QPointF& position,
                                       const WorldOverlay::Marker& marker,
                                       const QColor& color, double opacity, double zoomFactor)
{
  QColor drawColor = color;
  drawColor.setAlphaF(opacity);

  painter.setPen(drawColor);

  QFont font = painter.font();
  font.setPixelSize(static_cast<int>(marker.fontSize * zoomFactor));
  painter.setFont(font);

  painter.drawText(position, marker.label);
}

void WorldOverlayRenderer::renderLabel(QPainter& painter, const QPointF& position,
                                        const QString& label, int fontSize,
                                        const QColor& color, double opacity)
{
  if (label.isEmpty()) return;

  QColor drawColor = color;
  drawColor.setAlphaF(opacity);

  // Draw background for better visibility
  QFont font = painter.font();
  font.setPixelSize(fontSize);
  painter.setFont(font);

  QFontMetrics fm(font);
  QRect textRect = fm.boundingRect(label);
  textRect.moveTopLeft(QPoint(static_cast<int>(position.x()) + m_labelOffsetX,
                               static_cast<int>(position.y()) + m_labelOffsetY));

  // Semi-transparent background
  QColor bgColor(0, 0, 0, 150);
  painter.fillRect(textRect.adjusted(-2, -1, 2, 1), bgColor);

  // Text
  painter.setPen(drawColor);
  painter.drawText(textRect.topLeft() + QPoint(0, fm.ascent()), label);
}

void WorldOverlayRenderer::renderSelectionHighlight(QPainter& painter, const QPointF& center,
                                                     const WorldOverlay::Marker& marker,
                                                     double zoomFactor)
{
  QPen pen(m_selectionColor, m_selectionWidth);
  pen.setStyle(Qt::DashLine);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  // Draw selection box around marker
  double size = 0;
  switch (marker.type) {
    case WorldOverlay::MarkerType::Circle:
      size = marker.radiusPx * zoomFactor + 5;
      painter.drawEllipse(center, size, size);
      break;

    case WorldOverlay::MarkerType::Rectangle:
      size = std::max(marker.widthPx, marker.heightPx) * zoomFactor / 2.0 + 5;
      painter.drawRect(QRectF(center.x() - size, center.y() - size, size * 2, size * 2));
      break;

    default:
      size = marker.sizePx * zoomFactor / 2.0 + 5;
      painter.drawRect(QRectF(center.x() - size, center.y() - size, size * 2, size * 2));
      break;
  }
}

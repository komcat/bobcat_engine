// WorldOverlayRenderer.h
// Renders overlay markers onto camera frames
#pragma once

#include <QObject>
#include <QImage>
#include <QPainter>
#include <QPointF>

namespace WorldOverlay {
  struct Marker;
  struct Layer;
  struct OverlayFile;
}

/**
 * @brief Renders world-locked overlay markers onto camera frames
 *
 * Features:
 * - Renders all marker types (circle, crosshair, rectangle, diamond, point, line, text)
 * - Applies world-to-screen coordinate transformation
 * - Supports layer visibility and opacity
 * - Highlights selected markers
 */
class WorldOverlayRenderer : public QObject
{
  Q_OBJECT

public:
  explicit WorldOverlayRenderer(QObject* parent = nullptr);
  ~WorldOverlayRenderer();

  /**
   * @brief Render overlay onto camera frame
   * @param frame The camera frame to render onto
   * @param overlay The overlay file containing layers and markers
   * @param gantryX Current gantry X position (mm)
   * @param gantryY Current gantry Y position (mm)
   * @param viewCenterX Screen X coordinate of view center (pixels)
   * @param viewCenterY Screen Y coordinate of view center (pixels)
   * @param pxPerMm Pixels per millimeter calibration
   * @param zoomFactor Current zoom level
   * @param selectedMarkerId ID of selected marker (for highlighting)
   * @return The rendered image
   */
  QImage renderOverlay(const QImage& frame,
                       const WorldOverlay::OverlayFile* overlay,
                       double gantryX, double gantryY,
                       double viewCenterX, double viewCenterY,
                       double pxPerMm, double zoomFactor,
                       const QString& selectedMarkerId = QString());

  // Render settings
  void setSelectionColor(const QColor& color) { m_selectionColor = color; }
  void setSelectionWidth(double width) { m_selectionWidth = width; }
  void setLabelOffset(int x, int y) { m_labelOffsetX = x; m_labelOffsetY = y; }

private:
  // World to screen coordinate conversion
  QPointF worldToScreen(double worldX, double worldY,
                        double gantryX, double gantryY,
                        double viewCenterX, double viewCenterY,
                        double pxPerMm, double zoomFactor) const;

  // Render individual marker types
  void renderCircle(QPainter& painter, const QPointF& center,
                    const WorldOverlay::Marker& marker,
                    const QColor& color, double opacity, double zoomFactor);

  void renderCrosshair(QPainter& painter, const QPointF& center,
                       const WorldOverlay::Marker& marker,
                       const QColor& color, double opacity, double zoomFactor);

  void renderRectangle(QPainter& painter, const QPointF& center,
                       const WorldOverlay::Marker& marker,
                       const QColor& color, double opacity, double zoomFactor);

  void renderDiamond(QPainter& painter, const QPointF& center,
                     const WorldOverlay::Marker& marker,
                     const QColor& color, double opacity, double zoomFactor);

  void renderPoint(QPainter& painter, const QPointF& center,
                   const WorldOverlay::Marker& marker,
                   const QColor& color, double opacity, double zoomFactor);

  void renderLine(QPainter& painter, const QPointF& start, const QPointF& end,
                  const WorldOverlay::Marker& marker,
                  const QColor& color, double opacity, double zoomFactor);

  void renderText(QPainter& painter, const QPointF& position,
                  const WorldOverlay::Marker& marker,
                  const QColor& color, double opacity, double zoomFactor);

  void renderLabel(QPainter& painter, const QPointF& position,
                   const QString& label, int fontSize,
                   const QColor& color, double opacity);

  // Render selection highlight
  void renderSelectionHighlight(QPainter& painter, const QPointF& center,
                                 const WorldOverlay::Marker& marker, double zoomFactor);

  // Settings
  QColor m_selectionColor = QColor(255, 255, 0);  // Yellow
  double m_selectionWidth = 3.0;
  int m_labelOffsetX = 10;
  int m_labelOffsetY = -10;
};

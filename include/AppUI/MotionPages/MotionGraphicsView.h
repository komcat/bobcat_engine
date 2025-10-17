// MotionGraphicsView.h
#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QWheelEvent>
#include "PositionAnchor.h"
#include "PositionLabel.h"
#include "DevicePositionMarker.h"
#include "PathPreview.h"  // ✅ NEW
#include <QMap>
#include <QGraphicsLineItem>
#include <QList>
#include <vector>
#include <string>

class MotionGraphicsView : public QGraphicsView
{
  Q_OBJECT

public:
  explicit MotionGraphicsView(QWidget* parent = nullptr);
  ~MotionGraphicsView();

  // Add/remove positions
  void addPosition(const QString& name, double x, double y, double z,
    double labelOffsetX = 0.0, double labelOffsetY = 0.0);
  void clearPositions();
  void updatePositions(const QMap<QString, QVector<double>>& positions,
    const QMap<QString, QPointF>& labelOffsets);

  // Get label offsets (for saving)
  QMap<QString, QPointF> getLabelOffsets() const;

  // Live marker control
  void updateLiveMarker(const QString& deviceName, double x, double y);
  void showLiveMarker(bool show);

  // Path preview
  void showPathPreview(const QString& targetPosition,
    const std::vector<std::string>& waypoints);
  void hidePathPreview();

  // Zoom controls
  void zoomIn();
  void zoomOut();
  void resetZoom();

signals:
  void positionSelected(const QString& name, double x, double y, double z);
  void positionDeselected();
  void labelOffsetChanged(const QString& name, double offsetX, double offsetY);

protected:
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void drawBackground(QPainter* painter, const QRectF& rect) override;

private slots:
  void onSelectionChanged();

private:
  QGraphicsScene* m_scene;

  // Position anchors (fixed dots)
  QMap<QString, PositionAnchor*> m_anchors;

  // Position labels (draggable)
  QMap<QString, PositionLabel*> m_labels;

  // Connection lines
  QMap<QString, QGraphicsLineItem*> m_connectionLines;

  DevicePositionMarker* m_liveMarker;
  PathPreview* m_pathPreview;  // ✅ NEW

  double m_zoomFactor;
  double m_zMin, m_zMax;

  bool m_isPanning;
  QPoint m_lastPanPoint;

  void setupScene();
  void calculateZRange();
  void updateColors();
  void drawGrid(QPainter* painter, const QRectF& rect);
  double calculateGridSpacing(double viewScale);

  QPointF getPositionPoint(const QString& positionName) const;
};
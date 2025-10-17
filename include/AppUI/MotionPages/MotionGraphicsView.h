// MotionGraphicsView.h
#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QWheelEvent>
#include "PositionDot.h"
#include "DevicePositionMarker.h"  // NEW
#include <QMap>
#include <QGraphicsLineItem>
#include <QList>

class MotionGraphicsView : public QGraphicsView
{
  Q_OBJECT

public:
  explicit MotionGraphicsView(QWidget* parent = nullptr);
  ~MotionGraphicsView() = default;

  // Add/remove positions
  void addPosition(const QString& name, double x, double y, double z);
  void clearPositions();
  void updatePositions(const QMap<QString, QVector<double>>& positions);

  // NEW - Live marker control
  void updateLiveMarker(const QString& deviceName, double x, double y);
  void showLiveMarker(bool show);

  // Zoom controls
  void zoomIn();
  void zoomOut();
  void resetZoom();

signals:
  void positionSelected(const QString& name, double x, double y, double z);
  void positionDeselected();

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
  QMap<QString, PositionDot*> m_dots;
  DevicePositionMarker* m_liveMarker;  // NEW

  double m_zoomFactor;
  double m_zMin, m_zMax;

  bool m_isPanning;
  QPoint m_lastPanPoint;

  void setupScene();
  void calculateZRange();
  void updateDotColors();
  void drawGrid(QPainter* painter, const QRectF& rect);
  double calculateGridSpacing(double viewScale);
};
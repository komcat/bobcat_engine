// MotionGraphicsView.h
#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPushButton>
#include <QHBoxLayout>
#include "PositionAnchor.h"
#include "DevicePositionMarker.h"
// PathPreview removed — graph edges show connectivity
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
  void addPosition(const QString& name, double x, double y, double z);
  void clearPositions();
  void updatePositions(const QMap<QString, QVector<double>>& positions);

  // Graph edge lines between positions
  struct GraphEdge { QString from; QString to; };
  void setGraphEdges(const QList<GraphEdge>& edges);
  void highlightEdgesForPosition(const QString& positionName);
  void clearEdgeHighlight();

  // Live marker control
  void updateLiveMarker(const QString& deviceName, double x, double y, double z = 0.0);
  void setLivePositionInfo(double x, double y, double z, double u, double v, double w);
  void showLiveMarker(bool show);

  // Selected position info overlay
  void setSelectedPositionInfo(const QString& name, double x, double y, double z,
    double u, double v, double w);
  void clearSelectedPositionInfo();

  // Path preview removed — graph edges show connectivity

  // Zoom controls
  void zoomIn();
  void zoomOut();
  void resetZoom();

  // Auto-fit and pan controls
  void fitAllPositions();
  void centerOnDeviceMarker();
  void autoFitView();
  void centerOnPosition(const QString& positionName, double zoomLevel = 23.0);

  // View state persistence
  void setDeviceName(const QString& deviceName);
  void saveViewState();
  void restoreViewState();

  // Isometric 3D rendering
  void setZScaleFactor(double factor);
  void setCamera(double azimuthDeg, double elevationDeg);
  void resetCamera();

  // Isometric projection helper
  QPointF toIsometric(double x, double y, double z = 0.0) const;

signals:
  // Kept for API compatibility but no longer emitted from click
  void positionSelected(const QString& name, double x, double y, double z);
  void positionDeselected();

protected:
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void drawBackground(QPainter* painter, const QRectF& rect) override;
  void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
  QGraphicsScene* m_scene;

  // Position anchors (elevated dots, colored by Z)
  QMap<QString, PositionAnchor*> m_anchors;

  // Store positions for lookup
  QMap<QString, QVector<double>> m_positions;

  DevicePositionMarker* m_liveMarker;
  // PathPreview removed

  double m_zoomFactor;
  double m_zMin, m_zMax;

  bool m_isPanning;
  QPoint m_lastPanPoint;
  bool m_isRotating;
  QPoint m_lastRotatePoint;

  QString m_deviceName;

  // Selected position info overlay data
  bool m_hasSelectedPositionInfo;
  QString m_selectedPosName;
  double m_selectedPosX, m_selectedPosY, m_selectedPosZ;
  double m_selectedPosU, m_selectedPosV, m_selectedPosW;

  // Live device position (raw) for overlay display
  bool m_hasLivePositionInfo;
  double m_livePosX, m_livePosY, m_livePosZ;
  double m_livePosU, m_livePosV, m_livePosW;

  // Graph edge lines (parallel lists — m_edgeLines[i] corresponds to m_graphEdges[i])
  QList<GraphEdge> m_graphEdges;
  QList<QGraphicsLineItem*> m_edgeLines;

  // Isometric rendering
  double m_zScaleFactor;
  double m_azimuthDeg;     // Camera rotation around Z axis (default 45)
  double m_elevationDeg;   // Camera tilt angle (default 35.264)
  QString m_highlightedPosition;  // Currently highlighted position for edge restore

  void setupScene();
  void calculateZRange();
  void updateColors();
  void drawGrid(QPainter* painter, const QRectF& rect);
  double calculateGridSpacing(double viewScale);

  QPointF getPositionPoint(const QString& positionName) const;
  void rebuildScene();
  void setupViewButtons();
  QWidget* m_viewButtonBar;
};

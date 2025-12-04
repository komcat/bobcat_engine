// ShapeCreatorDialog.h
// Popup dialog for creating complex overlay shapes from still images
#pragma once

#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QPoint>
#include <QVector>
#include "WorldOverlay.h"

class WorldOverlayManager;

/**
 * @brief Dialog for creating complex shapes by clicking on a still image
 *
 * Supported shapes:
 * - 3-Point Rectangle: Click 3 corners, 4th is calculated
 * - Multi-Point Polygon: Click multiple points to form a polygon
 * - Line Array: Click start/end points for multiple parallel lines
 */
class ShapeCreatorDialog : public QDialog
{
  Q_OBJECT

public:
  enum class ShapeType {
    ThreePointRectangle,  // Click 3 corners, 4th calculated
    Polygon,              // Click multiple points
    LineArray             // Parallel lines
  };

  struct CapturedPoint {
    QPoint screenPos;     // Position in the image (pixels)
    double worldX;        // Calculated world X
    double worldY;        // Calculated world Y
    double worldZ;        // Captured world Z (constant)
  };

  explicit ShapeCreatorDialog(const QImage& stillFrame,
                               double gantryX, double gantryY, double gantryZ,
                               double pxPerMm,
                               WorldOverlayManager* overlayManager,
                               QWidget* parent = nullptr);
  ~ShapeCreatorDialog();

  // Get the created markers
  QVector<WorldOverlay::Marker> getCreatedMarkers() const { return m_createdMarkers; }

signals:
  void shapeCreated(const QVector<WorldOverlay::Marker>& markers);

protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
  void onShapeTypeChanged(int index);
  void onClearPoints();
  void onUndoLastPoint();
  void onCreateShape();
  void onCancel();

private:
  void setupUI();
  void updatePreview();
  void updatePointsList();
  QPoint calculateFourthCorner(const QPoint& p1, const QPoint& p2, const QPoint& p3);
  CapturedPoint screenToWorld(const QPoint& screenPos);
  void createRectangleMarkers(const QString& shapeName);
  void createPolygonMarkers(const QString& shapeName);

  // Still frame data
  QImage m_stillFrame;
  QPixmap m_displayPixmap;
  double m_gantryX;
  double m_gantryY;
  double m_gantryZ;
  double m_pxPerMm;
  double m_displayScale = 1.0;

  // Overlay manager reference
  WorldOverlayManager* m_overlayManager;

  // UI elements
  QLabel* m_imageLabel;
  QLabel* m_instructionLabel;
  QLabel* m_mousePositionLabel;
  QComboBox* m_shapeTypeDropdown;
  QListWidget* m_pointsList;
  QPushButton* m_clearButton;
  QPushButton* m_undoButton;
  QPushButton* m_createButton;
  QPushButton* m_cancelButton;

  // State
  ShapeType m_currentShapeType = ShapeType::ThreePointRectangle;
  QVector<CapturedPoint> m_capturedPoints;
  QVector<WorldOverlay::Marker> m_createdMarkers;
  QPoint m_currentMousePos;
  bool m_mouseOverImage = false;

  // Shape-specific
  int m_requiredPoints = 3;  // For 3-point rectangle
};

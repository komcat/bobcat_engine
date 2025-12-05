// WorldOverlayWidget.h
// Camera view with world-locked overlay markers
#pragma once

#include <QWidget>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QSlider>
#include <QTimer>
#include <QSplitter>
#include <memory>

class CameraManager;
class UnifiedMotionManager;
class ProductPositionManager;
class WorldOverlayManager;
class WorldOverlayRenderer;
class LiveVideoSubscriber;

namespace WorldOverlay {
  struct Marker;
  struct Layer;
}

/**
 * @brief Widget displaying camera view with world-locked overlay markers
 *
 * Features:
 * - Camera selection dropdown
 * - Real-time camera feed with overlay
 * - Layer visibility controls
 * - Marker editing (add, remove, move)
 * - Gantry position tracking for overlay updates
 */
class WorldOverlayWidget : public QWidget
{
  Q_OBJECT

public:
  explicit WorldOverlayWidget(CameraManager* cameraManager,
                               UnifiedMotionManager* motionManager,
                               QWidget* parent = nullptr);
  ~WorldOverlayWidget();

  // Set ProductPositionManager for calibration data
  void setProductPositionManager(ProductPositionManager* positionManager);

  // Update overlay directory when product changes
  void updateOverlayDirectoryForProduct(const QString& productName);

  // Get the overlay manager
  WorldOverlayManager* overlayManager() const { return m_overlayManager; }

  // Add marker at current gantry position
  void addMarkerAtCurrentPosition(const QString& layerName,
                                   WorldOverlay::Marker marker);

public slots:
  // Camera control
  void onCameraChanged(int index);
  void startCamera();
  void stopCamera();

  // Overlay file management
  void onNewOverlay();
  void onLoadOverlay();
  void onSaveOverlay();

  // Layer management
  void onLayerSelectionChanged();
  void onLayerVisibilityChanged(QListWidgetItem* item);
  void onAddLayer();
  void onRemoveLayer();

  // Marker management
  void onAddMarkerClicked();
  void onRemoveMarkerClicked();
  void onClearLayerClicked();
  void onMarkerSelectionChanged();
  void populateMarkerList();
  void onCreateShapeClicked();
  void onMarkerContextMenu(const QPoint& pos);
  void onRenameMarker();

  // Click-to-move and crosshair
  void onClickToMoveToggled(bool checked);
  void onCrosshairToggled(bool checked);

  // Update loop
  void updateOverlay();

signals:
  void markerAdded(const QString& layerName, const QString& markerId);
  void overlayFileChanged(const QString& filename);
  void clickToMoveTriggered(double offsetX_mm, double offsetY_mm);

protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  void setupUI();
  void setupCameraPanel();
  void setupControlPanel();
  void setupConnections();
  void populateCameraDropdown();
  void populateLayerList();
  void updateGantryPosition();
  void handleImageClick(const QPoint& pixmapClickPos);
  void loadPixelToMmCalibration();

  // Dependencies
  CameraManager* m_cameraManager;
  UnifiedMotionManager* m_motionManager;
  ProductPositionManager* m_positionManager = nullptr;

  // Camera subscriber for frame updates
  std::shared_ptr<LiveVideoSubscriber> m_subscriber;

  // Overlay system
  WorldOverlayManager* m_overlayManager;
  WorldOverlayRenderer* m_renderer;

  // Layout
  QSplitter* m_splitter;
  QWidget* m_cameraPanel;
  QWidget* m_controlPanel;

  // Camera controls
  QComboBox* m_cameraDropdown;
  QPushButton* m_startButton;
  QPushButton* m_stopButton;
  QLabel* m_cameraView;
  QLabel* m_positionLabel;
  QLabel* m_mousePositionLabel;  // Shows mouse position in pixmap coords

  // Overlay file controls
  QComboBox* m_overlayFileDropdown;
  QPushButton* m_newOverlayButton;
  QPushButton* m_loadOverlayButton;
  QPushButton* m_saveOverlayButton;

  // Layer controls
  QListWidget* m_layerList;
  QPushButton* m_addLayerButton;
  QPushButton* m_removeLayerButton;
  QSlider* m_opacitySlider;

  // Marker controls
  QListWidget* m_markerList;
  QPushButton* m_addMarkerButton;
  QPushButton* m_removeMarkerButton;
  QPushButton* m_clearLayerButton;
  QPushButton* m_createShapeButton;
  QComboBox* m_markerTypeDropdown;

  // Click-to-move and crosshair controls
  QCheckBox* m_clickToMoveCheckbox;
  QCheckBox* m_crosshairCheckbox;

  // State
  QString m_currentCamera;
  bool m_cameraRunning = false;
  double m_gantryX = 0.0;
  double m_gantryY = 0.0;
  double m_gantryZ = 0.0;
  double m_gantryXSmoothed = 0.0;  // Smoothed position for overlay rendering
  double m_gantryYSmoothed = 0.0;
  double m_zoomFactor = 1.0;
  double m_smoothingFactor = 0.3;  // 0.0 = no smoothing, 1.0 = instant (0.3 = smooth)

  // Click-to-move state
  bool m_clickToMoveEnabled = false;
  bool m_crosshairEnabled = false;
  bool m_mouseOverImage = false;
  QPoint m_mousePosInPixmap;
  double m_pixelToMmX = 0.00248;
  double m_pixelToMmY = 0.00248;

  // Mouse state for dragging
  bool m_isDragging = false;
  QPoint m_lastMousePos;
  QString m_selectedMarkerId;

  // Update timer
  QTimer* m_updateTimer;
  QTimer* m_positionTimer;

  // Current frame
  QImage m_currentFrame;
  int m_originalFrameWidth = 0;   // Original camera frame dimensions for click-to-move
  int m_originalFrameHeight = 0;
};

// LiveViewCameraWidget.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QTimer>

class CameraManager;
class CameraImageWidget;
class LiveVideoSubscriber;
class UnifiedMotionManager;
class WorldOverlayManager;
class WorldOverlayRenderer;

/**
 * @brief Reusable live camera view widget with camera selector
 *
 * Features:
 * - Dropdown to select which camera to display
 * - Real-time camera feed display using CameraImageWidget
 * - Auto-refresh camera list
 * - Polls subscriber for new frames at 30 FPS
 *
 * Usage:
 *   LiveViewCameraWidget* view = new LiveViewCameraWidget(cameraManager);
 *   // User selects camera from dropdown
 */
class LiveViewCameraWidget : public QWidget
{
  Q_OBJECT

public:
  explicit LiveViewCameraWidget(CameraManager* cameraManager, QWidget* parent = nullptr);
  ~LiveViewCameraWidget();

  // Get currently selected camera
  std::string getCurrentCamera() const;

  // Set which camera to display
  void setCamera(const std::string& cameraId);

  // Access to image widget for external controls
  CameraImageWidget* getImageWidget() { return m_imageWidget; }

  // Pause/resume camera updates (for performance optimization)
  void setPaused(bool paused);
  bool isPaused() const { return m_isPaused; }

  // Overlay support - allows showing World Overlay markers on live view
  void setOverlayManager(WorldOverlayManager* overlayManager);
  void setMotionManager(UnifiedMotionManager* motionManager);
  void setOverlayEnabled(bool enabled);
  bool isOverlayEnabled() const { return m_overlayEnabled; }

signals:
  void cameraChanged(const std::string& cameraId);
  void overlayToggled(bool enabled);

private slots:
  void onCameraSelected(int index);
  void onRefreshCameras();
  void onUpdateFrame();  // Poll for new frames
  void onCrosshairToggled(bool checked);
  void onZoomChanged(int zoomPercent);
  void onOverlayToggled(bool checked);
  void updateGantryPosition();

private:
  void setupUI();
  void refreshCameraList();
  void subscribeToCameraFeed(const std::string& cameraId);
  void unsubscribeFromCameraFeed();

  CameraManager* m_cameraManager;

  // UI Components
  QComboBox* m_cameraSelector;
  QLabel* m_cameraInfoLabel;
  CameraImageWidget* m_imageWidget;
  QCheckBox* m_crosshairCheckbox;
  QCheckBox* m_overlayCheckbox;
  QPushButton* m_zoom1xButton;
  QPushButton* m_zoom15xButton;
  QPushButton* m_zoom2xButton;

  // Frame update
  std::shared_ptr<LiveVideoSubscriber> m_subscriber;  // Changed to shared_ptr
  QTimer* m_frameUpdateTimer;

  std::string m_currentCameraId;
  QTimer* m_refreshTimer;
  bool m_isPaused;

  // Overlay support
  WorldOverlayManager* m_overlayManager = nullptr;
  UnifiedMotionManager* m_motionManager = nullptr;
  WorldOverlayRenderer* m_overlayRenderer = nullptr;
  QTimer* m_positionTimer = nullptr;
  bool m_overlayEnabled = false;
  double m_gantryX = 0.0;
  double m_gantryY = 0.0;
  double m_gantryZ = 0.0;
};
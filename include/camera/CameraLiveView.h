// CameraLiveView.h - Using CameraImageWidget
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <memory>
#include <atomic>
#include <chrono>

// Forward declarations
class CameraManager;
class CameraConfigManager;
class LiveVideoSubscriber;
class CameraImageWidget;  // ✅ ADD THIS

/**
 * @brief Standalone live video view using optimized pub-sub pattern
 *
 * Performance optimizations:
 * - CameraImageWidget for reusable rendering
 * - Fast transformation for real-time scaling
 * - Direct memory copy to buffer
 */
class CameraLiveView : public QWidget
{
  Q_OBJECT

public:
  explicit CameraLiveView(QWidget* parent = nullptr);
  ~CameraLiveView();

private slots:
  void onConnectClicked();
  void onDisconnectClicked();
  void onUpdateDisplay();
  void onZoomSliderChanged(int value);
  void onZoomSpinBoxChanged(int value);
  void onZoomResetClicked();

private:
  void setupUI();
  void loadCameraConfig();
  void updateConnectionStatus(bool connected);
  void updateZoomLabel(int zoomPercent);

  // UI Components
  QPushButton* m_connectButton;
  QPushButton* m_disconnectButton;
  CameraImageWidget* m_viewport;  // ✅ CHANGED from QLabel* m_videoLabel
  QLabel* m_statusLabel;
  QLabel* m_fpsLabel;
  QLabel* m_performanceLabel;

  // Zoom controls
  QSlider* m_zoomSlider;
  QSpinBox* m_zoomSpinBox;
  QPushButton* m_zoomResetButton;
  QLabel* m_zoomLabel;

  // Camera components
  std::unique_ptr<CameraManager> m_cameraManager;
  std::unique_ptr<CameraConfigManager> m_configManager;
  std::shared_ptr<LiveVideoSubscriber> m_subscriber;
  QTimer* m_displayUpdateTimer;

  std::string m_cameraId;
  bool m_isConnected;

  // Performance tracking
  std::chrono::steady_clock::time_point m_lastUpdateTime;
  int m_frameCounter;
  double m_avgProcessingTime;
  uint64_t m_lastTotalFrames;
};
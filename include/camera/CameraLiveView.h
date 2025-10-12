// CameraLiveView.h - Standalone view using pub-sub pattern
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QImage>
#include <memory>
#include <atomic>
#include <chrono>

// Forward declarations
class CameraManager;
class CameraConfigManager;
class LiveVideoSubscriber;

/**
 * @brief Standalone live video view using pub-sub pattern
 *
 * Subscribes to camera broadcasts instead of using worker threads
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

  // UI update timer (polls subscriber for new frames)
  void onUpdateDisplay();

  // Zoom control slots
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
  QLabel* m_videoLabel;
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

  // Pub-Sub components (replaces worker thread!)
  std::shared_ptr<LiveVideoSubscriber> m_subscriber;
  QTimer* m_displayUpdateTimer;

  std::string m_cameraId;
  bool m_isConnected;
  std::atomic<int> m_zoomPercent;

  // Performance tracking
  std::chrono::steady_clock::time_point m_lastUpdateTime;
  int m_frameCounter;
  double m_avgProcessingTime;
};
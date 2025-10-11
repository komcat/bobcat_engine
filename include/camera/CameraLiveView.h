// CameraLiveView.h - Updated for worker thread
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QImage>
#include <QThread>
#include <memory>

// Forward declarations
class CameraManager;
class CameraConfigManager;
class CameraWorker;

class CameraLiveView : public QWidget
{
  Q_OBJECT

public:
  explicit CameraLiveView(QWidget* parent = nullptr);
  ~CameraLiveView();

private slots:
  void onConnectClicked();
  void onDisconnectClicked();

  // Frame display (runs on UI thread)
  void onFrameReady(QImage image);

  // Performance monitoring
  void onFpsUpdated(double fps);
  void onProcessingTimeUpdated(double ms);

  // Error handling
  void onWorkerError(QString errorMsg);
  void onWorkerStatus(QString msg);

private:
  void setupUI();
  void loadCameraConfig();
  void updateConnectionStatus(bool connected);
  void cleanupWorkerThread();

  // UI Components
  QPushButton* m_connectButton;
  QPushButton* m_disconnectButton;
  QLabel* m_videoLabel;
  QLabel* m_statusLabel;
  QLabel* m_fpsLabel;        // NEW: FPS display
  QLabel* m_performanceLabel; // NEW: Performance info

  // Camera components
  std::unique_ptr<CameraManager> m_cameraManager;
  std::unique_ptr<CameraConfigManager> m_configManager;

  // Worker thread components
  QThread* m_workerThread;
  CameraWorker* m_worker;

  std::string m_cameraId;
  bool m_isConnected;
};
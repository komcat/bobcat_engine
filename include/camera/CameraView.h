// CameraView.h - Widget for displaying a single camera
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QImage>
#include <QThread>
#include <memory>
#include <string>

// Forward declarations
class CameraManager;
class CameraWorker;

/**
 * @brief Widget for controlling and displaying a single camera
 *
 * Each camera gets its own CameraView instance with:
 * - Independent worker thread
 * - Independent zoom controls
 * - Independent connect/disconnect
 * - Performance monitoring
 */
class CameraView : public QWidget
{
  Q_OBJECT

public:
  explicit CameraView(CameraManager* cameraManager,
    const std::string& cameraId,
    const std::string& displayName,
    QWidget* parent = nullptr);
  ~CameraView();

  // Camera control
  bool connectCamera();
  void disconnectCamera();
  bool isConnected() const { return m_isConnected; }
  std::string getCameraId() const { return m_cameraId; }

signals:
  // Status signals (can be connected to main window)
  void cameraConnected(QString cameraId);
  void cameraDisconnected(QString cameraId);
  void cameraError(QString cameraId, QString error);

private slots:
  void onConnectClicked();
  void onDisconnectClicked();

  // Frame display
  void onFrameReady(QImage image);

  // Performance monitoring
  void onFpsUpdated(double fps);
  void onProcessingTimeUpdated(double ms);

  // Error handling
  void onWorkerError(QString errorMsg);
  void onWorkerStatus(QString msg);

  // Zoom control
  void onZoomSliderChanged(int value);
  void onZoomSpinBoxChanged(int value);
  void onZoomResetClicked();
  void onZoomChanged(int zoomPercent);

private:
  void setupUI();
  void updateConnectionStatus(bool connected);
  void cleanupWorkerThread();
  void updateZoomLabel(int zoomPercent);

  // Camera info
  CameraManager* m_cameraManager;
  std::string m_cameraId;
  std::string m_displayName;
  bool m_isConnected;

  // Worker thread components
  QThread* m_workerThread;
  CameraWorker* m_worker;

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
};
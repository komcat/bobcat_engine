// CameraView.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QTimer>
#include <memory>
#include <string>
#include <atomic>
#include <chrono>

// Forward declarations
class CameraManager;
class LiveVideoSubscriber;
class CameraImageWidget;  // ✅ ADD THIS

class CameraView : public QWidget
{
  Q_OBJECT

public:
  explicit CameraView(CameraManager* cameraManager,
    const std::string& cameraId,
    const std::string& displayName,
    QWidget* parent = nullptr);
  ~CameraView();

  bool connectCamera();
  void disconnectCamera();
  bool isConnected() const { return m_isConnected; }
  std::string getCameraId() const { return m_cameraId; }

signals:
  void cameraConnected(QString cameraId);
  void cameraDisconnected(QString cameraId);
  void cameraError(QString cameraId, QString error);

private slots:
  void onConnectClicked();
  void onDisconnectClicked();
  void onUpdateDisplay();
  void onZoomSliderChanged(int value);
  void onZoomSpinBoxChanged(int value);
  void onZoomResetClicked();

private:
  void setupUI();
  void updateConnectionStatus(bool connected);
  void updateZoomLabel(int zoomPercent);

  // Camera info
  CameraManager* m_cameraManager;
  std::string m_cameraId;
  std::string m_displayName;
  bool m_isConnected;

  // Pub-Sub components
  std::shared_ptr<LiveVideoSubscriber> m_subscriber;
  QTimer* m_displayUpdateTimer;

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

  // Performance tracking
  std::chrono::steady_clock::time_point m_lastUpdateTime;
  int m_frameCounter;
  double m_avgProcessingTime;
  uint64_t m_lastTotalFrames;
};
// CameraView.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>      // ✅ NEW
#include <QGroupBox>      // ✅ NEW
#include <QVBoxLayout>
#include <QTimer>
#include <memory>
#include <string>
#include <atomic>
#include <chrono>

struct ApplicationContext;
class LiveVideoSubscriber;
class CameraImageWidget;

class CameraView : public QWidget
{
  Q_OBJECT

public:
  explicit CameraView(ApplicationContext* context,
    const std::string& cameraId,
    const std::string& displayName,
    QWidget* parent = nullptr);
  ~CameraView();

  bool connectCamera();
  void disconnectCamera();
  bool isConnected() const { return m_isConnected; }
  std::string getCameraId() const { return m_cameraId; }

  // Check if camera is already connected in CameraManager and sync state
  void syncWithManagerState();

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

  // ✅ NEW - Camera settings slots
  void onAutoExposureChanged(int state);
  void onAutoGainChanged(int state);
  void onExposureSliderChanged(int value);
  void onGainSliderChanged(int value);
  void onSaveImageClicked();

protected:
  void showEvent(QShowEvent* event) override;  // ✅ NEW - Force refresh on show

private:
  void setupUI();
  void updateConnectionStatus(bool connected);
  void updateZoomLabel(int zoomPercent);
  void updateExposureLabel(int microseconds);  // ✅ NEW
  void updateGainLabel(double gain);           // ✅ NEW
  void applyCameraSettings();                  // ✅ NEW
  void loadCurrentSettings();                  // ✅ NEW
  QString generateSaveFilename();              // ✅ NEW

  ApplicationContext* m_context;
  std::string m_cameraId;
  std::string m_displayName;
  bool m_isConnected;

  std::shared_ptr<LiveVideoSubscriber> m_subscriber;
  QTimer* m_displayUpdateTimer;

  QPushButton* m_connectButton;
  QPushButton* m_disconnectButton;
  CameraImageWidget* m_viewport;
  QLabel* m_statusLabel;
  QLabel* m_fpsLabel;
  QLabel* m_performanceLabel;

  // ✅ NEW - Camera settings controls
  QGroupBox* m_settingsGroup;
  QCheckBox* m_autoExposureCheck;
  QCheckBox* m_autoGainCheck;
  QSlider* m_exposureSlider;
  QLabel* m_exposureValueLabel;
  QSlider* m_gainSlider;
  QLabel* m_gainValueLabel;
  QPushButton* m_saveImageButton;

  QSlider* m_zoomSlider;
  QSpinBox* m_zoomSpinBox;
  QPushButton* m_zoomResetButton;
  QLabel* m_zoomLabel;

  std::chrono::steady_clock::time_point m_lastUpdateTime;
  int m_frameCounter;
  double m_avgProcessingTime;
  uint64_t m_lastTotalFrames;
};
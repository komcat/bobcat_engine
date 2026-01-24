// TECControlPage.h
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QGroupBox>
#include <QSpinBox>
#include <QFrame>

struct ApplicationContext;
class TECConnectionWorker;
class TECParameterWorker;

/**
 * @brief TEC Control Page
 * Left panel (15%): Device list
 * Right panel (85%): Controls with 500ms polling
 */
class TECControlPage : public QWidget
{
  Q_OBJECT

public:
  explicit TECControlPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~TECControlPage();

protected:
  // Lifecycle events to control polling
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private slots:
  // Device selection
  void onDeviceSelected();

  // Connection
  void onConnectClicked();
  void onDisconnectClicked();

  // TEC controls
  void onTemperatureSliderChanged(int value);
  void onTemperatureSpinBoxChanged(int value);
  void onTECOnClicked();
  void onTECOffClicked();

  // Laser controls
  void onLaserCurrentSliderChanged(int value);
  void onLaserCurrentSpinBoxChanged(int value);
  void onLaserOnClicked();
  void onLaserOffClicked();

  // Presets
  void onPreset1Clicked();  // 25C, 150mA, TEC ON, Laser ON
  void onPreset2Clicked();  // 25C, 250mA, TEC ON, Laser ON

  // Polling
  void onPollTimer();

  // Connection worker callbacks
  void onConnectionSuccess(QString deviceName);
  void onConnectionFailed(QString deviceName, QString error);
  void onDisconnectionComplete(QString deviceName);
  void onConnectionOperationStarted(QString operation, QString deviceName);

  // Parameter worker callbacks
  void onStatusUpdated(double currentTemp, double tecCurrent, double laserCurrent, bool tecOn, bool laserOn);
  void onParameterCommandError(QString operation, QString error);

private:
  void setupUI();
  void createLeftPanel();
  void createRightPanel();

  void updateDeviceList();
  void updateControls();
  void updateStatusDisplay();

  void enableControls(bool enabled);
  void applyPreset(double temp, double current, bool tecOn, bool laserOn);

  QString getCurrentDevice() const;
  bool deviceSupportsLaser() const;

  ApplicationContext* m_context;

  // Left panel
  QListWidget* m_deviceList;

  // Right panel - Connection
  QPushButton* m_connectButton;
  QPushButton* m_disconnectButton;
  QLabel* m_connectionStatus;

  // Right panel - TEC controls
  QSlider* m_temperatureSlider;
  QSpinBox* m_temperatureSpinBox;
  QLabel* m_currentTemperature;
  QLabel* m_currentTECCurrent;
  QPushButton* m_tecOnButton;
  QPushButton* m_tecOffButton;
  QFrame* m_tecStatusFrame;
  QLabel* m_tecStatusLabel;

  // Right panel - Laser controls
  QSlider* m_laserCurrentSlider;
  QSpinBox* m_laserCurrentSpinBox;
  QLabel* m_actualLaserCurrent;
  QPushButton* m_laserOnButton;
  QPushButton* m_laserOffButton;
  QFrame* m_laserStatusFrame;
  QLabel* m_laserStatusLabel;

  // Right panel - Presets
  QPushButton* m_preset1Button;
  QPushButton* m_preset2Button;

  // Polling timer
  QTimer* m_pollTimer;

  // State tracking
  bool m_isConnected;
  bool m_isUpdatingFromDevice;  // Prevent loops during polling updates

  // Workers for non-blocking operations
  TECConnectionWorker* m_connectionWorker;
  TECParameterWorker* m_parameterWorker;
  bool m_connectionPending;
};
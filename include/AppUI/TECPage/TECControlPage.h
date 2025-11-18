// TECControlPage.h
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QGroupBox>

struct ApplicationContext;

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

private slots:
  // Device selection
  void onDeviceSelected();

  // Connection
  void onConnectClicked();
  void onDisconnectClicked();

  // TEC controls
  void onTemperatureChanged(int value);
  void onTECToggled();

  // Laser controls
  void onLaserCurrentChanged(int value);
  void onLaserToggled();

  // Presets
  void onPreset1Clicked();  // 25C, 150mA, TEC ON, Laser ON
  void onPreset2Clicked();  // 25C, 250mA, TEC ON, Laser ON

  // Polling
  void onPollTimer();

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
  QLabel* m_temperatureValue;
  QLabel* m_currentTemperature;
  QLabel* m_currentTECCurrent;
  QPushButton* m_tecToggle;

  // Right panel - Laser controls
  QSlider* m_laserCurrentSlider;
  QLabel* m_laserCurrentValue;
  QLabel* m_actualLaserCurrent;
  QPushButton* m_laserToggle;

  // Right panel - Presets
  QPushButton* m_preset1Button;
  QPushButton* m_preset2Button;

  // Right panel - Status
  QLabel* m_tecStatus;
  QLabel* m_laserStatus;

  // Polling timer
  QTimer* m_pollTimer;

  // State tracking
  bool m_isConnected;
  bool m_isUpdatingFromDevice;  // Prevent loops during polling updates
};
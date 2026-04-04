// PowerSupplyControlPage.h - UI page for power supply control
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTimer>
#include <QListWidget>
#include <QFrame>
#include <QVBoxLayout>
#include <QTabWidget>
#include <vector>

struct ApplicationContext;
class PowerSupplyManager;
class PSUParameterWorker;

/**
 * @brief Per-channel control widgets
 */
struct ChannelControlWidgets
{
  int channel = 1;
  // Controls
  QPushButton* outputOnButton = nullptr;
  QPushButton* outputOffButton = nullptr;
  QDoubleSpinBox* voltageSpinBox = nullptr;
  QPushButton* setVoltageButton = nullptr;
  QDoubleSpinBox* currentSpinBox = nullptr;
  QPushButton* setCurrentButton = nullptr;
  QDoubleSpinBox* ovpSpinBox = nullptr;
  QPushButton* setOVPButton = nullptr;
  QDoubleSpinBox* ocpSpinBox = nullptr;
  QPushButton* setOCPButton = nullptr;
  // Embedded live measurements
  QLabel* outputStateLabel = nullptr;
  QLabel* voltageLabel = nullptr;
  QLabel* currentLabel = nullptr;
  QPushButton* refreshButton = nullptr;
};

/**
 * @brief Control page for power supply devices
 * Two-panel layout: Device list (left) + Channel tabs with embedded measurements (right)
 */
class PowerSupplyControlPage : public QWidget
{
  Q_OBJECT

public:
  explicit PowerSupplyControlPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~PowerSupplyControlPage();

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private slots:
  // Device management
  void onDeviceSelected(int row);
  void onConnectClicked();
  void onDisconnectClicked();

  // Per-channel control slots
  void onOutputOnClicked(int channel);
  void onOutputOffClicked(int channel);
  void onSetVoltageClicked(int channel);
  void onSetCurrentClicked(int channel);
  void onSetOVPClicked(int channel);
  void onSetOCPClicked(int channel);

  // Measurement update
  void onRefreshClicked();
  void updateMeasurements();

  // Worker signal handlers
  void onWorkerVoltageApplied(int channel, double voltage);
  void onWorkerCurrentApplied(int channel, double current);
  void onWorkerOVPApplied(int channel, double ovp);
  void onWorkerOCPApplied(int channel, double ocp);
  void onWorkerOutputChanged(int channel, bool on);
  void onWorkerMeasurementsUpdated(int channel, double voltage, double current, bool outputOn);
  void onWorkerError(int channel, QString operation, QString error);
  void onWorkerDeviceConnected(QString deviceName);
  void onWorkerDeviceDisconnected(QString deviceName);

private:
  void setupUI();
  void createLeftPanel(QWidget* leftPanel);
  void createMiddlePanel(QWidget* middlePanel);

  QWidget* createChannelTab(int channel);

  void loadDevices();
  void updateConnectionStatus();
  void updateDeviceList();
  void rebuildChannelTabs();
  void showError(const QString& message);
  void showSuccess(const QString& message);
  QString createStyleSheet();

  ApplicationContext* m_context;
  PowerSupplyManager* m_psuManager;

  // Left Panel - Device List
  QListWidget* m_deviceList;
  QPushButton* m_connectButton;
  QPushButton* m_disconnectButton;
  QLabel* m_connectionStatus;
  QLabel* m_deviceInfoLabel;
  QLabel* m_statusLabel;

  // Middle Panel - Channel Tabs
  QWidget* m_middlePanel;
  QTabWidget* m_channelTabs;
  std::vector<ChannelControlWidgets> m_channelControls;

  // Refresh timer (fallback when worker not available)
  QTimer* m_refreshTimer;

  // Current device
  QString m_currentDevice;
  int m_currentChannelCount = 1;

  // Background worker for non-blocking operations
  PSUParameterWorker* m_parameterWorker = nullptr;
  bool m_useParameterWorker = true;
  int m_outputCommandPending = 0; // Suppress polling output updates while command in flight
};

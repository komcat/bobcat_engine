// SMUControlPage.h - UI page for SMU control (Tabbed Interface)
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QFrame>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QShowEvent>
#include <QHideEvent>
#include <vector>

struct ApplicationContext;
class SMUManager;
class SMUConnectionWorker;
class SMUParameterWorker;

/**
 * @brief Control page for SMU devices
 * Two-panel layout: Device list (left) + Controls (right)
 * Simple design following PSU pattern
 */
class SMUControlPage : public QWidget
{
  Q_OBJECT

public:
  explicit SMUControlPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~SMUControlPage();

protected:
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private slots:
  // Device management
  void onDeviceSelected(int row);
  void onConnectClicked();
  void onDisconnectClicked();

  // Channel selection
  void onChannelSelected(int index);
  void onChannelTabChanged(int index);

  // Measurement mode
  void onModeChanged(int index);

  // Measurement type selection
  void onMeasurementTypeChanged(int index);

  // Output control
  void onOutputOnClicked();
  void onOutputOffClicked();
  void updateOutputState();

  // Set voltage/current/compliance (NEW)
  void onSetVoltageClicked();
  void onSetCurrentClicked();
  void onSetVoltageComplianceClicked();
  void onSetCurrentComplianceClicked();

  // Polling control
  void onAddPollingClicked();
  void onStopSelectedSessionClicked();
  void onStopAllSessionsClicked();
  void refreshActiveSessionsList();

  // Measurement update
  void onRefreshClicked();
  void updateMeasurements();

  // Polling measurement callback
  void onPollingMeasurement(QString deviceName, int channel, double current, QString timestamp);

  // Connection worker callbacks
  void onConnectionSuccess(QString deviceName, QString deviceId);
  void onConnectionFailed(QString deviceName, QString error);
  void onDisconnectionComplete(QString deviceName);
  void onConnectionOperationStarted(QString operation, QString deviceName);

  // Parameter worker callbacks
  void onMeasurementsUpdated(int channel, double voltage, double current, bool outputOn);
  void onOutputStateChangedByWorker(bool on);
  void onParameterCommandError(QString operation, QString error);
  void onParameterCommandSuccess(QString operation, QString message);

private:
  void setupUI();
  void createConnectionTab();
  void createSimpleTab();
  void createPollingTab();

  void loadDevices();
  void updateConnectionStatus();
  void updateDeviceList();
  void updateChannelList();
  void updateDeviceCapabilities();  // Update UI based on device mode (MEASURE_ONLY vs source-measure)
  void updateDeviceLabels();  // Update device name labels in Simple and Polling tabs
  void showError(const QString& message);
  void showSuccess(const QString& message);
  QString createStyleSheet();

  ApplicationContext* m_context;
  SMUManager* m_smuManager;

  // Main tab widget
  QTabWidget* m_tabWidget;

  // ===== CONNECTION TAB =====
  QListWidget* m_deviceList;
  QPushButton* m_connectButton;
  QPushButton* m_disconnectButton;
  QLabel* m_connectionStatus;
  QLabel* m_deviceInfoLabel;

  // ===== SIMPLE TAB =====
  // Device indicator
  QLabel* m_simpleDeviceLabel;
  QLabel* m_simpleConnectionLabel;

  // Channel tabs (replacing dropdown)
  QTabWidget* m_channelTabWidget;
  QPointer<QWidget> m_channelControlsWidget;  // Shared controls widget that moves between tabs (QPointer for safety)

  // Channel selection (hidden, for compatibility)
  QComboBox* m_channelComboBox;
  QLabel* m_channelCountLabel;

  // Output control
  QGroupBox* m_outputControlGroup;
  QPushButton* m_outputOnButton;
  QPushButton* m_outputOffButton;
  QLabel* m_outputStateLabel;

  // Read measurements
  QLabel* m_voltageReadLabel;
  QLabel* m_currentReadLabel;
  QPushButton* m_refreshButton;

  // Set voltage/current (NEW)
  QDoubleSpinBox* m_setVoltageSpinBox;
  QPushButton* m_setVoltageButton;
  QDoubleSpinBox* m_setCurrentSpinBox;
  QPushButton* m_setCurrentButton;
  QDoubleSpinBox* m_voltageComplianceSpinBox;
  QPushButton* m_setVoltageComplianceButton;
  QDoubleSpinBox* m_currentComplianceSpinBox;
  QPushButton* m_setCurrentComplianceButton;

  // ===== POLLING TAB =====
  // Device indicator
  QLabel* m_pollingDeviceLabel;

  // Configuration for new polling session
  QComboBox* m_pollingChannelComboBox;
  QComboBox* m_pollingModeComboBox;
  QComboBox* m_pollingTypeComboBox;
  QComboBox* m_pollingRangeComboBox;  // Measurement range selection
  QPushButton* m_addPollingButton;

  // Active polling sessions list
  QListWidget* m_activeSessionsList;
  QPushButton* m_stopSelectedSessionButton;
  QPushButton* m_stopAllSessionsButton;

  // Live measurements display (for all sessions)
  QLabel* m_pollingValueLabel;
  QLabel* m_pollingSampleLabel;

  // ===== SHARED =====
  QLabel* m_statusLabel;
  QString m_currentDevice;

  // ===== CONNECTION WORKER =====
  SMUConnectionWorker* m_connectionWorker;
  bool m_connectionPending;  // True when waiting for connection result

  // ===== PARAMETER WORKER =====
  SMUParameterWorker* m_parameterWorker;

  // ===== CACHED STATE (updated by worker) =====
  bool m_cachedOutputOn = false;  // Cached output state from worker
};

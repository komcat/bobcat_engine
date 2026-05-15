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
#include <QShowEvent>
#include <QHideEvent>
#include <QTimer>
#include <vector>

struct ApplicationContext;
class SMUManager;
class SMUConnectionWorker;
class SMUParameterWorker;

struct SMUChannelWidgets
{
  int channel = 1;
  // Measurement display
  QLabel* outputStateLabel = nullptr;
  QLabel* voltageLabel = nullptr;
  QLabel* currentLabel = nullptr;
  QPushButton* refreshButton = nullptr;
  // Controls
  QDoubleSpinBox* voltageSpinBox = nullptr;
  QPushButton* setVoltageButton = nullptr;
  QDoubleSpinBox* currentSpinBox = nullptr;
  QPushButton* setCurrentButton = nullptr;
  QPushButton* outputOnButton = nullptr;
  QPushButton* outputOffButton = nullptr;
  QGroupBox* outputControlGroup = nullptr;
  // Cached display state — diff before setText / setStyleSheet to avoid
  // forcing a full QSS recompute on every measurement tick.
  QString lastVoltageText;
  QString lastCurrentText;
  // -1=unknown (first paint), 0=off, 1=on, 2=disconnected
  int lastOutputState = -1;
};

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
  void onOutputOnClicked(int channel);
  void onOutputOffClicked(int channel);
  void updateOutputState();

  // Set voltage/current/compliance (NEW)
  void onSetVoltageClicked(int channel);
  void onSetCurrentClicked(int channel);
  void onSetVoltageComplianceClicked();
  void onSetCurrentComplianceClicked();

  // Polling control
  void onAddPollingClicked();
  void onStopSelectedSessionClicked();
  void onStopAllSessionsClicked();
  void refreshActiveSessionsList();

  // Measurement update
  void onRefreshClicked(int channel);
  void updateMeasurements();

  // Polling measurement callback
  void onPollingMeasurement(QString deviceName, int channel, double current, QString timestamp);

  // Slow-tick UI refresh for polling tab — decouples label/list redraws
  // from the 10-20 Hz measurementReady signal so SMU polling no longer
  // stalls the Qt camera window (see commit 8dd4ce1 for the same pattern
  // applied to motion + chart overlay).
  void onPollingUiTick();

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
  void createSimpleTab();
  void createPollingTab();
  QWidget* createSMUChannelTab(int channel);
  void rebuildSimpleTabs();

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

  // Channel tabs (for channel selection)
  QTabWidget* m_channelTabWidget;

  // Channel selection (hidden, for compatibility)
  QComboBox* m_channelComboBox;
  QLabel* m_channelCountLabel;

  // Per-channel widget sets
  std::vector<SMUChannelWidgets> m_channelControls;

  // Compliance controls (shared, not per-channel)
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
  int m_outputCommandPending = 0; // Suppress polling output updates while command in flight

  // ===== POLLING UI THROTTLE =====
  // Latest sample text cached by onPollingMeasurement; drained by onPollingUiTick.
  QTimer* m_pollingUiTimer = nullptr;
  QString m_pollingPendingValueText;
  bool    m_pollingValueDirty = false;
  int     m_pollingSessionTickCounter = 0;
};

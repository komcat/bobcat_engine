// SMUControlPage.h - UI page for SMU control
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <QListWidget>
#include <QFrame>
#include <QVBoxLayout>
#include <vector>

struct ApplicationContext;
class SMUManager;

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
  ~SMUControlPage() = default;

private slots:
  // Device management
  void onDeviceSelected(int row);
  void onConnectClicked();
  void onDisconnectClicked();

  // Channel selection
  void onChannelSelected(int index);

  // Measurement mode
  void onModeChanged(int index);

  // Measurement type selection
  void onMeasurementTypeChanged(int index);

  // Output control
  void onOutputOnClicked();
  void onOutputOffClicked();
  void updateOutputState();

  // Polling control
  void onStartPollingClicked();
  void onStopPollingClicked();

  // Measurement update
  void onRefreshClicked();
  void updateMeasurements();

  // Polling measurement callback
  void onPollingMeasurement(QString deviceName, int channel, double current, QString timestamp);

private:
  void setupUI();
  void createLeftPanel(QWidget* leftPanel);
  void createRightPanel(QWidget* rightPanel);
  void createChannelSection(QVBoxLayout* layout);
  void createOutputControlSection(QVBoxLayout* layout);
  void createMeasurementSection(QVBoxLayout* layout);
  void createPollingSection(QVBoxLayout* layout);

  void loadDevices();
  void updateConnectionStatus();
  void updateDeviceList();
  void updateChannelList();
  void showError(const QString& message);
  void showSuccess(const QString& message);
  QString createStyleSheet();

  ApplicationContext* m_context;
  SMUManager* m_smuManager;

  // Left Panel - Device List
  QListWidget* m_deviceList;
  QPushButton* m_connectButton;
  QPushButton* m_disconnectButton;
  QLabel* m_connectionStatus;

  // Right Panel - Controls
  QWidget* m_controlPanel;

  // Channel selection
  QComboBox* m_channelComboBox;
  QLabel* m_channelCountLabel;

  // Measurement mode
  QComboBox* m_modeComboBox;

  // Measurement type (Current/Voltage)
  QComboBox* m_measurementTypeComboBox;

  // Output control
  QPushButton* m_outputOnButton;
  QPushButton* m_outputOffButton;
  QLabel* m_outputStateLabel;

  // Measurements (per channel)
  std::vector<QLabel*> m_channelReadLabels;
  QLabel* m_currentChannelReadLabel;  // Current selected channel

  // Polling control
  QPushButton* m_startPollingButton;
  QPushButton* m_stopPollingButton;
  QLabel* m_pollingStatusLabel;
  QLabel* m_pollingValueLabel;     // Real-time polled value
  QLabel* m_pollingSampleLabel;    // Sample count

  // Refresh
  QPushButton* m_refreshButton;
  QTimer* m_refreshTimer;

  // Status
  QLabel* m_statusLabel;

  // Current device
  QString m_currentDevice;
};

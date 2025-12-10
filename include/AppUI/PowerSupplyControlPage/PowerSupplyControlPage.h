// PowerSupplyControlPage.h - UI page for power supply control (Redesigned)
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
#include <QScrollArea>
#include <vector>

struct ApplicationContext;
class PowerSupplyManager;

/**
 * @brief Per-channel control widgets
 */
struct ChannelControlWidgets
{
  int channel = 1;
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
};

/**
 * @brief Per-channel measurement display
 */
struct ChannelMeasurementWidgets
{
  int channel = 1;
  QLabel* channelLabel = nullptr;
  QLabel* voltageLabel = nullptr;
  QLabel* currentLabel = nullptr;
  QLabel* outputStateLabel = nullptr;
};

/**
 * @brief Control page for power supply devices
 * Three-panel layout: Device list (left) + Channel tabs (middle) + All measurements (right)
 */
class PowerSupplyControlPage : public QWidget
{
  Q_OBJECT

public:
  explicit PowerSupplyControlPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~PowerSupplyControlPage() = default;

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

private:
  void setupUI();
  void createLeftPanel(QWidget* leftPanel);
  void createMiddlePanel(QWidget* middlePanel);
  void createRightPanel(QWidget* rightPanel);

  QWidget* createChannelTab(int channel);
  QWidget* createChannelMeasurementWidget(int channel);

  void loadDevices();
  void updateConnectionStatus();
  void updateDeviceList();
  void rebuildChannelTabs();
  void rebuildMeasurementPanel();
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

  // Right Panel - All Channel Measurements
  QWidget* m_rightPanel;
  QWidget* m_measurementContainer;
  QVBoxLayout* m_measurementLayout;
  std::vector<ChannelMeasurementWidgets> m_channelMeasurements;
  QPushButton* m_refreshButton;
  QTimer* m_refreshTimer;

  // Current device
  QString m_currentDevice;
  int m_currentChannelCount = 1;
};

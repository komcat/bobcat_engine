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

struct ApplicationContext;
class PowerSupplyManager;

/**
 * @brief Control page for power supply devices
 * Two-panel layout: Device list (left) + Controls (right)
 * Light theme with compact design
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

  // Output control
  void onOutputOnClicked();
  void onOutputOffClicked();

  // Voltage/Current control
  void onSetVoltageClicked();
  void onSetCurrentClicked();

  // Protection control
  void onSetOVPClicked();
  void onSetOCPClicked();

  // Measurement update
  void onRefreshClicked();
  void updateMeasurements();

private:
  void setupUI();
  void createLeftPanel(QWidget* leftPanel);
  void createRightPanel(QWidget* rightPanel);
  void createControlSection(QVBoxLayout* layout);
  void createMeasurementSection(QVBoxLayout* layout);

  void loadDevices();
  void updateConnectionStatus();
  void updateDeviceList();
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

  // Right Panel - Controls
  QWidget* m_controlPanel;

  // Output control
  QPushButton* m_outputOnButton;
  QPushButton* m_outputOffButton;

  // Voltage/Current control
  QDoubleSpinBox* m_voltageSpinBox;
  QPushButton* m_setVoltageButton;
  QDoubleSpinBox* m_currentSpinBox;
  QPushButton* m_setCurrentButton;

  // Protection control
  QDoubleSpinBox* m_ovpSpinBox;
  QPushButton* m_setOVPButton;
  QDoubleSpinBox* m_ocpSpinBox;
  QPushButton* m_setOCPButton;

  // Measurements
  QLabel* m_voltageReadLabel;
  QLabel* m_currentReadLabel;
  QPushButton* m_refreshButton;
  QTimer* m_refreshTimer;

  // Status
  QLabel* m_statusLabel;

  // Current device
  QString m_currentDevice;
};
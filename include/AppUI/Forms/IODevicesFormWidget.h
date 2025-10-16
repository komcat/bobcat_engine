// IODevicesFormWidget.h - Form for editing IO devices configuration
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <string>

/**
 * @brief Widget for editing IO devices configuration
 *
 * Layout:
 * - Left panel (30%): Device list with connection info
 * - Right panel (70%): Pin name mapping (inputs/outputs)
 */
class IODevicesFormWidget : public QWidget
{
  Q_OBJECT

public:
  explicit IODevicesFormWidget(QWidget* parent = nullptr);
  ~IODevicesFormWidget() = default;

  // Load JSON data into form
  void loadFromJson(const QJsonDocument& config);

  // Collect form data into JSON
  QJsonDocument collectToJson() const;

  // Clear all form fields
  void clear();

private slots:
  void onDeviceSelected();
  void onAddDeviceClicked();
  void onRemoveDeviceClicked();
  void onAddInputPinClicked();
  void onRemoveInputPinClicked();
  void onAddOutputPinClicked();
  void onRemoveOutputPinClicked();

private:
  void setupUI();
  void createLeftPanel();
  void createRightPanel();
  void updateDeviceInfo();
  void loadDevicePinMappings(const QJsonObject& device);
  void saveCurrentDeviceData();
  QJsonObject getCurrentDeviceJson() const;

  // Left panel - Device list
  QWidget* m_leftPanel;
  QListWidget* m_deviceList;
  QPushButton* m_addDeviceButton;
  QPushButton* m_removeDeviceButton;

  // Right panel - Device info and pin mappings
  QWidget* m_rightPanel;

  // Device information fields
  QLineEdit* m_deviceName;
  QSpinBox* m_deviceId;
  QLineEdit* m_ipAddress;
  QSpinBox* m_inputCount;
  QSpinBox* m_outputCount;
  QCheckBox* m_enabled;

  // Pin mapping tables
  QTableWidget* m_inputPinsTable;
  QTableWidget* m_outputPinsTable;
  QPushButton* m_addInputButton;
  QPushButton* m_removeInputButton;
  QPushButton* m_addOutputButton;
  QPushButton* m_removeOutputButton;

  // Current selection
  std::string m_selectedDevice;
  QJsonDocument m_currentConfig;
};
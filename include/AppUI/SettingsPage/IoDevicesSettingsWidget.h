// IoDevicesSettingsWidget.h - Editor for io_devices.local.json (restart required)
#pragma once

#include <QWidget>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class QListWidget;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QTableWidget;
class QPushButton;
class QLabel;
class QListWidgetItem;

struct ApplicationContext;

/**
 * @brief Structured editor for config/io_devices.local.json.
 *
 * Master-detail:
 *   Left  - list of all devices (EziIO + WAGO, prefixed by type)
 *   Right - selected device form (type, name, IP, id, enabled, counts)
 *           plus two pin-name tables (inputs, outputs).
 *
 * Add EziIO / Add WAGO / Remove Device / Save. pin_overrides block is
 * preserved round-trip. Changes require an application restart.
 */
class IoDevicesSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit IoDevicesSettingsWidget(ApplicationContext* context, QWidget* parent = nullptr);
  ~IoDevicesSettingsWidget() override = default;

private slots:
  void onDeviceSelected();
  void onAddEziIO();
  void onAddWago();
  void onRemoveDevice();
  void onAddInput();
  void onAddOutput();
  void onRemoveInputRow();
  void onRemoveOutputRow();
  void onSave();
  void onReloadFromDisk();

private:
  enum class DeviceType { EziIO, Wago };

  struct DeviceEntry {
    DeviceType type;
    nlohmann::json data;  // full device JSON blob
  };

  void setupUI();
  void loadFromDisk();
  void refreshDeviceList();
  void flushCurrentDeviceFromUI();
  void showDeviceInPanel(int index);
  void clearPanel();
  bool saveToLocalJson();
  void setStatus(const QString& text, bool isError = false);
  QString deviceDisplayName(const DeviceEntry& e) const;
  void loadPinTable(QTableWidget* table, const nlohmann::json& pinsArray);
  nlohmann::json pinTableToJson(QTableWidget* table) const;

  ApplicationContext* m_context;

  std::vector<DeviceEntry> m_devices;
  int m_currentIndex;
  nlohmann::json m_pinOverrides;  // preserved as-is

  // Master list + its buttons
  QListWidget* m_deviceList;
  QPushButton* m_addEziIOButton;
  QPushButton* m_addWagoButton;
  QPushButton* m_removeButton;

  // Detail panel
  QWidget* m_detailPanel;
  QLabel* m_typeLabel;
  QLineEdit* m_nameEdit;
  QLineEdit* m_ipEdit;
  QSpinBox* m_idSpin;
  QCheckBox* m_enabledCheck;
  QSpinBox* m_inputCountSpin;
  QSpinBox* m_outputCountSpin;
  QTableWidget* m_inputsTable;
  QTableWidget* m_outputsTable;
  QPushButton* m_addInputButton;
  QPushButton* m_addOutputButton;
  QPushButton* m_removeInputButton;
  QPushButton* m_removeOutputButton;

  // Bottom buttons
  QPushButton* m_saveButton;
  QPushButton* m_reloadButton;
  QLabel* m_statusLabel;

  static const std::string kLocalConfigPath;
};

// IoWatchSettingsWidget.h - Editor for io_watch.local.json with hot reload
#pragma once

#include <QWidget>
#include <QString>
#include <string>

class QTableWidget;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QLabel;

struct ApplicationContext;

/**
 * @brief UI for editing io_watch.local.json bindings with hot reload.
 *
 * Columns: Enabled | Name | Input Device | Input Pin | Output Device |
 *          Output Pin | Trigger | Debounce(ms) | Remove
 *
 * Device combos are populated from EziIOManager::GetAllDeviceNames().
 * Pin combos follow device selection (GetAllInputPinNames / GetAllOutputPinNames).
 * Save writes to config/io_watch.local.json.
 * Hot Reload calls IoWatchManager::Reload() to stop, reload, and restart watching.
 */
class IoWatchSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit IoWatchSettingsWidget(ApplicationContext* context, QWidget* parent = nullptr);
  ~IoWatchSettingsWidget() override = default;

private slots:
  void onAddBinding();
  void onRemoveBindingAtRow(int row);
  void onSave();
  void onHotReload();
  void onReloadFromDisk();

private:
  void setupUI();
  void loadFromDisk();
  void populateRow(int row,
                   bool enabled,
                   const QString& name,
                   const QString& inputDevice,
                   const QString& inputPin,
                   const QString& outputDevice,
                   const QString& outputPin,
                   const QString& trigger,
                   int debounceMs);

  void repopulatePinCombo(int row, int pinColumn, bool isInput);

  bool saveToLocalJson();
  void setStatus(const QString& text, bool isError = false);

  ApplicationContext* m_context;

  // Top controls
  QCheckBox* m_enabledCheck;
  QSpinBox* m_intervalSpin;

  // Table + buttons
  QTableWidget* m_table;
  QPushButton* m_addButton;
  QPushButton* m_saveButton;
  QPushButton* m_reloadButton;
  QPushButton* m_reloadFromDiskButton;
  QLabel* m_statusLabel;
  QLabel* m_pathLabel;

  static const std::string kLocalConfigPath;
};

// PneumaticSettingsWidget.h - Editor for pneumatic_slides.local.json (restart required)
#pragma once

#include <QWidget>
#include <string>

class QTableWidget;
class QPushButton;
class QLabel;

struct ApplicationContext;

/**
 * @brief Structured editor for config/pneumatic_slides.local.json.
 *
 * Columns: Name | Output Device/Pin | Extended Input Device/Pin |
 *          Retracted Input Device/Pin | Timeout (ms) | Remove
 * Save writes to the .local.json only. Changes require an application
 * restart to take effect; PneumaticManager state is not reloaded at runtime.
 */
class PneumaticSettingsWidget : public QWidget
{
  Q_OBJECT

public:
  explicit PneumaticSettingsWidget(ApplicationContext* context, QWidget* parent = nullptr);
  ~PneumaticSettingsWidget() override = default;

private slots:
  void onAddSlide();
  void onRemoveSlideAtRow(int row);
  void onSave();
  void onReloadFromDisk();

private:
  void setupUI();
  void loadFromDisk();
  void populateRow(int row,
                   const QString& name,
                   const QString& outDevice, const QString& outPin,
                   const QString& extDevice, const QString& extPin,
                   const QString& retDevice, const QString& retPin,
                   int timeoutMs);
  void repopulatePinCombo(int row, int pinColumn, bool isInput);
  bool saveToLocalJson();
  void setStatus(const QString& text, bool isError = false);

  ApplicationContext* m_context;

  QTableWidget* m_table;
  QPushButton* m_addButton;
  QPushButton* m_saveButton;
  QPushButton* m_reloadButton;
  QLabel* m_statusLabel;

  static const std::string kLocalConfigPath;
};

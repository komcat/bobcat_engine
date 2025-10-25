// PowerSupplyFormWidget.h - Form for editing power supply configuration
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QJsonObject>
#include <QJsonDocument>
#include <vector>

/**
 * @brief Widget for editing power supply configuration
 */
class PowerSupplyFormWidget : public QWidget
{
  Q_OBJECT

public:
  explicit PowerSupplyFormWidget(QWidget* parent = nullptr);
  ~PowerSupplyFormWidget() = default;

  // Load JSON data into form
  void loadFromJson(const QJsonDocument& config);

  // Collect form data into JSON
  QJsonDocument collectToJson() const;

  // Clear all form fields
  void clear();

private slots:
  void onAddDeviceClicked();
  void onRemoveDeviceClicked(int index);

private:
  void setupUI();
  void addDeviceGroup(const QJsonObject& device = QJsonObject());
  void rebuildForm();

  QScrollArea* m_scrollArea;
  QWidget* m_formContent;

  // Server section fields
  QLineEdit* m_serverHost;
  QSpinBox* m_serverPort;

  // Form fields for each power supply device
  struct PowerSupplyDeviceFields {
    QLineEdit* name;
    QComboBox* type;
    QLineEdit* ip;
    QSpinBox* port;
  };

  std::vector<PowerSupplyDeviceFields> m_deviceFields;
};

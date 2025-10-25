// TECDevicesFormWidget.h - Form for editing TEC devices configuration
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QJsonObject>
#include <QJsonDocument>
#include <vector>

/**
 * @brief Widget for editing TEC (Thermoelectric Cooler) devices configuration
 */
class TECDevicesFormWidget : public QWidget
{
  Q_OBJECT

public:
  explicit TECDevicesFormWidget(QWidget* parent = nullptr);
  ~TECDevicesFormWidget() = default;

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

  // Form fields for each TEC device
  struct TECDeviceFields {
    QLineEdit* name;
    QComboBox* controllerType;
    QLineEdit* ipAddress;
    QSpinBox* port;
    QCheckBox* enabled;
    QDoubleSpinBox* defaultTemperature;
    QDoubleSpinBox* defaultLaserCurrent;
  };

  std::vector<TECDeviceFields> m_deviceFields;
};

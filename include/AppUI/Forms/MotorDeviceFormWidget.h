// MotorDeviceFormWidget.h - Form for editing motor device configuration
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QJsonDocument>
#include <QJsonObject>
#include <vector>

/**
 * @brief Widget for editing motor device configuration
 */
class MotorDeviceFormWidget : public QWidget
{
  Q_OBJECT

public:
  explicit MotorDeviceFormWidget(QWidget* parent = nullptr);
  ~MotorDeviceFormWidget() = default;

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

  // Form fields for each motor device
  struct MotorDeviceFields {
    QLineEdit* id;
    QLineEdit* name;
    QSpinBox* deviceId;
    QLineEdit* ipAddress;
    QSpinBox* port;
    QCheckBox* enabled;
    QComboBox* controllerType;
    QLineEdit* axes;
  };

  std::vector<MotorDeviceFields> m_fields;
};
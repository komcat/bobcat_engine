// PneumaticSlidesFormWidget.h - Form for editing pneumatic slides configuration
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QJsonDocument>
#include <QJsonObject>
#include <vector>

/**
 * @brief Widget for editing pneumatic slides configuration
 */
class PneumaticSlidesFormWidget : public QWidget
{
  Q_OBJECT

public:
  explicit PneumaticSlidesFormWidget(QWidget* parent = nullptr);
  ~PneumaticSlidesFormWidget() = default;

  // Set available IO device names (call this before loading config)
  void setAvailableDevices(const QStringList& deviceNames);

  // Load JSON data into form
  void loadFromJson(const QJsonDocument& config);

  // Collect form data into JSON
  QJsonDocument collectToJson() const;

  // Clear all form fields
  void clear();

private slots:
  void onAddSlideClicked();
  void onRemoveSlideClicked(int index);

private:
  void setupUI();
  void addSlideGroup(const QJsonObject& slide = QJsonObject());
  void rebuildForm();
  QComboBox* createDeviceComboBox();

  QScrollArea* m_scrollArea;
  QWidget* m_formContent;

  // Form fields for each pneumatic slide
  struct PneumaticSlideFields {
    QLineEdit* name;

    // Output
    QComboBox* outputDevice;
    QLineEdit* outputPin;

    // Extended sensor
    QComboBox* extendedDevice;
    QLineEdit* extendedPin;

    // Retracted sensor
    QComboBox* retractedDevice;
    QLineEdit* retractedPin;

    // Timeout
    QSpinBox* timeout;
  };

  std::vector<PneumaticSlideFields> m_fields;
  QStringList m_availableDevices;  // ✅ Store available device names
};
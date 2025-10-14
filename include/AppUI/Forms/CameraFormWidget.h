// CameraFormWidget.h - Form for editing camera configuration
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QJsonObject>
#include <QScrollArea>
#include <QJsonDocument>
#include <vector>

/**
 * @brief Widget for editing camera configuration
 */
class CameraFormWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CameraFormWidget(QWidget* parent = nullptr);
  ~CameraFormWidget() = default;

  // Load JSON data into form
  void loadFromJson(const QJsonDocument& config);

  // Collect form data into JSON
  QJsonDocument collectToJson() const;

  // Clear all form fields
  void clear();

private slots:
  void onAddCameraClicked();
  void onRemoveCameraClicked(int index);

private:
  void setupUI();
  void addCameraGroup(const QJsonObject& camera = QJsonObject());
  void rebuildForm();

  QScrollArea* m_scrollArea;
  QWidget* m_formContent;

  // Form fields for each camera
  struct CameraFields {
    QLineEdit* id;
    QLineEdit* displayName;
    QCheckBox* enabled;
    QComboBox* cameraType;
    QComboBox* connectionType;
    QSpinBox* port;
  };

  std::vector<CameraFields> m_fields;
};
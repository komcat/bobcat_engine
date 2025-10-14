// SavedPositionsFormWidget.h - Form for editing saved positions
#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QButtonGroup>
#include <QJsonDocument>
#include <QJsonObject>

/**
 * @brief Widget for editing saved positions with 3-column layout:
 * Column 1: Device buttons
 * Column 2: Position list
 * Column 3: Position editor form
 */
class SavedPositionsFormWidget : public QWidget
{
  Q_OBJECT

public:
  explicit SavedPositionsFormWidget(QWidget* parent = nullptr);
  ~SavedPositionsFormWidget() = default;

  // Load JSON data into form
  void loadFromJson(const QJsonDocument& config);

  // Collect form data into JSON
  QJsonDocument collectToJson() const;

  // Clear all form fields
  void clear();

private slots:
  void onDeviceButtonClicked(const QString& deviceId);
  void onPositionSelected(QListWidgetItem* item);
  void onAddPositionClicked();
  void onRemovePositionClicked();
  void onPositionNameChanged();

private:
  void setupUI();
  void loadDeviceButtons();
  void loadPositionsList(const QString& deviceId);
  void loadPositionForm(const QString& deviceId, const QString& positionName);
  void clearPositionForm();
  void saveCurrentPosition();

  // Column 1: Device buttons
  QWidget* m_devicePanel;
  QVBoxLayout* m_deviceLayout;
  QButtonGroup* m_deviceButtonGroup;

  // Column 2: Position list
  QListWidget* m_positionList;

  // Column 3: Position form
  QWidget* m_formPanel;
  QLineEdit* m_positionNameEdit;
  QDoubleSpinBox* m_xSpin;
  QDoubleSpinBox* m_ySpin;
  QDoubleSpinBox* m_zSpin;
  QDoubleSpinBox* m_uSpin;
  QDoubleSpinBox* m_vSpin;
  QDoubleSpinBox* m_wSpin;
  QPushButton* m_addButton;
  QPushButton* m_removeButton;

  // Current state
  QString m_currentDevice;
  QString m_currentPosition;
  QJsonDocument m_config;
};
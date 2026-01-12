// ACSControllerPage.h - ACS-specific controller page
#pragma once

#include "MotionControllerPageBase.h"
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTimer>
#include <QLabel>
#include <map>

/**
 * @brief Page for ACS Motion Control controllers
 *
 * Displays ACS stages with dynamic axis support based on device configuration.
 * Only shows controls for axes that are actually installed on each device.
 */
class ACSControllerPage : public MotionControllerPageBase
{
  Q_OBJECT

public:
  explicit ACSControllerPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~ACSControllerPage() = default;

  // Override base class virtual methods
  std::string getControllerType() const override;
  std::vector<std::string> getAxisNames() const override;
  QString getPageTitle() const override;

protected:
  // Override to add velocity controls
  void setupUI() override;

private slots:
  void onVelocityChanged();
  void onPresetTurtleClicked();
  void onPresetRabbitClicked();
  void onDeviceSelected(const QString& deviceName);
  void onApplyVelocityClicked();
  void onAxisEnableClicked(const QString& axis, bool enable);

private:
  void createVelocityControls();
  void createAxisControlGroup();
  void updateVelocityDisplay();
  void updateAxisControlButtons();
  void updateUIForSelectedDevice();
  std::vector<std::string> getAvailableAxesForDevice(const std::string& deviceName);

  // Velocity control widgets - dynamic per axis
  QComboBox* m_deviceSelector;
  std::map<std::string, QLabel*> m_velocityLabels;      // "X" -> label widget
  std::map<std::string, QDoubleSpinBox*> m_velocitySpinBoxes;  // "X" -> spinbox
  QPushButton* m_turtleButton;
  QPushButton* m_rabbitButton;
  QPushButton* m_applyButton;

  // Axis enable/disable control widgets - dynamic per axis
  std::map<std::string, QPushButton*> m_enableButtons;  // "X" -> button
  QTimer* m_axisStatusTimer;

  // Styles for enable buttons
  QString m_enabledStyle;
  QString m_disabledStyle;
};
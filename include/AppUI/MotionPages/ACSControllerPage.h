// ACSControllerPage.h - ACS-specific controller page
#pragma once

#include "MotionControllerPageBase.h"
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTimer>

/**
 * @brief Page for ACS Motion Control controllers
 *
 * Displays ACS stages with X, Y, Z axes (or X, Y for 2-axis stages)
 * Includes velocity controls for each axis
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

  // Velocity control widgets
  QComboBox* m_deviceSelector;
  QDoubleSpinBox* m_velocityX;
  QDoubleSpinBox* m_velocityY;
  QDoubleSpinBox* m_velocityZ;
  QPushButton* m_turtleButton;
  QPushButton* m_rabbitButton;
  QPushButton* m_applyButton;

  // Axis enable/disable control widgets
  QPushButton* m_enableX;
  QPushButton* m_enableY;
  QPushButton* m_enableZ;
  QTimer* m_axisStatusTimer;
};
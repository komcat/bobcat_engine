// PIControllerPage.h - PI-specific controller page
#pragma once

#include "MotionControllerPageBase.h"
#include <QDoubleSpinBox>
#include <QComboBox>

/**
 * @brief Page for PI (Physik Instrumente) controllers
 *
 * Displays PI hexapods with 6 axes: X, Y, Z, U, V, W
 * Includes system-wide velocity control (not per-axis)
 */
class PIControllerPage : public MotionControllerPageBase
{
  Q_OBJECT

public:
  explicit PIControllerPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~PIControllerPage() = default;

  // Override base class virtual methods
  std::string getControllerType() const override;
  std::vector<std::string> getAxisNames() const override;
  QString getPageTitle() const override;

protected:
  // Override to add velocity controls
  void setupUI() override;

private slots:
  void onPresetTurtleClicked();
  void onPresetRabbitClicked();
  void onDeviceSelected(const QString& deviceName);
  void onApplyVelocityClicked();

private:
  void createVelocityControls();
  void updateVelocityDisplay();

  // Velocity control widgets (system-wide velocity, not per-axis)
  QComboBox* m_deviceSelector;
  QDoubleSpinBox* m_systemVelocity;
  QPushButton* m_turtleButton;
  QPushButton* m_rabbitButton;
  QPushButton* m_applyButton;
};
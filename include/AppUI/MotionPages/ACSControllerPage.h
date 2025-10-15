// ACSControllerPage.h - ACS-specific controller page
#pragma once

#include "MotionControllerPageBase.h"

/**
 * @brief Page for ACS Motion Control controllers
 *
 * Displays ACS stages with X, Y, Z axes (or X, Y for 2-axis stages)
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
};
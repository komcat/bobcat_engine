// PIControllerPage.h - PI-specific controller page
#pragma once

#include "MotionControllerPageBase.h"

/**
 * @brief Page for PI (Physik Instrumente) controllers
 *
 * Displays PI hexapods with 6 axes: X, Y, Z, U, V, W
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
};
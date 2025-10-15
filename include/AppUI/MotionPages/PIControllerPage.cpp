// PIControllerPage.cpp
#include "PIControllerPage.h"
#include <iostream>

PIControllerPage::PIControllerPage(ApplicationContext* context, QWidget* parent)
  : MotionControllerPageBase(context, parent)
{
  std::cout << "[PIControllerPage] Initialized" << std::endl;

  // NOW safe to call virtual functions - derived class is constructed
  initialize();
}

std::string PIControllerPage::getControllerType() const
{
  return "PI";
}

std::vector<std::string> PIControllerPage::getAxisNames() const
{
  // PI Hexapod has 6 axes: X, Y, Z, U, V, W
  return { "X", "Y", "Z", "U", "V", "W" };
}

QString PIControllerPage::getPageTitle() const
{
  return "PI Hexapod Controllers";
}
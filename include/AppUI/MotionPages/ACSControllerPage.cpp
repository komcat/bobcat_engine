// ACSControllerPage.cpp
#include "ACSControllerPage.h"
#include <iostream>

ACSControllerPage::ACSControllerPage(ApplicationContext* context, QWidget* parent)
  : MotionControllerPageBase(context, parent)
{
  std::cout << "[ACSControllerPage] Initialized" << std::endl;

  // NOW safe to call virtual functions - derived class is constructed
  initialize();
}

std::string ACSControllerPage::getControllerType() const
{
  return "ACS";
}

std::vector<std::string> ACSControllerPage::getAxisNames() const
{
  // ACS can have X,Y,Z or just X,Y depending on config
  // Show all 3 columns - will display "--" for missing axes
  return { "X", "Y", "Z" };
}

QString ACSControllerPage::getPageTitle() const
{
  return "ACS Motion Controllers";
}
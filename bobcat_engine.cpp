// bobcat_engine.cpp - Entry point
#include "Application.h"
#include <iostream>

int main(int argc, char* argv[])
{
  std::cout << "=== Bobcat Engine Starting ===" << std::endl;

  try {
    Application app(argc, argv);
    return app.run();
  }
  catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return -1;
  }
}
// bobcat_engine.cpp - Entry point
#include "Application.h"
#include <iostream>
#include <cstdlib>
#include <string>

int main(int argc, char* argv[])
{
  std::cout << "=== Bobcat Engine Starting ===" << std::endl;

  try {
    Application app(argc, argv);

    // Check for simulation mode
    // Priority: 1) Command line args, 2) Environment variable (set by CMake)
    bool simulationMode = false;
    bool modeSpecified = false;

    // Check command line arguments first (highest priority)
    for (int i = 1; i < argc; ++i) {
      std::string arg(argv[i]);
      if (arg == "--simulation" || arg == "--sim" || arg == "-s") {
        simulationMode = true;
        modeSpecified = true;
        break;
      }
      else if (arg == "--hardware" || arg == "--hw" || arg == "-h") {
        simulationMode = false;
        modeSpecified = true;
        break;
      }
    }

    // Check environment variable (set by CMake configuration)
    if (!modeSpecified) {
      const char* envSimulation = std::getenv("BOBCAT_SIMULATION");
      if (envSimulation) {
        std::string envValue(envSimulation);
        if (envValue == "1" || envValue == "true" || envValue == "TRUE") {
          simulationMode = true;
          modeSpecified = true;
        }
        else if (envValue == "0" || envValue == "false" || envValue == "FALSE") {
          simulationMode = false;
          modeSpecified = true;
        }
      }
    }

    // Default to simulation mode if not specified (safest for development)
    if (!modeSpecified) {
      simulationMode = true;
      std::cout << "\n⚠️  Mode not specified - defaulting to SIMULATION MODE" << std::endl;
      std::cout << "    (Configure via CMake preset or use --hardware flag)\n" << std::endl;
    }

    // Enable simulation mode if selected
    if (simulationMode) {
      app.setSimulationMode(true);
      std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
      std::cout << "║  🔧 SIMULATION MODE ENABLED 🔧        ║" << std::endl;
      std::cout << "║  Hardware connections will be SKIPPED  ║" << std::endl;
      std::cout << "║  Safe for UI/file development          ║" << std::endl;
      std::cout << "╚════════════════════════════════════════╝\n" << std::endl;
    }
    else {
      std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
      std::cout << "║  ⚡ HARDWARE MODE ENABLED ⚡           ║" << std::endl;
      std::cout << "║  Connecting to physical devices...     ║" << std::endl;
      std::cout << "╚════════════════════════════════════════╝\n" << std::endl;
    }

    return app.run();
  }
  catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return -1;
  }
}
// ProcessSequenceFactory.cpp
#include "ProcessSequenceFactory.h"
#include "GripperSequenceHelpers.h"
#include "SequenceBuilder.h"
#include <iostream>

ProcessSequenceFactory::ProcessSequenceFactory()
{
  std::cout << "[ProcessSequenceFactory] Initializing..." << std::endl;

  // ============================================================================
  // REGISTER PICK PROCESS BUILDER
  // ============================================================================
  RegisterProcessBuilder("Pick", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    GripperSequenceHelpers::GripperConfig config;
    config.gripperDevice = params.at("gripper_device");
    config.cameraDevice = params.at("camera_device");
    config.ioDevice = params.at("io_device");
    config.gripperPin = std::stoi(params.at("gripper_pin"));
    config.releaseWaitMs = std::stoi(params.at("release_wait_ms"));
    config.regripWaitMs = std::stoi(params.at("regrip_wait_ms"));

    std::string pickNode = params.at("pick_node");
    std::string viewNode = params.at("view_node");
    std::string lensName = params.at("lens_name");

    GripperSequenceHelpers::AddPickSequence(
      sequence, config, pickNode, viewNode, lensName
    );

    std::cout << "[ProcessSequenceFactory] Built Pick sequence for: "
      << step->userNickname << std::endl;
  });

  // ============================================================================
  // REGISTER PLACE PROCESS BUILDER
  // ============================================================================
  RegisterProcessBuilder("Place", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    GripperSequenceHelpers::GripperConfig config;
    config.gripperDevice = params.at("gripper_device");
    config.cameraDevice = params.at("camera_device");
    config.ioDevice = params.at("io_device");
    config.gripperPin = std::stoi(params.at("gripper_pin"));

    std::string placeNode = params.at("place_node");
    std::string viewNode = params.at("view_node");
    std::string lensName = params.at("lens_name");

    GripperSequenceHelpers::AddPlaceSequence(
      sequence, config, placeNode, viewNode, lensName
    );

    std::cout << "[ProcessSequenceFactory] Built Place sequence for: "
      << step->userNickname << std::endl;
  });

  // ============================================================================
  // REGISTER PICK AND PLACE PROCESS BUILDER
  // ============================================================================
  RegisterProcessBuilder("Pick and Place", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    GripperSequenceHelpers::GripperConfig config;
    config.gripperDevice = params.at("gripper_device");
    config.cameraDevice = params.at("camera_device");
    config.ioDevice = params.at("io_device");
    config.gripperPin = std::stoi(params.at("gripper_pin"));
    config.releaseWaitMs = std::stoi(params.at("release_wait_ms"));
    config.regripWaitMs = std::stoi(params.at("regrip_wait_ms"));

    std::string pickNode = params.at("pick_node");
    std::string pickViewNode = params.at("pick_view_node");
    std::string placeNode = params.at("place_node");
    std::string placeViewNode = params.at("place_view_node");
    std::string lensName = params.at("lens_name");

    GripperSequenceHelpers::AddPickAndPlaceSequence(
      sequence, config,
      pickNode, pickViewNode,
      placeNode, placeViewNode,
      lensName
    );

    std::cout << "[ProcessSequenceFactory] Built Pick and Place sequence for: "
      << step->userNickname << std::endl;
  });

  // ============================================================================
  // REGISTER MOVE TO POSITION PROCESS
  // ============================================================================
  RegisterProcessBuilder("Move to Position", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    sequence->AddAction(params.at("device"), "MoveToNamedPosition", {
      {"positionName", params.at("target_position")}
      });
    sequence->AddAction(params.at("device"), "WaitForMotionComplete", {});

    std::cout << "[ProcessSequenceFactory] Built Move to Position sequence for: "
      << step->userNickname << std::endl;
  });

  // ============================================================================
  // REGISTER MOVE RELATIVE PROCESS (SINGLE AXIS)
  // ============================================================================
  RegisterProcessBuilder("Move Relative", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string axis = params.at("axis");
    std::string distance = params.at("distance");

    sequence->AddAction(device, "MoveRelative", {
      {"axis", axis},
      {"distance", distance}
      });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    std::cout << "[ProcessSequenceFactory] Built Move Relative sequence for: "
      << step->userNickname << std::endl;
  });

  // ============================================================================
  // REGISTER SET OUTPUT PROCESS
  // ============================================================================
  RegisterProcessBuilder("Set Output", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    sequence->AddAction(params.at("io_device"), "SetOutputPin", {
      {"pin", params.at("pin")},
      {"state", params.at("state")}
      });

    std::cout << "[ProcessSequenceFactory] Built Set Output sequence for: "
      << step->userNickname << std::endl;
  });

  // ============================================================================
  // REGISTER WAIT PROCESS
  // ============================================================================
  RegisterProcessBuilder("Wait", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    sequence->AddAction("", "Wait", {
      {"duration_ms", params.at("duration_ms")}
      });

    std::cout << "[ProcessSequenceFactory] Built Wait sequence for: "
      << step->userNickname << " (" << params.at("duration_ms") << " ms)" << std::endl;
  });

  // ============================================================================
  // REGISTER MOVE RELATIVE (MULTI-AXIS) PROCESS
  // ============================================================================
  RegisterProcessBuilder("Move Relative Multi-Axis", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::string device = params.at("device");

    // Move each axis that has a non-zero distance
    std::string distX = params.at("distance_x");
    std::string distY = params.at("distance_y");
    std::string distZ = params.at("distance_z");

    bool hasMoves = false;

    if (distX != "0" && distX != "0.0" && !distX.empty()) {
      sequence->AddAction(device, "MoveRelative", {
        {"axis", "X"},
        {"distance", distX}
        });
      hasMoves = true;
    }

    if (distY != "0" && distY != "0.0" && !distY.empty()) {
      sequence->AddAction(device, "MoveRelative", {
        {"axis", "Y"},
        {"distance", distY}
        });
      hasMoves = true;
    }

    if (distZ != "0" && distZ != "0.0" && !distZ.empty()) {
      sequence->AddAction(device, "MoveRelative", {
        {"axis", "Z"},
        {"distance", distZ}
        });
      hasMoves = true;
    }

    // Wait for motion complete if any moves were added
    if (hasMoves) {
      sequence->AddAction(device, "WaitForMotionComplete", {});
    }

    std::cout << "[ProcessSequenceFactory] Built Move Relative Multi-Axis sequence for: "
      << step->userNickname << std::endl;
  });

  // ============================================================================
  // REGISTER MOVE TO POSITION WITH OFFSET PROCESS
  // ============================================================================
  RegisterProcessBuilder("Move to Position with Offset", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    // Build the calculated position name (e.g. "base+X1+Y-2+Z0.5")
    std::string basePosition = params.at("base_position");
    std::string offsetX = params.at("offset_x");
    std::string offsetY = params.at("offset_y");
    std::string offsetZ = params.at("offset_z");

    // Build position name with offsets
    std::string calculatedPosition = basePosition;

    // Add X offset if non-zero
    if (offsetX != "0" && offsetX != "0.0" && !offsetX.empty()) {
      calculatedPosition += "+X" + offsetX;
    }

    // Add Y offset if non-zero
    if (offsetY != "0" && offsetY != "0.0" && !offsetY.empty()) {
      calculatedPosition += "+Y" + offsetY;
    }

    // Add Z offset if non-zero
    if (offsetZ != "0" && offsetZ != "0.0" && !offsetZ.empty()) {
      calculatedPosition += "+Z" + offsetZ;
    }

    sequence->AddAction(params.at("device"), "MoveToNamedPosition", {
      {"positionName", calculatedPosition}
      });
    sequence->AddAction(params.at("device"), "WaitForMotionComplete", {});

    std::cout << "[ProcessSequenceFactory] Built Move to Position with Offset sequence for: "
      << step->userNickname << " -> " << calculatedPosition << std::endl;
  });


  // ============================================================================
  // REGISTER PNEUMATIC SLIDE EXTEND PROCESS
  // ============================================================================
  RegisterProcessBuilder("Slide Extend", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::string slideName = params.at("slide_name");
    std::string inbounceDelay = params.at("inbounce_delay_ms");
    std::string timeout = params.at("timeout_ms");
    std::string debounceDelay = params.at("debounce_delay_ms");

    // Inbounce delay (wait before action)
    if (inbounceDelay != "0" && !inbounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", inbounceDelay}  // ✅ Changed from "duration_ms"
        });
    }

    // Extend the slide (blocking - waits for completion or timeout)
    sequence->AddAction(slideName, "Extend", {
      {"timeout_ms", timeout}
      });

    // Debounce delay (wait after action completed)
    if (debounceDelay != "0" && !debounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", debounceDelay}  // ✅ Changed from "duration_ms"
        });
    }

    std::cout << "[ProcessSequenceFactory] Built Slide Extend sequence for: "
      << step->userNickname << " (Slide: " << slideName << ")" << std::endl;
  });

  // ============================================================================
  // REGISTER PNEUMATIC SLIDE RETRACT PROCESS
  // ============================================================================
  RegisterProcessBuilder("Slide Retract", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::string slideName = params.at("slide_name");
    std::string inbounceDelay = params.at("inbounce_delay_ms");
    std::string timeout = params.at("timeout_ms");
    std::string debounceDelay = params.at("debounce_delay_ms");

    // Inbounce delay (wait before action)
    if (inbounceDelay != "0" && !inbounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", inbounceDelay}  // ✅ Changed from "duration_ms"
        });
    }

    // Retract the slide (blocking - waits for completion or timeout)
    sequence->AddAction(slideName, "Retract", {
      {"timeout_ms", timeout}
      });

    // Debounce delay (wait after action completed)
    if (debounceDelay != "0" && !debounceDelay.empty()) {
      sequence->AddAction("system", "Wait", {
        {"milliseconds", debounceDelay}  // ✅ Changed from "duration_ms"
        });
    }

    std::cout << "[ProcessSequenceFactory] Built Slide Retract sequence for: "
      << step->userNickname << " (Slide: " << slideName << ")" << std::endl;
  });


  // ============================================================================
  // REGISTER SET TEC TEMP PROCESS
  // ============================================================================
  RegisterProcessBuilder("Set TEC Temp", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string temperature = params.at("temperature");
    std::string powerState = params.at("power_state");

    sequence->AddAction(device, "SetTemperature", {
      {"temperature", temperature}
      });

    if (powerState == "on") {
      sequence->AddAction(device, "TEC_On", {});
    }
    else if (powerState == "off") {
      sequence->AddAction(device, "TEC_Off", {});
    }

    std::cout << "[ProcessSequenceFactory] Built Set TEC Temp sequence for: "
      << step->userNickname << " (" << temperature << "°C, " << powerState << ")" << std::endl;
  });

  // ============================================================================
  // REGISTER SET LASER CURRENT PROCESS
  // ============================================================================
  RegisterProcessBuilder("Set Laser Current", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string currentMa = params.at("current_ma");
    std::string powerState = params.at("power_state");

    sequence->AddAction(device, "SetLaserCurrent", {
      {"current", currentMa}
      });

    if (powerState == "on") {
      sequence->AddAction(device, "Laser_On", {});
    }
    else if (powerState == "off") {
      sequence->AddAction(device, "Laser_Off", {});
    }

    std::cout << "[ProcessSequenceFactory] Built Set Laser Current sequence for: "
      << step->userNickname << " (" << currentMa << " mA, " << powerState << ")" << std::endl;
  });

  // ============================================================================
  // REGISTER READ DATASTORE PROCESS BUILDER
  // ============================================================================
  RegisterProcessBuilder("Read DataStore", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::string channelName = params.at("channel_name");
    std::string logLevel = params.at("log_level");

    // Add action to read from DataStore and log it
    sequence->AddAction("datastore", "ReadAndLog", {
      {"channelName", channelName},
      {"logLevel", logLevel}
      });

    std::cout << "[ProcessSequenceFactory] Built Read DataStore sequence for: "
      << step->userNickname << " (Channel: " << channelName << ")" << std::endl;
  });


  // ============================================================================
  // REGISTER READ TEMPERATURE PROCESS BUILDER
  // ============================================================================
  RegisterProcessBuilder("Read Temperature", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::string tecDevice = params.at("tec_device");
    std::string logLevel = params.at("log_level");

    sequence->AddAction(tecDevice, "ReadTemperature", {
      {"logLevel", logLevel}  // ← ADD THIS
      });

    std::cout << "[ProcessSequenceFactory] Built Read Temperature sequence for: "
      << step->userNickname << " (Device: " << tecDevice << ")" << std::endl;
  });

  // ============================================================================
  // REGISTER READ LASER CURRENT PROCESS BUILDER
  // ============================================================================
  RegisterProcessBuilder("Read Laser Current", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::string tecDevice = params.at("tec_device");
    std::string logLevel = params.at("log_level");

    sequence->AddAction(tecDevice, "ReadLaserCurrent", {
      {"logLevel", logLevel}  // ← ADD THIS
      });

    std::cout << "[ProcessSequenceFactory] Built Read Laser Current sequence for: "
      << step->userNickname << " (Device: " << tecDevice << ")" << std::endl;
  });
  
  
  
  
  
  
  
  std::cout << "[ProcessSequenceFactory] Registered " << m_builders.size()
    << " process builders" << std::endl;
}

void ProcessSequenceFactory::RegisterProcessBuilder(
  const std::string& processType,
  SequenceBuilderFunc builder)
{
  m_builders[processType] = builder;
  std::cout << "[ProcessSequenceFactory] Registered builder: " << processType << std::endl;
}

bool ProcessSequenceFactory::BuildStep(
  SequenceBuilder* sequence,
  const ProcessInstance* step,
  std::string& errorMsg)
{
  if (!sequence || !step) {
    errorMsg = "Invalid sequence or step pointer";
    return false;
  }

  // Check if builder exists
  auto it = m_builders.find(step->processType);
  if (it == m_builders.end()) {
    errorMsg = "No builder registered for process type: " + step->processType;
    std::cerr << "[ProcessSequenceFactory] " << errorMsg << std::endl;
    return false;
  }

  // Try to build the step
  try {
    it->second(sequence, step);
    return true;
  }
  catch (const std::out_of_range& e) {
    errorMsg = "Missing parameter in step '" + step->userNickname + "': " + std::string(e.what());
    std::cerr << "[ProcessSequenceFactory] " << errorMsg << std::endl;
    return false;
  }
  catch (const std::invalid_argument& e) {
    errorMsg = "Invalid parameter value in step '" + step->userNickname + "': " + std::string(e.what());
    std::cerr << "[ProcessSequenceFactory] " << errorMsg << std::endl;
    return false;
  }
  catch (const std::exception& e) {
    errorMsg = "Error building step '" + step->userNickname + "': " + std::string(e.what());
    std::cerr << "[ProcessSequenceFactory] " << errorMsg << std::endl;
    return false;
  }
}

bool ProcessSequenceFactory::IsSupported(const std::string& processType) const
{
  return m_builders.find(processType) != m_builders.end();
}

std::vector<std::string> ProcessSequenceFactory::GetSupportedProcessTypes() const
{
  std::vector<std::string> types;
  types.reserve(m_builders.size());

  for (const auto& [type, builder] : m_builders) {
    types.push_back(type);
  }

  return types;
}


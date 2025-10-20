// ProcessSequenceFactory.cpp
#include "ProcessSequenceFactory.h"
#include "GripperSequenceHelpers.h"
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
  // REGISTER MOVE RELATIVE PROCESS
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
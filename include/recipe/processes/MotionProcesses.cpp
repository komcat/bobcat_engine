// MotionProcesses.cpp
// Motion process plugins: Pick, Place, Pick and Place, Move to Position, Move Relative, etc.

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"  // For ProcessInstance
#include "../../sequence/SequenceBuilder.h"
#include "../../sequence/GripperSequenceHelpers.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>
#include <algorithm>  // for std::transform

//=============================================================================
// PICK PLUGIN
//=============================================================================

class PickPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Pick";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Pick a lens from a specified position using gripper";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"gripper_device", "hex-left"},
      {"camera_device", "gantry-main"},
      {"io_device", "IOBottom"},
      {"gripper_pin", "0"},
      {"pick_node", "node_5647"},
      {"view_node", "node_4186"},
      {"release_wait_ms", "1500"},
      {"regrip_wait_ms", "500"},
      {"lens_name", "lens"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("gripper_device", "Gripper Device", params.at("gripper_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDeviceSelector("camera_device", "Camera Device", params.at("camera_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addParameterField("gripper_pin", "Gripper Pin", params.at("gripper_pin"));
    widget->addNodeSelector("pick_node", "Pick Node", params.at("pick_node"), "camera_device");
    widget->addNodeSelector("view_node", "View Node", params.at("view_node"), "camera_device");
    widget->addParameterField("release_wait_ms", "Release Wait (ms)", params.at("release_wait_ms"));
    widget->addParameterField("regrip_wait_ms", "Regrip Wait (ms)", params.at("regrip_wait_ms"));
    widget->addParameterField("lens_name", "Lens Name", params.at("lens_name"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
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

    std::cout << "[MotionProcesses] Built Pick sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// PLACE PLUGIN
//=============================================================================

class PlacePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Place";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Place a lens at a specified position using gripper";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"gripper_device", "hex-left"},
      {"camera_device", "gantry-main"},
      {"io_device", "IOBottom"},
      {"gripper_pin", "0"},
      {"place_node", "node_5662"},
      {"view_node", "node_4137"},
      {"lens_name", "lens"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("gripper_device", "Gripper Device", params.at("gripper_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDeviceSelector("camera_device", "Camera Device", params.at("camera_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addParameterField("gripper_pin", "Gripper Pin", params.at("gripper_pin"));
    widget->addNodeSelector("place_node", "Place Node", params.at("place_node"), "camera_device");
    widget->addNodeSelector("view_node", "View Node", params.at("view_node"), "camera_device");
    widget->addParameterField("lens_name", "Lens Name", params.at("lens_name"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
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

    std::cout << "[MotionProcesses] Built Place sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// PICK AND PLACE PLUGIN
//=============================================================================

class PickAndPlacePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Pick and Place";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Pick a lens from one position and place it at another";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"gripper_device", "hex-left"},
      {"camera_device", "gantry-main"},
      {"io_device", "IOBottom"},
      {"gripper_pin", "0"},
      {"pick_node", "node_5647"},
      {"pick_view_node", "node_4186"},
      {"place_node", "node_5662"},
      {"place_view_node", "node_4137"},
      {"release_wait_ms", "1500"},
      {"regrip_wait_ms", "500"},
      {"lens_name", "lens"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("gripper_device", "Gripper Device", params.at("gripper_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDeviceSelector("camera_device", "Camera Device", params.at("camera_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
      static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addParameterField("gripper_pin", "Gripper Pin", params.at("gripper_pin"));
    widget->addNodeSelector("pick_node", "Pick Node", params.at("pick_node"), "camera_device");
    widget->addNodeSelector("pick_view_node", "Pick View Node", params.at("pick_view_node"), "camera_device");
    widget->addNodeSelector("place_node", "Place Node", params.at("place_node"), "camera_device");
    widget->addNodeSelector("place_view_node", "Place View Node", params.at("place_view_node"), "camera_device");
    widget->addParameterField("release_wait_ms", "Release Wait (ms)", params.at("release_wait_ms"));
    widget->addParameterField("regrip_wait_ms", "Regrip Wait (ms)", params.at("regrip_wait_ms"));
    widget->addParameterField("lens_name", "Lens Name", params.at("lens_name"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
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

    std::cout << "[MotionProcesses] Built Pick and Place sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// MOVE TO POSITION PLUGIN
//=============================================================================

class MoveToPositionPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Move to Position";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Move device to a named position";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"target_position", "safe"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addNodeSelector("target_position", "Target Position", params.at("target_position"), "device");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction(params.at("device"), "MoveToNamedPosition", {
      {"positionName", params.at("target_position")}
    });
    sequence->AddAction(params.at("device"), "WaitForMotionComplete", {});

    std::cout << "[MotionProcesses] Built Move to Position sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// MOVE RELATIVE PLUGIN (Single Axis)
//=============================================================================

class MoveRelativePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Move Relative";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Move device relative to current position (single axis)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"axis", "Z"},
      {"distance", "10.0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("axis", "Axis (X/Y/Z)", params.at("axis"));
    widget->addParameterField("distance", "Distance (mm)", params.at("distance"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string axis = params.at("axis");
    std::string distance = params.at("distance");

    sequence->AddAction(device, "MoveRelative", {
      {"axis", axis},
      {"distance", distance}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    std::cout << "[MotionProcesses] Built Move Relative sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// MOVE RELATIVE MULTI-AXIS PLUGIN
//=============================================================================

class MoveRelativeMultiAxisPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Move Relative Multi-Axis";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Move device relative to current position (multiple axes simultaneously)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"distance_x", "0"},
      {"distance_y", "0"},
      {"distance_z", "0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("distance_x", "X Distance (mm)", params.at("distance_x"));
    widget->addParameterField("distance_y", "Y Distance (mm)", params.at("distance_y"));
    widget->addParameterField("distance_z", "Z Distance (mm)", params.at("distance_z"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string distX = params.at("distance_x");
    std::string distY = params.at("distance_y");
    std::string distZ = params.at("distance_z");

    bool hasMoves = false;

    if (distX != "0" && distX != "0.0" && !distX.empty()) {
      sequence->AddAction(device, "MoveRelative", {{"axis", "X"}, {"distance", distX}});
      hasMoves = true;
    }

    if (distY != "0" && distY != "0.0" && !distY.empty()) {
      sequence->AddAction(device, "MoveRelative", {{"axis", "Y"}, {"distance", distY}});
      hasMoves = true;
    }

    if (distZ != "0" && distZ != "0.0" && !distZ.empty()) {
      sequence->AddAction(device, "MoveRelative", {{"axis", "Z"}, {"distance", distZ}});
      hasMoves = true;
    }

    if (hasMoves) {
      sequence->AddAction(device, "WaitForMotionComplete", {});
    }

    std::cout << "[MotionProcesses] Built Move Relative Multi-Axis sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// MOVE TO POSITION WITH OFFSET PLUGIN
//=============================================================================

class MoveToPositionWithOffsetPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Move to Position with Offset";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Move to a named position with X/Y/Z offsets applied";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"base_position", "safe"},
      {"offset_x", "0"},
      {"offset_y", "0"},
      {"offset_z", "0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addNodeSelector("base_position", "Base Position", params.at("base_position"), "device");
    widget->addParameterField("offset_x", "X Offset (mm)", params.at("offset_x"));
    widget->addParameterField("offset_y", "Y Offset (mm)", params.at("offset_y"));
    widget->addParameterField("offset_z", "Z Offset (mm)", params.at("offset_z"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string basePosition = params.at("base_position");
    std::string offsetX = params.at("offset_x");
    std::string offsetY = params.at("offset_y");
    std::string offsetZ = params.at("offset_z");

    // Build position name with offsets (e.g. "base+X1+Y-2+Z0.5")
    std::string calculatedPosition = basePosition;

    if (offsetX != "0" && offsetX != "0.0" && !offsetX.empty()) {
      calculatedPosition += "+X" + offsetX;
    }
    if (offsetY != "0" && offsetY != "0.0" && !offsetY.empty()) {
      calculatedPosition += "+Y" + offsetY;
    }
    if (offsetZ != "0" && offsetZ != "0.0" && !offsetZ.empty()) {
      calculatedPosition += "+Z" + offsetZ;
    }

    sequence->AddAction(params.at("device"), "MoveToNamedPosition", {
      {"positionName", calculatedPosition}
    });
    sequence->AddAction(params.at("device"), "WaitForMotionComplete", {});

    std::cout << "[MotionProcesses] Built Move to Position with Offset sequence: "
              << calculatedPosition << std::endl;
  }
};

//=============================================================================
// SET VELOCITY PLUGIN
//=============================================================================

class SetVelocityPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Velocity";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Set motion speed for a device (ACS: per-axis, PI: system-wide)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"speed", "10.0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Use numeric parameter with conservative range that works for both ACS and PI
    // ACS: 1-70 mm/s, PI: 0.1-10 mm/s
    // Conservative range: 0.1-70 mm/s
    widget->addNumericParameter("speed", "Speed (mm/s) [ACS: 1-70, PI: 0.1-10]",
      params.at("speed"),
      0.1,   // min (PI minimum)
      70.0,  // max (ACS maximum)
      0.1,   // step
      1,     // decimals
      "mm/s" // suffix
    );
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string speedStr = params.at("speed");
    double speed = std::stod(speedStr);

    // ✅ Detect device type based on naming convention
    // PI hexapods: device names contain "hex", "hexapod", "pi"
    // ACS gantries: device names contain "gantry", "acs", or others
    std::string deviceLower = device;
    std::transform(deviceLower.begin(), deviceLower.end(), deviceLower.begin(), ::tolower);

    bool isPIDevice = (deviceLower.find("hex") != std::string::npos) ||
                      (deviceLower.find("pi") != std::string::npos);

    if (isPIDevice) {
      // PI hexapods: Use system-wide velocity
      sequence->AddAction(device, "SetSystemVelocity", {
        {"velocity", speedStr}
      });
      std::cout << "[MotionProcesses] Built Set Velocity (PI system) sequence for device: "
                << device << " at " << speed << " mm/s" << std::endl;
    }
    else {
      // ACS gantries: Use per-axis velocity
      sequence->AddAction(device, "SetVelocity", {
        {"axis", "X"},
        {"velocity", speedStr}
      });
      sequence->AddAction(device, "SetVelocity", {
        {"axis", "Y"},
        {"velocity", speedStr}
      });
      sequence->AddAction(device, "SetVelocity", {
        {"axis", "Z"},
        {"velocity", speedStr}
      });
      std::cout << "[MotionProcesses] Built Set Velocity (ACS per-axis) sequence for device: "
                << device << " at " << speed << " mm/s" << std::endl;
    }
  }
};

//=============================================================================
// XCAN PLUGIN (Hexapod Adaptive Scanning)
//=============================================================================

class XCANPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "XCAN";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Hexapod adaptive scanning (coarse/fine) to find optimal alignment";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "hex-left"},
      {"mode", "coarse"},
      {"channel", ""},
      {"settling_time_ms", "300"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Hexapod Device (hex-*)", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("mode", "Scan Mode (coarse/fine)", params.at("mode"));
    widget->addParameterField("channel", "Data Channel", params.at("channel"));
    widget->addNumericParameter("settling_time_ms", "Settling Time (ms)",
      params.at("settling_time_ms"),
      50,     // min
      2000,   // max
      10,     // step
      0,      // decimals (integer)
      "ms"    // suffix
    );
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");

    // Add XCAN command to motion manager
    sequence->AddAction(device, "XCAN", {
      {"mode", params.at("mode")},
      {"channel", params.at("channel")},
      {"settling_time_ms", params.at("settling_time_ms")}
    });

    std::cout << "[MotionProcesses] Built XCAN sequence for: " << step->userNickname
              << " (mode=" << params.at("mode") << ", channel=" << params.at("channel") << ")"
              << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterMotionProcesses {
    RegisterMotionProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<PickPlugin>());
      registry.RegisterPlugin(std::make_unique<PlacePlugin>());
      registry.RegisterPlugin(std::make_unique<PickAndPlacePlugin>());
      registry.RegisterPlugin(std::make_unique<MoveToPositionPlugin>());
      registry.RegisterPlugin(std::make_unique<MoveRelativePlugin>());
      registry.RegisterPlugin(std::make_unique<MoveRelativeMultiAxisPlugin>());
      registry.RegisterPlugin(std::make_unique<MoveToPositionWithOffsetPlugin>());
      registry.RegisterPlugin(std::make_unique<SetVelocityPlugin>());
      registry.RegisterPlugin(std::make_unique<XCANPlugin>());

      std::cout << "[MotionProcesses] Registered 9 motion process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterMotionProcesses g_registerMotionProcesses;
}

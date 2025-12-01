// MotionProcesses.cpp
// Motion process plugins: Pick, Place, Pick and Place, Move to Position, Move Relative, etc.

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"  // For ProcessInstance
#include "../../sequence/SequenceBuilder.h"
#include "../../sequence/GripperSequenceHelpers.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"
#include "../../AppUI/RecipePage/PropertyTableWidget.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>
#include <algorithm>  // for std::transform
#include <cmath>      // for std::abs

namespace {
  // Helper to check if a string value represents zero (handles "0", "0.0", "0.000000", etc.)
  bool isZeroDistance(const std::string& value) {
    if (value.empty()) return true;
    try {
      double d = std::stod(value);
      return std::abs(d) < 1e-9;  // Effectively zero
    } catch (...) {
      return true;  // If can't parse, treat as zero (don't move)
    }
  }
}

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
// MOVE TO POSITION PLUGIN - Table-Based UI
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

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addNodeSelector("target_position", "Target Position", params.at("target_position"), "device");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", params.at("device"),
      "Select the motion device to control (click [...] to browse connected devices)");

    table->addCategoryHeader("Target");
    table->addNodeProperty("target_position", "Target Position", params.at("target_position"),
      "device",
      "Named position to move to (click [...] to browse)");
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
// MOVE RELATIVE PLUGIN (Single Axis) - Table-Based UI
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

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("axis", "Axis (X/Y/Z)", params.at("axis"));
    widget->addParameterField("distance", "Distance (mm)", params.at("distance"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", params.at("device"),
      "Select the motion device to move");

    table->addCategoryHeader("Movement");
    table->addDropdownProperty("axis", "Axis", params.at("axis"),
      {"X", "Y", "Z"},
      "Axis to move along");
    table->addNumericProperty("distance", "Distance",
      std::stod(params.at("distance")),
      -1000, 1000, 0.1, 2, "mm",
      "Distance to move (positive or negative)");
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
// MOVE RELATIVE MULTI-AXIS PLUGIN - Table-Based UI
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

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("distance_x", "X Distance (mm)", params.at("distance_x"));
    widget->addParameterField("distance_y", "Y Distance (mm)", params.at("distance_y"));
    widget->addParameterField("distance_z", "Z Distance (mm)", params.at("distance_z"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", params.at("device"),
      "Select the motion device to move");

    table->addCategoryHeader("Movement Distances");
    table->addNumericProperty("distance_x", "X Distance",
      std::stod(params.at("distance_x")),
      -1000, 1000, 0.1, 2, "mm",
      "Distance to move on X axis (0 = no move)");
    table->addNumericProperty("distance_y", "Y Distance",
      std::stod(params.at("distance_y")),
      -1000, 1000, 0.1, 2, "mm",
      "Distance to move on Y axis (0 = no move)");
    table->addNumericProperty("distance_z", "Z Distance",
      std::stod(params.at("distance_z")),
      -1000, 1000, 0.1, 2, "mm",
      "Distance to move on Z axis (0 = no move)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string distX = params.at("distance_x");
    std::string distY = params.at("distance_y");
    std::string distZ = params.at("distance_z");

    bool hasMoves = false;

    if (!isZeroDistance(distX)) {
      sequence->AddAction(device, "MoveRelative", {{"axis", "X"}, {"distance", distX}});
      hasMoves = true;
    }

    if (!isZeroDistance(distY)) {
      sequence->AddAction(device, "MoveRelative", {{"axis", "Y"}, {"distance", distY}});
      hasMoves = true;
    }

    if (!isZeroDistance(distZ)) {
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
// MOVE RELATIVE THETA PLUGIN (U/V/W axes) - Table-Based UI
//=============================================================================

class MoveRelativeThetaPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Move Relative Theta";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Move device relative on rotational axes (U/V/W) - for 6-axis devices only";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "hex-left"},
      {"distance_u", "0"},
      {"distance_v", "0"},
      {"distance_w", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "Motion Device (6-axis)", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("distance_u", "U Distance (deg)", params.at("distance_u"));
    widget->addParameterField("distance_v", "V Distance (deg)", params.at("distance_v"));
    widget->addParameterField("distance_w", "W Distance (deg)", params.at("distance_w"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    // Use axis-filtered selector to show only devices with U/V/W axes
    table->addMotionDevicePropertyWithAxes("device", "Motion Device (6-axis)", params.at("device"),
      "UVW",  // Filter: require U, V, W axes
      "Select a 6-axis motion device");

    table->addCategoryHeader("Rotational Distances");
    table->addNumericProperty("distance_u", "U Distance",
      std::stod(params.at("distance_u")),
      -180, 180, 0.1, 3, "",
      "U axis movement (0 = no move)");
    table->addNumericProperty("distance_v", "V Distance",
      std::stod(params.at("distance_v")),
      -180, 180, 0.1, 3, "",
      "V axis movement (0 = no move)");
    table->addNumericProperty("distance_w", "W Distance",
      std::stod(params.at("distance_w")),
      -180, 180, 0.1, 3, "",
      "W axis movement (0 = no move)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string distU = params.at("distance_u");
    std::string distV = params.at("distance_v");
    std::string distW = params.at("distance_w");

    bool hasMoves = false;

    if (!isZeroDistance(distU)) {
      sequence->AddAction(device, "MoveRelative", {{"axis", "U"}, {"distance", distU}});
      hasMoves = true;
    }

    if (!isZeroDistance(distV)) {
      sequence->AddAction(device, "MoveRelative", {{"axis", "V"}, {"distance", distV}});
      hasMoves = true;
    }

    if (!isZeroDistance(distW)) {
      sequence->AddAction(device, "MoveRelative", {{"axis", "W"}, {"distance", distW}});
      hasMoves = true;
    }

    if (hasMoves) {
      sequence->AddAction(device, "WaitForMotionComplete", {});
    }

    std::cout << "[MotionProcesses] Built Move Relative Theta sequence for: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// MOVE TO POSITION WITH OFFSET PLUGIN - Table-Based UI
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

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addNodeSelector("base_position", "Base Position", params.at("base_position"), "device");
    widget->addParameterField("offset_x", "X Offset (mm)", params.at("offset_x"));
    widget->addParameterField("offset_y", "Y Offset (mm)", params.at("offset_y"));
    widget->addParameterField("offset_z", "Z Offset (mm)", params.at("offset_z"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", params.at("device"),
      "Select the motion device to control");

    table->addCategoryHeader("Base Position");
    table->addNodeProperty("base_position", "Base Position", params.at("base_position"),
      "device",
      "Named position to use as base (click [...] to browse)");

    table->addCategoryHeader("Offsets");
    table->addNumericProperty("offset_x", "X Offset",
      std::stod(params.at("offset_x")),
      -1000, 1000, 0.1, 2, "mm",
      "Offset to add to base X position");
    table->addNumericProperty("offset_y", "Y Offset",
      std::stod(params.at("offset_y")),
      -1000, 1000, 0.1, 2, "mm",
      "Offset to add to base Y position");
    table->addNumericProperty("offset_z", "Z Offset",
      std::stod(params.at("offset_z")),
      -1000, 1000, 0.1, 2, "mm",
      "Offset to add to base Z position");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string basePosition = params.at("base_position");
    std::string offsetX = params.at("offset_x");
    std::string offsetY = params.at("offset_y");
    std::string offsetZ = params.at("offset_z");

    // Build position name with offsets (e.g. "base+X1+Y-2+Z0.5")
    std::string calculatedPosition = basePosition;

    if (!isZeroDistance(offsetX)) {
      calculatedPosition += "+X" + offsetX;
    }
    if (!isZeroDistance(offsetY)) {
      calculatedPosition += "+Y" + offsetY;
    }
    if (!isZeroDistance(offsetZ)) {
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
// SET VELOCITY PLUGIN - Table-Based UI
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

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addNumericParameter("speed", "Speed (mm/s) [ACS: 1-70, PI: 0.1-10]",
      params.at("speed"), 0.1, 70.0, 0.1, 1, "mm/s");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", params.at("device"),
      "Select the motion device");

    table->addCategoryHeader("Velocity Settings");
    table->addNumericProperty("speed", "Speed",
      std::stod(params.at("speed")),
      0.1, 70.0, 0.1, 1, "mm/s",
      "Motion speed (ACS: 1-70, PI: 0.1-10 mm/s)");
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
// XCAN PLUGIN (Hexapod Adaptive Scanning) - Table-Based UI
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

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Hexapod Device (hex-*)", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("mode", "Scan Mode (coarse/fine)", params.at("mode"));
    // ✅ UPDATED: Use data channel selector with auto-refresh instead of plain text field
    widget->addDataChannelSelector("channel", "Data Channel (DataStore)", params.at("channel"));
    widget->addNumericParameter("settling_time_ms", "Settling Time (ms)",
      params.at("settling_time_ms"),
      50,     // min
      2000,   // max
      10,     // step
      0,      // decimals (integer)
      "ms"    // suffix
    );
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Hexapod Device", params.at("device"),
      "Select the hexapod device for scanning (hex-* naming)");

    table->addCategoryHeader("Scan Configuration");
    table->addDropdownProperty("mode", "Scan Mode", params.at("mode"),
      {"coarse", "fine"},
      "Scanning resolution (coarse for initial search, fine for precision)");
    table->addDataChannelProperty("channel", "Data Channel", params.at("channel"),
      "DataStore channel to read scan feedback values");

    table->addCategoryHeader("Timing");
    table->addNumericProperty("settling_time_ms", "Settling Time",
      std::stod(params.at("settling_time_ms")),
      50, 2000, 10, 0, "ms",
      "Wait time after each position move for signal stabilization");
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
// MOVE TO GRAPH NODE PLUGIN - Table-Based UI
//=============================================================================

class MoveToGraphNodePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Move to Graph Node";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Execute graph-based path planning to move to a target node";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "hex-left"},
      {"graphName", "default"},
      {"endNodeName", "home"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Device selector
    widget->addDeviceSelector("device", "Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Graph selector (populates based on selected device)
    widget->addGraphSelector("graphName", "Graph Name", params.at("graphName"), "device");

    // Node selector (populates based on selected device + graph)
    widget->addGraphNodeSelector("endNodeName", "End Node Position", params.at("endNodeName"),
      "device", "graphName");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", params.at("device"),
      "Select the motion device to control");

    table->addCategoryHeader("Graph Path");
    table->addGraphProperty("graphName", "Graph Name", params.at("graphName"),
      "device",
      "Select the motion graph to use for path planning");
    table->addGraphNodeProperty("endNodeName", "End Node", params.at("endNodeName"),
      "device", "graphName",
      "Select the target node in the graph");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    // Single command: MoveToGraphNode (does all the work)
    sequence->AddAction(params.at("device"), "MoveToGraphNode", {
      {"graphName", params.at("graphName")},
      {"endNodeName", params.at("endNodeName")}
    });

    std::cout << "[MotionProcesses] Built Move to Graph Node sequence: "
              << params.at("device") << " -> graph=" << params.at("graphName")
              << ", endNode=" << params.at("endNodeName") << std::endl;
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
      registry.RegisterPlugin(std::make_unique<MoveRelativeThetaPlugin>());
      registry.RegisterPlugin(std::make_unique<MoveToPositionWithOffsetPlugin>());
      registry.RegisterPlugin(std::make_unique<SetVelocityPlugin>());
      registry.RegisterPlugin(std::make_unique<XCANPlugin>());
      registry.RegisterPlugin(std::make_unique<MoveToGraphNodePlugin>());

      std::cout << "[MotionProcesses] Registered 11 motion process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterMotionProcesses g_registerMotionProcesses;
}

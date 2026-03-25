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
#include <nlohmann/json.hpp>  // for ParallelMove JSON serialization

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

  // Safe parameter access with fallback to default value (backward compatibility
  // for old recipes that don't have newly-added parameters)
  std::string getParamOr(const std::map<std::string, std::string>& params,
                          const std::string& key, const std::string& defaultValue) {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : defaultValue;
  }

  // Helper to detect PI hexapod devices by name convention
  bool isPIHexDevice(const std::string& device) {
    std::string lower = device;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower.find("hex") != std::string::npos) ||
           (lower.find("pi") != std::string::npos);
  }

  // Helper to add velocity set action before a move (if velocity > 0)
  void addVelocityAction(SequenceBuilder* sequence, const std::string& device, const std::string& velocityStr) {
    if (isZeroDistance(velocityStr)) return;

    double velocity = std::stod(velocityStr);
    if (velocity <= 0.0) return;

    // Clamp hex devices to 20 mm/s max
    if (isPIHexDevice(device) && velocity > 20.0) {
      velocity = 20.0;
    }

    std::string clampedStr = std::to_string(velocity);

    if (isPIHexDevice(device)) {
      sequence->AddAction(device, "SetSystemVelocity", {{"velocity", clampedStr}});
    } else {
      sequence->AddAction(device, "SetAllAxisVelocity", {{"velocity", clampedStr}});
    }
  }

  // Helper to add a delay/wait action (if ms > 0)
  void addDelayAction(SequenceBuilder* sequence, const std::string& msStr) {
    if (isZeroDistance(msStr)) return;
    int ms = static_cast<int>(std::stod(msStr));
    if (ms <= 0) return;
    sequence->AddAction("system", "Wait", {{"milliseconds", std::to_string(ms)}});
  }

  // Helper to add velocity/delay table UI entries under a "Timing & Velocity" category
  void addTimingAndVelocityTableUI(PropertyTableWidget* table,
                                    const std::map<std::string, std::string>& params) {
    table->addCategoryHeader("Timing & Velocity");
    table->addNumericProperty("velocity", "Velocity",
      std::stod(getParamOr(params, "velocity", "0")),
      0, 70, 0.1, 1, "mm/s",
      "Move speed (0 = use controller default, hex devices max 20 mm/s)");
    table->addNumericProperty("delay_before_ms", "Delay Before",
      std::stod(getParamOr(params, "delay_before_ms", "0")),
      0, 60000, 100, 0, "ms",
      "Wait before executing the move (0 = no delay)");
    table->addNumericProperty("delay_after_ms", "Delay After",
      std::stod(getParamOr(params, "delay_after_ms", "0")),
      0, 60000, 100, 0, "ms",
      "Wait after the move completes (0 = no delay)");
  }

  // Helper to add velocity/delay form UI entries
  void addTimingAndVelocityFormUI(ProcessConfigWidget* widget,
                                   const std::map<std::string, std::string>& params) {
    widget->addNumericParameter("velocity", "Velocity (mm/s, 0=default)",
      getParamOr(params, "velocity", "0"), 0, 70, 0.1, 1, "mm/s");
    widget->addNumericParameter("delay_before_ms", "Delay Before (ms)",
      getParamOr(params, "delay_before_ms", "0"), 0, 60000, 100, 0, "ms");
    widget->addNumericParameter("delay_after_ms", "Delay After (ms)",
      getParamOr(params, "delay_after_ms", "0"), 0, 60000, 100, 0, "ms");
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
      {"target_position", "safe"},
      {"velocity", "0"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addNodeSelector("target_position", "Target Position", params.at("target_position"), "device");
    addTimingAndVelocityFormUI(widget, params);
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

    addTimingAndVelocityTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    std::string device = params.at("device");

    addVelocityAction(sequence, device, getParamOr(params, "velocity", "0"));
    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    sequence->AddAction(device, "MoveToNamedPosition", {
      {"positionName", params.at("target_position")}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

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
      {"distance", "10.0"},
      {"velocity", "0"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("axis", "Axis (X/Y/Z)", params.at("axis"));
    widget->addParameterField("distance", "Distance (mm)", params.at("distance"));
    addTimingAndVelocityFormUI(widget, params);
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
      -1000, 1000, 0.001, 3, "mm",
      "Distance to move (positive or negative)");

    addTimingAndVelocityTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string axis = params.at("axis");
    std::string distance = params.at("distance");

    addVelocityAction(sequence, device, getParamOr(params, "velocity", "0"));
    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    sequence->AddAction(device, "MoveRelative", {
      {"axis", axis},
      {"distance", distance}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

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
      {"distance_z", "0"},
      {"velocity", "0"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
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
    addTimingAndVelocityFormUI(widget, params);
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

    addTimingAndVelocityTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string distX = params.at("distance_x");
    std::string distY = params.at("distance_y");
    std::string distZ = params.at("distance_z");

    addVelocityAction(sequence, device, getParamOr(params, "velocity", "0"));
    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    // Build a JSON array of parallel moves for all non-zero axes
    // ParallelMoveRelative starts all axes simultaneously (non-blocking),
    // then waits for all to complete — true multi-axis relative move
    nlohmann::json movesArray = nlohmann::json::array();

    if (!isZeroDistance(distX)) {
      movesArray.push_back({{"device", device}, {"axis", "X"}, {"distance", distX}});
    }
    if (!isZeroDistance(distY)) {
      movesArray.push_back({{"device", device}, {"axis", "Y"}, {"distance", distY}});
    }
    if (!isZeroDistance(distZ)) {
      movesArray.push_back({{"device", device}, {"axis", "Z"}, {"distance", distZ}});
    }

    if (!movesArray.empty()) {
      sequence->AddAction(device, "ParallelMoveRelative", {
        {"moves", movesArray.dump()}
      });
    }

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[MotionProcesses] Built Move Relative Multi-Axis sequence for: " << step->userNickname
              << " (" << movesArray.size() << " axes)" << std::endl;
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
      {"distance_w", "0"},
      {"velocity", "0"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
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
    addTimingAndVelocityFormUI(widget, params);
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

    addTimingAndVelocityTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string distU = params.at("distance_u");
    std::string distV = params.at("distance_v");
    std::string distW = params.at("distance_w");

    addVelocityAction(sequence, device, getParamOr(params, "velocity", "0"));
    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

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

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

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
      {"offset_z", "0"},
      {"velocity", "0"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
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
    addTimingAndVelocityFormUI(widget, params);
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

    addTimingAndVelocityTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
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

    addVelocityAction(sequence, device, getParamOr(params, "velocity", "0"));
    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    sequence->AddAction(device, "MoveToNamedPosition", {
      {"positionName", calculatedPosition}
    });
    sequence->AddAction(device, "WaitForMotionComplete", {});

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

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
      // ACS gantries: Set velocity on all installed axes
      // Uses SetAllAxisVelocity so the handler queries installed axes at runtime,
      // avoiding errors when a device has only a subset of axes (e.g., Y only)
      sequence->AddAction(device, "SetAllAxisVelocity", {
        {"velocity", speedStr}
      });
      std::cout << "[MotionProcesses] Built Set Velocity (ACS all-axis) sequence for device: "
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
// XCAN EXTENDED PLUGIN - Flexible Adaptive Scanning with Custom Parameters
//=============================================================================

class XCANExtendedPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "XCAN Extended";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Extended adaptive scanning with custom axes, step sizes, exit criteria, and result capture";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "hex-left"},
      {"axes", "XYZ"},                    // Default XYZ, can be "XY", "Z", etc.
      {"step_sizes_um", "5,2,1,0.5"},     // Step sizes in microns
      {"channel", ""},                    // Data channel to optimize
      {"settling_time_ms", "300"},        // Wait time after each move
      {"exit_threshold", ""},             // Stop if reading exceeds this value (empty = disabled)
      {"capture_as", ""}                  // Store peak value to context field (empty = disabled)
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("axes", "Scan Axes (e.g., XYZ, XY, Z)", params.at("axes"));
    widget->addParameterField("step_sizes_um", "Step Sizes (microns, comma-separated)", params.at("step_sizes_um"));
    widget->addDataChannelSelector("channel", "Data Channel", params.at("channel"));
    widget->addNumericParameter("settling_time_ms", "Settling Time (ms)",
      params.at("settling_time_ms"), 50, 2000, 10, 0, "ms");
    widget->addParameterField("exit_threshold", "Exit Threshold (empty = disabled)", params.at("exit_threshold"));
    widget->addParameterField("capture_as", "Capture Peak As (context field)", params.at("capture_as"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", params.at("device"),
      "Select the motion device for scanning");

    table->addCategoryHeader("Scan Configuration");
    table->addDropdownProperty("axes", "Scan Axes", params.at("axes"),
      {"XYZ", "XY", "XZ", "YZ", "X", "Y", "Z"},
      "Axes to scan (XYZ = all three axes)");
    table->addTextProperty("step_sizes_um", "Step Sizes", params.at("step_sizes_um"),
      "Comma-separated step sizes in microns (e.g., 5,2,1,0.5). Scans from largest to smallest.");
    table->addDataChannelProperty("channel", "Data Channel", params.at("channel"),
      "DataStore channel to read scan feedback values");

    table->addCategoryHeader("Timing");
    table->addNumericProperty("settling_time_ms", "Settling Time",
      std::stod(params.at("settling_time_ms")),
      50, 2000, 10, 0, "ms",
      "Wait time after each position move for signal stabilization");

    table->addCategoryHeader("Exit Criteria");
    table->addTextProperty("exit_threshold", "Exit Threshold", params.at("exit_threshold"),
      "Stop and return to peak if reading exceeds this value (leave empty to disable)");

    table->addCategoryHeader("Result Capture");
    table->addTextProperty("capture_as", "Capture Peak As", params.at("capture_as"),
      "Store peak reading value to sequence context field (leave empty to disable)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");

    // Build XCANExtended command
    std::map<std::string, std::string> actionParams = {
      {"axes", params.at("axes")},
      {"step_sizes_um", params.at("step_sizes_um")},
      {"channel", params.at("channel")},
      {"settling_time_ms", params.at("settling_time_ms")},
      {"exit_threshold", params.at("exit_threshold")},
      {"capture_as", params.at("capture_as")}
    };

    sequence->AddAction(device, "XCANExtended", actionParams);

    std::cout << "[MotionProcesses] Built XCANExtended sequence for: " << step->userNickname
              << " (axes=" << params.at("axes")
              << ", steps=" << params.at("step_sizes_um")
              << ", channel=" << params.at("channel") << ")"
              << std::endl;
  }
};

//=============================================================================
// GRID SCAN PLUGIN - 2D Planar Grid Scanning with Preset Modes
//=============================================================================

class GridScanPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Grid Scan";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "2D planar grid scan for alignment optimization using preset scan modes";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "hex-left"},
      {"mode", "fine"},                    // "coarse", "fine", "ultrafine"
      {"plane", "XY"},                     // XY, XZ, YZ
      {"channel", ""},                     // DataStore feedback channel
      {"settling_time_ms", "300"},
      {"capture_as", ""},                  // Optional: store peak result
      {"timeout_ms", "600000"}             // 10 minute timeout for scans
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    auto defaults = GetDefaultParameters();

    // Safe param getter - falls back to default if key missing (backward compatibility)
    auto getParam = [&](const std::string& key) -> std::string {
      auto it = params.find(key);
      if (it != params.end()) return it->second;
      auto dit = defaults.find(key);
      if (dit != defaults.end()) return dit->second;
      return "";
    };

    widget->addDeviceSelector("device", "Hexapod Device (hex-*)", getParam("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDropdownParameter("mode", "Scan Mode", getParam("mode"),
      {"coarse", "fine", "ultrafine"});
    widget->addDropdownParameter("plane", "Scan Plane", getParam("plane"),
      {"XY", "XZ", "YZ"});
    widget->addDataChannelSelector("channel", "Data Channel (DataStore)", getParam("channel"));
    widget->addNumericParameter("settling_time_ms", "Settling Time (ms)",
      getParam("settling_time_ms"), 50, 2000, 10, 0, "ms");
    widget->addParameterField("capture_as", "Capture Peak As (optional)", getParam("capture_as"));
    widget->addNumericParameter("timeout_ms", "Timeout (ms)",
      getParam("timeout_ms"), 60000, 1800000, 60000, 0, "ms");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    auto defaults = GetDefaultParameters();

    // Safe param getter - falls back to default if key missing (backward compatibility)
    auto getParam = [&](const std::string& key) -> std::string {
      auto it = params.find(key);
      if (it != params.end()) return it->second;
      auto dit = defaults.find(key);
      if (dit != defaults.end()) return dit->second;
      return "";
    };

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", getParam("device"),
      "Select the PI hexapod device for grid scanning (hex-* naming)");

    table->addCategoryHeader("Scan Configuration");
    table->addDropdownProperty("mode", "Scan Mode", getParam("mode"),
      {"coarse", "fine", "ultrafine"},
      "coarse=200um/20um, fine=100um/10um, ultrafine=50um/5um (11x11 grid)");
    table->addDropdownProperty("plane", "Scan Plane", getParam("plane"),
      {"XY", "XZ", "YZ"},
      "2D plane to scan centered on current position: XY, XZ, or YZ");
    table->addDataChannelProperty("channel", "Data Channel", getParam("channel"),
      "DataStore channel to read feedback values during scan");

    table->addCategoryHeader("Timing");
    table->addNumericProperty("settling_time_ms", "Settling Time",
      std::stod(getParam("settling_time_ms")),
      50, 2000, 10, 0, "ms",
      "Wait time after each position move for signal stabilization");

    table->addCategoryHeader("Result Capture");
    table->addTextProperty("capture_as", "Save Peak Value As", getParam("capture_as"),
      "Context variable name to store the peak value found (leave empty to disable)");

    table->addCategoryHeader("Timeout");
    table->addNumericProperty("timeout_ms", "Command Timeout",
      std::stod(getParam("timeout_ms")),
      60000, 1800000, 60000, 0, "ms",
      "Maximum time allowed for scan (60s-30min). Default 10 minutes.");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    auto defaults = GetDefaultParameters();

    // Safe param getter - falls back to default if key missing (backward compatibility)
    auto getParam = [&](const std::string& key) -> std::string {
      auto it = params.find(key);
      if (it != params.end()) return it->second;
      auto dit = defaults.find(key);
      if (dit != defaults.end()) return dit->second;
      return "";
    };

    std::string device = getParam("device");

    // Build GridScan command
    sequence->AddAction(device, "GridScan", {
      {"mode", getParam("mode")},
      {"plane", getParam("plane")},
      {"channel", getParam("channel")},
      {"settling_time_ms", getParam("settling_time_ms")},
      {"capture_as", getParam("capture_as")},
      {"timeout_ms", getParam("timeout_ms")}  // Custom timeout for long scans
    });

    std::cout << "[MotionProcesses] Built GridScan sequence for: " << step->userNickname
              << " (mode=" << getParam("mode")
              << ", plane=" << getParam("plane")
              << ", channel=" << getParam("channel") << ")"
              << std::endl;
  }
};

//=============================================================================
// GRID SCAN EXTENDED PLUGIN - Full Control 2D Grid Scanning
//=============================================================================

class GridScanExtendedPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Grid Scan Extended";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Extended 2D grid scan with custom range, step size, and exit criteria";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "hex-left"},
      {"plane", "XY"},                     // XY, XZ, YZ
      {"range_axis1_um", "100"},           // Total scan range on first axis (um)
      {"direction_axis1", "centered"},     // centered, positive, negative
      {"range_axis2_um", "100"},           // Total scan range on second axis (um)
      {"direction_axis2", "centered"},     // centered, positive, negative
      {"step_size_um", "10"},              // Step size (um)
      {"channel", ""},                     // DataStore feedback channel
      {"settling_time_ms", "300"},
      {"snake_pattern", "true"},           // Serpentine pattern
      {"return_to_peak", "true"},          // Move to peak after scan
      {"exit_threshold", ""},              // Early exit if value exceeds
      {"capture_as", ""},                  // Store peak value to context
      {"capture_position_as", ""},         // Store peak position to context
      {"timeout_ms", "600000"}             // 10 minute timeout for long scans
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    auto defaults = GetDefaultParameters();

    // Safe param getter - falls back to default if key missing (backward compatibility)
    auto getParam = [&](const std::string& key) -> std::string {
      auto it = params.find(key);
      if (it != params.end()) return it->second;
      auto dit = defaults.find(key);
      if (dit != defaults.end()) return dit->second;
      return "";
    };

    widget->addDeviceSelector("device", "Motion Device", getParam("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDropdownParameter("plane", "Scan Plane", getParam("plane"),
      {"XY", "XZ", "YZ"});
    widget->addParameterField("range_axis1_um", "Range Axis 1 (um)", getParam("range_axis1_um"));
    widget->addDropdownParameter("direction_axis1", "Direction Axis 1", getParam("direction_axis1"),
      {"centered", "positive", "negative"});
    widget->addParameterField("range_axis2_um", "Range Axis 2 (um)", getParam("range_axis2_um"));
    widget->addDropdownParameter("direction_axis2", "Direction Axis 2", getParam("direction_axis2"),
      {"centered", "positive", "negative"});
    widget->addParameterField("step_size_um", "Step Size (um)", getParam("step_size_um"));
    widget->addDataChannelSelector("channel", "Data Channel", getParam("channel"));
    widget->addNumericParameter("settling_time_ms", "Settling Time (ms)",
      getParam("settling_time_ms"), 50, 2000, 10, 0, "ms");
    widget->addDropdownParameter("snake_pattern", "Snake Pattern", getParam("snake_pattern"),
      {"true", "false"});
    widget->addDropdownParameter("return_to_peak", "Return to Peak", getParam("return_to_peak"),
      {"true", "false"});
    widget->addParameterField("exit_threshold", "Exit Threshold (optional)", getParam("exit_threshold"));
    widget->addParameterField("capture_as", "Capture Peak As", getParam("capture_as"));
    widget->addParameterField("capture_position_as", "Capture Position As", getParam("capture_position_as"));
    widget->addNumericParameter("timeout_ms", "Timeout (ms)",
      getParam("timeout_ms"), 60000, 1800000, 60000, 0, "ms");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    auto defaults = GetDefaultParameters();

    // Safe param getter - falls back to default if key missing (backward compatibility)
    auto getParam = [&](const std::string& key) -> std::string {
      auto it = params.find(key);
      if (it != params.end()) return it->second;
      auto dit = defaults.find(key);
      if (dit != defaults.end()) return dit->second;
      return "";
    };

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", getParam("device"),
      "Select the motion device for grid scanning");

    table->addCategoryHeader("Grid Configuration");
    table->addDropdownProperty("plane", "Scan Plane", getParam("plane"),
      {"XY", "XZ", "YZ"},
      "2D plane to scan: XY scans X then Y, XZ scans X then Z, YZ scans Y then Z");

    table->addCategoryHeader("Axis 1 (1st letter of plane)");
    table->addNumericProperty("range_axis1_um", "Range",
      std::stod(getParam("range_axis1_um")), 10, 1000, 10, 0, "um",
      "Scan range for 1st axis (XY->X, XZ->X, YZ->Y)");
    table->addDropdownProperty("direction_axis1", "Direction", getParam("direction_axis1"),
      {"centered", "positive", "negative"},
      "centered=+/-half, positive=0 to +range, negative=-range to 0");

    table->addCategoryHeader("Axis 2 (2nd letter of plane)");
    table->addNumericProperty("range_axis2_um", "Range",
      std::stod(getParam("range_axis2_um")), 10, 1000, 10, 0, "um",
      "Scan range for 2nd axis (XY->Y, XZ->Z, YZ->Z)");
    table->addDropdownProperty("direction_axis2", "Direction", getParam("direction_axis2"),
      {"centered", "positive", "negative"},
      "centered=+/-half, positive=0 to +range, negative=-range to 0");

    table->addCategoryHeader("Step Size");
    table->addNumericProperty("step_size_um", "Step Size",
      std::stod(getParam("step_size_um")), 1, 100, 1, 0, "um",
      "Distance between grid points (same for both axes)");

    table->addCategoryHeader("Data Source");
    table->addDataChannelProperty("channel", "Data Channel", getParam("channel"),
      "DataStore channel to read feedback values during scan");

    table->addCategoryHeader("Timing");
    table->addNumericProperty("settling_time_ms", "Settling Time",
      std::stod(getParam("settling_time_ms")),
      50, 2000, 10, 0, "ms",
      "Wait time after each position move for signal stabilization");

    table->addCategoryHeader("Scan Options");
    table->addCheckboxProperty("snake_pattern", "Snake Pattern",
      (getParam("snake_pattern") == "true" || getParam("snake_pattern") == "1"),
      "Use serpentine pattern to minimize backlash (recommended)");
    table->addCheckboxProperty("return_to_peak", "Return to Peak",
      (getParam("return_to_peak") == "true" || getParam("return_to_peak") == "1"),
      "Move to the position with highest value after scan completes");

    table->addCategoryHeader("Exit Criteria");
    table->addTextProperty("exit_threshold", "Exit Threshold", getParam("exit_threshold"),
      "Stop scan early if value exceeds this threshold (leave empty to disable)");

    table->addCategoryHeader("Result Capture");
    table->addTextProperty("capture_as", "Save Peak Value As", getParam("capture_as"),
      "Context variable name for peak value (leave empty to disable)");
    table->addTextProperty("capture_position_as", "Save Peak Position As",
      getParam("capture_position_as"),
      "Context variable name for peak position as 'axis1,axis2' (leave empty to disable)");

    table->addCategoryHeader("Timeout");
    table->addNumericProperty("timeout_ms", "Command Timeout",
      std::stod(getParam("timeout_ms")),
      60000, 1800000, 60000, 0, "ms",
      "Maximum time allowed for scan (60s-30min). Default 10 minutes for large grids.");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    auto defaults = GetDefaultParameters();

    // Safe param getter - falls back to default if key missing (backward compatibility)
    auto getParam = [&](const std::string& key) -> std::string {
      auto it = params.find(key);
      if (it != params.end()) return it->second;
      auto dit = defaults.find(key);
      if (dit != defaults.end()) return dit->second;
      return "";
    };

    std::string device = getParam("device");

    // Build GridScanExtended command
    std::map<std::string, std::string> actionParams = {
      {"plane", getParam("plane")},
      {"range_axis1_um", getParam("range_axis1_um")},
      {"direction_axis1", getParam("direction_axis1")},
      {"range_axis2_um", getParam("range_axis2_um")},
      {"direction_axis2", getParam("direction_axis2")},
      {"step_size_um", getParam("step_size_um")},
      {"channel", getParam("channel")},
      {"settling_time_ms", getParam("settling_time_ms")},
      {"snake_pattern", getParam("snake_pattern")},
      {"return_to_peak", getParam("return_to_peak")},
      {"exit_threshold", getParam("exit_threshold")},
      {"capture_as", getParam("capture_as")},
      {"capture_position_as", getParam("capture_position_as")},
      {"timeout_ms", getParam("timeout_ms")}  // Custom timeout for long scans
    };

    sequence->AddAction(device, "GridScanExtended", actionParams);

    std::cout << "[MotionProcesses] Built GridScanExtended sequence for: " << step->userNickname
              << " (plane=" << getParam("plane")
              << ", range=" << getParam("range_axis1_um") << "x" << getParam("range_axis2_um") << "um"
              << ", step=" << getParam("step_size_um") << "um"
              << ", channel=" << getParam("channel") << ")"
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
      {"endNodeName", "home"},
      {"velocity", "0"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
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
    addTimingAndVelocityFormUI(widget, params);
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

    addTimingAndVelocityTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    std::string device = params.at("device");

    addVelocityAction(sequence, device, getParamOr(params, "velocity", "0"));
    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    // Single command: MoveToGraphNode (does all the work)
    sequence->AddAction(device, "MoveToGraphNode", {
      {"graphName", params.at("graphName")},
      {"endNodeName", params.at("endNodeName")}
    });

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[MotionProcesses] Built Move to Graph Node sequence: "
              << device << " -> graph=" << params.at("graphName")
              << ", endNode=" << params.at("endNodeName") << std::endl;
  }
};

//=============================================================================
// PARALLEL MOVE PLUGIN - Move multiple devices simultaneously
//=============================================================================

class ParallelMovePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Parallel Move";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Move multiple devices simultaneously to their target positions";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"move_count", "2"},
      // Move 1
      {"device_1", "hex-left"},
      {"type_1", "position"},
      {"target_1", "home"},
      {"graph_1", ""},
      // Move 2
      {"device_2", "hex-right"},
      {"type_2", "position"},
      {"target_2", "home"},
      {"graph_2", ""},
      // Move 3 (optional)
      {"device_3", ""},
      {"type_3", "position"},
      {"target_3", ""},
      {"graph_3", ""},
      // Move 4 (optional)
      {"device_4", ""},
      {"type_4", "position"},
      {"target_4", ""},
      {"graph_4", ""}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("move_count", "Number of Moves (2-4)", params.at("move_count"));

    // Move 1
    widget->addDeviceSelector("device_1", "Device 1", params.at("device_1"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDropdownParameter("type_1", "Type 1", params.at("type_1"), {"position", "graphNode"});
    widget->addNodeSelector("target_1", "Target 1", params.at("target_1"), "device_1");
    widget->addParameterField("graph_1", "Graph 1 (for graphNode)", params.at("graph_1"));

    // Move 2
    widget->addDeviceSelector("device_2", "Device 2", params.at("device_2"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDropdownParameter("type_2", "Type 2", params.at("type_2"), {"position", "graphNode"});
    widget->addNodeSelector("target_2", "Target 2", params.at("target_2"), "device_2");
    widget->addParameterField("graph_2", "Graph 2 (for graphNode)", params.at("graph_2"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Parallel Move Configuration");
    table->addNumericProperty("move_count", "Number of Moves",
      std::stod(params.at("move_count")), 2, 4, 1, 0, "",
      "Number of devices to move simultaneously (2-4)");

    // Move 1
    table->addCategoryHeader("Move 1");
    table->addMotionDeviceProperty("device_1", "Device", params.at("device_1"),
      "First device to move");
    table->addDropdownProperty("type_1", "Move Type", params.at("type_1"),
      {"position", "graphNode"},
      "position = named position, graphNode = graph-based path");
    table->addNodeProperty("target_1", "Target Position", params.at("target_1"),
      "device_1", "Target position name");
    table->addTextProperty("graph_1", "Graph Name", params.at("graph_1"),
      "Graph name (only for graphNode type)");

    // Move 2
    table->addCategoryHeader("Move 2");
    table->addMotionDeviceProperty("device_2", "Device", params.at("device_2"),
      "Second device to move");
    table->addDropdownProperty("type_2", "Move Type", params.at("type_2"),
      {"position", "graphNode"},
      "position = named position, graphNode = graph-based path");
    table->addNodeProperty("target_2", "Target Position", params.at("target_2"),
      "device_2", "Target position name");
    table->addTextProperty("graph_2", "Graph Name", params.at("graph_2"),
      "Graph name (only for graphNode type)");

    // Move 3 (optional)
    table->addCategoryHeader("Move 3 (Optional)");
    table->addMotionDeviceProperty("device_3", "Device", params.at("device_3"),
      "Third device to move (leave empty to skip)");
    table->addDropdownProperty("type_3", "Move Type", params.at("type_3"),
      {"position", "graphNode"},
      "position = named position, graphNode = graph-based path");
    table->addNodeProperty("target_3", "Target Position", params.at("target_3"),
      "device_3", "Target position name");
    table->addTextProperty("graph_3", "Graph Name", params.at("graph_3"),
      "Graph name (only for graphNode type)");

    // Move 4 (optional)
    table->addCategoryHeader("Move 4 (Optional)");
    table->addMotionDeviceProperty("device_4", "Device", params.at("device_4"),
      "Fourth device to move (leave empty to skip)");
    table->addDropdownProperty("type_4", "Move Type", params.at("type_4"),
      {"position", "graphNode"},
      "position = named position, graphNode = graph-based path");
    table->addNodeProperty("target_4", "Target Position", params.at("target_4"),
      "device_4", "Target position name");
    table->addTextProperty("graph_4", "Graph Name", params.at("graph_4"),
      "Graph name (only for graphNode type)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    int moveCount = std::stoi(params.at("move_count"));
    if (moveCount < 2) moveCount = 2;
    if (moveCount > 4) moveCount = 4;

    // Build JSON array of moves
    nlohmann::json movesArray = nlohmann::json::array();

    for (int i = 1; i <= moveCount; ++i) {
      std::string suffix = "_" + std::to_string(i);
      std::string device = params.at("device" + suffix);
      std::string type = params.at("type" + suffix);
      std::string target = params.at("target" + suffix);
      std::string graph = params.at("graph" + suffix);

      // Skip empty entries
      if (device.empty() || target.empty()) {
        continue;
      }

      nlohmann::json moveObj;
      moveObj["device"] = device;
      moveObj["type"] = type;
      moveObj["target"] = target;

      if (type == "graphNode" && !graph.empty()) {
        moveObj["graph"] = graph;
      }

      movesArray.push_back(moveObj);
    }

    if (movesArray.empty()) {
      std::cerr << "[ParallelMovePlugin] ERROR: No valid moves specified" << std::endl;
      return;
    }

    // Use a pseudo device name for parallel move (command handler ignores it)
    sequence->AddAction("parallel", "ParallelMove", {
      {"moves", movesArray.dump()}
    });

    std::cout << "[MotionProcesses] Built Parallel Move sequence with " << movesArray.size()
              << " devices: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// PARALLEL MOVE RELATIVE PLUGIN - Move multiple devices with relative distances simultaneously
//=============================================================================

class ParallelMoveRelativePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Parallel Move Relative";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Move multiple devices by relative distances simultaneously";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"move_count", "2"},
      // Move 1
      {"device_1", "hex-left"},
      {"axis_1", "X"},
      {"distance_1", "0"},
      // Move 2
      {"device_2", "hex-right"},
      {"axis_2", "X"},
      {"distance_2", "0"},
      // Move 3 (optional)
      {"device_3", ""},
      {"axis_3", "X"},
      {"distance_3", "0"},
      // Move 4 (optional)
      {"device_4", ""},
      {"axis_4", "X"},
      {"distance_4", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("move_count", "Number of Moves (2-4)", params.at("move_count"));

    // Move 1
    widget->addDeviceSelector("device_1", "Device 1", params.at("device_1"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDropdownParameter("axis_1", "Axis 1", params.at("axis_1"), {"X", "Y", "Z", "U", "V", "W"});
    widget->addParameterField("distance_1", "Distance 1 (mm)", params.at("distance_1"));

    // Move 2
    widget->addDeviceSelector("device_2", "Device 2", params.at("device_2"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDropdownParameter("axis_2", "Axis 2", params.at("axis_2"), {"X", "Y", "Z", "U", "V", "W"});
    widget->addParameterField("distance_2", "Distance 2 (mm)", params.at("distance_2"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Parallel Move Relative Configuration");
    table->addNumericProperty("move_count", "Number of Moves",
      std::stod(params.at("move_count")), 2, 4, 1, 0, "",
      "Number of devices to move simultaneously (2-4)");

    // Move 1
    table->addCategoryHeader("Move 1");
    table->addMotionDeviceProperty("device_1", "Device", params.at("device_1"),
      "First device to move");
    table->addDropdownProperty("axis_1", "Axis", params.at("axis_1"),
      {"X", "Y", "Z", "U", "V", "W"},
      "Axis to move on");
    table->addNumericProperty("distance_1", "Distance",
      std::stod(params.at("distance_1")), -1000, 1000, 0.1, 2, "mm",
      "Distance to move (positive or negative)");

    // Move 2
    table->addCategoryHeader("Move 2");
    table->addMotionDeviceProperty("device_2", "Device", params.at("device_2"),
      "Second device to move");
    table->addDropdownProperty("axis_2", "Axis", params.at("axis_2"),
      {"X", "Y", "Z", "U", "V", "W"},
      "Axis to move on");
    table->addNumericProperty("distance_2", "Distance",
      std::stod(params.at("distance_2")), -1000, 1000, 0.1, 2, "mm",
      "Distance to move (positive or negative)");

    // Move 3 (optional)
    table->addCategoryHeader("Move 3 (Optional)");
    table->addMotionDeviceProperty("device_3", "Device", params.at("device_3"),
      "Third device to move (leave empty to skip)");
    table->addDropdownProperty("axis_3", "Axis", params.at("axis_3"),
      {"X", "Y", "Z", "U", "V", "W"},
      "Axis to move on");
    table->addNumericProperty("distance_3", "Distance",
      std::stod(params.at("distance_3")), -1000, 1000, 0.1, 2, "mm",
      "Distance to move (positive or negative)");

    // Move 4 (optional)
    table->addCategoryHeader("Move 4 (Optional)");
    table->addMotionDeviceProperty("device_4", "Device", params.at("device_4"),
      "Fourth device to move (leave empty to skip)");
    table->addDropdownProperty("axis_4", "Axis", params.at("axis_4"),
      {"X", "Y", "Z", "U", "V", "W"},
      "Axis to move on");
    table->addNumericProperty("distance_4", "Distance",
      std::stod(params.at("distance_4")), -1000, 1000, 0.1, 2, "mm",
      "Distance to move (positive or negative)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    int moveCount = std::stoi(params.at("move_count"));
    if (moveCount < 2) moveCount = 2;
    if (moveCount > 4) moveCount = 4;

    // Build JSON array of moves
    nlohmann::json movesArray = nlohmann::json::array();

    for (int i = 1; i <= moveCount; ++i) {
      std::string suffix = "_" + std::to_string(i);
      std::string device = params.at("device" + suffix);
      std::string axis = params.at("axis" + suffix);
      std::string distanceStr = params.at("distance" + suffix);

      // Skip empty device entries
      if (device.empty()) {
        continue;
      }

      // Skip zero distance
      double distance = std::stod(distanceStr);
      if (std::abs(distance) < 1e-9) {
        continue;
      }

      nlohmann::json moveObj;
      moveObj["device"] = device;
      moveObj["axis"] = axis;
      moveObj["distance"] = distance;

      movesArray.push_back(moveObj);
    }

    if (movesArray.empty()) {
      std::cerr << "[ParallelMoveRelativePlugin] WARNING: No valid moves specified (all zero distance)" << std::endl;
      return;
    }

    // Use a pseudo device name for parallel move (command handler ignores it)
    sequence->AddAction("parallel", "ParallelMoveRelative", {
      {"moves", movesArray.dump()}
    });

    std::cout << "[MotionProcesses] Built Parallel Move Relative sequence with " << movesArray.size()
              << " devices: " << step->userNickname << std::endl;
  }
};

//=============================================================================
// MOVE TO CONTEXT XY PLUGIN - Move X/Y to positions from context, keep Z
//=============================================================================

class MoveToContextXYPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Move to Context XY";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Move device X/Y to coordinates from context variables, keeping current Z";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"x_key", "tgtSled.x"},
      {"y_key", "tgtSled.y"},
      {"velocity", "0"},
      {"delay_before_ms", "0"},
      {"delay_after_ms", "0"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("x_key", "X Context Key", params.at("x_key"));
    widget->addParameterField("y_key", "Y Context Key", params.at("y_key"));
    addTimingAndVelocityFormUI(widget, params);
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "Motion Device", params.at("device"),
      "Select the motion device to move");

    table->addCategoryHeader("Target Coordinates from Context");
    table->addTextProperty("x_key", "X Context Key", params.at("x_key"),
      "Context variable name for X coordinate (e.g., 'tgtSled.x' from TransformLocalPoint)");
    table->addTextProperty("y_key", "Y Context Key", params.at("y_key"),
      "Context variable name for Y coordinate (e.g., 'tgtSled.y' from TransformLocalPoint)");

    addTimingAndVelocityTableUI(table, params);
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");
    std::string xKey = params.at("x_key");
    std::string yKey = params.at("y_key");

    addVelocityAction(sequence, device, getParamOr(params, "velocity", "0"));
    addDelayAction(sequence, getParamOr(params, "delay_before_ms", "0"));

    // Use MoveToContextXY command - it will read X/Y from context and keep current Z
    sequence->AddAction(device, "MoveToContextXY", {
      {"x_key", xKey},
      {"y_key", yKey}
    });

    addDelayAction(sequence, getParamOr(params, "delay_after_ms", "0"));

    std::cout << "[MotionProcesses] Built Move to Context XY sequence for: " << step->userNickname
              << " (x_key=" << xKey << ", y_key=" << yKey << ")" << std::endl;
  }
};

//=============================================================================
// FAST ALIGN XY PLUGIN - PI hexapod FSM 2D alignment
//=============================================================================

class FastAlignXYPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Fast Align XY";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "PI hexapod Fast Scan Maximum (FSM) 2D alignment using analog feedback";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "hex-left"},
      {"axis1", "X"},
      {"length1_um", "20"},
      {"axis2", "Y"},
      {"length2_um", "20"},
      {"threshold", "0.01"},
      {"line_distance_um", "1"},
      {"analog_channel", "5"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDropdownParameter("axis1", "Axis 1", params.at("axis1"), {"X", "Y", "Z"});
    widget->addNumericParameter("length1_um", "Scan Length 1 (um)", params.at("length1_um"),
      1, 500, 1, 0, "um");
    widget->addDropdownParameter("axis2", "Axis 2", params.at("axis2"), {"X", "Y", "Z"});
    widget->addNumericParameter("length2_um", "Scan Length 2 (um)", params.at("length2_um"),
      1, 500, 1, 0, "um");
    widget->addNumericParameter("threshold", "Signal Threshold (V)", params.at("threshold"),
      0.001, 10, 0.001, 3, "V");
    widget->addNumericParameter("line_distance_um", "Line Distance (um)", params.at("line_distance_um"),
      0.1, 50, 0.1, 1, "um");
    widget->addDropdownParameter("analog_channel", "Analog Channel", params.at("analog_channel"),
      {"1", "2", "3", "4", "5", "6"});
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "PI Hexapod", params.at("device"),
      "Select the PI hexapod device (hex-* naming required)");

    table->addCategoryHeader("Axis 1 Configuration");
    table->addDropdownProperty("axis1", "Axis", params.at("axis1"),
      {"X", "Y", "Z"},
      "First scan axis (typically X for line-to-line movement)");
    table->addNumericProperty("length1_um", "Scan Length",
      std::stod(params.at("length1_um")), 1, 500, 1, 0, "um",
      "Scan range along axis 1 (in micrometers)");

    table->addCategoryHeader("Axis 2 Configuration");
    table->addDropdownProperty("axis2", "Axis", params.at("axis2"),
      {"X", "Y", "Z"},
      "Second scan axis (typically Y for scanning lines)");
    table->addNumericProperty("length2_um", "Scan Length",
      std::stod(params.at("length2_um")), 1, 500, 1, 0, "um",
      "Scan range along axis 2 (in micrometers)");

    table->addCategoryHeader("Scan Parameters");
    table->addNumericProperty("threshold", "Signal Threshold",
      std::stod(params.at("threshold")), 0.001, 10.0, 0.001, 3, "V",
      "Minimum signal level to consider valid (voltage threshold)");
    table->addNumericProperty("line_distance_um", "Line Spacing",
      std::stod(params.at("line_distance_um")), 0.1, 50.0, 0.1, 1, "um",
      "Distance between scan lines (affects scan resolution)");

    table->addCategoryHeader("Analog Input");
    table->addDropdownProperty("analog_channel", "ADC Channel", params.at("analog_channel"),
      {"1", "2", "3", "4", "5", "6"},
      "Analog input channel for signal feedback (1-6)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");

    // Build FastAlignXY command
    sequence->AddAction(device, "FastAlignXY", {
      {"axis1", params.at("axis1")},
      {"length1_um", params.at("length1_um")},
      {"axis2", params.at("axis2")},
      {"length2_um", params.at("length2_um")},
      {"threshold", params.at("threshold")},
      {"line_distance_um", params.at("line_distance_um")},
      {"analog_channel", params.at("analog_channel")}
    });

    std::cout << "[MotionProcesses] Built Fast Align XY sequence for: " << step->userNickname
              << " (device=" << device
              << ", axis1=" << params.at("axis1") << "/" << params.at("length1_um") << "um"
              << ", axis2=" << params.at("axis2") << "/" << params.at("length2_um") << "um"
              << ", ch=" << params.at("analog_channel") << ")"
              << std::endl;
  }
};

//=============================================================================
// FAST ALIGN Z-XY PLUGIN - PI hexapod FSM with Z-iteration (3D alignment)
//=============================================================================

class FastAlignZXYPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Fast Align Z-XY";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "PI hexapod FSM with Z-iteration: scans multiple Z levels to find optimal 3D position";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "hex-left"},
      {"axis1", "X"},
      {"length1_um", "20"},
      {"axis2", "Y"},
      {"length2_um", "20"},
      {"threshold", "0.01"},
      {"line_distance_um", "1"},
      {"analog_channel", "5"},
      {"z_direction", "Z-"},
      {"z_range_um", "5"},
      {"z_step_um", "1"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDropdownParameter("axis1", "Axis 1", params.at("axis1"), {"X", "Y", "Z"});
    widget->addNumericParameter("length1_um", "Scan Length 1 (um)", params.at("length1_um"),
      1, 500, 1, 0, "um");
    widget->addDropdownParameter("axis2", "Axis 2", params.at("axis2"), {"X", "Y", "Z"});
    widget->addNumericParameter("length2_um", "Scan Length 2 (um)", params.at("length2_um"),
      1, 500, 1, 0, "um");
    widget->addNumericParameter("threshold", "Signal Threshold (V)", params.at("threshold"),
      0.001, 10, 0.001, 3, "V");
    widget->addNumericParameter("line_distance_um", "Line Distance (um)", params.at("line_distance_um"),
      0.1, 50, 0.1, 1, "um");
    widget->addDropdownParameter("analog_channel", "Analog Channel", params.at("analog_channel"),
      {"1", "2", "3", "4", "5", "6"});
    widget->addDropdownParameter("z_direction", "Z Direction", params.at("z_direction"),
      {"Z+", "Z-"});
    widget->addNumericParameter("z_range_um", "Z Range (um)", params.at("z_range_um"),
      1, 100, 1, 0, "um");
    widget->addNumericParameter("z_step_um", "Z Step (um)", params.at("z_step_um"),
      0.1, 10, 0.1, 1, "um");
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device");
    table->addMotionDeviceProperty("device", "PI Hexapod", params.at("device"),
      "Select the PI hexapod device (hex-* naming required)");

    table->addCategoryHeader("XY Axis 1 Configuration");
    table->addDropdownProperty("axis1", "Axis", params.at("axis1"),
      {"X", "Y", "Z"},
      "First XY scan axis (typically X for line-to-line movement)");
    table->addNumericProperty("length1_um", "Scan Length",
      std::stod(params.at("length1_um")), 1, 500, 1, 0, "um",
      "Scan range along axis 1 (in micrometers)");

    table->addCategoryHeader("XY Axis 2 Configuration");
    table->addDropdownProperty("axis2", "Axis", params.at("axis2"),
      {"X", "Y", "Z"},
      "Second XY scan axis (typically Y for scanning lines)");
    table->addNumericProperty("length2_um", "Scan Length",
      std::stod(params.at("length2_um")), 1, 500, 1, 0, "um",
      "Scan range along axis 2 (in micrometers)");

    table->addCategoryHeader("XY Scan Parameters");
    table->addNumericProperty("threshold", "Signal Threshold",
      std::stod(params.at("threshold")), 0.001, 10.0, 0.001, 3, "V",
      "Minimum signal level to consider valid (voltage threshold)");
    table->addNumericProperty("line_distance_um", "Line Spacing",
      std::stod(params.at("line_distance_um")), 0.1, 50.0, 0.1, 1, "um",
      "Distance between scan lines (affects XY scan resolution)");

    table->addCategoryHeader("Z-Iteration Parameters");
    table->addDropdownProperty("z_direction", "Z Direction", params.at("z_direction"),
      {"Z+", "Z-"},
      "Z+ scans upward, Z- scans downward from current position");
    table->addNumericProperty("z_range_um", "Z Range",
      std::stod(params.at("z_range_um")), 1, 100, 1, 0, "um",
      "Total Z distance to scan (in micrometers)");
    table->addNumericProperty("z_step_um", "Z Step",
      std::stod(params.at("z_step_um")), 0.1, 10.0, 0.1, 1, "um",
      "Z increment between FSM iterations (smaller = more precise, slower)");

    table->addCategoryHeader("Analog Input");
    table->addDropdownProperty("analog_channel", "ADC Channel", params.at("analog_channel"),
      {"1", "2", "3", "4", "5", "6"},
      "Analog input channel for signal feedback (1-6)");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string device = params.at("device");

    // Build FastAlignZXY command
    sequence->AddAction(device, "FastAlignZXY", {
      {"axis1", params.at("axis1")},
      {"length1_um", params.at("length1_um")},
      {"axis2", params.at("axis2")},
      {"length2_um", params.at("length2_um")},
      {"threshold", params.at("threshold")},
      {"line_distance_um", params.at("line_distance_um")},
      {"analog_channel", params.at("analog_channel")},
      {"z_direction", params.at("z_direction")},
      {"z_range_um", params.at("z_range_um")},
      {"z_step_um", params.at("z_step_um")}
    });

    std::cout << "[MotionProcesses] Built Fast Align Z-XY sequence for: " << step->userNickname
              << " (device=" << device
              << ", XY: " << params.at("axis1") << "/" << params.at("length1_um") << "um"
              << " x " << params.at("axis2") << "/" << params.at("length2_um") << "um"
              << ", Z: " << params.at("z_direction") << "/" << params.at("z_range_um") << "um"
              << " step=" << params.at("z_step_um") << "um"
              << ", ch=" << params.at("analog_channel") << ")"
              << std::endl;
  }
};

//=============================================================================
// READ POSITION PLUGIN - Read current axis positions into context
//=============================================================================

class ReadPositionPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Read Position";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Read current device position (XYZ) into context variables. "
           "Stores as prefix_x, prefix_y, prefix_z (and optionally UVW).";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "gantry-main"},
      {"key_prefix", "pos"},
      {"save_uvw", "false"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Motion Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("key_prefix", "Context Key Prefix", params.at("key_prefix"));
    widget->addDropdownParameter("save_uvw", "Include UVW", params.at("save_uvw"),
                                 {"false", "true"});
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction(params.at("device"), "SavePositionToContext", {
      {"key_prefix", params.at("key_prefix")},
      {"save_uvw", params.at("save_uvw")}
    });

    std::cout << "[MotionProcesses] Built Read Position for: " << step->userNickname
              << " -> " << params.at("key_prefix") << std::endl;
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
      registry.RegisterPlugin(std::make_unique<XCANExtendedPlugin>());
      registry.RegisterPlugin(std::make_unique<GridScanPlugin>());
      registry.RegisterPlugin(std::make_unique<GridScanExtendedPlugin>());
      registry.RegisterPlugin(std::make_unique<MoveToGraphNodePlugin>());
      registry.RegisterPlugin(std::make_unique<ParallelMovePlugin>());
      registry.RegisterPlugin(std::make_unique<ParallelMoveRelativePlugin>());
      registry.RegisterPlugin(std::make_unique<MoveToContextXYPlugin>());
      registry.RegisterPlugin(std::make_unique<FastAlignXYPlugin>());
      registry.RegisterPlugin(std::make_unique<FastAlignZXYPlugin>());
      registry.RegisterPlugin(std::make_unique<ReadPositionPlugin>());

      std::cout << "[MotionProcesses] Registered 20 motion process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterMotionProcesses g_registerMotionProcesses;
}

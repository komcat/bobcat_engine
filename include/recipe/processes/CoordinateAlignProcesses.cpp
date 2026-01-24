// CoordinateAlignProcesses.cpp
// Coordinate alignment process plugins for 3-point vision-based alignment

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../sequence/SequenceEngine.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"
#include "../RecipeManager.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// TEACH COORDINATE SYSTEM PLUGIN
//=============================================================================

class TeachCoordinateSystemPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Teach Coordinate System";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Create a taught coordinate system from 3 fiducial vision results already in context";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"uid", "master_coord_system"},
      {"fid1_prefix", "dx_Fid1"},
      {"fid2_prefix", "dx_Fid2"},
      {"fid3_prefix", "dx_Fid3"},
      {"fid1_profile", ""},
      {"fid2_profile", ""},
      {"fid3_profile", ""},
      {"motion_device", "gantry-main"},
      {"camera_name", "main_camera"},
      {"description", ""}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // UID for the coordinate system
    widget->addParameterField("uid", "Coordinate System UID",
                             params.at("uid"));

    // Fiducial prefixes (context keys from prior vision execution)
    widget->addParameterField("fid1_prefix", "Fiducial 1 Context Prefix",
                             params.at("fid1_prefix"));
    widget->addParameterField("fid2_prefix", "Fiducial 2 Context Prefix",
                             params.at("fid2_prefix"));
    widget->addParameterField("fid3_prefix", "Fiducial 3 Context Prefix",
                             params.at("fid3_prefix"));

    // Vision profile paths (for later re-alignment)
    widget->addVisionProfileSelector("fid1_profile", "Fiducial 1 Vision Profile",
                                     params.at("fid1_profile"));
    widget->addVisionProfileSelector("fid2_profile", "Fiducial 2 Vision Profile",
                                     params.at("fid2_profile"));
    widget->addVisionProfileSelector("fid3_profile", "Fiducial 3 Vision Profile",
                                     params.at("fid3_profile"));

    // Hardware configuration
    widget->addDeviceSelector("motion_device", "Motion Device",
                              params.at("motion_device"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("camera_name", "Camera Name",
                             params.at("camera_name"));

    // Optional description
    widget->addParameterField("description", "Description (optional)",
                             params.at("description"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction("coordalign", "TeachCoordinateSystem", {
      {"uid", params.at("uid")},
      {"process_uid", step->uniqueId},
      {"fid1_prefix", params.at("fid1_prefix")},
      {"fid2_prefix", params.at("fid2_prefix")},
      {"fid3_prefix", params.at("fid3_prefix")},
      {"fid1_profile", params.at("fid1_profile")},
      {"fid2_profile", params.at("fid2_profile")},
      {"fid3_profile", params.at("fid3_profile")},
      {"motion_device", params.at("motion_device")},
      {"camera_name", params.at("camera_name")},
      {"description", params.at("description")}
    });

    std::cout << "[TeachCoordinateSystemPlugin] Built sequence for: "
      << step->userNickname << " (UID: " << params.at("uid") << "_" << step->uniqueId << ")" << std::endl;
  }
};

//=============================================================================
// ALIGN LIVE COORDINATE SYSTEM PLUGIN
//=============================================================================

class AlignLiveCoordinateSystemPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Align Live Coordinate System";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Re-execute vision at taught positions and compute transformation to live coordinate system";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"taught_uid", "master_coord_system"},
      {"live_uid", "live_session"},
      {"output_prefix", "transform"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Taught system UID (reference) - with browse button to select from available systems
    widget->addTaughtCoordinateSystemSelector("taught_uid", "Taught System UID",
                             params.at("taught_uid"));

    // Live system UID (output)
    widget->addParameterField("live_uid", "Live System UID",
                             params.at("live_uid"));

    // Output prefix for transform results
    widget->addParameterField("output_prefix", "Output Prefix",
                             params.at("output_prefix"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction("coordalign", "AlignLiveCoordinateSystem", {
      {"taught_uid", params.at("taught_uid")},
      {"live_uid", params.at("live_uid")},
      {"output_prefix", params.at("output_prefix")}
    });

    std::cout << "[AlignLiveCoordinateSystemPlugin] Built sequence for: "
      << step->userNickname << " (taught: " << params.at("taught_uid")
      << " -> live: " << params.at("live_uid") << ")" << std::endl;
  }
};

//=============================================================================
// TRANSFORM POINT PLUGIN
//=============================================================================

class TransformPointPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Transform Point";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Transform a point from taught to live coordinates using a live coordinate system";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"live_uid", "live_session"},
      {"input_x", "0.0"},
      {"input_y", "0.0"},
      {"output_prefix", "transformed"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Live system UID
    widget->addParameterField("live_uid", "Live System UID",
                             params.at("live_uid"));

    // Input coordinates (can be numeric or context reference like $key)
    widget->addParameterField("input_x", "Input X (value or $context_key)",
                             params.at("input_x"));
    widget->addParameterField("input_y", "Input Y (value or $context_key)",
                             params.at("input_y"));

    // Output prefix
    widget->addParameterField("output_prefix", "Output Prefix",
                             params.at("output_prefix"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction("coordalign", "TransformPoint", {
      {"live_uid", params.at("live_uid")},
      {"input_x", params.at("input_x")},
      {"input_y", params.at("input_y")},
      {"output_prefix", params.at("output_prefix")}
    });

    std::cout << "[TransformPointPlugin] Built sequence for: "
      << step->userNickname << " (live: " << params.at("live_uid") << ")" << std::endl;
  }
};

//=============================================================================
// LIST COORDINATE SYSTEMS PLUGIN
//=============================================================================

class ListCoordinateSystemsPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "List Coordinate Systems";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "List all stored taught and live coordinate systems";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    (void)widget;
    (void)instance;
    // No parameters needed
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    (void)step;

    sequence->AddAction("coordalign", "ListCoordinateSystems", {});

    std::cout << "[ListCoordinateSystemsPlugin] Built sequence" << std::endl;
  }
};

//=============================================================================
// CLEAR COORDINATE SYSTEM PLUGIN
//=============================================================================

class ClearCoordinateSystemPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Clear Coordinate System";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Remove a coordinate system by UID";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"uid", ""},
      {"type", "both"}  // "taught", "live", or "both"
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addTaughtCoordinateSystemSelector("uid", "Coordinate System UID",
                             params.at("uid"));
    widget->addDropdownParameter("type", "Type to Clear",
                                params.at("type"),
                                {"both", "taught", "live"});
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction("coordalign", "ClearCoordinateSystem", {
      {"uid", params.at("uid")},
      {"type", params.at("type")}
    });

    std::cout << "[ClearCoordinateSystemPlugin] Built sequence for UID: "
      << params.at("uid") << std::endl;
  }
};

//=============================================================================
// TEACH LOCAL POINT PLUGIN
//=============================================================================

class TeachLocalPointPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Teach Local Point";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Teach a point in local coordinates (user moves gantry to position, captures as local coords)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"uid", "target_point_1"},
      {"coord_system_uid", "master_coord_system"},
      {"motion_device", "gantry-main"},
      {"world_x", ""},
      {"world_y", ""},
      {"description", ""}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("uid", "Local Point UID",
                             params.at("uid"));
    widget->addTaughtCoordinateSystemSelector("coord_system_uid", "Coordinate System UID",
                             params.at("coord_system_uid"));
    widget->addDeviceSelector("motion_device", "Motion Device",
                              params.at("motion_device"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("world_x", "World X (or $context_key, leave empty for device pos)",
                             params.at("world_x"));
    widget->addParameterField("world_y", "World Y (or $context_key, leave empty for device pos)",
                             params.at("world_y"));
    widget->addParameterField("description", "Description (optional)",
                             params.at("description"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    // Combine user-provided UID with process unique ID to ensure uniqueness
    std::string combinedUid = params.at("uid") + "_" + step->uniqueId;

    std::map<std::string, std::string> actionParams = {
      {"uid", combinedUid},
      {"coord_system_uid", params.at("coord_system_uid")},
      {"motion_device", params.at("motion_device")},
      {"description", params.at("description")}
    };

    // Only add world_x/world_y if they're not empty
    if (!params.at("world_x").empty()) {
      actionParams["world_x"] = params.at("world_x");
    }
    if (!params.at("world_y").empty()) {
      actionParams["world_y"] = params.at("world_y");
    }

    sequence->AddAction("coordalign", "TeachLocalPoint", actionParams);

    std::cout << "[TeachLocalPointPlugin] Built sequence for: "
      << step->userNickname << " (UID: " << combinedUid << ")" << std::endl;
  }
};

//=============================================================================
// TRANSFORM LOCAL POINT PLUGIN
//=============================================================================

class TransformLocalPointPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Transform Local Point";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Transform a stored local point to live world coordinates";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"point_uid", "target_point_1"},
      {"live_uid", "live_session"},
      {"output_prefix", "target"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Local point UID - with browse button to select from available points
    widget->addLocalPointSelector("point_uid", "Local Point UID",
                             params.at("point_uid"));
    widget->addParameterField("live_uid", "Live System UID",
                             params.at("live_uid"));
    widget->addParameterField("output_prefix", "Output Prefix",
                             params.at("output_prefix"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    sequence->AddAction("coordalign", "TransformLocalPoint", {
      {"point_uid", params.at("point_uid")},
      {"live_uid", params.at("live_uid")},
      {"output_prefix", params.at("output_prefix")}
    });

    std::cout << "[TransformLocalPointPlugin] Built sequence for: "
      << step->userNickname << " (point: " << params.at("point_uid") << ")" << std::endl;
  }
};

//=============================================================================
// LIST LOCAL POINTS PLUGIN
//=============================================================================

class ListLocalPointsPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "List Local Points";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "List all taught local points";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    (void)widget;
    (void)instance;
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    (void)step;
    sequence->AddAction("coordalign", "ListLocalPoints", {});
    std::cout << "[ListLocalPointsPlugin] Built sequence" << std::endl;
  }
};

//=============================================================================
// CLEAR LOCAL POINT PLUGIN
//=============================================================================

class ClearLocalPointPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Clear Local Point";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Remove a local point by UID";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"uid", ""}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addLocalPointSelector("uid", "Local Point UID",
                             params.at("uid"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    sequence->AddAction("coordalign", "ClearLocalPoint", {
      {"uid", params.at("uid")}
    });
    std::cout << "[ClearLocalPointPlugin] Built sequence for UID: "
      << params.at("uid") << std::endl;
  }
};

//=============================================================================
// MOVE TO LOCAL XY PLUGIN
//=============================================================================

class MoveToLocalXYPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Move to Local XY";
  }

  std::string GetCategory() const override {
    return "Calibration";
  }

  std::string GetDescription() const override {
    return "Transform local coordinates using coordinate system and move device to the resulting world X/Y (keeping Z)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"motion_device", "gantry-main"},
      {"coord_system_uid", "live_session"},
      {"local_x", "0.0"},
      {"local_y", "0.0"},
      {"output_prefix", ""}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Motion device to move
    widget->addDeviceSelector("motion_device", "Motion Device",
                              params.at("motion_device"),
                              static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Coordinate system UID (can be live or taught - live is preferred)
    widget->addTaughtCoordinateSystemSelector("coord_system_uid", "Coordinate System UID",
                             params.at("coord_system_uid"));

    // Local coordinates (can be numeric or context reference)
    widget->addParameterField("local_x", "Local X (value or $context_key)",
                             params.at("local_x"));
    widget->addParameterField("local_y", "Local Y (value or $context_key)",
                             params.at("local_y"));

    // Optional output prefix to store resulting world coordinates
    widget->addParameterField("output_prefix", "Output Prefix (optional)",
                             params.at("output_prefix"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> actionParams = {
      {"motion_device", params.at("motion_device")},
      {"coord_system_uid", params.at("coord_system_uid")},
      {"local_x", params.at("local_x")},
      {"local_y", params.at("local_y")}
    };

    // Only add output_prefix if specified
    if (!params.at("output_prefix").empty()) {
      actionParams["output_prefix"] = params.at("output_prefix");
    }

    sequence->AddAction("coordalign", "MoveToLocalXY", actionParams);

    std::cout << "[MoveToLocalXYPlugin] Built sequence for: "
      << step->userNickname << " (device: " << params.at("motion_device")
      << ", coord: " << params.at("coord_system_uid") << ")" << std::endl;
  }
};

//=============================================================================
// PLUGIN REGISTRATION (Static initialization)
//=============================================================================

namespace {
  struct CoordinateAlignProcessRegistrar {
    CoordinateAlignProcessRegistrar() {
      auto& registry = ProcessPluginRegistry::Instance();
      registry.RegisterPlugin(std::make_unique<TeachCoordinateSystemPlugin>());
      registry.RegisterPlugin(std::make_unique<AlignLiveCoordinateSystemPlugin>());
      registry.RegisterPlugin(std::make_unique<TransformPointPlugin>());
      registry.RegisterPlugin(std::make_unique<ListCoordinateSystemsPlugin>());
      registry.RegisterPlugin(std::make_unique<ClearCoordinateSystemPlugin>());
      registry.RegisterPlugin(std::make_unique<TeachLocalPointPlugin>());
      registry.RegisterPlugin(std::make_unique<TransformLocalPointPlugin>());
      registry.RegisterPlugin(std::make_unique<ListLocalPointsPlugin>());
      registry.RegisterPlugin(std::make_unique<ClearLocalPointPlugin>());
      registry.RegisterPlugin(std::make_unique<MoveToLocalXYPlugin>());
      std::cout << "[CoordinateAlignProcesses] Registered 10 coordinate alignment plugins" << std::endl;
    }
  };

  static CoordinateAlignProcessRegistrar s_registrar;
}

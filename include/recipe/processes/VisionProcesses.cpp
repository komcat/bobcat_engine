// VisionProcesses.cpp
// Vision processing plugins: Load Profile, Execute Vision, Capture and Execute
#ifdef HAS_OPENCV

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"
#include "../RecipeManager.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// LOAD VISION PROFILE PLUGIN
//=============================================================================

class LoadVisionProfilePlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Load Vision Profile";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Load a vision processing profile (plugin settings, ROI, calibration)";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"profile_path", "config/vision/profile.json"}
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Profile path (use find to avoid exception if key is missing)
        auto it = params.find("profile_path");
        std::string profilePath = (it != params.end()) ? it->second : "config/vision/profile.json";
        widget->addParameterField("profile_path", "Profile Path", profilePath);
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        std::map<std::string, std::string> loadParams;

        auto it = params.find("profile_path");
        if (it != params.end()) {
            loadParams["path"] = it->second;
        }

        sequence->AddAction("vision", "load_profile", loadParams);
    }
};

//=============================================================================
// EXECUTE VISION PROFILE PLUGIN
//=============================================================================

class ExecuteVisionProfilePlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Execute Vision Profile";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Load profile, capture image from camera, execute vision, return results to context";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"profile_path", ""},  // Will be selected from dropdown (auto-discovers from product vision folder)
            {"camera_name", "main_camera"},
            {"capture_as", ""},     // Optional: store results with custom prefix (e.g., "sled_position")
            {"calculate_world_position", "false"},  // Calculate world position from detected feature
            {"motion_device", "gantry-main"}        // Motion device for world position calculation
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Helper to get parameter with default value (safe for missing keys)
        auto getParam = [&params](const std::string& key, const std::string& defaultVal = "") {
            auto it = params.find(key);
            return (it != params.end()) ? it->second : defaultVal;
        };

        // Vision profile selector (dropdown of .json files from product vision folder)
        widget->addVisionProfileSelector("profile_path", "Vision Profile", getParam("profile_path", ""));

        // Camera selector
        widget->addDeviceSelector("camera_name", "Camera", getParam("camera_name", "main_camera"),
                                 static_cast<int>(DeviceComboBox::DeviceType::Camera));

        // Capture As (optional custom prefix for results)
        widget->addParameterField("capture_as", "Capture As (optional)", getParam("capture_as", ""));

        // World position calculation (like GotoDetect)
        widget->addCheckboxParameter("calculate_world_position", "Calculate World Position",
            getParam("calculate_world_position", "false"),
            "Calculate detected feature world position in mm (like GotoDetect)");

        // Motion device for world position calculation
        widget->addDeviceSelector("motion_device", "Motion Device (for world pos)",
            getParam("motion_device", "gantry-main"),
            static_cast<int>(DeviceComboBox::DeviceType::Motion));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        std::map<std::string, std::string> execParams;

        // Profile path
        auto profileIt = params.find("profile_path");
        if (profileIt != params.end()) {
            execParams["profile_path"] = profileIt->second;
        }

        // Camera name
        auto cameraIt = params.find("camera_name");
        if (cameraIt != params.end()) {
            execParams["camera"] = cameraIt->second;
        }

        // Capture As prefix (optional)
        auto captureAsIt = params.find("capture_as");
        bool hasCaptureAs = (captureAsIt != params.end() && !captureAsIt->second.empty());
        if (hasCaptureAs) {
            execParams["capture_as"] = captureAsIt->second;
        }

        // ✅ World position calculation (like GotoDetect)
        // Auto-enable if capture_as is specified (since you need device position to move to detected coords)
        auto calcWorldIt = params.find("calculate_world_position");
        bool shouldCalcWorld = (calcWorldIt != params.end() && calcWorldIt->second == "true") || hasCaptureAs;

        if (shouldCalcWorld) {
            execParams["calculate_world_position"] = "true";

            // Motion device for world position calculation
            auto motionDeviceIt = params.find("motion_device");
            if (motionDeviceIt != params.end() && !motionDeviceIt->second.empty()) {
                execParams["motion_device"] = motionDeviceIt->second;
            } else {
                // Default to gantry-main if not specified
                execParams["motion_device"] = "gantry-main";
            }

            std::cout << "[ExecuteVisionProfile] World position calculation enabled (device: "
                      << execParams["motion_device"] << ")" << std::endl;
        }

        // Single command that does: load profile + capture + execute + return results
        sequence->AddAction("vision", "execute_profile", execParams);
    }
};

//=============================================================================
// CAPTURE AND EXECUTE VISION PLUGIN
//=============================================================================

class CaptureAndExecuteVisionPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Capture and Execute Vision";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Capture image from camera and execute vision plugin";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"camera_name", "main_camera"},
            {"plugin_name", ""},  // Empty = use what was loaded from profile
            {"profile_path", ""}  // Optional - load profile first
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Camera selector
        widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                                 static_cast<int>(DeviceComboBox::DeviceType::Camera));

        // Plugin name (optional)
        widget->addParameterField("plugin_name", "Plugin Name (optional)", params.at("plugin_name"));

        // Profile path (optional - load before execution)
        widget->addParameterField("profile_path", "Profile Path (optional)", params.at("profile_path"));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        // Optionally load profile first
        auto profileIt = params.find("profile_path");
        if (profileIt != params.end() && !profileIt->second.empty()) {
            std::map<std::string, std::string> loadParams;
            loadParams["path"] = profileIt->second;
            sequence->AddAction("vision", "load_profile", loadParams);
        }

        // Build capture and execute command
        std::map<std::string, std::string> execParams;

        auto cameraIt = params.find("camera_name");
        if (cameraIt != params.end()) {
            execParams["camera"] = cameraIt->second;
        }

        auto pluginIt = params.find("plugin_name");
        if (pluginIt != params.end() && !pluginIt->second.empty()) {
            execParams["plugin"] = pluginIt->second;
        }

        sequence->AddAction("vision", "capture_and_execute", execParams);
    }
};

//=============================================================================
// PATTERN MATCHING PLUGIN (Convenience wrapper)
//=============================================================================

class RunPatternMatchingPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Run Pattern Matching";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Execute pattern matching on camera image and store results";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"camera_name", "main_camera"},
            {"profile_path", ""},  // Vision profile with PatternMatching plugin
            {"store_result_as", "pr_result"}  // Context key prefix for results
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Camera selector
        widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                                 static_cast<int>(DeviceComboBox::DeviceType::Camera));

        // Profile path
        widget->addParameterField("profile_path", "Profile Path", params.at("profile_path"));

        // Result storage key
        widget->addParameterField("store_result_as", "Store Result As", params.at("store_result_as"));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        // Load profile
        auto profileIt = params.find("profile_path");
        if (profileIt != params.end() && !profileIt->second.empty()) {
            std::map<std::string, std::string> loadParams;
            loadParams["path"] = profileIt->second;
            sequence->AddAction("vision", "load_profile", loadParams);
        }

        // Capture and execute
        std::map<std::string, std::string> execParams;

        auto cameraIt = params.find("camera_name");
        if (cameraIt != params.end()) {
            execParams["camera"] = cameraIt->second;
        }

        execParams["plugin"] = "PatternMatching";

        sequence->AddAction("vision", "capture_and_execute", execParams);

        // Results are automatically stored in context as:
        // vision.result.best_score
        // vision.result.match_0_anchor_x
        // vision.result.match_0_anchor_y
        // vision.point.0.x, vision.point.0.y
        // etc.
    }
};

//=============================================================================
// CIRCLE DETECTION PLUGIN (Convenience wrapper)
//=============================================================================

class RunCircleDetectionPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Run Circle Detection";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Execute circle detection on camera image and store results";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"camera_name", "main_camera"},
            {"profile_path", ""}  // Vision profile with CircleDetection plugin
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Camera selector
        widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                                 static_cast<int>(DeviceComboBox::DeviceType::Camera));

        // Profile path
        widget->addParameterField("profile_path", "Profile Path", params.at("profile_path"));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        // Load profile
        auto profileIt = params.find("profile_path");
        if (profileIt != params.end() && !profileIt->second.empty()) {
            std::map<std::string, std::string> loadParams;
            loadParams["path"] = profileIt->second;
            sequence->AddAction("vision", "load_profile", loadParams);
        }

        // Capture and execute
        std::map<std::string, std::string> execParams;

        auto cameraIt = params.find("camera_name");
        if (cameraIt != params.end()) {
            execParams["camera"] = cameraIt->second;
        }

        execParams["plugin"] = "CircleDetection";

        sequence->AddAction("vision", "capture_and_execute", execParams);
    }
};

//=============================================================================
// MOVE TO DETECTED POSITION PLUGIN - Move to world coordinates from previous detection
//=============================================================================

class MoveToDetectedPositionPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Move to Detected Position";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Move gantry-main XY to world coordinates from previous GotoDetect result (keeps current Z)";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"result_name", ""},                      // Context prefix from GotoDetect (e.g., "loc_sled")
            {"keep_z", "true"},                       // Keep current Z position
            {"coord_source", "Auto (Primary Detection)"}  // Coordinate source dropdown display text
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Helper to get parameter with default value
        auto getParam = [&params](const std::string& key, const std::string& defaultVal = "") {
            auto it = params.find(key);
            return (it != params.end()) ? it->second : defaultVal;
        };

        // Result name (context prefix from GotoDetect)
        widget->addParameterField("result_name", "Detection Result Name",
            getParam("result_name", ""));

        // Coordinate source selector
        std::vector<std::string> coordSourceOptions = {
            "Auto (Primary Detection)",
            "Line Center",
            "Line Start Point",
            "Line End Point",
            "Corner Point"
        };
        widget->addDropdownParameter("coord_source", "Coordinate Source",
            getParam("coord_source", "Auto (Primary Detection)"), coordSourceOptions);

        // Keep Z checkbox
        widget->addCheckboxParameter("keep_z", "Keep Current Z Position",
            getParam("keep_z", "true"), "When enabled, only moves XY and keeps current Z position");
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        std::string resultName = params.at("result_name");
        std::string keepZ = params.count("keep_z") ? params.at("keep_z") : "true";
        std::string coordSourceDisplay = params.count("coord_source") ? params.at("coord_source") : "Auto (Primary Detection)";

        // Map display text to internal value
        std::string coordSource = "auto";
        if (coordSourceDisplay == "Line Center") {
            coordSource = "line_center";
        } else if (coordSourceDisplay == "Line Start Point") {
            coordSource = "line_start";
        } else if (coordSourceDisplay == "Line End Point") {
            coordSource = "line_end";
        } else if (coordSourceDisplay == "Corner Point") {
            coordSource = "corner";
        }

        // Determine X/Y coordinate keys based on source
        std::string xKey, yKey;
        if (coordSource == "line_center") {
            xKey = resultName + ".result.center_x";
            yKey = resultName + ".result.center_y";
        } else if (coordSource == "line_start") {
            xKey = resultName + ".result.start_x";
            yKey = resultName + ".result.start_y";
        } else if (coordSource == "line_end") {
            xKey = resultName + ".result.end_x";
            yKey = resultName + ".result.end_y";
        } else if (coordSource == "corner") {
            xKey = resultName + ".result.corner_x";
            yKey = resultName + ".result.corner_y";
        } else {
            // "auto" - use default .detected.world_x/y_mm
            xKey = "";
            yKey = "";
        }

        // Move gantry-main to detected world position
        std::map<std::string, std::string> moveParams = {
            {"context_prefix", resultName},
            {"keep_z", keepZ}
        };
        if (!xKey.empty() && !yKey.empty()) {
            moveParams["x_key"] = xKey;
            moveParams["y_key"] = yKey;
        }
        sequence->AddAction("gantry-main", "MoveToContextPosition", moveParams);

        std::string coordInfo = (coordSource == "auto") ? ".detected.world_x/y_mm" :
                                 "(" + xKey + ", " + yKey + ")";
        std::cout << "[VisionProcesses] Built MoveToDetectedPosition sequence: "
                  << "gantry-main -> " << resultName << coordInfo
                  << " (keep_z=" << keepZ << ", source=" << coordSource << ")" << std::endl;
    }
};

//=============================================================================
// GOTO DETECT PLUGIN - Move to graph node and detect feature world position
//=============================================================================

class GotoDetectPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "GotoDetect";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Move gantry-main to graph node, execute vision profile, calculate detected feature world position (mm)";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"graphName", ""},           // Graph name for gantry-main
            {"endNodeName", ""},         // Target node position
            {"profile_path", ""},        // Vision profile to execute
            {"camera_name", "main_camera"}, // Camera for capture
            {"capture_as", ""}           // Optional: store results with custom prefix
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Helper to get parameter with default value
        auto getParam = [&params](const std::string& key, const std::string& defaultVal = "") {
            auto it = params.find(key);
            return (it != params.end()) ? it->second : defaultVal;
        };

        // Graph selector for gantry-main (fixed device)
        widget->addGraphSelectorForDevice("graphName", "Graph Name",
            getParam("graphName", ""), "gantry-main");

        // Node selector (based on selected graph, fixed device = gantry-main)
        widget->addGraphNodeSelectorForDevice("endNodeName", "Target Node",
            getParam("endNodeName", ""), "gantry-main", "graphName");

        // Vision profile selector
        widget->addVisionProfileSelector("profile_path", "Vision Profile",
            getParam("profile_path", ""));

        // Camera selector
        widget->addDeviceSelector("camera_name", "Camera",
            getParam("camera_name", "main_camera"),
            static_cast<int>(DeviceComboBox::DeviceType::Camera));

        // Capture As (optional custom prefix for results)
        widget->addParameterField("capture_as", "Capture As (optional)",
            getParam("capture_as", ""));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        // Step 1: Move gantry-main to target graph node
        sequence->AddAction("gantry-main", "MoveToGraphNode", {
            {"graphName", params.at("graphName")},
            {"endNodeName", params.at("endNodeName")}
        });

        // Step 2: Execute vision profile with world position calculation
        std::map<std::string, std::string> execParams;

        // Profile path
        auto profileIt = params.find("profile_path");
        if (profileIt != params.end()) {
            execParams["profile_path"] = profileIt->second;
        }

        // Camera name
        auto cameraIt = params.find("camera_name");
        if (cameraIt != params.end()) {
            execParams["camera"] = cameraIt->second;
        }

        // Capture As prefix (optional)
        auto captureAsIt = params.find("capture_as");
        if (captureAsIt != params.end() && !captureAsIt->second.empty()) {
            execParams["capture_as"] = captureAsIt->second;
        }

        // Enable world position calculation for gantry-main
        execParams["calculate_world_position"] = "true";
        execParams["motion_device"] = "gantry-main";

        // Execute vision profile (load + capture + execute + world position calculation)
        sequence->AddAction("vision", "execute_profile", execParams);

        std::cout << "[VisionProcesses] Built GotoDetect sequence: "
                  << "gantry-main -> " << params.at("graphName") << ":" << params.at("endNodeName")
                  << " + vision profile: " << params.at("profile_path") << std::endl;
    }
};

//=============================================================================
// SHOW CONTEXT IMAGE PLUGIN - Display image from context in popup window
//=============================================================================

class ShowContextImagePlugin : public IProcessPlugin
{
public:
    std::string GetName() const override {
        return "Show Context Image";
    }

    std::string GetCategory() const override {
        return "Vision";
    }

    std::string GetDescription() const override {
        return "Display an image from sequence context in a non-modal popup window";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"context_key", ""},        // Context key for the image (e.g., "$FLIPSN" or "vision.result.image")
            {"title", ""},              // Window title (optional, uses context_key if empty)
            {"auto_close_ms", "0"}      // Auto-close after ms (0 = no auto-close)
        };
    }

    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Helper to get parameter with default value
        auto getParam = [&params](const std::string& key, const std::string& defaultVal = "") {
            auto it = params.find(key);
            return (it != params.end()) ? it->second : defaultVal;
        };

        // Context key input (e.g., "$FLIPSN" or "vision.result.image")
        widget->addParameterField("context_key", "Context Key",
            getParam("context_key", ""));

        // Window title (optional)
        widget->addParameterField("title", "Window Title (optional)",
            getParam("title", ""));

        // Auto-close time
        widget->addParameterField("auto_close_ms", "Auto Close (ms)",
            getParam("auto_close_ms", "0"));
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        const auto& params = step->parameters;

        std::map<std::string, std::string> cmdParams;

        auto keyIt = params.find("context_key");
        if (keyIt != params.end() && !keyIt->second.empty()) {
            cmdParams["context_key"] = keyIt->second;
        }

        auto titleIt = params.find("title");
        if (titleIt != params.end() && !titleIt->second.empty()) {
            cmdParams["title"] = titleIt->second;
        }

        auto autoCloseIt = params.find("auto_close_ms");
        if (autoCloseIt != params.end() && !autoCloseIt->second.empty()) {
            cmdParams["auto_close_ms"] = autoCloseIt->second;
        }

        sequence->AddAction("vision", "show_context_image", cmdParams);

        std::cout << "[VisionProcesses] Built ShowContextImage sequence: key="
                  << cmdParams["context_key"] << std::endl;
    }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
    struct VisionProcessesRegistrar {
        VisionProcessesRegistrar() {
            auto& registry = ProcessPluginRegistry::Instance();

            // Only register Execute Vision Profile (combines load + capture + execute)
            registry.RegisterPlugin(std::make_unique<ExecuteVisionProfilePlugin>());
            std::cout << "[ProcessPluginRegistry] Registered plugin: Execute Vision Profile (Category: Vision)" << std::endl;

            // MoveToDetectedPosition: Move to world coordinates from previous detection
            registry.RegisterPlugin(std::make_unique<MoveToDetectedPositionPlugin>());
            std::cout << "[ProcessPluginRegistry] Registered plugin: Move to Detected Position (Category: Vision)" << std::endl;

            // GotoDetect: Move to node + vision detection with world position
            registry.RegisterPlugin(std::make_unique<GotoDetectPlugin>());
            std::cout << "[ProcessPluginRegistry] Registered plugin: GotoDetect (Category: Vision)" << std::endl;

            // ShowContextImage: Display image from context in popup window
            registry.RegisterPlugin(std::make_unique<ShowContextImagePlugin>());
            std::cout << "[ProcessPluginRegistry] Registered plugin: Show Context Image (Category: Vision)" << std::endl;

            std::cout << "[VisionProcesses] Registered 4 vision processing plugins" << std::endl;
        }
    };

    // Static instance to trigger registration at load time
    static VisionProcessesRegistrar s_visionProcessesRegistrar;
}

#endif // HAS_OPENCV

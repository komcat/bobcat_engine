// CustomServerProcesses.cpp
// Recipe plugins for the beam detection server: Connect, Disconnect, Detect, Fit, Reset

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// BEAM: CONNECT SERVER
//=============================================================================

class BeamConnectPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Beam: Connect Server";
  }

  std::string GetCategory() const override {
    return "Custom Server";
  }

  std::string GetDescription() const override {
    return "Connect to the beam detection TCP server";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"host", "127.0.0.1"},
      {"port", "5555"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("host", "Host Address", params.at("host"));
    widget->addParameterField("port", "Port", params.at("port"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    sequence->AddAction("beamserver", "Connect", {
      {"host", params.at("host")},
      {"port", params.at("port")}
    });
    std::cout << "[CustomServerProcesses] Built Beam Connect for: "
              << step->userNickname << std::endl;
  }
};

//=============================================================================
// BEAM: DISCONNECT SERVER
//=============================================================================

class BeamDisconnectPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Beam: Disconnect Server";
  }

  std::string GetCategory() const override {
    return "Custom Server";
  }

  std::string GetDescription() const override {
    return "Disconnect from the beam detection TCP server";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};
  }

  void CreateConfigUI(ProcessConfigWidget* /*widget*/, const ProcessInstance* /*instance*/) override {
    // No parameters
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    sequence->AddAction("beamserver", "Disconnect", {});
    std::cout << "[CustomServerProcesses] Built Beam Disconnect for: "
              << step->userNickname << std::endl;
  }
};

//=============================================================================
// BEAM: DETECT
//=============================================================================

class BeamDetectPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Beam: Detect";
  }

  std::string GetCategory() const override {
    return "Custom Server";
  }

  std::string GetDescription() const override {
    return "Capture camera image and send to beam server for beam detection";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"camera_device", "main_camera"},
      {"z_um", "context:swir_cam_z"},
      {"rotate_180", "false"},
      {"um_per_px_x", "0.25"},
      {"um_per_px_y", "0.25"},
      {"captureAs", "beam"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addDeviceSelector("camera_device", "Camera", params.at("camera_device"),
                              static_cast<int>(DeviceComboBox::DeviceType::Camera));
    widget->addParameterField("z_um", "Z Position (context:key or value)", params.at("z_um"));
    widget->addDropdownParameter("rotate_180", "Rotate 180", params.at("rotate_180"),
                                 {"false", "true"});
    widget->addParameterField("um_per_px_x", "um/px X", params.at("um_per_px_x"));
    widget->addParameterField("um_per_px_y", "um/px Y", params.at("um_per_px_y"));
    widget->addParameterField("captureAs", "Capture As (Context Prefix)", params.at("captureAs"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    sequence->AddAction("beamserver", "Detect", {
      {"camera_device", params.at("camera_device")},
      {"z_um", params.at("z_um")},
      {"rotate_180", params.at("rotate_180")},
      {"um_per_px_x", params.at("um_per_px_x")},
      {"um_per_px_y", params.at("um_per_px_y")},
      {"captureAs", params.at("captureAs")}
    });
    std::cout << "[CustomServerProcesses] Built Beam Detect for: "
              << step->userNickname << std::endl;
  }
};

//=============================================================================
// BEAM: DETECT FILE
//=============================================================================

class BeamDetectFilePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Beam: Detect File";
  }

  std::string GetCategory() const override {
    return "Custom Server";
  }

  std::string GetDescription() const override {
    return "Send a saved image file path to the beam server for detection (no camera needed)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"file_path", ""},
      {"z_um", "0.0"},
      {"rotate_180", "false"},
      {"um_per_px_x", "0.25"},
      {"um_per_px_y", "0.25"},
      {"captureAs", "beam"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addFileSelector("file_path", "Image File Path", params.at("file_path"),
                            "Image Files (*.png *.bmp *.tif *.tiff *.jpg *.jpeg)");
    widget->addParameterField("z_um", "Z Position (context:key or value)", params.at("z_um"));
    widget->addDropdownParameter("rotate_180", "Rotate 180", params.at("rotate_180"),
                                 {"false", "true"});
    widget->addParameterField("um_per_px_x", "um/px X", params.at("um_per_px_x"));
    widget->addParameterField("um_per_px_y", "um/px Y", params.at("um_per_px_y"));
    widget->addParameterField("captureAs", "Capture As (Context Prefix)", params.at("captureAs"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    sequence->AddAction("beamserver", "DetectFile", {
      {"file_path", params.at("file_path")},
      {"z_um", params.at("z_um")},
      {"rotate_180", params.at("rotate_180")},
      {"um_per_px_x", params.at("um_per_px_x")},
      {"um_per_px_y", params.at("um_per_px_y")},
      {"captureAs", params.at("captureAs")}
    });
    std::cout << "[CustomServerProcesses] Built Beam Detect File for: "
              << step->userNickname << std::endl;
  }
};

//=============================================================================
// BEAM: FIT
//=============================================================================

class BeamFitPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Beam: Fit";
  }

  std::string GetCategory() const override {
    return "Custom Server";
  }

  std::string GetDescription() const override {
    return "Run Gaussian fit on accumulated beam detections to compute waist, M2, poynting angle";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"um_per_px_x", "0.25"},
      {"um_per_px_y", "0.25"},
      {"wavelength_m", "1550e-9"},
      {"captureAs", "beam"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    auto get = [&](const std::string& key, const std::string& def = "") -> std::string {
      auto it = params.find(key);
      return (it != params.end()) ? it->second : def;
    };
    widget->addParameterField("um_per_px_x", "um/px X", get("um_per_px_x", "0.25"));
    widget->addParameterField("um_per_px_y", "um/px Y", get("um_per_px_y", "0.25"));
    widget->addParameterField("wavelength_m", "Wavelength (m)", get("wavelength_m", "1550e-9"));
    widget->addParameterField("captureAs", "Capture As (Context Prefix)", get("captureAs", "beam"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    auto get = [&](const std::string& key, const std::string& def = "") -> std::string {
      auto it = params.find(key);
      return (it != params.end()) ? it->second : def;
    };
    sequence->AddAction("beamserver", "Fit", {
      {"um_per_px_x", get("um_per_px_x", "0.25")},
      {"um_per_px_y", get("um_per_px_y", "0.25")},
      {"wavelength_m", get("wavelength_m", "1550e-9")},
      {"captureAs", get("captureAs", "beam")}
    });
    std::cout << "[CustomServerProcesses] Built Beam Fit for: "
              << step->userNickname << std::endl;
  }
};

//=============================================================================
// BEAM: DETECT FOLDER
//=============================================================================

class BeamDetectFolderPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Beam: Detect Folder";
  }

  std::string GetCategory() const override {
    return "Custom Server";
  }

  std::string GetDescription() const override {
    return "Send all images in a folder to beam server for detection (batch DETECT_FILE with delay)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"folder_path", ""},
      {"z_pattern", "zheight<number>um"},
      {"um_per_px_x", "0.25"},
      {"um_per_px_y", "0.25"},
      {"delay_ms", "150"},
      {"captureAs", "beam"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    auto get = [&](const std::string& key, const std::string& def = "") -> std::string {
      auto it = params.find(key);
      return (it != params.end()) ? it->second : def;
    };
    widget->addFolderSelector("folder_path", "Image Folder Path", get("folder_path"));
    widget->addDropdownParameter("z_pattern", "Z-height Filename Pattern", get("z_pattern", "zheight<number>um"),
      {"zheight<number>um", "z<number>um", "z_<number>", "<number>um"});
    widget->addParameterField("um_per_px_x", "um/px X", get("um_per_px_x", "0.25"));
    widget->addParameterField("um_per_px_y", "um/px Y", get("um_per_px_y", "0.25"));
    widget->addParameterField("delay_ms", "Delay Between Images (ms)", get("delay_ms", "150"));
    widget->addParameterField("captureAs", "Capture As (Context Prefix)", get("captureAs", "beam"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    auto get = [&](const std::string& key, const std::string& def = "") -> std::string {
      auto it = params.find(key);
      return (it != params.end()) ? it->second : def;
    };

    // Convert user-friendly pattern name to regex
    std::string pattern = get("z_pattern", "zheight<number>um");
    std::string regex;
    if (pattern == "zheight<number>um") regex = R"(zheight([\d.]+)um)";
    else if (pattern == "z<number>um") regex = R"(z([\d.]+)um)";
    else if (pattern == "z_<number>") regex = R"(z_([\d.]+))";
    else if (pattern == "<number>um") regex = R"(([\d.]+)um)";
    else regex = pattern;  // raw regex fallback

    sequence->AddAction("beamserver", "DetectFolder", {
      {"folder_path", get("folder_path")},
      {"z_regex", regex},
      {"um_per_px_x", get("um_per_px_x", "0.25")},
      {"um_per_px_y", get("um_per_px_y", "0.25")},
      {"delay_ms", get("delay_ms", "150")},
      {"captureAs", get("captureAs", "beam")}
    });
    std::cout << "[CustomServerProcesses] Built Beam Detect Folder for: "
              << step->userNickname << std::endl;
  }
};

//=============================================================================
// BEAM: SAVE
//=============================================================================

class BeamSavePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Beam: Save";
  }

  std::string GetCategory() const override {
    return "Custom Server";
  }

  std::string GetDescription() const override {
    return "Save accumulated images and fit results to folder, then clear accumulated data";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"name", ""},
      {"captureAs", "beam"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    auto get = [&](const std::string& key, const std::string& def = "") -> std::string {
      auto it = params.find(key);
      return (it != params.end()) ? it->second : def;
    };
    widget->addParameterField("name", "Save Name (optional, supports context:)", get("name"));
    widget->addParameterField("captureAs", "Capture As (Context Prefix)", get("captureAs", "beam"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;
    auto get = [&](const std::string& key, const std::string& def = "") -> std::string {
      auto it = params.find(key);
      return (it != params.end()) ? it->second : def;
    };
    sequence->AddAction("beamserver", "Save", {
      {"name", get("name")},
      {"captureAs", get("captureAs", "beam")}
    });
    std::cout << "[CustomServerProcesses] Built Beam Save for: "
              << step->userNickname << std::endl;
  }
};

//=============================================================================
// BEAM: RESET
//=============================================================================

class BeamResetPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Beam: Reset";
  }

  std::string GetCategory() const override {
    return "Custom Server";
  }

  std::string GetDescription() const override {
    return "Reset accumulated beam detection data on the server";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {};
  }

  void CreateConfigUI(ProcessConfigWidget* /*widget*/, const ProcessInstance* /*instance*/) override {
    // No parameters
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    sequence->AddAction("beamserver", "Reset", {});
    std::cout << "[CustomServerProcesses] Built Beam Reset for: "
              << step->userNickname << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterCustomServerProcesses {
    RegisterCustomServerProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<BeamConnectPlugin>());
      registry.RegisterPlugin(std::make_unique<BeamDisconnectPlugin>());
      registry.RegisterPlugin(std::make_unique<BeamDetectPlugin>());
      registry.RegisterPlugin(std::make_unique<BeamDetectFilePlugin>());
      registry.RegisterPlugin(std::make_unique<BeamDetectFolderPlugin>());
      registry.RegisterPlugin(std::make_unique<BeamFitPlugin>());
      registry.RegisterPlugin(std::make_unique<BeamSavePlugin>());
      registry.RegisterPlugin(std::make_unique<BeamResetPlugin>());

      std::cout << "[CustomServerProcesses] Registered 8 Custom Server process plugins" << std::endl;
    }
  };

  static RegisterCustomServerProcesses g_registerCustomServerProcesses;
}

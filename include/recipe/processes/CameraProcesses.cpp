// CameraProcesses.cpp
// Camera/Vision process plugins: CaptureToFile, CaptureToContext, CaptureToDUT, CaptureAndConfirm

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
// CAPTURE TO FILE PLUGIN
//=============================================================================

class CaptureToFilePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Capture to File";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Capture image from camera and save to product folder";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"camera_name", "main_camera"},
      {"product_name", ""},
      {"filename", ""},  // Optional - auto-generated if empty
      {"captureAs", ""}  // Optional - store filepath in context
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Camera selector
    widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                             static_cast<int>(DeviceComboBox::DeviceType::Camera));

    // Product name
    widget->addParameterField("product_name", "Product Name", params.at("product_name"));

    // Optional filename
    widget->addParameterField("filename", "Filename (optional)", params.at("filename"));

    // Optional context key
    widget->addParameterField("captureAs", "Store Path As (optional)", params.at("captureAs"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> captureParams;

    // Required parameter
    auto it = params.find("product_name");
    if (it != params.end()) {
      captureParams["product_name"] = it->second;
    }

    // Optional parameters
    it = params.find("filename");
    if (it != params.end() && !it->second.empty()) {
      captureParams["filename"] = it->second;
    }

    it = params.find("captureAs");
    if (it != params.end() && !it->second.empty()) {
      captureParams["captureAs"] = it->second;
    }

    // Get camera name
    std::string cameraName = "main_camera";  // default
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "CaptureToFile", captureParams);
  }
};

//=============================================================================
// CAPTURE TO CONTEXT PLUGIN
//=============================================================================

class CaptureToContextPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Capture to Context";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Capture image and store in memory for later processing";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"camera_name", "main_camera"},
      {"context_key", "captured_image"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Camera selector
    widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                             static_cast<int>(DeviceComboBox::DeviceType::Camera));

    // Context key
    widget->addParameterField("context_key", "Context Key", params.at("context_key"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> captureParams;

    // Required parameter
    auto it = params.find("context_key");
    if (it != params.end()) {
      captureParams["context_key"] = it->second;
    }

    // Get camera name
    std::string cameraName = "main_camera";  // default
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "CaptureToContext", captureParams);
  }
};

//=============================================================================
// SAVE LAST CAPTURE TO FILE PLUGIN
//=============================================================================

class SaveLastCaptureToFilePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Save Last Capture to File";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Save the most recently captured image to a file";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"product_name", ""},       // Optional - uses default folder if empty
      {"filename", ""},           // Optional - auto-generated if empty
      {"captureAs", ""}           // Optional - store filepath in context
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Product name (optional)
    widget->addParameterField("product_name", "Product Name (optional)", params.at("product_name"));

    // Filename (optional)
    widget->addParameterField("filename", "Filename (optional - auto-generated if empty)", params.at("filename"));

    // Optional context key to store filepath
    widget->addParameterField("captureAs", "Store Path As (optional)", params.at("captureAs"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> saveParams;

    // Optional parameters
    auto it = params.find("product_name");
    if (it != params.end() && !it->second.empty()) {
      saveParams["product_name"] = it->second;
    }

    it = params.find("filename");
    if (it != params.end() && !it->second.empty()) {
      saveParams["filename"] = it->second;
    }

    it = params.find("captureAs");
    if (it != params.end() && !it->second.empty()) {
      saveParams["captureAs"] = it->second;
    }

    // Use special device name "system" for this command
    sequence->AddAction("system", "SaveLastCaptureToFile", saveParams);
  }
};

//=============================================================================
// CROP ROI PLUGIN
//=============================================================================

class CropROIPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Crop ROI";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Crop Region of Interest (ROI) from captured image in context. ROI is centered and sized by width/height percentage.";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"source_context_key", "captured_image"},      // Source image key in context
      {"output_context_key", "cropped_image"},       // Output image key in context
      {"width_percent", "100"},                      // ROI width as % of total image (100 = full width)
      {"height_percent", "100"},                     // ROI height as % of total image (100 = full height)
      {"save_to_file", "false"},                     // Optional: save cropped image to file
      {"filename", ""}                               // Optional: filename for saved crop
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Source context key
    widget->addParameterField("source_context_key", "Source Image (context key)", params.at("source_context_key"));

    // Output context key
    widget->addParameterField("output_context_key", "Output Image (context key)", params.at("output_context_key"));

    // Width percentage
    widget->addParameterField("width_percent", "ROI Width % (0-100)", params.at("width_percent"));

    // Height percentage
    widget->addParameterField("height_percent", "ROI Height % (0-100)", params.at("height_percent"));

    // Save to file option
    widget->addParameterField("save_to_file", "Save to File (true/false)", params.at("save_to_file"));

    // Filename (optional)
    widget->addParameterField("filename", "Filename (optional)", params.at("filename"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> cropParams;

    // Copy all parameters
    for (const auto& param : params) {
      cropParams[param.first] = param.second;
    }

    // Use special device name "system" for this command
    sequence->AddAction("system", "CropROI", cropParams);
  }
};

//=============================================================================
// MANUAL ALIGN POINT PLUGIN
//=============================================================================

class ManualAlignPointPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Manual Align Point";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Manually align and save a point using camera click-to-move. Stores XYZ position in context.";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"point_name", "alignment_point_1"},   // Name of the alignment point
      {"message", "Align the crosshair to the target point"},  // Instruction message
      {"enable_x", "true"},                   // Enable X axis adjustment
      {"enable_y", "true"},                   // Enable Y axis adjustment
      {"enable_z", "true"}                    // Enable Z axis adjustment
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Point name
    widget->addParameterField("point_name", "Point Name", params.at("point_name"));

    // Message
    widget->addParameterField("message", "Instruction Message", params.at("message"));

    // Enable X axis
    widget->addParameterField("enable_x", "Enable X Axis (true/false)", params.at("enable_x"));

    // Enable Y axis
    widget->addParameterField("enable_y", "Enable Y Axis (true/false)", params.at("enable_y"));

    // Enable Z axis
    widget->addParameterField("enable_z", "Enable Z Axis (true/false)", params.at("enable_z"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> alignParams;

    // Copy all parameters
    for (const auto& param : params) {
      alignParams[param.first] = param.second;
    }

    // Use special device name "system" for this command
    sequence->AddAction("system", "ManualAlignPoint", alignParams);
  }
};

//=============================================================================
// CAPTURE TO DUT PLUGIN
//=============================================================================

class CaptureToDUTPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Capture to DUT Folder";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Capture image and save to DUT's image folder (uses current DUT if not specified)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"camera_name", "main_camera"},
      {"product_name", ""},       // Optional - uses current DUT if empty
      {"serial_number", ""},      // Optional - uses current DUT if empty
      {"filename", ""},           // Optional - auto-generated if empty
      {"captureAs", ""}           // Optional - store filepath in context
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Camera selector
    widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                             static_cast<int>(DeviceComboBox::DeviceType::Camera));

    // Product name (optional - uses current DUT if empty)
    widget->addParameterField("product_name", "Product Name (optional - uses current DUT)", params.at("product_name"));

    // Serial number (optional - uses current DUT if empty)
    widget->addParameterField("serial_number", "Serial Number (optional - uses current DUT)", params.at("serial_number"));

    // Optional filename
    widget->addParameterField("filename", "Filename (optional)", params.at("filename"));

    // Optional context key
    widget->addParameterField("captureAs", "Store Path As (optional)", params.at("captureAs"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> captureParams;

    // Optional parameters - only include if they exist and are not empty
    auto it = params.find("product_name");
    if (it != params.end() && !it->second.empty()) {
      captureParams["product_name"] = it->second;
    }

    it = params.find("serial_number");
    if (it != params.end() && !it->second.empty()) {
      captureParams["serial_number"] = it->second;
    }

    it = params.find("filename");
    if (it != params.end() && !it->second.empty()) {
      captureParams["filename"] = it->second;
    }

    it = params.find("captureAs");
    if (it != params.end() && !it->second.empty()) {
      captureParams["captureAs"] = it->second;
    }

    // Get camera name
    std::string cameraName = "main_camera";  // default
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "CaptureToDUT", captureParams);
  }
};

//=============================================================================
// CAPTURE AND CONFIRM PLUGIN
//=============================================================================

class CaptureAndConfirmPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Capture and Confirm";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Capture image, show to user, and wait for confirmation";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"camera_name", "main_camera"},
      {"message", "Review captured image - Continue?"},
      {"save_on_confirm", "false"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Camera selector
    widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                             static_cast<int>(DeviceComboBox::DeviceType::Camera));

    // Message
    widget->addParameterField("message", "Message", params.at("message"));

    // Save on confirm (checkbox would be better, but using text field for now)
    widget->addParameterField("save_on_confirm", "Save on Confirm (true/false)", params.at("save_on_confirm"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> captureParams;

    // Optional parameters
    auto it = params.find("message");
    if (it != params.end()) {
      captureParams["message"] = it->second;
    }

    it = params.find("save_on_confirm");
    if (it != params.end()) {
      captureParams["save_on_confirm"] = it->second;
    }

    // Get camera name
    std::string cameraName = "main_camera";  // default
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "CaptureAndConfirm", captureParams);
  }
};

//=============================================================================
// SET EXPOSURE PLUGIN
//=============================================================================

class SetExposurePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Exposure";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Set camera exposure time (1-10000 microseconds)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"camera_name", "main_camera"},
      {"value", "1000"}  // microseconds
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Camera selector
    widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                             static_cast<int>(DeviceComboBox::DeviceType::Camera));

    // Exposure value
    widget->addParameterField("value", "Exposure (1-10000 μs)", params.at("value"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> exposureParams;

    auto it = params.find("value");
    if (it != params.end()) {
      exposureParams["value"] = it->second;
    }

    // Get camera name
    std::string cameraName = "main_camera";
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "SetExposure", exposureParams);
  }
};

//=============================================================================
// SET GAIN PLUGIN
//=============================================================================

class SetGainPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Gain";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Set camera gain (0.0-10.0)";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"camera_name", "main_camera"},
      {"value", "1.0"}
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Camera selector
    widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                             static_cast<int>(DeviceComboBox::DeviceType::Camera));

    // Gain value
    widget->addParameterField("value", "Gain (0.0-10.0)", params.at("value"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> gainParams;

    auto it = params.find("value");
    if (it != params.end()) {
      gainParams["value"] = it->second;
    }

    // Get camera name
    std::string cameraName = "main_camera";
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "SetGain", gainParams);
  }
};

//=============================================================================
// SET AUTO EXPOSURE PLUGIN
//=============================================================================

class SetAutoExposurePlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Auto Exposure";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Enable or disable auto exposure";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"camera_name", "main_camera"},
      {"value", "on"}  // on/off
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Camera selector
    widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                             static_cast<int>(DeviceComboBox::DeviceType::Camera));

    // Auto exposure value
    widget->addParameterField("value", "Auto Exposure (on/off)", params.at("value"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> autoExpParams;

    auto it = params.find("value");
    if (it != params.end()) {
      autoExpParams["value"] = it->second;
    }

    // Get camera name
    std::string cameraName = "main_camera";
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "SetAutoExposure", autoExpParams);
  }
};

//=============================================================================
// SET AUTO GAIN PLUGIN
//=============================================================================

class SetAutoGainPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Set Auto Gain";
  }

  std::string GetCategory() const override {
    return "Vision";
  }

  std::string GetDescription() const override {
    return "Enable or disable auto gain";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"camera_name", "main_camera"},
      {"value", "on"}  // on/off
    };
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Camera selector
    widget->addDeviceSelector("camera_name", "Camera", params.at("camera_name"),
                             static_cast<int>(DeviceComboBox::DeviceType::Camera));

    // Auto gain value
    widget->addParameterField("value", "Auto Gain (on/off)", params.at("value"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> autoGainParams;

    auto it = params.find("value");
    if (it != params.end()) {
      autoGainParams["value"] = it->second;
    }

    // Get camera name
    std::string cameraName = "main_camera";
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "SetAutoGain", autoGainParams);
  }
};

//=============================================================================
// REGISTRATION
//=============================================================================

// Static registration - automatically registers when program starts
namespace {
  struct CameraProcessesRegistrar {
    CameraProcessesRegistrar() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<CaptureToFilePlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Capture to File (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<CaptureToContextPlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Capture to Context (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<SaveLastCaptureToFilePlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Save Last Capture to File (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<CropROIPlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Crop ROI (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<ManualAlignPointPlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Manual Align Point (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<CaptureToDUTPlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Capture to DUT Folder (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<CaptureAndConfirmPlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Capture and Confirm (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<SetExposurePlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Set Exposure (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<SetGainPlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Set Gain (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<SetAutoExposurePlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Set Auto Exposure (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<SetAutoGainPlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Set Auto Gain (Category: Vision)" << std::endl;

      std::cout << "[CameraProcesses] Registered 11 camera/vision process plugins" << std::endl;
    }
  };

  static CameraProcessesRegistrar s_registrar;
}

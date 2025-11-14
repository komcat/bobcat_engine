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
      {"camera_name", "camera_1"},
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
    std::string cameraName = "camera_1";  // default
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
      {"camera_name", "camera_1"},
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
    std::string cameraName = "camera_1";  // default
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "CaptureToContext", captureParams);
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
      {"camera_name", "camera_1"},
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
    std::string cameraName = "camera_1";  // default
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
      {"camera_name", "camera_1"},
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
    std::string cameraName = "camera_1";  // default
    it = params.find("camera_name");
    if (it != params.end()) {
      cameraName = it->second;
    }

    sequence->AddAction(cameraName, "CaptureAndConfirm", captureParams);
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

      registry.RegisterPlugin(std::make_unique<CaptureToDUTPlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Capture to DUT Folder (Category: Vision)" << std::endl;

      registry.RegisterPlugin(std::make_unique<CaptureAndConfirmPlugin>());
      std::cout << "[ProcessPluginRegistry] Registered plugin: Capture and Confirm (Category: Vision)" << std::endl;

      std::cout << "[CameraProcesses] Registered 4 camera/vision process plugins" << std::endl;
    }
  };

  static CameraProcessesRegistrar s_registrar;
}

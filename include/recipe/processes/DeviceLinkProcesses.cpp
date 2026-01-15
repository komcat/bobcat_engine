// DeviceLinkProcesses.cpp
// Device link process plugins: Register Device Link, Compensate Device Link

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"
#include "../../AppUI/RecipePage/PropertyTableWidget.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

//=============================================================================
// REGISTER DEVICE LINK PLUGIN
//=============================================================================

class RegisterDeviceLinkPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Register Device Link";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Register a spatial link between two devices. Captures current positions and stores the offset for later compensation.";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"primary_device", "gantry-main"},
      {"linked_device", "hex-left"},
      {"save_to_file", "true"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("primary_device", "Primary Device (Reference)", params.at("primary_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDeviceSelector("linked_device", "Linked Device (Follower)", params.at("linked_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("save_to_file", "Save to File (true/false)", params.at("save_to_file"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device Link Configuration");
    table->addMotionDeviceProperty("primary_device", "Primary Device", params.at("primary_device"),
      "Reference device (typically gantry-main). This device's position is the 'measurer'.");
    table->addMotionDeviceProperty("linked_device", "Linked Device", params.at("linked_device"),
      "Follower device (e.g., hex-left). This device will move to maintain the relationship.");

    table->addCategoryHeader("Persistence");
    table->addDropdownProperty("save_to_file", "Save to File", params.at("save_to_file"),
      {"true", "false"},
      "Save the link to config/device_links.json for persistence across sessions.");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string primaryDevice = params.at("primary_device");
    std::string linkedDevice = params.at("linked_device");
    std::string saveToFile = params.at("save_to_file");

    // Use the motion handler's RegisterDeviceLink command
    // The deviceName parameter is used for logging but the actual devices are in the params
    sequence->AddAction(primaryDevice, "RegisterDeviceLink", {
      {"primaryDevice", primaryDevice},
      {"linkedDevice", linkedDevice},
      {"save_to_file", saveToFile}
    });

    std::cout << "[DeviceLinkProcesses] Built Register Device Link sequence: "
              << primaryDevice << " -> " << linkedDevice << std::endl;
  }
};

//=============================================================================
// COMPENSATE DEVICE LINK PLUGIN
//=============================================================================

class CompensateDeviceLinkPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override {
    return "Compensate Device Link";
  }

  std::string GetCategory() const override {
    return "Motion Control";
  }

  std::string GetDescription() const override {
    return "Move the linked device to restore its original spatial relationship with the primary device. Uses the previously registered link offset.";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"primary_device", "gantry-main"},
      {"linked_device", "hex-left"},
      {"axes", "XY"}
    };
  }

  bool PrefersTableUI() const override { return true; }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("primary_device", "Primary Device (Reference)", params.at("primary_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addDeviceSelector("linked_device", "Linked Device (Follower)", params.at("linked_device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("axes", "Axes to Compensate (XY, XYZ, etc.)", params.at("axes"));
  }

  void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    table->addCategoryHeader("Device Link Configuration");
    table->addMotionDeviceProperty("primary_device", "Primary Device", params.at("primary_device"),
      "Reference device (typically gantry-main). The link was registered relative to this device.");
    table->addMotionDeviceProperty("linked_device", "Linked Device", params.at("linked_device"),
      "Follower device that will move to restore the original relationship.");

    table->addCategoryHeader("Compensation Settings");
    table->addDropdownProperty("axes", "Axes to Compensate", params.at("axes"),
      {"XY", "XYZ", "X", "Y", "Z"},
      "Which axes to compensate. XY is typical for horizontal alignment.");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::string primaryDevice = params.at("primary_device");
    std::string linkedDevice = params.at("linked_device");
    std::string axes = params.at("axes");

    // Use the motion handler's CompensateDeviceLink command
    sequence->AddAction(linkedDevice, "CompensateDeviceLink", {
      {"primaryDevice", primaryDevice},
      {"linkedDevice", linkedDevice},
      {"axes", axes}
    });

    std::cout << "[DeviceLinkProcesses] Built Compensate Device Link sequence: "
              << linkedDevice << " compensating for " << primaryDevice
              << " (axes: " << axes << ")" << std::endl;
  }
};

//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterDeviceLinkProcesses {
    RegisterDeviceLinkProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<RegisterDeviceLinkPlugin>());
      registry.RegisterPlugin(std::make_unique<CompensateDeviceLinkPlugin>());

      std::cout << "[DeviceLinkProcesses] Registered 2 device link process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterDeviceLinkProcesses g_registerDeviceLinkProcesses;
}

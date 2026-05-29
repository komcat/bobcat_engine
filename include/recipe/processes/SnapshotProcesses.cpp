// SnapshotProcesses.cpp
// "Capture Snapshot" recipe process — invokes StateSnapshotManager via the
// system/CaptureSnapshot command. Use this anywhere in a recipe to save a
// labeled JSON dump of every reachable manager's state (motion positions,
// TEC/PSU/SMU/lighting readings, IO bits, pneumatic positions, DataStore,
// active DUT) plus optional camera frames. See StateSnapshotManager.h for
// the output layout.

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"

#include <iostream>
#include <memory>
#include <map>
#include <string>

class CaptureSnapshotPlugin : public IProcessPlugin
{
public:
  std::string GetName() const override        { return "Capture Snapshot"; }
  std::string GetCategory() const override    { return "System"; }
  std::string GetDescription() const override {
    return "Capture a labeled JSON snapshot of all machine state "
           "(motion, TEC/PSU/SMU, IO, pneumatic, DataStore, DUT) "
           "+ optional camera frames. For later debugging.";
  }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"label",         ""},
      {"tags",          ""},
      {"note",          ""},
      {"capture_image", "true"}
    };
  }

  std::string getParam(const std::map<std::string, std::string>& params,
                       const std::string& key, const std::string& defaultVal) const {
    auto it = params.find(key);
    return (it != params.end()) ? it->second : defaultVal;
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addParameterField("label", "Label",
                              getParam(params, "label", ""));
    widget->addParameterField("tags", "Tags (comma-separated)",
                              getParam(params, "tags", ""));
    widget->addParameterField("note", "Note (optional)",
                              getParam(params, "note", ""));
    widget->addCheckboxParameter("capture_image", "Capture camera frames",
                                 getParam(params, "capture_image", "true"),
                                 "Save a PNG from every grabbing camera "
                                 "alongside the JSON snapshot.");
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    std::map<std::string, std::string> cmdParams = {
      {"label",         getParam(params, "label", "")},
      {"tags",          getParam(params, "tags", "")},
      {"note",          getParam(params, "note", "")},
      {"source",        "recipe"},
      {"capture_image", getParam(params, "capture_image", "true")}
    };

    sequence->AddAction("system", "CaptureSnapshot", cmdParams);

    std::cout << "[CaptureSnapshotPlugin] Built sequence for: "
              << step->userNickname
              << " (label='" << cmdParams["label"]
              << "', tags='" << cmdParams["tags"] << "')" << std::endl;
  }
};

namespace {
struct CaptureSnapshotRegistrar {
  CaptureSnapshotRegistrar() {
    auto& registry = ProcessPluginRegistry::Instance();
    registry.RegisterPlugin(std::make_unique<CaptureSnapshotPlugin>());
    std::cout << "[SnapshotProcesses] Registered Capture Snapshot plugin" << std::endl;
  }
};
static CaptureSnapshotRegistrar s_registrar;
}

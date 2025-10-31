# Creating Recipe Process Plugins - Complete Guide

## Overview

The Bobcat Engine uses a **plugin-based architecture** for recipe process types. This guide shows you how to create new process types that appear in the RecipePage action list and can be used in recipes.

**Benefits of Plugin System:**
- ✅ No need to modify multiple files (RecipeManager, ProcessSequenceFactory, ProcessConfigWidget, RecipePage)
- ✅ Auto-registration at program startup
- ✅ Clean separation of concerns
- ✅ Easy to add/remove process types
- ✅ Consistent API across all plugins

---

## Quick Start: 5 Steps to Create a Plugin

1. **Create plugin class** implementing `IProcessPlugin` interface
2. **Define metadata** (name, category, description)
3. **Specify parameters** with defaults
4. **Create UI** for parameter configuration
5. **Register plugin** in auto-registration section

**That's it!** No need to touch RecipeManager, ProcessSequenceFactory, or RecipePage.

---

## Complete Example: "Move to Graph Node" Plugin

This example implements graph-based path planning as a recipe action.

### Step 1: Include Required Headers

```cpp
// MotionProcesses.cpp (or create new file like GraphProcesses.cpp)

#include "../IProcessPlugin.h"
#include "../ProcessPluginRegistry.h"
#include "../RecipeManager.h"  // For ProcessInstance
#include "../../sequence/SequenceBuilder.h"
#include "../../AppUI/RecipePage/ProcessConfigWidget.h"
#include "../../AppUI/RecipePage/DeviceSelectorWidgets.h"
```

### Step 2: Create Plugin Class

```cpp
//=============================================================================
// MOVE TO GRAPH NODE PLUGIN
//=============================================================================

class MoveToGraphNodePlugin : public IProcessPlugin
{
public:
  // 1️⃣ METADATA: Process name (shows in action list)
  std::string GetName() const override {
    return "Move to Graph Node";
  }

  // 2️⃣ CATEGORY: Groups processes in UI
  std::string GetCategory() const override {
    return "Motion Control";
  }

  // 3️⃣ DESCRIPTION: Tooltip/help text
  std::string GetDescription() const override {
    return "Execute graph-based path planning to move to a target node";
  }

  // 4️⃣ DEFAULT PARAMETERS: Initial values when action added
  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"device", "hex-left"},           // Motion device name
      {"graphName", "default"},         // Graph to use for planning
      {"endNodeName", "home"}           // Target position name
    };
  }

  // 5️⃣ CREATE UI: Build parameter configuration form
  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Device selector dropdown (filtered by type)
    widget->addDeviceSelector("device", "Device", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Text fields for graph and node names
    widget->addParameterField("graphName", "Graph Name", params.at("graphName"));
    widget->addParameterField("endNodeName", "End Node Position", params.at("endNodeName"));
  }

  // 6️⃣ BUILD SEQUENCE: Convert parameters to sequence commands
  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    // Call the MoveToGraphNode command (implemented in MotionCommandHandler)
    sequence->AddAction(params.at("device"), "MoveToGraphNode", {
      {"graphName", params.at("graphName")},
      {"endNodeName", params.at("endNodeName")}
    });

    // Optional: Log for debugging
    std::cout << "[MotionProcesses] Built Move to Graph Node sequence: "
              << params.at("device") << " -> graph=" << params.at("graphName")
              << ", endNode=" << params.at("endNodeName") << std::endl;
  }
};
```

### Step 3: Register Plugin (Auto-Registration Pattern)

Add your plugin to the registration section at the bottom of the file:

```cpp
//=============================================================================
// AUTO-REGISTRATION
//=============================================================================

namespace {
  struct RegisterMotionProcesses {
    RegisterMotionProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      registry.RegisterPlugin(std::make_unique<PickPlugin>());
      registry.RegisterPlugin(std::make_unique<PlacePlugin>());
      // ... existing plugins ...
      registry.RegisterPlugin(std::make_unique<MoveToGraphNodePlugin>());  // ✅ ADD THIS

      std::cout << "[MotionProcesses] Registered 10 motion process plugins" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterMotionProcesses g_registerMotionProcesses;
}
```

**Done!** Your plugin is now available in the RecipePage action list.

---

## IProcessPlugin Interface

All plugins must implement this interface:

```cpp
class IProcessPlugin
{
public:
  virtual ~IProcessPlugin() = default;

  // METADATA
  virtual std::string GetName() const = 0;         // Process name in UI
  virtual std::string GetCategory() const = 0;     // Grouping category
  virtual std::string GetDescription() const = 0;  // Tooltip text

  // PARAMETERS
  virtual std::map<std::string, std::string> GetDefaultParameters() const = 0;

  // UI GENERATION
  virtual void CreateConfigUI(ProcessConfigWidget* widget,
                               const ProcessInstance* instance) = 0;

  // SEQUENCE BUILDING
  virtual void BuildSequence(SequenceBuilder* sequence,
                             const ProcessInstance* step) = 0;
};
```

---

## Available Categories

Organize your plugin into one of these categories:

- `"Motion Control"` - Movement and positioning
- `"I/O & Pneumatics"` - Digital I/O and pneumatic slides
- `"TEC & Laser"` - Temperature control and laser operations
- `"Power Supply"` - Power supply control
- `"Data"` - DataStore operations
- `"System"` - Wait, prompts, logging, manual adjust
- `"Control Flow"` - Conditionals, loops, groups
- `"Calibration"` - Calibration procedures

---

## UI Widgets Available

The `ProcessConfigWidget` provides these helpers:

### Device Selector

```cpp
widget->addDeviceSelector("device", "Device", currentValue,
  static_cast<int>(DeviceComboBox::DeviceType::Motion));
```

**Device Types:**
- `DeviceComboBox::DeviceType::Motion` - Motion devices
- `DeviceComboBox::DeviceType::IO` - I/O devices
- `DeviceComboBox::DeviceType::Pneumatic` - Pneumatic slides
- `DeviceComboBox::DeviceType::TEC` - TEC devices
- `DeviceComboBox::DeviceType::PSU` - Power supplies

### Text Field

```cpp
widget->addParameterField("paramName", "Label", currentValue);
```

### Node Selector (Position Dropdown)

```cpp
widget->addNodeSelector("nodeName", "Label", currentValue, "deviceParamName");
```

Automatically populates with positions for the selected device.

### Pneumatic Slide Selector

```cpp
widget->addPneumaticSlideSelector("slideName", "Label", currentValue);
```

### Custom Widgets

You can also add custom Qt widgets:

```cpp
void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) {
  QComboBox* combo = new QComboBox();
  combo->addItems({"Option1", "Option2"});

  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          widget, &ProcessConfigWidget::parameterChanged);

  widget->m_formLayout->addRow("My Param:", combo);
  widget->m_parameterEdits["my_param"] = combo;
}
```

---

## Command Handler Integration

Your plugin calls commands that are implemented in command handlers. For the graph example:

### Command Implementation (MotionCommandHandler.cpp)

```cpp
bool MotionCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  // ... existing commands ...

  else if (methodName == "MoveToGraphNode") {
    // 1. Validate parameters
    auto graphNameIt = params.find("graphName");
    if (graphNameIt == params.end()) {
      errorMessage = "MoveToGraphNode: Missing required parameter 'graphName'";
      return false;
    }

    // 2. Execute the operation
    std::string graphName = graphNameIt->second;
    std::string endNodeName = params.at("endNodeName");

    // ... implementation details ...

    return true;
  }
}
```

### Calling the Command from Plugin

```cpp
void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
  const auto& params = step->parameters;

  // Single command call
  sequence->AddAction(params.at("device"), "MoveToGraphNode", {
    {"graphName", params.at("graphName")},
    {"endNodeName", params.at("endNodeName")}
  });
}
```

---

## Plugin File Organization

Plugins are organized by category in separate files:

```
include/recipe/processes/
├── MotionProcesses.cpp      # Motion control plugins
├── IOProcesses.cpp           # I/O and pneumatic plugins
├── SystemProcesses.cpp       # System operation plugins
├── TECProcesses.cpp          # TEC and laser plugins
├── PSUProcesses.cpp          # Power supply plugins
├── DataStoreProcesses.cpp    # Data storage plugins
├── ConditionalProcesses.cpp  # Control flow plugins
└── CalibrationProcesses.cpp  # Calibration plugins
```

**Guidelines:**
- Add to existing file if your plugin fits the category
- Create new file (e.g., `GraphProcesses.cpp`) for new categories
- Remember to add new files to `CMakeLists.txt`

---

## Multi-Step Sequence Example

Some plugins need multiple commands:

```cpp
void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
  const auto& params = step->parameters;
  std::string device = params.at("device");

  // Step 1: Move to position
  sequence->AddAction(device, "MoveToNamedPosition", {
    {"positionName", params.at("position")}
  });

  // Step 2: Wait for motion to complete
  sequence->AddAction(device, "WaitForMotionComplete", {});

  // Step 3: Do something else
  sequence->AddAction("system", "Wait", {
    {"milliseconds", "500"}
  });
}
```

---

## Parameter Validation

Validate parameters in the command handler:

```cpp
else if (methodName == "MyCommand") {
  // Required parameter
  auto it = params.find("requiredParam");
  if (it == params.end()) {
    errorMessage = "MyCommand: Missing required parameter 'requiredParam'";
    return false;
  }

  // Optional parameter with default
  int timeout = 30000;
  auto timeoutIt = params.find("timeout");
  if (timeoutIt != params.end()) {
    timeout = std::stoi(timeoutIt->second);
  }

  // Validate range
  if (timeout < 0 || timeout > 60000) {
    errorMessage = "MyCommand: timeout must be 0-60000ms";
    return false;
  }

  // ... execute command ...
}
```

---

## Context Integration

Use `SequenceContext` for dynamic values:

```cpp
// In plugin BuildSequence:
sequence->AddAction(device, "MoveToAbs", {
  {"x", "context:calculated_x"},  // Reference context value
  {"y", "10.5"},                  // Or direct value
  {"z", params.at("z_position")}  // Or parameter value
});
```

```cpp
// In command handler:
double x = ResolveNumericValue(params.at("x"), context);
// If "context:calculated_x", retrieves from context
// If "10.5", parses as direct value
```

---

## Testing Checklist

Before considering your plugin complete:

- [ ] Plugin compiles without errors
- [ ] Appears in RecipePage action list under correct category
- [ ] Configuration UI shows all parameters
- [ ] Default values populate correctly
- [ ] Device selectors filter correctly
- [ ] Sequence builds without errors
- [ ] Command executes successfully
- [ ] Error messages are clear and helpful
- [ ] Logging provides useful debug output
- [ ] Recipe saves/loads with your process type
- [ ] Recipe executes your process correctly
- [ ] Stop button interrupts your process
- [ ] Multiple instances in recipe work correctly

---

## Common Patterns

### Pattern 1: Simple Single-Command Plugin

```cpp
class MyPlugin : public IProcessPlugin {
public:
  std::string GetName() const override { return "My Action"; }
  std::string GetCategory() const override { return "System"; }
  std::string GetDescription() const override { return "Does something"; }

  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {{"param1", "default"}};
  }

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;
    widget->addParameterField("param1", "Parameter 1", params.at("param1"));
  }

  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    sequence->AddAction("system", "MyCommand", step->parameters);
  }
};
```

### Pattern 2: Multi-Device Coordination

```cpp
void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
  const auto& params = step->parameters;

  // Device 1: Move
  sequence->AddAction(params.at("device1"), "MoveToNamedPosition", {
    {"positionName", params.at("pos1")}
  });
  sequence->AddAction(params.at("device1"), "WaitForMotionComplete", {});

  // Device 2: Move (simultaneous)
  sequence->AddAction(params.at("device2"), "MoveToNamedPosition", {
    {"positionName", params.at("pos2")}
  });
  sequence->AddAction(params.at("device2"), "WaitForMotionComplete", {});

  // Both devices now at target - do next operation
  sequence->AddAction(params.at("io_device"), "SetOutputPin", {
    {"pin", "0"},
    {"state", "true"}
  });
}
```

### Pattern 3: User Interaction

```cpp
void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
  const auto& params = step->parameters;

  // Move to position
  sequence->AddAction(params.at("device"), "MoveToNamedPosition", {
    {"positionName", params.at("position")}
  });
  sequence->AddAction(params.at("device"), "WaitForMotionComplete", {});

  // Prompt user
  sequence->AddAction("system", "UserPrompt", {
    {"title", "Confirm Position"},
    {"message", "Check the position and click OK to continue"}
  });

  // Continue after user confirmation
  // ... next steps ...
}
```

### Pattern 4: Conditional Logic (Advanced)

```cpp
void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
  const auto& params = step->parameters;

  // Measure something
  sequence->AddAction("tec1", "ReadTemperature", {
    {"captureAs", "temp"}  // Store in context
  });

  // Conditional: if temp > threshold
  sequence->BeginIf("temp", ConditionalBlock::ComparisonType::GREATER_THAN, 25.0f);
    // Temperature too high - cool down
    sequence->AddAction("tec1", "SetTemperature", {
      {"temperature", "20.0"}
    });
    sequence->AddAction("system", "Wait", {{"milliseconds", "5000"}});
  sequence->Else();
    // Temperature OK - proceed
    sequence->AddAction("system", "LogMessage", {
      {"message", "Temperature within range"}
    });
  sequence->EndIf();
}
```

---

## File Locations

| Purpose | Location |
|---------|----------|
| Plugin interface | `include/recipe/IProcessPlugin.h` |
| Plugin registry | `include/recipe/ProcessPluginRegistry.h` |
| Motion plugins | `include/recipe/processes/MotionProcesses.cpp` |
| I/O plugins | `include/recipe/processes/IOProcesses.cpp` |
| System plugins | `include/recipe/processes/SystemProcesses.cpp` |
| TEC plugins | `include/recipe/processes/TECProcesses.cpp` |
| PSU plugins | `include/recipe/processes/PSUProcesses.cpp` |
| DataStore plugins | `include/recipe/processes/DataStoreProcesses.cpp` |
| Conditional plugins | `include/recipe/processes/ConditionalProcesses.cpp` |
| Calibration plugins | `include/recipe/processes/CalibrationProcesses.cpp` |
| Config widget base | `include/AppUI/RecipePage/ProcessConfigWidget.h` |
| Device selectors | `include/AppUI/RecipePage/DeviceSelectorWidgets.h` |
| Sequence builder | `include/sequence/SequenceBuilder.h` |
| Command handlers | `include/sequence/*CommandHandler.cpp` |

---

## Adding New Command Handler

If your plugin needs a completely new device type:

### 1. Create Command Handler

```cpp
// include/sequence/MyDeviceCommandHandler.h
#pragma once
#include "ICommandHandler.h"

class MyDeviceCommandHandler : public ICommandHandler
{
public:
  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context
  ) override;

  std::string GetManagerType() const override { return "mydevice"; }
};
```

### 2. Register in SequenceEngine

```cpp
// include/sequence/SequenceEngine.cpp

// In constructor:
m_dispatcher.RegisterHandler("mydevice",
  std::make_unique<MyDeviceCommandHandler>());

// In BuildDeviceRegistry():
if (m_context->myDeviceManager) {
  auto devices = m_context->myDeviceManager->GetAllDeviceNames();
  for (const auto& name : devices) {
    m_deviceToManagerMap[name] = m_context->myDeviceManager;
    m_deviceToTypeMap[name] = "mydevice";
  }
}
```

### 3. Add to CMakeLists.txt

```cmake
# In CMakeLists.txt, add:
include/sequence/MyDeviceCommandHandler.cpp
```

---

## Troubleshooting

### Plugin doesn't appear in action list

- **Check:** Is plugin registered in auto-registration section?
- **Check:** Does `GetName()` return unique name?
- **Check:** Is file included in CMakeLists.txt?
- **Check:** Did you rebuild after adding plugin?
- **Check:** Console output for registration message

### Parameters not saving/loading

- **Check:** Parameter names match in all methods
- **Check:** `GetDefaultParameters()` includes all parameters
- **Check:** `CreateConfigUI()` connects to `parameterChanged` signal
- **Check:** Parameter values stored in `m_parameterEdits` map

### Command not executing

- **Check:** Command name in `BuildSequence()` matches handler
- **Check:** Parameter names match what command expects
- **Check:** Device type matches command handler
- **Check:** Command handler is registered in SequenceEngine
- **Check:** Console output for error messages

### UI widget not updating

- **Check:** Connected to `parameterChanged` signal
- **Check:** Widget added to `m_parameterEdits` map with correct key
- **Check:** Widget created with correct parent
- **Check:** Dark theme applied if needed

---

## Best Practices

1. **Naming Conventions**
   - Plugin class: `MyActionPlugin`
   - Process name: "My Action" (user-facing)
   - Command name: "MyCommand" (PascalCase)
   - Parameters: "myParameter" (camelCase)

2. **Error Handling**
   - Always validate parameters
   - Provide clear error messages
   - Return false on error with descriptive `errorMessage`

3. **Logging**
   - Log sequence building for debugging
   - Log command execution in handler
   - Use consistent format: `"[Category] Message"`

4. **Documentation**
   - Add inline comments explaining complex logic
   - Use descriptive parameter names
   - Provide helpful description text

5. **Testing**
   - Test with invalid parameters
   - Test with missing parameters
   - Test stop button functionality
   - Test in recipes with other actions

---

## Real-World Example: Complete Plugin

See `MoveToGraphNodePlugin` in `include/recipe/processes/MotionProcesses.cpp` (lines 633-682) and the corresponding command handler in `include/sequence/MotionCommandHandler.cpp` (lines 656-815) for a complete, production-ready example.

**Features demonstrated:**
- Graph-based path planning
- Auto-detection of current position
- Multi-waypoint execution
- Position verification
- Error handling
- Logging

---

## Related Documentation

- [Adding New Commands Guide](ADDING_NEW_COMMANDS_GUIDE.md)
- [Graph Path Execution](graph_path_execution.md)
- [Sequence System Overview](sequence_context_usage_guide.md)
- [Recipe System](new process addition.md)

---

## Version History

- **v1.0** (2025-01-30) - Initial plugin system documentation
  - Complete guide with Move to Graph Node example
  - All patterns and best practices
  - Troubleshooting section

---

## Summary

Creating a new recipe process plugin involves:

1. ✅ Create plugin class implementing `IProcessPlugin`
2. ✅ Define metadata, parameters, and UI
3. ✅ Implement `BuildSequence()` to convert parameters to commands
4. ✅ Register plugin in auto-registration section
5. ✅ (Optional) Implement command handler if new device type

The plugin system handles everything else automatically - no need to modify RecipeManager, ProcessSequenceFactory, ProcessConfigWidget, or RecipePage!

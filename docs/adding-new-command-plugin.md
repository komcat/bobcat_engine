# Adding New Command Plugin to Sequence Engine

This guide explains the command sequence action plugin architecture and provides step-by-step instructions for adding new command plugins to the system.

## Table of Contents
1. [Architecture Overview](#architecture-overview)
2. [Core Components](#core-components)
3. [Adding a New Command Handler](#adding-a-new-command-handler)
4. [Adding a New Process Type](#adding-a-new-process-type)
5. [Examples](#examples)
6. [Best Practices](#best-practices)

---

## Architecture Overview

The command sequence system uses a **dispatcher pattern** with pluggable command handlers. This architecture allows for clean separation of concerns and easy extensibility.

```
┌─────────────────────────────────────────────────────────────┐
│                      SequenceEngine                         │
│  - Manages execution flow (loops, conditionals)             │
│  - Validates sequences                                      │
│  - Builds device registry                                   │
│  - Delegates command execution to CommandDispatcher         │
└──────────────────┬──────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────┐
│                   CommandDispatcher                         │
│  - Routes commands to appropriate handlers                  │
│  - Maintains handler registry (by manager type)             │
└──────────────────┬──────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────────────────────┐
│                  ICommandHandler (Interface)                │
│  - ExecuteCommand(manager, device, method, params, ctx)     │
│  - GetManagerType()                                         │
└─────────────┬───────────────────────────────────────────────┘
              │
              ├── MotionCommandHandler      (manager type: "motion")
              ├── IOCommandHandler           (manager type: "io")
              ├── PneumaticCommandHandler    (manager type: "pneumatic")
              ├── CameraCommandHandler       (manager type: "camera")
              ├── TECCommandHandler          (manager type: "tec")
              ├── PSUCommandHandler          (manager type: "psu")
              ├── DataStoreCommandHandler    (manager type: "datastore")
              ├── SystemCommandHandler       (manager type: "system")
              └── SequenceContextCommandHandler (manager type: "context")
```

### Key Design Principles

1. **Separation of Concerns**: Each hardware subsystem has its own command handler
2. **Extensibility**: New handlers can be added without modifying existing code
3. **Type Safety**: Commands are validated before execution
4. **Context Awareness**: Commands can read/write to SequenceContext for data sharing
5. **Manager Agnostic**: SequenceEngine doesn't need to know about specific hardware APIs

---

## Core Components

### 1. ICommandHandler Interface

All command handlers must implement this interface:

**Location**: `include/sequence/ICommandHandler.h`

```cpp
class ICommandHandler
{
public:
  virtual ~ICommandHandler() = default;

  // Execute a command for a specific device
  virtual bool ExecuteCommand(
    void* manager,                              // Manager pointer (cast to specific type)
    const std::string& deviceName,              // Device name (e.g., "hex-left", "IOBottom")
    const std::string& methodName,              // Method name (e.g., "MoveAbsolute", "SetOutput")
    const std::map<std::string, std::string>& params,  // Parameters
    std::string& errorMessage,                  // Output: error message if failed
    SequenceContext* context                    // Execution context (can be nullptr)
  ) = 0;

  // Get the manager type this handler handles (e.g., "motion", "io")
  virtual std::string GetManagerType() const = 0;

  // Set logger for this handler
  virtual void SetLogger(LogManager* logger) { m_logger = logger; }

protected:
  LogManager* m_logger = nullptr;
};
```

### 2. CommandDispatcher

Routes commands to appropriate handlers based on manager type.

**Location**: `include/sequence/CommandDispatcher.h`

```cpp
class CommandDispatcher
{
public:
  // Register a command handler for a manager type
  void RegisterHandler(const std::string& managerType,
                       std::unique_ptr<ICommandHandler> handler);

  // Execute a command with execution context
  bool ExecuteCommand(
    const std::string& managerType,
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context = nullptr
  );
};
```

### 3. SequenceEngine

Manages sequence execution and handler registration.

**Location**: `include/sequence/SequenceEngine.h`

Handlers are registered in the constructor:

```cpp
SequenceEngine::SequenceEngine(ApplicationContext* context)
{
  // Register all command handlers
  m_dispatcher.RegisterHandler("motion", std::make_unique<MotionCommandHandler>());
  m_dispatcher.RegisterHandler("io", std::make_unique<IOCommandHandler>());
  m_dispatcher.RegisterHandler("pneumatic", std::make_unique<PneumaticCommandHandler>());
  m_dispatcher.RegisterHandler("camera", std::make_unique<CameraCommandHandler>());
  m_dispatcher.RegisterHandler("tec", std::make_unique<TECCommandHandler>());
  m_dispatcher.RegisterHandler("psu", std::make_unique<PSUCommandHandler>());
  m_dispatcher.RegisterHandler("datastore", std::make_unique<DataStoreCommandHandler>());
  m_dispatcher.RegisterHandler("context", std::make_unique<SequenceContextCommandHandler>());
  m_dispatcher.RegisterHandler("system", std::make_unique<SystemCommandHandler>());
}
```

### 4. ProcessSequenceFactory

Builds sequences from recipe process instances.

**Location**: `include/recipe/ProcessSequenceFactory.h`

Process builders are registered in the constructor:

```cpp
ProcessSequenceFactory::ProcessSequenceFactory()
{
  RegisterProcessBuilder("Wait", [](SequenceBuilder* sequence, const ProcessInstance* step) {
    const auto& params = step->parameters;

    std::map<std::string, std::string> waitParams;
    waitParams["milliseconds"] = params.at("duration_ms");

    sequence->AddAction("system", "Wait", waitParams);
  });

  // ... more process builders
}
```

---

## Adding a New Command Handler

Follow these steps to add a new command handler for a new hardware subsystem.

### Step 1: Create Handler Header File

**Location**: `include/sequence/YourNewCommandHandler.h`

```cpp
// YourNewCommandHandler.h
#pragma once

#include "ICommandHandler.h"

class YourNewCommandHandler : public ICommandHandler
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

  std::string GetManagerType() const override { return "your_manager_type"; }
};
```

### Step 2: Implement Handler Logic

**Location**: `include/sequence/YourNewCommandHandler.cpp`

```cpp
// YourNewCommandHandler.cpp
#include "YourNewCommandHandler.h"
#include "YourHardwareManager.h"  // Include your manager
#include "LogManager.h"
#include "SequenceContext.h"

bool YourNewCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  // Cast manager to your specific manager type
  YourHardwareManager* hwManager = static_cast<YourHardwareManager*>(manager);

  // ============================================================================
  // METHOD 1: Example command implementation
  // ============================================================================
  if (methodName == "YourMethod1") {
    // Extract parameters
    auto param1It = params.find("param1");
    auto param2It = params.find("param2");

    // Validate parameters
    if (param1It == params.end() || param2It == params.end()) {
      errorMessage = "Missing required parameters: param1, param2";
      return false;
    }

    // Convert parameter types as needed
    std::string param1 = param1It->second;
    int param2 = std::stoi(param2It->second);

    // Log the action
    if (m_logger) {
      m_logger->Info(
        "Executing " + deviceName + "." + methodName +
        " with param1=" + param1 + ", param2=" + std::to_string(param2),
        "YourHandler"
      );
    }

    // Execute the hardware operation
    if (!hwManager->DoSomething(deviceName, param1, param2)) {
      errorMessage = "YourMethod1 failed";
      return false;
    }

    // Optional: Store result in context for later use
    if (context) {
      context->SetValue("last_operation_result", "success", deviceName, methodName);
    }

    return true;
  }

  // ============================================================================
  // METHOD 2: Another command
  // ============================================================================
  else if (methodName == "YourMethod2") {
    // Implementation...
    return true;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown method: " + methodName;
    return false;
  }
}
```

### Step 3: Register Handler in SequenceEngine

**Location**: `include/sequence/SequenceEngine.cpp`

1. Add include at top of file:
```cpp
#include "YourNewCommandHandler.h"
```

2. Register in constructor (around line 40):
```cpp
SequenceEngine::SequenceEngine(ApplicationContext* context)
{
  // ... existing registrations ...

  // Add your new handler
  m_dispatcher.RegisterHandler("your_manager_type",
                               std::make_unique<YourNewCommandHandler>());
}
```

### Step 4: Register Devices in BuildDeviceRegistry

**Location**: `include/sequence/SequenceEngine.cpp` (around line 140)

```cpp
void SequenceEngine::BuildDeviceRegistry()
{
  // ... existing device registrations ...

  // Register your devices
  if (m_context->yourManager) {
    auto devices = m_context->yourManager->GetAllDeviceNames();
    for (const auto& name : devices) {
      m_deviceToManagerMap[name] = m_context->yourManager;
      m_deviceToTypeMap[name] = "your_manager_type";
    }
    LogInfo("Registered " + std::to_string(devices.size()) + " your devices");
  }
}
```

### Step 5: Add Manager to ApplicationContext (if needed)

**Location**: `include/ApplicationContext.h`

```cpp
struct ApplicationContext
{
  // ... existing managers ...
  YourHardwareManager* yourManager = nullptr;
};
```

Initialize in Application.cpp when creating the context.

---

## Adding a New Process Type

If you want to add a new **Recipe Process Type** that uses existing or new command handlers:

### Step 1: Register Process in RecipeManager

**Location**: `include/recipe/RecipeManager.h` and `.cpp`

Add to `getAvailableProcessTypes()`:
```cpp
std::vector<std::string> RecipeManager::getAvailableProcessTypes() const
{
  return {
    "Pick", "Place", "Pick and Place",
    "Wait", "Ask User", "Manual Adjust", "Log Message",
    "Move to Position", "Move Relative", "Set Output",
    "Your New Process Type"  // Add here
  };
}
```

Add default parameters in `getDefaultParameters()`:
```cpp
std::map<std::string, std::string> RecipeManager::getDefaultParameters(
  const std::string& processType) const
{
  // ... existing process types ...

  if (processType == "Your New Process Type") {
    return {
      {"device", ""},
      {"param1", "default_value"},
      {"param2", "0"}
    };
  }

  return {};
}
```

### Step 2: Register Builder in ProcessSequenceFactory

**Location**: `include/recipe/ProcessSequenceFactory.cpp`

Add in constructor:
```cpp
ProcessSequenceFactory::ProcessSequenceFactory()
{
  // ... existing registrations ...

  // Register your new process builder
  RegisterProcessBuilder("Your New Process Type",
    [](SequenceBuilder* sequence, const ProcessInstance* step) {
      const auto& params = step->parameters;

      // Extract parameters from recipe step
      std::string device = params.at("device");
      std::string param1 = params.at("param1");
      std::string param2 = params.at("param2");

      // Build sequence actions using command handlers
      sequence->AddAction(device, "YourMethod1", {
        {"param1", param1},
        {"param2", param2}
      });

      // Optional: Add more actions
      sequence->AddAction(device, "YourMethod2", {});

      std::cout << "[ProcessSequenceFactory] Built Your New Process sequence for: "
        << step->userNickname << std::endl;
    });
}
```

### Step 3: Create UI Widget for Configuration (Optional)

**Location**: `include/AppUI/RecipePage/ProcessConfigWidget.h` and `.cpp`

1. Add widget creation method in header:
```cpp
// In ProcessConfigWidget.h
QWidget* createYourNewProcessWidget(const std::map<std::string, std::string>& params);
```

2. Implement widget in .cpp:
```cpp
QWidget* ProcessConfigWidget::createYourNewProcessWidget(
  const std::map<std::string, std::string>& params)
{
  QWidget* widget = new QWidget(this);
  QFormLayout* layout = new QFormLayout(widget);

  // Device selector
  QComboBox* deviceCombo = new QComboBox();
  // Populate with devices from ApplicationContext
  deviceCombo->setObjectName("device");
  layout->addRow("Device:", deviceCombo);

  // Parameter 1
  QLineEdit* param1Edit = new QLineEdit(QString::fromStdString(params.at("param1")));
  param1Edit->setObjectName("param1");
  layout->addRow("Parameter 1:", param1Edit);

  // Parameter 2
  QSpinBox* param2Spin = new QSpinBox();
  param2Spin->setObjectName("param2");
  param2Spin->setValue(std::stoi(params.at("param2")));
  layout->addRow("Parameter 2:", param2Spin);

  return widget;
}
```

3. Register widget creation in RecipePage:
```cpp
// In RecipePage::updateConfigurationForm()
else if (processType == "Your New Process Type") {
  configWidget = configWidgetHelper.createYourNewProcessWidget(currentParams);
}
```

---

## Examples

### Example 1: Simple System Command (Wait)

```cpp
// In ProcessSequenceFactory.cpp
RegisterProcessBuilder("Wait", [](SequenceBuilder* sequence, const ProcessInstance* step) {
  const auto& params = step->parameters;

  std::map<std::string, std::string> waitParams;
  waitParams["milliseconds"] = params.at("duration_ms");

  sequence->AddAction("system", "Wait", waitParams);
});
```

### Example 2: Motion Command

```cpp
// In MotionCommandHandler.cpp
if (methodName == "MoveToNamedPosition") {
  auto posNameIt = params.find("positionName");

  if (posNameIt == params.end()) {
    errorMessage = "Missing required parameter: positionName";
    return false;
  }

  std::string positionName = posNameIt->second;

  if (m_logger) {
    m_logger->Info("Moving " + deviceName + " to: " + positionName, "MotionHandler");
  }

  if (!motionManager->MoveToNamedPosition(deviceName, positionName)) {
    errorMessage = "MoveToNamedPosition failed";
    return false;
  }

  return true;
}
```

### Example 3: IO Command with Context

```cpp
// In IOCommandHandler.cpp
if (methodName == "GetInput") {
  auto pinIt = params.find("pinName");

  if (pinIt == params.end()) {
    errorMessage = "Missing required parameter: pinName";
    return false;
  }

  std::string pinName = pinIt->second;
  bool state = false;

  if (!ioManager->GetInputByName(deviceName, pinName, state)) {
    errorMessage = "GetInput failed for pin: " + pinName;
    return false;
  }

  // Store result in context
  if (context) {
    context->SetValue(deviceName + "." + pinName, state ? 1.0f : 0.0f,
                     deviceName, methodName);
  }

  return true;
}
```

### Example 4: Complex Process Builder

```cpp
// In ProcessSequenceFactory.cpp
RegisterProcessBuilder("Pick and Place", [](SequenceBuilder* sequence, const ProcessInstance* step) {
  const auto& params = step->parameters;

  GripperSequenceHelpers::GripperConfig config;
  config.gripperDevice = params.at("gripper_device");
  config.cameraDevice = params.at("camera_device");

  std::string pickNode = params.at("pick_node");
  std::string placeNode = params.at("place_node");

  // Add pick sequence
  GripperSequenceHelpers::AddPickSequence(sequence, config, pickNode, ...);

  // Add place sequence
  GripperSequenceHelpers::AddPlaceSequence(sequence, config, placeNode, ...);
});
```

---

## Best Practices

### 1. Parameter Validation

Always validate parameters before use:
```cpp
// Check required parameters exist
auto paramIt = params.find("required_param");
if (paramIt == params.end()) {
  errorMessage = "Missing required parameter: required_param";
  return false;
}

// Validate parameter values
int value = std::stoi(paramIt->second);
if (value < 0 || value > 100) {
  errorMessage = "Parameter out of range: required_param must be 0-100";
  return false;
}
```

### 2. Logging

Use consistent logging:
```cpp
if (m_logger) {
  m_logger->Info("Action description", "HandlerName");
  m_logger->Warning("Warning message", "HandlerName");
  m_logger->Error("Error message", "HandlerName");
}
```

### 3. Error Messages

Provide clear error messages:
```cpp
// Good
errorMessage = "Failed to move axis X to position 100.5: motor stall detected";

// Bad
errorMessage = "Error";
```

### 4. Context Usage

Use SequenceContext for data sharing between commands:
```cpp
// Write to context
if (context) {
  context->SetValue("key", value, deviceName, methodName);
  context->SetString("key", stringValue, deviceName, methodName);
  context->SetBoolean("key", boolValue, deviceName, methodName);
}

// Read from context
if (context && context->HasValue("key")) {
  float value = context->GetNumeric("key", 0.0f);
  std::string str = context->GetString("key", "");
  bool flag = context->GetBoolean("key", false);
}
```

### 5. Blocking vs Non-Blocking Operations

Support both blocking and non-blocking modes where appropriate:
```cpp
bool blocking = true;  // Default
auto blockingIt = params.find("blocking");
if (blockingIt != params.end()) {
  blocking = (blockingIt->second == "true");
}

if (!hwManager->DoOperation(deviceName, blocking)) {
  errorMessage = "Operation failed";
  return false;
}
```

### 6. Handler Organization

Keep command handlers focused:
- One handler per hardware subsystem
- Group related methods together
- Use helper functions for complex logic
- Document each method with comments

### 7. Testing

Test your command handler:
1. Create a test sequence in RecipePage
2. Validate parameters are correctly passed
3. Test error conditions (missing params, invalid values)
4. Test context read/write functionality
5. Verify logging output

---

## File Checklist

When adding a new command plugin, you'll typically modify these files:

- [ ] `include/sequence/YourNewCommandHandler.h` (create new)
- [ ] `include/sequence/YourNewCommandHandler.cpp` (create new)
- [ ] `include/sequence/SequenceEngine.cpp` (register handler and devices)
- [ ] `include/recipe/RecipeManager.cpp` (add process type and defaults)
- [ ] `include/recipe/ProcessSequenceFactory.cpp` (register builder)
- [ ] `include/AppUI/RecipePage/ProcessConfigWidget.h` (add widget method)
- [ ] `include/AppUI/RecipePage/ProcessConfigWidget.cpp` (implement widget)
- [ ] `include/AppUI/RecipePage/RecipePage.cpp` (register widget creation)
- [ ] `include/ApplicationContext.h` (add manager if new hardware)
- [ ] `Application.cpp` (initialize manager if new hardware)
- [ ] `CMakeLists.txt` (add source files to build)

---

## Summary

The command sequence action plugin architecture provides:

1. **Modularity** - Each subsystem has its own handler
2. **Extensibility** - Easy to add new handlers and commands
3. **Type Safety** - Strong typing through manager pointers
4. **Flexibility** - Commands can be composed into complex sequences
5. **Testability** - Handlers can be tested independently
6. **Maintainability** - Clear separation of concerns

By following this guide, you can extend the system with new hardware capabilities while maintaining the clean architecture and avoiding tight coupling between components.

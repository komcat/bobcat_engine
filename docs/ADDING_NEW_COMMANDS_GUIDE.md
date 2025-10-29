# Complete Guide: Adding New Commands to Bobcat Engine

## Table of Contents
1. [Overview](#overview)
2. [Architecture Summary](#architecture-summary)
3. [Step-by-Step Process](#step-by-step-process)
4. [Example: XCAN Command](#example-xcan-command)
5. [Common Patterns](#common-patterns)
6. [Testing Checklist](#testing-checklist)
7. [Troubleshooting](#troubleshooting)

---

## Overview

This guide explains how to add new commands to the Bobcat Engine sequence system and make them available in the RecipePage UI for recipe creation. The process involves two main parts:

1. **Backend**: Implementing the command handler in the sequence system
2. **Frontend**: Creating a process plugin for the RecipePage

---

## Architecture Summary

### Sequence System Architecture

```
SequenceEngine
    ├── CommandDispatcher (routes commands to handlers)
    │   ├── MotionCommandHandler (motion commands)
    │   ├── IOCommandHandler (I/O operations)
    │   ├── PneumaticCommandHandler (pneumatic controls)
    │   ├── CameraCommandHandler (camera operations)
    │   ├── TECCommandHandler (TEC device commands)
    │   ├── SystemCommandHandler (system operations)
    │   └── DataStoreCommandHandler (data storage)
    │
    └── SequenceBuilder (builds command sequences)
```

### RecipePage Plugin Architecture

```
ProcessPluginRegistry (singleton)
    ├── IProcessPlugin (interface)
    │   ├── GetName() - Process name
    │   ├── GetCategory() - Category grouping
    │   ├── GetDescription() - User-visible description
    │   ├── GetDefaultParameters() - Default parameter values
    │   ├── CreateConfigUI() - UI for parameter editing
    │   └── BuildSequence() - Converts parameters to sequence commands
    │
    └── Plugins (auto-registered at startup)
        ├── MotionProcesses.cpp - Motion control plugins
        ├── SystemProcesses.cpp - System operation plugins
        └── [other plugin files]
```

---

## Step-by-Step Process

### Part 1: Backend Implementation (Command Handler)

#### Option A: Adding to Existing Handler

If your command fits an existing category (motion, I/O, pneumatic, etc.), add it to the appropriate handler.

**Example**: Adding a motion command to `MotionCommandHandler.cpp`

1. **Locate the handler file**:
   - Motion: `include/sequence/MotionCommandHandler.cpp`
   - I/O: `include/sequence/IOCommandHandler.cpp`
   - Pneumatic: `include/sequence/PneumaticCommandHandler.cpp`
   - Camera: `include/sequence/CameraCommandHandler.cpp`
   - TEC: `include/sequence/TECCommandHandler.cpp`
   - System: `include/sequence/SystemCommandHandler.cpp`
   - DataStore: `include/sequence/DataStoreCommandHandler.cpp`

2. **Add your command to ExecuteCommand()**:

```cpp
bool MotionCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);

  // ... existing commands ...

  // ✅ NEW: Add your command
  else if (methodName == "YOUR_COMMAND_NAME") {
    // 1. Validate parameters
    auto it_param1 = params.find("param1");
    if (it_param1 == params.end()) {
      errorMessage = "YOUR_COMMAND_NAME: Missing required parameter 'param1'";
      return false;
    }

    // 2. Parse parameters
    std::string param1 = it_param1->second;

    // 3. Execute your logic
    try {
      // Your implementation here

      // Optional: Log progress
      if (context && context->logger) {
        context->logger->LogInfo("YOUR_COMMAND_NAME: Operation completed");
      }

      return true;
    }
    catch (const std::exception& e) {
      errorMessage = "YOUR_COMMAND_NAME: " + std::string(e.what());
      return false;
    }
  }

  // ... rest of commands ...
}
```

#### Option B: Creating New Handler

If your command needs a new category, create a new handler class.

1. **Create header file**: `include/sequence/YourCommandHandler.h`

```cpp
#pragma once

#include "ICommandHandler.h"

class YourCommandHandler : public ICommandHandler
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

  std::string GetManagerType() const override { return "your_category"; }
};
```

2. **Create implementation file**: `include/sequence/YourCommandHandler.cpp`

```cpp
#include "YourCommandHandler.h"
#include <iostream>

bool YourCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (methodName == "YOUR_COMMAND") {
    // Implementation here
    return true;
  }

  errorMessage = "Unknown command: " + methodName;
  return false;
}
```

3. **Register handler in SequenceEngine.cpp**:

```cpp
// In SequenceEngine constructor
m_dispatcher.RegisterHandler("your_category", std::make_unique<YourCommandHandler>());
```

---

### Part 2: Frontend Integration (RecipePage Plugin)

#### 1. Identify Plugin File

Based on your command category, identify the appropriate plugin file:
- Motion commands → `include/recipe/processes/MotionProcesses.cpp`
- System commands → `include/recipe/processes/SystemProcesses.cpp`
- Create new file if needed → `include/recipe/processes/YourProcesses.cpp`

#### 2. Create Plugin Class

Add your plugin class before the auto-registration section:

```cpp
//=============================================================================
// YOUR PROCESS PLUGIN
//=============================================================================

class YourProcessPlugin : public IProcessPlugin
{
public:
  // 1. Process name (shown in Available Processes panel)
  std::string GetName() const override {
    return "Your Process Name";
  }

  // 2. Category for grouping (e.g., "Motion Control", "System Operations")
  std::string GetCategory() const override {
    return "Your Category";
  }

  // 3. Description shown in UI tooltip
  std::string GetDescription() const override {
    return "Brief description of what this process does";
  }

  // 4. Default parameter values
  std::map<std::string, std::string> GetDefaultParameters() const override {
    return {
      {"param1", "default_value1"},
      {"param2", "default_value2"},
      {"param3", "0"}
    };
  }

  // 5. Create UI widgets for parameter editing
  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Device selector (Motion/Camera/IO/Pneumatic)
    widget->addDeviceSelector("device_param", "Device Label",
      params.at("device_param"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));

    // Text field
    widget->addParameterField("text_param", "Text Label",
      params.at("text_param"));

    // Numeric parameter with validation
    widget->addNumericParameter("numeric_param", "Numeric Label",
      params.at("numeric_param"),
      0.0,    // min
      100.0,  // max
      0.1,    // step
      2,      // decimals
      "unit"  // suffix (e.g., "mm", "ms", "%")
    );

    // Checkbox parameter
    widget->addCheckboxParameter("bool_param", "Checkbox Label",
      params.at("bool_param"),
      "Optional tooltip text");

    // Node/position selector (with browse button)
    widget->addNodeSelector("node_param", "Position Label",
      params.at("node_param"),
      "device_param");  // References device selector parameter
  }

  // 6. Convert parameters to sequence commands
  void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
    const auto& params = step->parameters;

    // Get device parameter (if applicable)
    std::string device = params.at("device_param");

    // Add command action
    // Format: sequence->AddAction(deviceName, "COMMAND_NAME", parameterMap)
    sequence->AddAction(device, "YOUR_COMMAND_NAME", {
      {"param1", params.at("param1")},
      {"param2", params.at("param2")},
      {"param3", params.at("param3")}
    });

    // Optional: Add follow-up commands
    sequence->AddAction(device, "WaitForComplete", {});

    // Debug logging
    std::cout << "[YourProcesses] Built sequence for: " << step->userNickname << std::endl;
  }
};
```

#### 3. Register Plugin

Add registration in the auto-registration section:

```cpp
namespace {
  struct RegisterYourProcesses {
    RegisterYourProcesses() {
      auto& registry = ProcessPluginRegistry::Instance();

      // Register your plugin
      registry.RegisterPlugin(std::make_unique<YourProcessPlugin>());

      std::cout << "[YourProcesses] Registered 1 process plugin" << std::endl;
    }
  };

  // Static instance causes registration at program startup
  static RegisterYourProcesses g_registerYourProcesses;
}
```

#### 4. Add to CMakeLists.txt (if new file)

If you created a new process file, add it to `CMakeLists.txt`:

```cmake
set(SOURCES
  # ... existing files ...
  include/recipe/processes/YourProcesses.cpp
)
```

---

## Example: XCAN Command

### Full Implementation

Here's the complete implementation of the XCAN (hexapod adaptive scanning) command as a reference.

#### Backend: MotionCommandHandler.cpp

```cpp
else if (methodName == "XCAN") {
  // ✅ VALIDATE DEVICE NAME (must match hex-*)
  std::regex hexPattern("^hex-.*");
  if (!std::regex_match(deviceName, hexPattern)) {
    errorMessage = "XCAN: Device must be a PI hexapod (hex-*), got: " + deviceName;
    return false;
  }

  // ✅ VALIDATE PARAMETERS
  auto it_mode = params.find("mode");
  auto it_channel = params.find("channel");

  if (it_mode == params.end()) {
    errorMessage = "XCAN: Missing required parameter 'mode' (coarse/fine)";
    return false;
  }
  if (it_channel == params.end()) {
    errorMessage = "XCAN: Missing required parameter 'channel'";
    return false;
  }

  std::string mode = it_mode->second;
  std::string channel = it_channel->second;

  // Parse optional settling time
  int settlingTimeMs = 300;  // Default
  auto it_settling = params.find("settling_time_ms");
  if (it_settling != params.end()) {
    try {
      settlingTimeMs = std::stoi(it_settling->second);
    } catch (...) {
      settlingTimeMs = 300;
    }
  }

  // ✅ PARSE MODE
  bool isCoarse;
  if (mode == "coarse") {
    isCoarse = true;
  } else if (mode == "fine") {
    isCoarse = false;
  } else {
    errorMessage = "XCAN: Invalid mode '" + mode + "' (must be 'coarse' or 'fine')";
    return false;
  }

  // ✅ CHECK DATASTORE AVAILABILITY
  if (!m_dataStore) {
    errorMessage = "XCAN: DataStore not available";
    return false;
  }

  // ✅ LOG START
  if (context && context->logger) {
    std::string modeStr = isCoarse ? "coarse" : "fine";
    context->logger->LogInfo("XCAN: Starting " + modeStr + " scan on " +
                              deviceName + " for channel '" + channel +
                              "' (settling=" + std::to_string(settlingTimeMs) + "ms)");
  }

  // ✅ CREATE SCAN PARAMETERS
  ScanParameters scanParams;
  scanParams.axesToScan = { "Z", "Y", "X" };  // ZYX order

  if (isCoarse) {
    // Coarse mode: 10→5→2→1 µm
    scanParams.stepSizes = { 0.010, 0.005, 0.002, 0.001 };
  } else {
    // Fine mode: 2→1→0.5 µm
    scanParams.stepSizes = { 0.002, 0.001, 0.0005 };
  }

  scanParams.motionSettleTimeMs = settlingTimeMs;
  scanParams.velocity = 50.0;  // 50% of max
  scanParams.consecutiveDecreases = 3;
  scanParams.significantDecrease = 0.05;  // 5%
  scanParams.maxTotalDistance = 0.2;  // 200 µm per direction
  scanParams.improvementThreshold = 0.01;  // 1%
  scanParams.averagingCount = 3;
  scanParams.measurementTimeoutMs = 5000;
  scanParams.validValueMin = -1000.0;
  scanParams.validValueMax = 1000.0;
  scanParams.maxStepSize = 0.5;  // 500 µm
  scanParams.totalTimeoutMs = 600000;  // 10 minutes

  // ✅ CREATE SCAN CONTROLLER
  AdaptiveScanController scanController(*motionManager, *m_dataStore, deviceName, channel);

  // ✅ SETUP COMPLETION TRACKING
  std::atomic<bool> scanComplete(false);
  std::atomic<bool> scanSuccess(false);
  std::mutex completionMutex;
  std::condition_variable completionCV;
  double peakValue = 0.0;

  scanController.SetCompletionCallback([&](const ScanResults& results) {
    std::lock_guard<std::mutex> lock(completionMutex);
    scanComplete = true;
    scanSuccess = results.globalPeak.valid;
    if (results.globalPeak.valid) {
      peakValue = results.globalPeak.value;
    }
    completionCV.notify_one();
  });

  scanController.SetErrorCallback([&](const std::string& error) {
    std::lock_guard<std::mutex> lock(completionMutex);
    scanComplete = true;
    scanSuccess = false;
    completionCV.notify_one();
    if (context && context->logger) {
      context->logger->LogError("XCAN: Scan error - " + error);
    }
  });

  // ✅ START SCAN
  if (!scanController.StartScan(scanParams)) {
    errorMessage = "XCAN: Failed to start scan";
    return false;
  }

  // ✅ WAIT FOR COMPLETION WITH STOP FLAG MONITORING
  while (!scanComplete) {
    // Check stop flag every 100ms
    if (context && context->IsStopRequestedAny()) {
      if (context->logger) {
        context->logger->LogWarning("XCAN: Stop requested, halting scan");
      }
      scanController.HaltScan();
      errorMessage = "XCAN: Scan stopped by user";
      return false;
    }

    // Wait with timeout
    std::unique_lock<std::mutex> lock(completionMutex);
    completionCV.wait_for(lock, std::chrono::milliseconds(100));
  }

  // ✅ CHECK RESULT
  if (!scanSuccess) {
    errorMessage = "XCAN: Scan failed or no peak found";
    return false;
  }

  // ✅ LOG SUCCESS
  if (context && context->logger) {
    context->logger->LogInfo("XCAN: Scan complete - Peak value: " +
                              std::to_string(peakValue));
  }

  return true;
}
```

#### Frontend: MotionProcesses.cpp

```cpp
//=============================================================================
// XCAN PLUGIN (Hexapod Adaptive Scanning)
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

  void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    widget->addDeviceSelector("device", "Hexapod Device (hex-*)", params.at("device"),
      static_cast<int>(DeviceComboBox::DeviceType::Motion));
    widget->addParameterField("mode", "Scan Mode (coarse/fine)", params.at("mode"));
    widget->addParameterField("channel", "Data Channel", params.at("channel"));
    widget->addNumericParameter("settling_time_ms", "Settling Time (ms)",
      params.at("settling_time_ms"),
      50,     // min
      2000,   // max
      10,     // step
      0,      // decimals (integer)
      "ms"    // suffix
    );
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

// Registration in RegisterMotionProcesses():
registry.RegisterPlugin(std::make_unique<XCANPlugin>());
```

---

## Common Patterns

### 1. Parameter Validation

Always validate parameters before use:

```cpp
// Required parameter
auto it_param = params.find("required_param");
if (it_param == params.end()) {
  errorMessage = "COMMAND: Missing required parameter 'required_param'";
  return false;
}

// Optional parameter with default
std::string optionalParam = "default_value";
auto it_optional = params.find("optional_param");
if (it_optional != params.end()) {
  optionalParam = it_optional->second;
}

// Numeric parameter with error handling
int numericParam = 100;  // default
auto it_numeric = params.find("numeric_param");
if (it_numeric != params.end()) {
  try {
    numericParam = std::stoi(it_numeric->second);
  } catch (const std::exception& e) {
    errorMessage = "COMMAND: Invalid numeric value for 'numeric_param'";
    return false;
  }
}
```

### 2. Device Validation

Validate device names/types when necessary:

```cpp
// Regex pattern matching
std::regex devicePattern("^prefix-.*");
if (!std::regex_match(deviceName, devicePattern)) {
  errorMessage = "COMMAND: Invalid device name, expected 'prefix-*' pattern";
  return false;
}

// Type checking
if (deviceType != ExpectedType) {
  errorMessage = "COMMAND: Device must be of type XYZ";
  return false;
}
```

### 3. Logging

Use context logger for visibility:

```cpp
if (context && context->logger) {
  // Info
  context->logger->LogInfo("COMMAND: Operation started");

  // Warning
  context->logger->LogWarning("COMMAND: Non-critical issue occurred");

  // Error
  context->logger->LogError("COMMAND: Critical error - " + errorDetails);
}
```

### 4. Stop Flag Monitoring

For long-running operations, check stop flag periodically:

```cpp
while (operationInProgress) {
  // Check stop flag
  if (context && context->IsStopRequestedAny()) {
    // Clean up and abort
    CleanupOperation();
    errorMessage = "COMMAND: Stopped by user";
    return false;
  }

  // Continue processing
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

### 5. Synchronous Operations

For async operations that need to block:

```cpp
std::atomic<bool> operationComplete(false);
std::atomic<bool> operationSuccess(false);
std::mutex completionMutex;
std::condition_variable completionCV;

// Setup completion callback
SetCompletionCallback([&](const Result& result) {
  std::lock_guard<std::mutex> lock(completionMutex);
  operationComplete = true;
  operationSuccess = result.success;
  completionCV.notify_one();
});

// Start async operation
StartOperation();

// Wait with stop monitoring
while (!operationComplete) {
  if (context && context->IsStopRequestedAny()) {
    StopOperation();
    return false;
  }
  std::unique_lock<std::mutex> lock(completionMutex);
  completionCV.wait_for(lock, std::chrono::milliseconds(100));
}

return operationSuccess;
```

### 6. UI Widget Patterns

Common UI widget configurations:

```cpp
// Device selector
widget->addDeviceSelector("device", "Device Name", params.at("device"),
  static_cast<int>(DeviceComboBox::DeviceType::Motion));
// DeviceType options: Motion, Camera, IO, Pneumatic, Lens

// Numeric with range validation
widget->addNumericParameter("speed", "Speed (mm/s)",
  params.at("speed"),
  0.1,    // min
  100.0,  // max
  0.1,    // step size
  2,      // decimal places
  "mm/s"  // unit suffix
);

// Node/position selector with device reference
widget->addNodeSelector("target_pos", "Target Position",
  params.at("target_pos"),
  "device");  // References "device" parameter

// Simple text field
widget->addParameterField("text_param", "Label", params.at("text_param"));

// Checkbox
widget->addCheckboxParameter("enabled", "Enable Feature",
  params.at("enabled"),
  "Tooltip text explaining what this does");
```

---

## Testing Checklist

### Backend Testing

- [ ] Command compiles without errors
- [ ] Command handler registered in SequenceEngine
- [ ] All parameters validated correctly
- [ ] Error messages are clear and descriptive
- [ ] Logging statements added for visibility
- [ ] Stop flag monitoring works (if applicable)
- [ ] Command executes successfully with valid parameters
- [ ] Command fails gracefully with invalid parameters
- [ ] No memory leaks or resource issues

### Frontend Testing

- [ ] Plugin appears in Available Processes panel
- [ ] Plugin category is correct
- [ ] Description is clear and helpful
- [ ] Default parameters are sensible
- [ ] All UI widgets render correctly
- [ ] Parameter validation works in UI
- [ ] Can add process to recipe
- [ ] Can edit parameters after adding
- [ ] Can save/load recipe with process
- [ ] BuildSequence generates correct commands
- [ ] Recipe executes successfully
- [ ] Recipe execution logs appear correctly

### Integration Testing

- [ ] Command works standalone (direct execution)
- [ ] Command works in recipe
- [ ] Command works in program
- [ ] Stop button halts command execution
- [ ] Multiple instances can be added to recipe
- [ ] Command works with other commands in sequence
- [ ] Error handling works end-to-end
- [ ] UI updates reflect command status

---

## Troubleshooting

### Common Issues

#### 1. Plugin Not Appearing in RecipePage

**Problem**: After adding plugin, it doesn't show in Available Processes panel.

**Solutions**:
- Verify plugin file is included in CMakeLists.txt
- Check that RegisterPlugin() is called in static initializer
- Rebuild entire project (clean build)
- Check console output for registration messages
- Verify GetCategory() returns expected category string

#### 2. Compilation Errors

**Problem**: Build fails after adding command.

**Solutions**:
- Check all required includes are present
- Verify class/struct definitions are available
- Check for typos in class/method names
- Ensure all dependencies are linked in CMakeLists.txt
- Check that forward declarations match actual classes

#### 3. Runtime Parameter Errors

**Problem**: Command fails with "parameter not found" errors.

**Solutions**:
- Verify parameter keys match between plugin and handler
- Check that GetDefaultParameters() includes all required parameters
- Ensure BuildSequence() passes all required parameters
- Verify parameter names are case-sensitive matches

#### 4. Device Not Found Errors

**Problem**: Command fails with "device not available" errors.

**Solutions**:
- Verify device name in config files (motor_devices.json, etc.)
- Check ApplicationContext has device manager initialized
- Verify device type matches handler expectations
- Check that device is properly initialized before command execution

#### 5. Stop Button Not Working

**Problem**: Stop button doesn't halt command execution.

**Solutions**:
- Add stop flag checks in command loop: `context->IsStopRequestedAny()`
- Check stop flag frequently (every 100-500ms recommended)
- Ensure command doesn't block for long periods without checking
- Verify command returns false when stopped

---

## Additional Resources

### Key Files Reference

| File | Purpose |
|------|---------|
| `include/sequence/SequenceEngine.h/cpp` | Main sequence execution engine |
| `include/sequence/SequenceBuilder.h` | Helper for building command sequences |
| `include/sequence/ICommandHandler.h` | Base interface for command handlers |
| `include/sequence/*CommandHandler.cpp` | Specific command handler implementations |
| `include/recipe/IProcessPlugin.h` | Base interface for process plugins |
| `include/recipe/ProcessPluginRegistry.h` | Plugin registration system |
| `include/recipe/processes/*.cpp` | Process plugin implementations |
| `include/AppUI/RecipePage/ProcessConfigWidget.h` | Base class for parameter UI |

### Configuration Files

| File | Purpose |
|------|---------|
| `config/motor_devices.json` | Motion device configuration |
| `config/camera_config.json` | Camera device configuration |
| `config/io_devices.json` | I/O device configuration |
| `config/pneumatic_slides.json` | Pneumatic device configuration |

### Existing Examples

Study these existing implementations for reference:
- **Simple command**: MoveToPosition in MotionProcesses.cpp
- **Complex command**: XCAN in MotionProcesses.cpp
- **Multi-step**: Pick/Place in MotionProcesses.cpp
- **Conditional logic**: SetVelocity (handles ACS vs PI) in MotionProcesses.cpp

---

## Summary

To add a new command to Bobcat Engine:

1. **Implement command handler** in appropriate handler file
   - Add command logic to ExecuteCommand()
   - Validate all parameters
   - Add error handling and logging
   - Monitor stop flag for long operations

2. **Create process plugin** in appropriate process file
   - Implement IProcessPlugin interface
   - Define default parameters
   - Create UI widgets in CreateConfigUI()
   - Build command sequence in BuildSequence()

3. **Register plugin** in static initializer

4. **Test thoroughly** both backend and frontend

5. **Document** your command's parameters and behavior

Following this guide ensures your command integrates smoothly with the existing architecture and provides a good user experience in the RecipePage.

---

**Document Version**: 1.0
**Last Updated**: 2025-10-29
**Author**: Bobcat Engine Development Team

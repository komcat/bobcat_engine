# Table-Based Process Configuration Pattern

This document describes how to implement table-based (Visual Studio property grid style) configuration UI for process plugins.

## Overview

The `PropertyTableWidget` provides a modern two-column property editor:
- **Property** column: Labels for each parameter
- **Value** column: Editable widgets (text, dropdown, checkbox, numeric, etc.)

Category headers can group related properties visually.

## When to Use Table-Based UI

Use table-based UI for processes with:
- Multiple parameters that benefit from organized grouping
- Parameters that need tooltips for explanation
- Complex configuration that benefits from Visual Studio-style layout

## Implementation Steps

### 1. Include the Header

In your process plugin file (e.g., `IOProcesses.cpp`):

```cpp
#include "../../AppUI/RecipePage/PropertyTableWidget.h"
```

### 2. Override `PrefersTableUI()`

Return `true` to indicate this plugin uses table-based UI:

```cpp
bool PrefersTableUI() const override { return true; }
```

### 3. Implement `CreateTableUI()`

Add the table UI creation method:

```cpp
void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
    const auto& params = instance->parameters;

    // Add category headers and properties
    table->addCategoryHeader("Device Configuration");
    table->addDeviceProperty("device", "Device Name", params.at("device"),
        static_cast<int>(DeviceComboBox::DeviceType::IO),
        "Tooltip description");

    table->addCategoryHeader("Settings");
    table->addNumericProperty("timeout", "Timeout",
        std::stod(params.at("timeout")),
        0, 60000, 100, 0, "ms",
        "Maximum wait time in milliseconds");
}
```

### 4. Keep `CreateConfigUI()` as Fallback

Always keep the form-based `CreateConfigUI()` for compatibility:

```cpp
void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
    // Fallback for compatibility
    const auto& params = instance->parameters;
    widget->addDeviceSelector("device", "Device", params.at("device"),
        static_cast<int>(DeviceComboBox::DeviceType::IO));
    widget->addParameterField("timeout", "Timeout (ms)", params.at("timeout"));
}
```

## Available Property Methods

### Basic Properties

```cpp
// Text input field
table->addTextProperty(key, label, value, tooltip);

// Dropdown with fixed options
table->addDropdownProperty(key, label, value, {"option1", "option2"}, tooltip);

// Checkbox (bool)
table->addCheckboxProperty(key, label, boolValue, tooltip);

// Numeric with spinbox
table->addNumericProperty(key, label,
    doubleValue,    // current value
    minValue,       // minimum
    maxValue,       // maximum
    step,           // increment step
    decimals,       // decimal places (0 for integers)
    suffix,         // unit suffix (e.g., "ms", "um")
    tooltip);
```

### Device/Hardware Properties

```cpp
// Motion device selector with browse button (shows only connected devices)
table->addMotionDeviceProperty(key, label, value, tooltip);

// Device selector dropdown (DeviceComboBox) - for non-motion devices
table->addDeviceProperty(key, label, value,
    static_cast<int>(DeviceComboBox::DeviceType::IO),  // or Camera, TEC, PSU, etc.
    tooltip);

// IO pin selector (depends on IO device selection)
table->addIOPinProperty(key, label, value,
    "io_device",   // key of the IO device property
    "output",      // "output" or "input"
    tooltip);

// Pneumatic slide selector with browse button
table->addSlideProperty(key, label, value, tooltip);

// Named position selector with browse button
table->addNodeProperty(key, label, value,
    "device_key",  // key of the motion device property
    tooltip);

// Motion graph selector dropdown
table->addGraphProperty(key, label, value,
    "device_key",  // key of the motion device property
    tooltip);

// Graph node selector dropdown
table->addGraphNodeProperty(key, label, value,
    "device_key",  // key of the motion device property
    "graph_key",   // key of the graph property
    tooltip);

// DataStore channel selector dropdown
table->addDataChannelProperty(key, label, value, tooltip);
```

### Layout Helpers

```cpp
// Category header (spans both columns, styled differently)
table->addCategoryHeader("Section Name");

// Horizontal separator line
table->addSeparator();
```

## Complete Example: Toggle Output Plugin

```cpp
class ToggleOutputPlugin : public IProcessPlugin
{
public:
    std::string GetName() const override { return "Toggle Output"; }
    std::string GetCategory() const override { return "I/O & Pneumatics"; }
    std::string GetDescription() const override {
        return "Toggle digital output pin with configurable timing";
    }

    std::map<std::string, std::string> GetDefaultParameters() const override {
        return {
            {"io_device", "IOBottom"},
            {"pin_name", ""},
            {"start_state", "off"},
            {"inbounce_delay_ms", "300"},
            {"dwell_time_ms", "300"},
            {"outbounce_delay_ms", "300"}
        };
    }

    // Enable table-based UI
    bool PrefersTableUI() const override { return true; }

    // Fallback form-based UI
    void CreateConfigUI(ProcessConfigWidget* widget, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;
        widget->addDeviceSelector("io_device", "I/O Device", params.at("io_device"),
            static_cast<int>(DeviceComboBox::DeviceType::IO));
        widget->addIOPinSelector("pin_name", "Output Pin", params.at("pin_name"),
            "io_device", "output");
        widget->addDropdownParameter("start_state", "Start State", params.at("start_state"),
            {"off", "on"});
        widget->addNumericParameter("inbounce_delay_ms", "Inbounce Delay (ms)",
            params.at("inbounce_delay_ms"), 0, 10000, 50, 0, "ms");
        widget->addNumericParameter("dwell_time_ms", "Dwell Time (ms)",
            params.at("dwell_time_ms"), 0, 10000, 50, 0, "ms");
        widget->addNumericParameter("outbounce_delay_ms", "Outbounce Delay (ms)",
            params.at("outbounce_delay_ms"), 0, 10000, 50, 0, "ms");
    }

    // Table-based UI (preferred)
    void CreateTableUI(PropertyTableWidget* table, const ProcessInstance* instance) override {
        const auto& params = instance->parameters;

        // Device configuration section
        table->addCategoryHeader("Device Configuration");
        table->addDeviceProperty("io_device", "I/O Device", params.at("io_device"),
            static_cast<int>(DeviceComboBox::DeviceType::IO),
            "Select the I/O device to control");
        table->addIOPinProperty("pin_name", "Output Pin", params.at("pin_name"),
            "io_device", "output",
            "Select the output pin to toggle");

        // Toggle settings section
        table->addCategoryHeader("Toggle Settings");
        table->addDropdownProperty("start_state", "Start State", params.at("start_state"),
            {"off", "on"},
            "Initial state before toggle sequence begins");

        // Timing section
        table->addCategoryHeader("Timing Configuration");
        table->addNumericProperty("inbounce_delay_ms", "Inbounce Delay",
            std::stod(params.at("inbounce_delay_ms")),
            0, 10000, 50, 0, "ms",
            "Delay before first toggle (debounce input)");
        table->addNumericProperty("dwell_time_ms", "Dwell Time",
            std::stod(params.at("dwell_time_ms")),
            0, 10000, 50, 0, "ms",
            "Time to hold the toggled state");
        table->addNumericProperty("outbounce_delay_ms", "Outbounce Delay",
            std::stod(params.at("outbounce_delay_ms")),
            0, 10000, 50, 0, "ms",
            "Delay after returning to original state");
    }

    void BuildSequence(SequenceBuilder* sequence, const ProcessInstance* step) override {
        // ... sequence building logic unchanged
    }
};
```

## DeviceComboBox Device Types

Available device types for `addDeviceProperty()`:

```cpp
enum class DeviceType {
    Motion = 0,    // Motion controllers (gantry, hexapod)
    Camera = 1,    // Cameras
    IO = 2,        // I/O devices (EziIO)
    Hexapod = 3,   // Hexapod-only devices
    TEC = 4,       // TEC controllers
    PSU = 5        // Power supply units
};
```

## Parameter Value Conversion

When using `addNumericProperty()`, convert string parameters to double:

```cpp
// Safe conversion with error handling
double value = 0.0;
try {
    value = std::stod(params.at("key"));
} catch (...) {
    value = 0.0;  // Default fallback
}

table->addNumericProperty("key", "Label", value, 0, 1000, 1, 0, "ms", "tooltip");
```

## Files Modified When Adding Table UI

1. **Process plugin file** (e.g., `IOProcesses.cpp`):
   - Add `#include "PropertyTableWidget.h"`
   - Add `PrefersTableUI()` override
   - Add `CreateTableUI()` implementation

No other files need modification - the infrastructure automatically detects and uses table-based UI when `PrefersTableUI()` returns `true`.

## Architecture

```
RecipePage::updateConfigurationForm()
    |
    v
ProcessPluginRegistry::GetPlugin(processType)
    |
    v
plugin->PrefersTableUI() ?
    |
    +-- true --> TableBasedConfigWidget
    |               |
    |               v
    |           plugin->CreateTableUI(PropertyTableWidget*, instance)
    |
    +-- false --> PluginBasedConfigWidget
                    |
                    v
                plugin->CreateConfigUI(ProcessConfigWidget*, instance)
```

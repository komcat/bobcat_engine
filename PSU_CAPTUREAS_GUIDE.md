# PSU Read Commands - CaptureAs Field Guide

## Overview

The **PSU Read Voltage** and **PSU Read Current** process commands now include a `captureAs` field that allows you to store measurement results in the **Sequence Context** for later use in your recipes.

---

## What is CaptureAs?

The `captureAs` field lets you assign a **variable name** to store the measured value. Other processes in the recipe can then reference this stored value using conditionals or data operations.

---

## Updated Process UI

### **PSU Read Voltage**

```
┌─────────────────────────────────────────────┐
│ PSU Read Voltage Configuration             │
├─────────────────────────────────────────────┤
│ PSU Device:          [PSU1              ▼] │
│ Read Count:          [3                  ] │
│ Delay (ms):          [100                ] │
│ Channel (1-4):       [1                  ] │
│ Capture As:          [measured_voltage   ] │ ← NEW!
└─────────────────────────────────────────────┘
```

### **PSU Read Current**

```
┌─────────────────────────────────────────────┐
│ PSU Read Current Configuration             │
├─────────────────────────────────────────────┤
│ PSU Device:          [PSU1              ▼] │
│ Read Count:          [3                  ] │
│ Delay (ms):          [100                ] │
│ Channel (1-4):       [1                  ] │
│ Capture As:          [measured_current   ] │ ← NEW!
└─────────────────────────────────────────────┘
```

---

## How It Works

### **1. Variable Storage**

When you specify a `captureAs` value (e.g., `"voltage_ch1"`):
1. The command reads the measurement (voltage or current)
2. The **last reading** is stored in the Sequence Context
3. The variable can be accessed by subsequent processes

### **2. Command Handler Behavior**

From `PSUCommandHandler.cpp`:

```cpp
// Read voltage and auto-capture to context
double voltage = psu->readVoltage(channel);

if (context && !captureAs.empty()) {
  context->SetValue(captureAs, static_cast<float>(voltage), deviceName, methodName);
  logger->Info("Measured " + std::to_string(voltage) + " V, stored as '" + captureAs + "'");
}
```

**Logged Output:**
```
[PSUHandler] Measured 5.023 V, stored as 'voltage_ch1'
[PSUHandler] Measured 0.150 A, stored as 'current_ch2'
```

---

## Usage Examples

### **Example 1: Basic Voltage Capture**

**Recipe:**
```
Step 1: PSU Set Voltage
  - Device: PSU1
  - Voltage: 5.0
  - Channel: 1

Step 2: PSU Output On
  - Device: PSU1
  - State: on
  - Channel: 1

Step 3: PSU Read Voltage
  - Device: PSU1
  - Read Count: 5
  - Delay: 200ms
  - Channel: 1
  - Capture As: v_measured    ← Store result
```

**Result:**
- Voltage is read 5 times with 200ms delay
- Last reading stored as `v_measured`
- Variable available for rest of recipe

---

### **Example 2: Multi-Channel Current Monitoring**

**Recipe:**
```
Step 1: PSU Read Current
  - Device: PSU1
  - Read Count: 3
  - Delay: 100ms
  - Channel: 1
  - Capture As: i_ch1    ← Capture Ch1 current

Step 2: PSU Read Current
  - Device: PSU1
  - Read Count: 3
  - Delay: 100ms
  - Channel: 2
  - Capture As: i_ch2    ← Capture Ch2 current

Step 3: PSU Read Current
  - Device: PSU1
  - Read Count: 3
  - Delay: 100ms
  - Channel: 3
  - Capture As: i_ch3    ← Capture Ch3 current
```

**Result:**
- Three variables stored: `i_ch1`, `i_ch2`, `i_ch3`
- Each contains the average/last current reading from respective channel

---

### **Example 3: Conditional Control Based on Voltage**

**Recipe (Conceptual):**
```
Step 1: PSU Read Voltage
  - Device: PSU1
  - Channel: 1
  - Capture As: vout

Step 2: Conditional Check (if supported)
  - If vout < 4.9V:
      → Log warning
      → Adjust voltage

Step 3: Continue processing...
```

---

## Variable Naming Best Practices

### **Good Variable Names:**
- `voltage_ch1`, `voltage_ch2`
- `i_load`, `v_sensor`
- `measured_voltage`, `measured_current`
- `ch1_v`, `ch2_i`

### **Avoid:**
- Spaces: `"voltage ch1"` ❌
- Special chars: `"voltage@ch1"` ❌
- Reserved keywords (if any): `"context"`, `"params"` ❌

### **Convention:**
Use descriptive names that indicate:
1. **What** is measured: `voltage`, `current`
2. **Where** it's from: `ch1`, `psu1`, `load`
3. **When/Why**: `initial`, `final`, `stable`

Examples:
- `voltage_ch1_initial`
- `current_load_stable`
- `v_sensor_final`

---

## Optional Field

### **Leave Empty to Skip Capture**

If you don't need to store the value:
```
Capture As: [                    ]  ← Leave blank
```

The measurement will still be:
- Logged to console
- Displayed in logs
- NOT stored in context

---

## Sequence Context Data Types

### **Stored as Float**

Both voltage and current are stored as `float` type in the Sequence Context:
```cpp
context->SetValue(captureAs, static_cast<float>(voltage), deviceName, methodName);
```

**Precision:**
- Voltage: Typically 3 decimal places (5.023 V)
- Current: Typically 4 decimal places (0.1523 A)

---

## Recipe JSON Example

When saved to JSON, a recipe with `captureAs` looks like:

```json
{
  "steps": [
    {
      "processType": "PSU Read Voltage",
      "userNickname": "Measure Ch1 Voltage",
      "parameters": {
        "psu_device": "PSU1",
        "read_count": "5",
        "delay_ms": "200",
        "channel": "1",
        "captureAs": "voltage_ch1"
      },
      "enabled": true
    },
    {
      "processType": "PSU Read Current",
      "userNickname": "Measure Ch1 Current",
      "parameters": {
        "psu_device": "PSU1",
        "read_count": "3",
        "delay_ms": "100",
        "channel": "1",
        "captureAs": "current_ch1"
      },
      "enabled": true
    }
  ]
}
```

---

## Execution Log Example

```
[SequenceEngine] Starting recipe execution...

[PSUHandler] Reading voltage from PSU1 3 time(s) with 100ms delay
[PSU] [1/3] PSU1 Voltage = 5.023 V
[PSU] [2/3] PSU1 Voltage = 5.025 V
[PSU] [3/3] PSU1 Voltage = 5.024 V
[PSUHandler] Last voltage reading 5.024 V stored as 'voltage_ch1'

[PSUHandler] Reading current from PSU1 3 time(s) with 100ms delay
[PSU] [1/3] PSU1 Current = 152.3 mA (0.152 A)
[PSU] [2/3] PSU1 Current = 151.8 mA (0.152 A)
[PSU] [3/3] PSU1 Current = 152.0 mA (0.152 A)
[PSUHandler] Last current reading 0.152 A stored as 'current_ch1'

[SequenceContext] Variables stored:
  - voltage_ch1 = 5.024
  - current_ch1 = 0.152

[SequenceEngine] Recipe completed successfully
```

---

## Integration with Other Systems

### **Accessing Stored Variables**

Other command handlers can access the stored values:

```cpp
// In another command handler
float voltage = context->GetValue("voltage_ch1");
float current = context->GetValue("current_ch1");

// Calculate power
float power = voltage * current;
```

### **Potential Use Cases**

1. **Data Logging** - Store measurements for later export
2. **Conditional Logic** - Make decisions based on measured values
3. **Calculations** - Compute derived values (power, resistance)
4. **Validation** - Check if voltage/current is within expected range
5. **Adaptive Control** - Adjust parameters based on measurements

---

## Comparison with Other Capture Fields

Similar `captureAs` pattern exists in other commands:

### **Motion Commands:**
```
Capture As: current_position
→ Stores X, Y, Z coordinates
```

### **Camera Commands:**
```
Capture As: brightness_value
→ Stores image analysis result
```

### **PSU Commands:**
```
Capture As: measured_voltage
→ Stores voltage/current reading
```

All use the same Sequence Context mechanism for data sharing between processes.

---

## Summary

✅ **PSU Read Voltage** - Now has `captureAs` field
✅ **PSU Read Current** - Now has `captureAs` field
✅ **Default Parameters** - Empty string (optional)
✅ **Always Visible** - Field always shown in config UI
✅ **Backward Compatible** - Old recipes still work (no capture)

The `captureAs` field enables data-driven recipes where measurements can influence subsequent steps!

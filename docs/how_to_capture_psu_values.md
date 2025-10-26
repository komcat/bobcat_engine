# How to Capture PSU Values to SequenceContext

## ✅ What I Just Implemented

I've added **automatic value capture** to PSUCommandHandler. Now PSU measurement commands can store their readings directly into the execution context.

### Updated Commands with Auto-Capture:
- ✅ `ReadVoltage` - Captures single voltage reading
- ✅ `ReadVoltageMultiple` - Captures **last** voltage reading from the series
- ✅ `ReadCurrent` - Captures single current reading
- ✅ `ReadCurrentMultiple` - Captures **last** current reading from the series

---

## 📝 How to Use It in Your Recipe

### Option 1: Modify Your Recipe JSON Directly

Open your recipe JSON file (e.g., `recipes/Read Voltage Test.json`) and add the `captureAs` parameter:

**BEFORE (no capture):**
```json
{
  "user_nickname": "PSU Read Voltage 4",
  "process_type": "PSU Read Voltage",
  "enabled": true,
  "parameters": {
    "psu_device": "PSU1",
    "read_count": "20",
    "delay_ms": "100"
  }
}
```

**AFTER (with capture):**
```json
{
  "user_nickname": "PSU Read Voltage 4",
  "process_type": "PSU Read Voltage",
  "enabled": true,
  "parameters": {
    "psu_device": "PSU1",
    "read_count": "20",
    "delay_ms": "100",
    "captureAs": "MeasuredVoltage"
  }
}
```

### Option 2: Add in Recipe Editor UI

If your recipe editor has a parameters section, simply add a new parameter:
- **Key:** `captureAs`
- **Value:** `MeasuredVoltage` (or any name you want)

---

## 🎯 Example: Complete Test Recipe

Here's a recipe that demonstrates full context capture and usage:

```json
{
  "recipe_name": "PSU Test with Context",
  "product_file": "",
  "steps": [
    {
      "user_nickname": "Turn On PSU",
      "process_type": "PSU Output On/Off",
      "enabled": true,
      "parameters": {
        "psu_device": "PSU1",
        "state": "on"
      }
    },
    {
      "user_nickname": "Set Target Voltage",
      "process_type": "PSU Set Voltage",
      "enabled": true,
      "parameters": {
        "psu_device": "PSU1",
        "voltage": "3.3"
      }
    },
    {
      "user_nickname": "Measure Actual Voltage",
      "process_type": "PSU Read Voltage",
      "enabled": true,
      "parameters": {
        "psu_device": "PSU1",
        "read_count": "20",
        "delay_ms": "100",
        "captureAs": "ActualVoltage"
      }
    },
    {
      "user_nickname": "Measure Current Draw",
      "process_type": "PSU Read Current",
      "enabled": true,
      "parameters": {
        "psu_device": "PSU1",
        "read_count": "10",
        "delay_ms": "100",
        "captureAs": "LoadCurrent"
      }
    },
    {
      "user_nickname": "Turn Off PSU",
      "process_type": "PSU Output On/Off",
      "enabled": true,
      "parameters": {
        "psu_device": "PSU1",
        "state": "off"
      }
    }
  ]
}
```

---

## 📊 What You'll See After Execution

Once you add `captureAs` and run your recipe, you'll see this in the execution log:

```
[17:07:11.708] [INFO] [PSU] [1/20] PSU1 Voltage = 3.297 V
[17:07:11.912] [INFO] [PSU] [2/20] PSU1 Voltage = 3.297 V
...
[17:07:16.181] [INFO] [PSU] [20/20] PSU1 Voltage = 3.297 V
[17:07:16.182] [INFO] [PSUHandler] Last voltage reading 3.297000 V stored as 'ActualVoltage'

========================================
Execution Context Summary:
  Duration: 4624 ms
  Steps Executed: 5
  Values Captured: 2
  Captured Values:
    - ActualVoltage = 3.297
    - LoadCurrent = 0.125
  Context saved to: logs/context_PSU_Test_with_Context_1234567890.json
========================================
```

---

## 💾 JSON Context File

Check the `logs/` folder for the saved context file:

```json
{
  "values": {
    "ActualVoltage": {
      "type": "numeric",
      "numericValue": 3.297,
      "timestamp": "2025-01-15T17:07:16Z",
      "sourceDevice": "PSU1",
      "sourceMethod": "ReadVoltageMultiple"
    },
    "LoadCurrent": {
      "type": "numeric",
      "numericValue": 0.125,
      "timestamp": "2025-01-15T17:07:20Z",
      "sourceDevice": "PSU1",
      "sourceMethod": "ReadCurrentMultiple"
    }
  },
  "executionMetadata": {
    "startTime": 1234567890000,
    "endTime": 1234568894624,
    "stepsExecuted": 5,
    "valuesCaptured": 2
  }
}
```

---

## 🔮 Future Usage: Conditionals

Once values are captured, you can use them in conditional logic (future enhancement):

```cpp
// In sequence building
builder->AddAction("PSU1", "ReadVoltage", {{"captureAs", "V"}});

builder->BeginIf("V", ComparisonType::LESS_THAN, 3.0f);
    builder->AddAction("system", "UserPrompt", {
        {"title", "Warning"},
        {"message", "Voltage too low!"}
    });
builder->EndIf();
```

Or perform calculations:

```cpp
builder->AddAction("PSU1", "ReadVoltage", {{"captureAs", "V"}});
builder->AddAction("PSU1", "ReadCurrent", {{"captureAs", "I"}});
builder->AddAction("context", "Calculate", {
    {"resultKey", "Power"},
    {"key1", "V"},
    {"key2", "I"},
    {"operation", "*"}
});
builder->AddAction("context", "LogValue", {{"key", "Power"}});
```

---

## 🎓 Quick Start

1. **Add `captureAs` to your "PSU Read Voltage" step** in the recipe JSON
2. **Run the recipe**
3. **Check the execution log** - you'll see:
   - Confirmation message: `"Last voltage reading X.XXX V stored as 'YourKeyName'"`
   - Context summary at the end
   - Path to JSON file
4. **Inspect the JSON file** in `logs/` folder for complete details

That's it! Your PSU measurements are now being automatically captured and saved! 🎉

# SequenceContext Testing Guide

## How to See Captured Data During Sequence Execution

The SequenceContext system is now fully integrated with visibility features to show captured data.

---

## 🔍 Automatic Logging After Execution

When you run any recipe through **RecipeWorker**, the execution context is **automatically logged** at the end:

```
========================================
Execution Context Summary:
  Duration: 1234 ms
  Steps Executed: 15
  Values Captured: 5
  Captured Values:
    - Temperature = 25.5
    - Voltage = 3.3
    - Current = 0.125
    - Power = 0.4125
    - TestPassed = "true"
  Context saved to: logs/context_MyRecipe_1234567890.json
========================================
```

The context is also **saved to a JSON file** in the `logs/` folder for detailed inspection.

---

## 📝 Testing Context Capture with Context Commands

The `SequenceContextCommandHandler` provides direct commands to manipulate context values. You can use these in your recipes to test context capture.

### Available Context Commands

| Command | Parameters | Description |
|---------|-----------|-------------|
| `SetValue` | `key`, `value`, `type` (optional) | Store a value in context |
| `LogValue` | `key` | Log a value from context |
| `Calculate` | `resultKey`, `key1`, `key2`, `operation` | Perform arithmetic |
| `Clear` | `key` | Clear a specific value |
| `ClearAll` | (none) | Clear all values |
| `Exists` | `key` | Check if value exists (returns via log) |

### Example 1: Basic Value Storage and Retrieval

Add these steps to a recipe to test context:

```json
{
  "process_type": "Custom",
  "enabled": true,
  "parameters": {
    "device": "context",
    "method": "SetValue",
    "key": "TestValue",
    "value": "42.5",
    "type": "numeric"
  }
}
```

Then log it:

```json
{
  "process_type": "Custom",
  "enabled": true,
  "parameters": {
    "device": "context",
    "method": "LogValue",
    "key": "TestValue"
  }
}
```

### Example 2: Arithmetic Calculations

Store two values and calculate their product:

```json
// Step 1: Set voltage
{
  "device": "context",
  "method": "SetValue",
  "key": "Voltage",
  "value": "3.3",
  "type": "numeric"
}

// Step 2: Set current
{
  "device": "context",
  "method": "SetValue",
  "key": "Current",
  "value": "0.5",
  "type": "numeric"
}

// Step 3: Calculate power (V * I)
{
  "device": "context",
  "method": "Calculate",
  "resultKey": "Power",
  "key1": "Voltage",
  "key2": "Current",
  "operation": "*"
}

// Step 4: Log the result
{
  "device": "context",
  "method": "LogValue",
  "key": "Power"
}
```

**Expected Output:**
```
[ContextHandler] Set 'Voltage' = 3.3 (numeric)
[ContextHandler] Set 'Current' = 0.5 (numeric)
[ContextHandler] Calculated 'Power' = 1.65 (Voltage * Current)
[ContextHandler] Value 'Power' = 1.65 (from context.Calculate)
```

---

## 🎯 Testing with Hardware Commands (Future)

Once individual command handlers implement the `captureAs` parameter, you'll be able to capture real hardware measurements:

### Example: PSU Measurement (Future Implementation)

```json
{
  "device": "PSU1",
  "method": "MeasureVoltage",
  "captureAs": "ActualVoltage"
}
```

This would:
1. Measure voltage from PSU1
2. Store it in context with key "ActualVoltage"
3. You can then use it in conditionals or calculations

---

## 🔬 Inspecting the JSON Context File

After execution, check the `logs/` folder for files like:
```
logs/context_MyRecipe_1234567890.json
```

The JSON file contains:
```json
{
  "values": {
    "Temperature": {
      "type": "numeric",
      "numericValue": 25.5,
      "timestamp": "2025-01-15T10:30:45Z",
      "sourceDevice": "TEC1",
      "sourceMethod": "GetTemperature"
    },
    "Power": {
      "type": "numeric",
      "numericValue": 1.65,
      "timestamp": "2025-01-15T10:30:46Z",
      "sourceDevice": "context",
      "sourceMethod": "Calculate"
    }
  },
  "executionMetadata": {
    "startTime": 1234567890000,
    "endTime": 1234568000000,
    "stepsExecuted": 15,
    "valuesCaptured": 5
  }
}
```

---

## 🧪 Quick Test Recipe

Create a simple recipe with these steps to verify context is working:

1. **SetValue** - Store a test number
   - Device: `context`
   - Method: `SetValue`
   - Params: `key=TestNum`, `value=100`, `type=numeric`

2. **SetValue** - Store another number
   - Device: `context`
   - Method: `SetValue`
   - Params: `key=TestNum2`, `value=50`, `type=numeric`

3. **Calculate** - Add them
   - Device: `context`
   - Method: `Calculate`
   - Params: `resultKey=Sum`, `key1=TestNum`, `key2=TestNum2`, `operation=+`

4. **LogValue** - Show the result
   - Device: `context`
   - Method: `LogValue`
   - Params: `key=Sum`

**After execution**, check:
- ✅ Execution log shows: `Value 'Sum' = 150`
- ✅ Context summary shows: `Values Captured: 3`
- ✅ JSON file exists in `logs/` folder

---

## 🎓 Next Steps

1. **Test with context commands** - Verify basic storage/retrieval works
2. **Implement captureAs in handlers** - Add real hardware value capture
3. **Use in conditionals** - Make decisions based on captured values

Example conditional (once implemented):
```cpp
builder->AddAction("PSU1", "MeasureVoltage", {{"captureAs", "V"}});
builder->BeginIf("V", ComparisonType::GREATER_THAN, 3.5f);
    builder->AddAction("system", "Notify", {{"message", "Voltage too high!"}});
builder->EndIf();
```

---

## 📊 What Gets Logged

After every recipe execution, you'll see:
- ⏱️ **Execution duration** in milliseconds
- 📈 **Number of steps executed**
- 💾 **Number of values captured**
- 📋 **List of all captured values** with their current values
- 💿 **Path to JSON file** for detailed inspection

This gives you complete visibility into what data was captured during sequence execution!

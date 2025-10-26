# CaptureAs UI Integration - COMPLETE ✅

## Summary

Added **"Capture As"** fields to the Recipe Editor UI for PSU, TEC, and DataStore process widgets, allowing users to configure value capture directly in the UI instead of manually editing JSON files.

---

## ✅ Updated Process Widgets

### 1. **PSU Read Voltage** (`PSUReadVoltageConfigWidget`)
- **File**: `include/AppUI/RecipePage/ProcessConfigWidget.cpp` (line 1120-1131)
- **Field Added**: "Capture As" text field
- **Placeholder**: "Optional: Variable name to store last voltage reading"
- **Functionality**: Captures the last voltage reading from multiple reads

### 2. **PSU Read Current** (`PSUReadCurrentConfigWidget`)
- **File**: `include/AppUI/RecipePage/ProcessConfigWidget.cpp` (line 1170-1181)
- **Field Added**: "Capture As" text field
- **Placeholder**: "Optional: Variable name to store last current reading"
- **Functionality**: Captures the last current reading from multiple reads

### 3. **TEC Read Temperature** (`ReadTemperatureConfigWidget`)
- **File**: `include/AppUI/RecipePage/ProcessConfigWidget.cpp` (line 907-914)
- **Field Added**: "Capture As" text field
- **Placeholder**: "Optional: Variable name to store temperature"
- **Functionality**: Captures temperature reading from TEC device

### 4. **TEC Read Laser Current** (`ReadLaserCurrentConfigWidget`)
- **File**: `include/AppUI/RecipePage/ProcessConfigWidget.cpp` (line 992-999)
- **Field Added**: "Capture As" text field
- **Placeholder**: "Optional: Variable name to store laser current"
- **Functionality**: Captures laser current reading from TEC device

### 5. **DataStore Read** (`ReadDataStoreConfigWidget`)
- **File**: `include/AppUI/RecipePage/ProcessConfigWidget.cpp` (line 834-841)
- **Field Added**: "Capture As" text field
- **Placeholder**: "Optional: Variable name to store DataStore value"
- **Functionality**: Captures value read from DataStore channel

---

## ✅ Command Handler Updates

### TECCommandHandler
- **File**: `include/sequence/TECCommandHandler.cpp`
- **Methods Updated**:
  - `ReadTemperature` (line 109-118) - Captures temperature value
  - `ReadLaserCurrent` (line 220-229) - Captures laser current value
- **Capture Logic**: Checks for `captureAs` parameter and stores value with metadata

### ProcessSequenceFactory
- **File**: `include/recipe/ProcessSequenceFactory.cpp`
- **Builders Updated**:
  - `Read Temperature` (line 422-431) - Passes captureAs to action
  - `Read Laser Current` (line 448-457) - Passes captureAs to action
- **Logic**: Checks if `captureAs` exists in recipe parameters and includes it in action params

---

## 🎯 How It Works

### UI Flow
1. **User opens recipe** in Recipe Editor
2. **Selects a process** (e.g., "PSU Read Voltage")
3. **Sees "Capture As" field** in the configuration form
4. **Enters variable name** (e.g., "MeasuredVoltage")
5. **Saves recipe** - captureAs parameter is automatically saved to JSON

### Execution Flow
```
Recipe JSON → ProcessSequenceFactory → Command Handler → SequenceContext
                   ↓                         ↓                   ↓
         Passes captureAs          Checks captureAs        Stores value
                                   parameter               with metadata
```

### Example Recipe Step (Auto-generated)
```json
{
  "user_nickname": "PSU Read Voltage 4",
  "process_type": "PSU Read Voltage",
  "enabled": true,
  "parameters": {
    "psu_device": "PSU1",
    "read_count": "20",
    "delay_ms": "100",
    "captureAs": "MeasuredVoltage"  ← Automatically saved from UI
  }
}
```

---

## 🎨 UI Features

### Field Characteristics
- **Optional**: Users can leave blank if capture not needed
- **Placeholder text**: Shows example usage
- **Real-time validation**: Connected to `parameterChanged()` signal
- **Persistent**: Automatically saved and loaded with recipe

### User Experience
```
┌─────────────────────────────────────────┐
│ PSU Voltage Reading                     │
├─────────────────────────────────────────┤
│ PSU Device Name: [PSU1            ▼]    │
│                                          │
│ Reading Parameters                       │
│ Number of Reads: [20              ]    │
│ Delay (ms):      [100             ]    │
│                                          │
│ Context Capture                          │
│ Capture As:      [MeasuredVoltage ]    │  ← NEW FIELD
│                   ↑                      │
│     Optional: Variable name to store     │
│           last voltage reading           │
└─────────────────────────────────────────┘
```

---

## ✅ Benefits

1. **No Manual JSON Editing** - Users configure capture directly in UI
2. **Visual Guidance** - Placeholder text shows what to enter
3. **Type Safety** - Form validation ensures correct format
4. **Backward Compatible** - Old recipes without captureAs still work
5. **Consistent UX** - Same pattern across all measurement widgets

---

## 📊 Complete Integration

### Already Implemented (PSU - from previous work)
- ✅ PSUCommandHandler capture logic
- ✅ ProcessSequenceFactory PSU builders
- ✅ PSU widget UI fields (this update)

### Newly Completed (TEC & DataStore)
- ✅ TECCommandHandler capture logic (this update)
- ✅ ProcessSequenceFactory TEC builders (this update)
- ✅ TEC widget UI fields (this update)
- ✅ DataStore widget UI field (this update)

### Full Stack
```
┌─────────────────────────────────────────┐
│         Recipe Editor UI                 │ ← ✅ Capture As fields added
│    (ProcessConfigWidget)                 │
└────────────────┬────────────────────────┘
                 │ Saves to JSON
┌────────────────▼────────────────────────┐
│         Recipe JSON File                 │ ← ✅ captureAs parameter
└────────────────┬────────────────────────┘
                 │ Loaded by
┌────────────────▼────────────────────────┐
│     ProcessSequenceFactory               │ ← ✅ Passes captureAs
└────────────────┬────────────────────────┘
                 │ Creates action
┌────────────────▼────────────────────────┐
│       Command Handler                    │ ← ✅ Capture logic implemented
│  (PSU/TEC/DataStore)                     │
└────────────────┬────────────────────────┘
                 │ Stores value
┌────────────────▼────────────────────────┐
│      SequenceContext                     │ ← ✅ Value stored with metadata
└─────────────────────────────────────────┘
```

---

## 🚀 Ready to Use!

Users can now:
1. **Open any recipe** in the Recipe Editor
2. **Add PSU/TEC/DataStore read steps**
3. **Fill in the "Capture As" field** with their desired variable name
4. **Run the recipe** and see captured values in the execution summary
5. **Use captured values** in future conditional logic or calculations

The entire SequenceContext data capture system is now **fully integrated** from UI to execution! 🎉

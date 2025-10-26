# Sequence Engine Return Values - Proposal

## Executive Summary

This proposal extends the Sequence Engine to support **action return values** that can be:
1. **Recorded** into DataStore for logging and tracking
2. **Used for decision-making** in conditional branches during execution

## Current Architecture Analysis

### Current Command Execution Flow

```cpp
// Current: Commands only return success/failure
bool ICommandHandler::ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage  // Only error info
) = 0;
```

### Current Limitations

1. ❌ **No Return Values**: Commands can only return `bool` (success/failure)
2. ❌ **No Automatic Data Capture**: Cannot capture measurement results automatically
3. ❌ **Conditionals Not Implemented**: ConditionalBlock exists but always evaluates to `false` (placeholder at line 409)
4. ⚠️ **Manual DataStore Access**: Must create explicit DataStore commands to save values

### Current Workaround

```cpp
// Current approach requires multiple steps:
builder->AddAction("camera", "Capture", {});
builder->AddAction("camera", "GetIntensity", {});  // Stores to internal variable
builder->AddAction("datastore", "WriteValue", {{"channel", "Intensity"}, {"value", "???"}}); // Manual
```

## Proposed Solution

### Design Philosophy

- ✅ **Backward Compatible**: Existing commands continue to work
- ✅ **Opt-in**: Handlers choose whether to support return values
- ✅ **Flexible**: Supports numeric values, strings, and boolean results
- ✅ **Integrated**: Automatic DataStore capture with optional naming

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Action Execution                         │
│                                                             │
│  1. Execute Command                                         │
│     ├─→ Handler processes command                          │
│     └─→ Returns: (bool success, ReturnValue result)        │
│                                                             │
│  2. Capture Return Value (if specified)                     │
│     ├─→ Check if "returnAs" parameter exists               │
│     ├─→ Store value in DataStore[returnAs]                 │
│     └─→ Log value for tracking                             │
│                                                             │
│  3. Use in Conditionals                                     │
│     ├─→ Evaluate conditions using DataStore values         │
│     ├─→ Support comparisons (>, <, ==, !=, >=, <=)         │
│     └─→ Branch execution based on result                   │
└─────────────────────────────────────────────────────────────┘
```

## Implementation Details

### 1. Return Value Structure

```cpp
// New: Flexible return value container
struct CommandReturnValue {
    enum class Type {
        NONE,       // No return value
        NUMERIC,    // Numeric value (float/double)
        STRING,     // Text value
        BOOLEAN     // True/false
    };

    Type type = Type::NONE;
    float numericValue = 0.0f;
    std::string stringValue;
    bool booleanValue = false;

    // Constructors
    CommandReturnValue() : type(Type::NONE) {}
    CommandReturnValue(float value) : type(Type::NUMERIC), numericValue(value) {}
    CommandReturnValue(const std::string& value) : type(Type::STRING), stringValue(value) {}
    CommandReturnValue(bool value) : type(Type::BOOLEAN), booleanValue(value) {}

    // Conversion helpers
    float AsFloat() const { return numericValue; }
    bool AsBool() const {
        return type == Type::BOOLEAN ? booleanValue : (numericValue != 0.0f);
    }
    std::string AsString() const;
};
```

### 2. Enhanced Command Handler Interface

```cpp
// Enhanced interface with optional return value
class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;

    // NEW: Execute with return value support
    virtual bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        CommandReturnValue& returnValue  // ← NEW: Output parameter
    ) = 0;

    virtual std::string GetManagerType() const = 0;
    virtual void SetLogger(LogManager* logger) { m_logger = logger; }

protected:
    LogManager* m_logger = nullptr;
};
```

### 3. Enhanced Action Structure

```cpp
struct Action : public ISequenceStep {
    std::string deviceName;
    std::string methodName;
    std::map<std::string, std::string> params;

    // NEW: Optional return value capture
    std::string returnAsChannelName;  // If set, store return value in DataStore

    Action(const std::string& device, const std::string& method,
           const std::map<std::string, std::string>& p,
           const std::string& returnAs = "")
        : deviceName(device), methodName(method), params(p),
          returnAsChannelName(returnAs) {}

    Type GetType() const override { return Type::ACTION; }
};
```

### 4. Enhanced Sequence Builder

```cpp
class SequenceBuilder {
public:
    // Existing method (unchanged)
    void AddAction(
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params = {}
    );

    // NEW: Action with return value capture
    void AddActionWithReturn(
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        const std::string& returnAsChannelName  // Store result here
    );

    // Enhanced conditionals
    void BeginIf(
        const std::string& channelName,     // DataStore channel to check
        const std::string& comparison,      // ">", "<", "==", "!=", ">=", "<="
        float threshold                     // Value to compare against
    );

    // Alternative: Boolean channel check
    void BeginIfTrue(const std::string& channelName);
    void BeginIfFalse(const std::string& channelName);
};
```

### 5. Enhanced Conditional Block

```cpp
struct ConditionalBlock : public ISequenceStep {
    enum class ComparisonType {
        GREATER_THAN,      // >
        LESS_THAN,         // <
        EQUAL,             // ==
        NOT_EQUAL,         // !=
        GREATER_OR_EQUAL,  // >=
        LESS_OR_EQUAL,     // <=
        IS_TRUE,           // Boolean true
        IS_FALSE           // Boolean false
    };

    std::string channelName;           // DataStore channel to evaluate
    ComparisonType comparison;
    float threshold;

    std::vector<std::shared_ptr<ISequenceStep>> thenActions;
    std::vector<std::shared_ptr<ISequenceStep>> elseActions;

    ConditionalBlock(const std::string& channel, ComparisonType comp, float thresh)
        : channelName(channel), comparison(comp), threshold(thresh) {}

    Type GetType() const override { return Type::CONDITIONAL; }
};
```

### 6. Execution Logic Updates

```cpp
bool SequenceEngine::ExecuteStep(ISequenceStep* step) {
    if (!step) return false;

    switch (step->GetType()) {
    case ISequenceStep::Type::ACTION: {
        Action* action = static_cast<Action*>(step);

        // Execute command
        void* manager = ResolveManager(action->deviceName);
        std::string managerType = ResolveManagerType(action->deviceName);

        std::string errorMessage;
        CommandReturnValue returnValue;  // NEW: Capture return value

        if (!m_dispatcher.ExecuteCommand(managerType, manager,
                                        action->deviceName,
                                        action->methodName,
                                        action->params,
                                        errorMessage,
                                        returnValue)) {  // NEW parameter
            LogError("Command failed: " + errorMessage);
            return false;
        }

        // NEW: Auto-capture return value to DataStore if requested
        if (!action->returnAsChannelName.empty() &&
            returnValue.type != CommandReturnValue::Type::NONE) {

            float valueToStore = returnValue.AsFloat();
            m_context->dataStore->SetValue(action->returnAsChannelName, valueToStore);

            LogInfo("Captured return value: " + action->returnAsChannelName +
                   " = " + returnValue.AsString());
        }

        break;
    }

    case ISequenceStep::Type::CONDITIONAL: {
        ConditionalBlock* cond = static_cast<ConditionalBlock*>(step);

        // NEW: Evaluate condition from DataStore
        float value = 0.0f;
        bool hasValue = m_context->dataStore->TryGetValue(cond->channelName, value);

        if (!hasValue) {
            LogWarning("Condition channel not found: " + cond->channelName +
                      ", defaulting to FALSE");
        }

        bool conditionResult = EvaluateCondition(value, cond->comparison, cond->threshold);

        LogInfo("Condition [" + cond->channelName + " " +
               ComparisonToString(cond->comparison) + " " +
               std::to_string(cond->threshold) + "] = " +
               (conditionResult ? "TRUE" : "FALSE"));

        // Execute appropriate branch
        auto& branch = conditionResult ? cond->thenActions : cond->elseActions;
        for (const auto& action : branch) {
            if (!ExecuteStep(action.get())) {
                return false;
            }
        }
        break;
    }
    }

    return true;
}
```

## Usage Examples

### Example 1: Capture Measurement Result

```cpp
// Measure voltage and store result
builder->AddActionWithReturn(
    "PSU1",                    // Device
    "MeasureVoltage",          // Method
    {},                        // No params
    "MeasuredVoltage"          // Store result in DataStore["MeasuredVoltage"]
);

// Log the captured value
builder->AddAction(
    "datastore",
    "ReadAndLog",
    {{"channelName", "MeasuredVoltage"}}
);
```

### Example 2: Conditional Execution Based on Measurement

```cpp
// Measure temperature
builder->AddActionWithReturn(
    "laser-main",              // TEC device
    "GetTemperature",
    {},
    "CurrentTemp"              // Store in DataStore["CurrentTemp"]
);

// Check if temperature is in range (20-30°C)
builder->BeginIf("CurrentTemp", ">", 30.0f);
    // Temperature too high
    builder->AddAction("system", "Notify",
        {{"title", "Warning"}, {"message", "Temperature too high!"}});
    builder->AddAction("laser-main", "SetTemperature", {{"value", "25"}});
builder->Else();
    builder->BeginIf("CurrentTemp", "<", 20.0f);
        // Temperature too low
        builder->AddAction("system", "Notify",
            {{"title", "Warning"}, {"message", "Temperature too low!"}});
        builder->AddAction("laser-main", "SetTemperature", {{"value", "25"}});
    builder->EndIf();
builder->EndIf();
```

### Example 3: Vision Inspection with Pass/Fail

```cpp
// Capture image
builder->AddAction("camera", "Capture", {{"cameraId", "camera-top"}});

// Perform inspection (returns pass/fail as boolean)
builder->AddActionWithReturn(
    "vision",                  // Vision system handler
    "InspectDefects",
    {{"threshold", "0.95"}},
    "InspectionResult"         // Stores 1.0 (pass) or 0.0 (fail)
);

// Branch based on result
builder->BeginIf("InspectionResult", "==", 1.0f);
    // PASS branch
    builder->AddAction("system", "Notify",
        {{"title", "Inspection"}, {"message", "PASS"}});
    builder->AddAction("gripper-place", "Extend", {});
    builder->AddAction("hex-left", "MoveAbsolute", {{"position", "good-bin"}});
builder->Else();
    // FAIL branch
    builder->AddAction("system", "Notify",
        {{"title", "Inspection"}, {"message", "FAIL"}});
    builder->AddAction("gripper-place", "Extend", {});
    builder->AddAction("hex-left", "MoveAbsolute", {{"position", "reject-bin"}});
builder->EndIf();
```

### Example 4: Multi-Point Data Collection

```cpp
// Loop through measurement points
builder->BeginLoop(5);
    // Move to next position
    builder->AddAction("hex-left", "MoveAbsolute",
        {{"position", "scan-point-${i}"}});  // Use loop variable

    // Measure and store (unique name per iteration)
    builder->AddActionWithReturn(
        "PSU1",
        "MeasureCurrent",
        {},
        "Current_Point_${i}"   // Creates Current_Point_0, Current_Point_1, etc.
    );

    // Log measurement
    builder->AddAction("datastore", "ReadAndLog",
        {{"channelName", "Current_Point_${i}"}});
builder->EndLoop();

// After loop: All measurements are in DataStore and logged
```

## Handler Implementation Examples

### Example: Camera Handler with Return Value

```cpp
bool CameraCommandHandler::ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    CommandReturnValue& returnValue)  // NEW parameter
{
    CameraManager* cameraManager = static_cast<CameraManager*>(manager);

    if (methodName == "GetAverageBrightness") {
        auto cameraIdIt = params.find("cameraId");
        if (cameraIdIt == params.end()) {
            errorMessage = "Missing parameter: cameraId";
            return false;
        }

        // Capture and analyze frame
        float brightness = cameraManager->CalculateAverageBrightness(cameraIdIt->second);

        // NEW: Return the value
        returnValue = CommandReturnValue(brightness);

        if (m_logger) {
            m_logger->Info("Average brightness: " + std::to_string(brightness),
                          "CameraHandler");
        }
        return true;
    }

    // Other methods...
    return false;
}
```

### Example: TEC Handler with Return Value

```cpp
bool TECCommandHandler::ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    CommandReturnValue& returnValue)
{
    TECManager* tecManager = static_cast<TECManager*>(manager);

    if (methodName == "GetTemperature") {
        float temperature = tecManager->ReadTemperature(deviceName);

        // NEW: Return the temperature
        returnValue = CommandReturnValue(temperature);

        if (m_logger) {
            m_logger->Info(deviceName + " temperature: " +
                          std::to_string(temperature) + "°C",
                          "TECHandler");
        }
        return true;
    }

    if (methodName == "IsStable") {
        bool stable = tecManager->IsTemperatureStable(deviceName);

        // NEW: Return boolean
        returnValue = CommandReturnValue(stable);

        if (m_logger) {
            m_logger->Info(deviceName + " stability: " +
                          (stable ? "STABLE" : "UNSTABLE"),
                          "TECHandler");
        }
        return true;
    }

    // Other methods...
    return false;
}
```

## Benefits

### 1. Simplified Data Collection
**Before:**
```cpp
// Requires manual DataStore commands
builder->AddAction("PSU1", "Measure", {});
builder->AddAction("PSU1", "GetLastMeasurement", {});
builder->AddAction("datastore", "Write", {{"channel", "V"}, {"value", "???"}});
```

**After:**
```cpp
// One step - automatic capture
builder->AddActionWithReturn("PSU1", "MeasureVoltage", {}, "MeasuredVoltage");
```

### 2. Real Decision Making
- Conditionals now actually work (currently placeholder)
- Quality checks, pass/fail routing
- Adaptive processes based on measurements

### 3. Complete Data Logging
- All measurements automatically stored in DataStore
- Available for real-time charts via AuxWindow
- Complete execution trace for analysis

### 4. Recipe Intelligence
- Recipes can adapt to real-world conditions
- Temperature compensation
- Automatic retry on failure
- Multi-stage inspection with routing

## Migration Strategy

### Phase 1: Core Infrastructure
1. Add `CommandReturnValue` structure
2. Update `ICommandHandler` interface (keep old signature as deprecated)
3. Add `returnAsChannelName` to Action
4. Update `SequenceEngine::ExecuteStep()` to capture return values

### Phase 2: Conditional Implementation
1. Update `ConditionalBlock` structure
2. Implement `EvaluateCondition()` logic
3. Add comparison helper functions
4. Update SequenceBuilder API

### Phase 3: Handler Updates
1. Update DataStoreCommandHandler
2. Update CameraCommandHandler (add measurement methods)
3. Update TECCommandHandler (add getter methods)
4. Update PSUCommandHandler (add measurement methods)
5. Update IOCommandHandler (add input reading methods)

### Phase 4: Builder Enhancements
1. Add `AddActionWithReturn()` method
2. Update `BeginIf()` with new signature
3. Add helper methods: `BeginIfTrue()`, `BeginIfFalse()`
4. Add loop variable support (`${i}` substitution)

## Backward Compatibility

All existing code continues to work:
- Old `AddAction()` method unchanged
- Handlers can ignore `returnValue` parameter
- Conditionals can still use old signature (always false)
- No breaking changes to existing recipes

## Testing Plan

### Unit Tests
- CommandReturnValue conversions
- Condition evaluation logic
- DataStore auto-capture
- Loop variable substitution

### Integration Tests
- Camera measurement with branching
- TEC temperature monitoring
- PSU voltage/current logging
- Multi-device coordination

### Validation Recipes
- Quality inspection workflow
- Temperature-compensated process
- Data collection loop
- Multi-criteria decision tree

## Timeline Estimate

- **Phase 1** (Core): 2-3 days
- **Phase 2** (Conditionals): 1-2 days
- **Phase 3** (Handlers): 3-4 days (varies by handler)
- **Phase 4** (Builder): 1 day
- **Testing**: 2-3 days

**Total: ~10-13 days**

## Conclusion

This proposal adds critical capabilities to the Sequence Engine:
1. ✅ **Automatic value capture** from actions
2. ✅ **Real conditional execution** based on measurements
3. ✅ **Complete data logging** for analysis
4. ✅ **Backward compatible** with existing code
5. ✅ **Flexible and extensible** for future needs

This transforms the sequence engine from a simple command executor into an **intelligent process controller** capable of real-world decision-making and data-driven automation.

---

**Next Steps:**
1. Review and approve proposal
2. Create detailed implementation tasks
3. Begin Phase 1 development
4. Iterate based on feedback

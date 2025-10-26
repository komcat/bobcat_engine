# Sequence Execution Context - Design Document

## Problem Statement

We need data storage for sequence execution that is:
- ✅ **Scoped to execution lifecycle** (not global like DataStore)
- ✅ **Accessible by sequence actions** (read/write during execution)
- ✅ **Cleaned up after execution** (no pollution)
- ✅ **Supports return value capture** from actions
- ✅ **Enables conditional logic** based on captured data
- ✅ **Optionally exportable** to DataStore if needed

## Architectural Separation

### DataStore (Existing - Global)
```
Purpose:     Instrument data buffering for real-time monitoring
Lifetime:    Application lifetime (persistent)
Access:      Global through ApplicationContext
Use Cases:   - Live charts (voltage, current, temperature)
             - TCP data streaming
             - Real-time UI updates
             - Cross-program data sharing
```

### SequenceContext (NEW - Execution-Scoped)
```
Purpose:     Temporary data storage during sequence execution
Lifetime:    Single execution (created → executed → destroyed)
Access:      Passed to command handlers during execution
Use Cases:   - Action return values
             - Conditional evaluation
             - Loop iteration data
             - Execution metadata
             - Temporary calculations
```

## Class Design: SequenceContext

### Core Structure

```cpp
// SequenceContext.h
#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>

/**
 * @brief Execution-scoped data storage for sequence runs
 *
 * Stores temporary data during sequence execution including:
 * - Action return values
 * - Intermediate calculations
 * - Conditional evaluation results
 * - Execution metadata (timestamps, iteration counts, etc.)
 *
 * Lifetime: Created before execution, destroyed after completion
 * Thread-safe: Yes (for async execution support)
 */
class SequenceContext {
public:
    // Value types that can be stored
    enum class ValueType {
        NONE,
        NUMERIC,
        STRING,
        BOOLEAN
    };

    // Stored value container
    struct Value {
        ValueType type = ValueType::NONE;
        float numericValue = 0.0f;
        std::string stringValue;
        bool booleanValue = false;

        std::chrono::system_clock::time_point timestamp;
        std::string sourceDevice;   // Device that produced this value
        std::string sourceMethod;   // Method that produced this value

        // Constructors
        Value() : type(ValueType::NONE) {}
        Value(float v, const std::string& device = "", const std::string& method = "");
        Value(const std::string& v, const std::string& device = "", const std::string& method = "");
        Value(bool v, const std::string& device = "", const std::string& method = "");

        // Conversion helpers
        float AsFloat() const;
        bool AsBool() const;
        std::string AsString() const;
    };

    SequenceContext();
    ~SequenceContext();

    // ======================================================================
    // VALUE OPERATIONS
    // ======================================================================

    // Set values
    void SetValue(const std::string& key, float value,
                  const std::string& device = "", const std::string& method = "");
    void SetValue(const std::string& key, const std::string& value,
                  const std::string& device = "", const std::string& method = "");
    void SetValue(const std::string& key, bool value,
                  const std::string& device = "", const std::string& method = "");

    // Get values
    bool TryGetValue(const std::string& key, Value& outValue) const;
    Value GetValue(const std::string& key) const;
    float GetNumeric(const std::string& key, float defaultValue = 0.0f) const;
    std::string GetString(const std::string& key, const std::string& defaultValue = "") const;
    bool GetBoolean(const std::string& key, bool defaultValue = false) const;

    // Check existence
    bool HasValue(const std::string& key) const;
    ValueType GetValueType(const std::string& key) const;

    // List all keys
    std::vector<std::string> GetAllKeys() const;

    // Clear operations
    void ClearValue(const std::string& key);
    void ClearAll();

    // ======================================================================
    // EXECUTION METADATA
    // ======================================================================

    // Execution tracking
    void SetExecutionStartTime();
    void SetExecutionEndTime();
    std::chrono::milliseconds GetExecutionDuration() const;

    void SetSequenceName(const std::string& name);
    std::string GetSequenceName() const;

    void IncrementStepCount();
    int GetStepCount() const;

    void SetCurrentStep(int stepIndex, const std::string& stepName);
    int GetCurrentStepIndex() const;
    std::string GetCurrentStepName() const;

    // Loop tracking (for nested loop support)
    void PushLoopIteration(int iteration);
    void PopLoopIteration();
    int GetCurrentLoopIteration() const;
    int GetLoopDepth() const;

    // ======================================================================
    // DATASTORE EXPORT (Optional)
    // ======================================================================

    // Export selected values to global DataStore
    void ExportToDataStore(DataStore* dataStore, const std::string& key);
    void ExportAllToDataStore(DataStore* dataStore, const std::string& prefix = "");

    // ======================================================================
    // RESULT COLLECTION
    // ======================================================================

    // Get execution summary
    struct ExecutionSummary {
        std::string sequenceName;
        std::chrono::system_clock::time_point startTime;
        std::chrono::system_clock::time_point endTime;
        int totalSteps;
        std::map<std::string, Value> capturedValues;
    };

    ExecutionSummary GetSummary() const;

    // Serialize for logging/debugging
    std::string ToJson() const;
    void SaveToFile(const std::string& filepath) const;

private:
    mutable std::mutex m_mutex;

    // Data storage
    std::map<std::string, Value> m_values;

    // Execution metadata
    std::string m_sequenceName;
    std::chrono::system_clock::time_point m_startTime;
    std::chrono::system_clock::time_point m_endTime;
    int m_stepCount;
    int m_currentStepIndex;
    std::string m_currentStepName;

    // Loop tracking stack
    std::vector<int> m_loopIterationStack;
};
```

### Value Implementation

```cpp
// SequenceContext.cpp

SequenceContext::Value::Value(float v, const std::string& device, const std::string& method)
    : type(ValueType::NUMERIC)
    , numericValue(v)
    , booleanValue(v != 0.0f)
    , timestamp(std::chrono::system_clock::now())
    , sourceDevice(device)
    , sourceMethod(method)
{
    stringValue = std::to_string(v);
}

SequenceContext::Value::Value(const std::string& v, const std::string& device, const std::string& method)
    : type(ValueType::STRING)
    , stringValue(v)
    , timestamp(std::chrono::system_clock::now())
    , sourceDevice(device)
    , sourceMethod(method)
{
    // Try to parse as number
    try {
        numericValue = std::stof(v);
    } catch (...) {
        numericValue = 0.0f;
    }
    booleanValue = !v.empty() && v != "0" && v != "false";
}

SequenceContext::Value::Value(bool v, const std::string& device, const std::string& method)
    : type(ValueType::BOOLEAN)
    , numericValue(v ? 1.0f : 0.0f)
    , stringValue(v ? "true" : "false")
    , booleanValue(v)
    , timestamp(std::chrono::system_clock::now())
    , sourceDevice(device)
    , sourceMethod(method)
{
}

float SequenceContext::Value::AsFloat() const {
    return numericValue;
}

bool SequenceContext::Value::AsBool() const {
    return booleanValue;
}

std::string SequenceContext::Value::AsString() const {
    switch (type) {
        case ValueType::NUMERIC:
            return std::to_string(numericValue);
        case ValueType::STRING:
            return stringValue;
        case ValueType::BOOLEAN:
            return booleanValue ? "true" : "false";
        default:
            return "";
    }
}
```

## Integration with Sequence Engine

### Updated Command Handler Interface

```cpp
// ICommandHandler.h - Enhanced with SequenceContext

class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;

    // Enhanced execute with SequenceContext
    virtual bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context  // NEW: Execution context
    ) = 0;

    virtual std::string GetManagerType() const = 0;
    virtual void SetLogger(LogManager* logger) { m_logger = logger; }

protected:
    LogManager* m_logger = nullptr;
};
```

### Enhanced Action with Return Capture

```cpp
// SequenceEngine.h - Updated Action

struct Action : public ISequenceStep {
    std::string deviceName;
    std::string methodName;
    std::map<std::string, std::string> params;

    // NEW: Optional context key to store return value
    std::string captureReturnAs;  // If set, store return value in context

    Action(const std::string& device, const std::string& method,
           const std::map<std::string, std::string>& p,
           const std::string& captureAs = "")
        : deviceName(device), methodName(method), params(p),
          captureReturnAs(captureAs) {}

    Type GetType() const override { return Type::ACTION; }
};
```

### Enhanced Conditional with Context Evaluation

```cpp
// SequenceEngine.h - Updated ConditionalBlock

struct ConditionalBlock : public ISequenceStep {
    enum class ComparisonType {
        GREATER_THAN, LESS_THAN, EQUAL, NOT_EQUAL,
        GREATER_OR_EQUAL, LESS_OR_EQUAL,
        IS_TRUE, IS_FALSE, EXISTS, NOT_EXISTS
    };

    std::string contextKey;        // Key in SequenceContext to evaluate
    ComparisonType comparison;
    float threshold;

    std::vector<std::shared_ptr<ISequenceStep>> thenActions;
    std::vector<std::shared_ptr<ISequenceStep>> elseActions;

    ConditionalBlock(const std::string& key, ComparisonType comp, float thresh = 0.0f)
        : contextKey(key), comparison(comp), threshold(thresh) {}

    Type GetType() const override { return Type::CONDITIONAL; }
};
```

### SequenceEngine with Context

```cpp
// SequenceEngine.h - Updated

class SequenceEngine {
public:
    explicit SequenceEngine(ApplicationContext* context);
    ~SequenceEngine();

    // Execute with context
    bool ExecuteSequence(SequenceBuilder* sequence, SequenceContext* context);

    // Create new context for execution
    std::unique_ptr<SequenceContext> CreateContext();

    // ... existing methods ...

private:
    // Execute step with context
    bool ExecuteStep(ISequenceStep* step, SequenceContext* context);
    bool EvaluateCondition(const ConditionalBlock* cond, SequenceContext* context);

    ApplicationContext* m_context;
    // ... existing members ...
};
```

### Execution Implementation

```cpp
// SequenceEngine.cpp

bool SequenceEngine::ExecuteSequence(SequenceBuilder* sequence, SequenceContext* context) {
    if (!sequence) {
        LogError("Sequence is null");
        return false;
    }

    if (!context) {
        LogError("SequenceContext is null");
        return false;
    }

    // Initialize context
    context->SetSequenceName(sequence->GetName());
    context->SetExecutionStartTime();
    context->ClearAll();

    ClearStopFlag();

    // Validate
    std::vector<std::string> errors;
    if (!ValidateSequence(sequence, errors)) {
        for (const auto& err : errors) {
            LogError(err);
        }
        return false;
    }

    LogInfo("Executing sequence: " + sequence->GetName());

    try {
        for (size_t i = 0; i < sequence->GetSteps().size(); ++i) {
            if (m_stopRequested.load()) {
                LogWarning("Sequence stopped by user");
                return false;
            }

            context->SetCurrentStep(i, "Step_" + std::to_string(i));
            context->IncrementStepCount();

            if (!ExecuteStep(sequence->GetSteps()[i].get(), context)) {
                LogError("Step execution failed");
                return false;
            }
        }

        context->SetExecutionEndTime();
        LogInfo("Sequence completed in " +
               std::to_string(context->GetExecutionDuration().count()) + " ms");
        return true;
    }
    catch (const std::exception& e) {
        LogError("Exception: " + std::string(e.what()));
        return false;
    }
}

bool SequenceEngine::ExecuteStep(ISequenceStep* step, SequenceContext* context) {
    if (!step || !context) return false;

    if (m_stopRequested.load()) return false;

    switch (step->GetType()) {
    case ISequenceStep::Type::ACTION: {
        Action* action = static_cast<Action*>(step);

        LogInfo("Executing: " + action->deviceName + "." + action->methodName);

        void* manager = ResolveManager(action->deviceName);
        std::string managerType = ResolveManagerType(action->deviceName);

        if (managerType.empty()) {
            LogError("Unknown device: " + action->deviceName);
            return false;
        }

        std::string errorMessage;

        // Execute command with context
        if (!m_dispatcher.ExecuteCommand(managerType, manager,
                                        action->deviceName,
                                        action->methodName,
                                        action->params,
                                        errorMessage,
                                        context)) {  // Pass context
            LogError("Command failed: " + errorMessage);
            return false;
        }

        // Context capture is now handled inside the command handler

        break;
    }

    case ISequenceStep::Type::LOOP: {
        LoopBlock* loop = static_cast<LoopBlock*>(step);

        LogInfo("Starting loop: " + std::to_string(loop->count) + " iterations");

        for (int i = 0; i < loop->count; ++i) {
            if (m_stopRequested.load()) {
                LogWarning("Loop stopped at iteration " + std::to_string(i + 1));
                return false;
            }

            context->PushLoopIteration(i);

            LogInfo("  Iteration " + std::to_string(i + 1));

            for (const auto& action : loop->actions) {
                if (!ExecuteStep(action.get(), context)) {
                    return false;
                }
            }

            context->PopLoopIteration();
        }

        LogInfo("Loop completed");
        break;
    }

    case ISequenceStep::Type::CONDITIONAL: {
        ConditionalBlock* cond = static_cast<ConditionalBlock*>(step);

        bool result = EvaluateCondition(cond, context);

        LogInfo("Condition [" + cond->contextKey + "] = " +
               (result ? "TRUE" : "FALSE"));

        auto& branch = result ? cond->thenActions : cond->elseActions;
        for (const auto& action : branch) {
            if (!ExecuteStep(action.get(), context)) {
                return false;
            }
        }

        break;
    }
    }

    return true;
}

bool SequenceEngine::EvaluateCondition(const ConditionalBlock* cond,
                                      SequenceContext* context) {
    if (!cond || !context) return false;

    // Check if key exists
    if (!context->HasValue(cond->contextKey)) {
        if (cond->comparison == ConditionalBlock::ComparisonType::NOT_EXISTS) {
            return true;
        }
        LogWarning("Context key not found: " + cond->contextKey);
        return false;
    }

    if (cond->comparison == ConditionalBlock::ComparisonType::EXISTS) {
        return true;
    }

    float value = context->GetNumeric(cond->contextKey, 0.0f);

    switch (cond->comparison) {
        case ConditionalBlock::ComparisonType::GREATER_THAN:
            return value > cond->threshold;
        case ConditionalBlock::ComparisonType::LESS_THAN:
            return value < cond->threshold;
        case ConditionalBlock::ComparisonType::EQUAL:
            return std::abs(value - cond->threshold) < 0.0001f;
        case ConditionalBlock::ComparisonType::NOT_EQUAL:
            return std::abs(value - cond->threshold) >= 0.0001f;
        case ConditionalBlock::ComparisonType::GREATER_OR_EQUAL:
            return value >= cond->threshold;
        case ConditionalBlock::ComparisonType::LESS_OR_EQUAL:
            return value <= cond->threshold;
        case ConditionalBlock::ComparisonType::IS_TRUE:
            return context->GetBoolean(cond->contextKey, false);
        case ConditionalBlock::ComparisonType::IS_FALSE:
            return !context->GetBoolean(cond->contextKey, true);
        default:
            return false;
    }
}
```

## Command Handler Examples

### Camera Handler with Context

```cpp
// CameraCommandHandler.cpp

bool CameraCommandHandler::ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)  // NEW: Context parameter
{
    CameraManager* cameraManager = static_cast<CameraManager*>(manager);

    if (methodName == "GetAverageBrightness") {
        auto cameraIdIt = params.find("cameraId");
        auto captureAsIt = params.find("captureAs");  // Optional return key

        if (cameraIdIt == params.end()) {
            errorMessage = "Missing parameter: cameraId";
            return false;
        }

        float brightness = cameraManager->CalculateAverageBrightness(cameraIdIt->second);

        // Store in context if requested
        if (context && captureAsIt != params.end()) {
            context->SetValue(captureAsIt->second, brightness,
                            deviceName, methodName);

            if (m_logger) {
                m_logger->Info("Captured brightness as '" + captureAsIt->second +
                             "': " + std::to_string(brightness), "CameraHandler");
            }
        }

        return true;
    }

    // ... other methods
}
```

### PSU Handler with Context

```cpp
bool PSUCommandHandler::ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    PowerSupplyManager* psuManager = static_cast<PowerSupplyManager*>(manager);

    if (methodName == "MeasureVoltage") {
        auto captureAsIt = params.find("captureAs");

        float voltage = psuManager->measureVoltage(deviceName);

        // Auto-store in context
        if (context && captureAsIt != params.end()) {
            context->SetValue(captureAsIt->second, voltage, deviceName, methodName);

            if (m_logger) {
                m_logger->Info("Measured voltage: " + std::to_string(voltage) + " V, " +
                             "stored as '" + captureAsIt->second + "'", "PSUHandler");
            }
        }

        return true;
    }

    if (methodName == "MeasureCurrent") {
        auto captureAsIt = params.find("captureAs");

        float current = psuManager->measureCurrent(deviceName);

        if (context && captureAsIt != params.end()) {
            context->SetValue(captureAsIt->second, current, deviceName, methodName);
        }

        return true;
    }

    // ... other methods
}
```

### NEW: SequenceContext Handler

```cpp
// SequenceContextCommandHandler.h
#pragma once

#include "ICommandHandler.h"

/**
 * @brief Handler for direct SequenceContext operations
 *
 * Allows sequences to:
 * - Read values from context
 * - Write calculated values
 * - Export to DataStore
 * - Log context values
 */
class SequenceContextCommandHandler : public ICommandHandler {
public:
    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context
    ) override;

    std::string GetManagerType() const override { return "context"; }
};

// SequenceContextCommandHandler.cpp

bool SequenceContextCommandHandler::ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (!context) {
        errorMessage = "SequenceContext is null";
        return false;
    }

    if (methodName == "SetValue") {
        // Set a value in context
        auto keyIt = params.find("key");
        auto valueIt = params.find("value");

        if (keyIt == params.end() || valueIt == params.end()) {
            errorMessage = "Missing parameters: key, value";
            return false;
        }

        // Try to parse as number, otherwise store as string
        try {
            float numValue = std::stof(valueIt->second);
            context->SetValue(keyIt->second, numValue);
        } catch (...) {
            context->SetValue(keyIt->second, valueIt->second);
        }

        return true;
    }

    if (methodName == "LogValue") {
        // Log a value from context
        auto keyIt = params.find("key");

        if (keyIt == params.end()) {
            errorMessage = "Missing parameter: key";
            return false;
        }

        if (!context->HasValue(keyIt->second)) {
            errorMessage = "Key not found in context: " + keyIt->second;
            return false;
        }

        auto value = context->GetValue(keyIt->second);

        if (m_logger) {
            m_logger->Info("Context[" + keyIt->second + "] = " + value.AsString() +
                         " (from: " + value.sourceDevice + "." + value.sourceMethod + ")",
                         "SequenceContext");
        }

        return true;
    }

    if (methodName == "ExportToDataStore") {
        // Export context value to DataStore
        auto keyIt = params.find("key");
        auto dataStoreIt = params.find("dataStore");  // Pointer passed as param

        if (keyIt == params.end()) {
            errorMessage = "Missing parameter: key";
            return false;
        }

        // This would need ApplicationContext access - TBD
        // For now, handled externally after execution

        if (m_logger) {
            m_logger->Info("Export to DataStore requested: " + keyIt->second,
                         "SequenceContext");
        }

        return true;
    }

    if (methodName == "Calculate") {
        // Perform calculation: result = value1 op value2
        auto resultKeyIt = params.find("resultKey");
        auto key1It = params.find("key1");
        auto key2It = params.find("key2");
        auto operationIt = params.find("operation");  // +, -, *, /

        if (resultKeyIt == params.end() || key1It == params.end() ||
            key2It == params.end() || operationIt == params.end()) {
            errorMessage = "Missing parameters: resultKey, key1, key2, operation";
            return false;
        }

        float val1 = context->GetNumeric(key1It->second, 0.0f);
        float val2 = context->GetNumeric(key2It->second, 0.0f);
        float result = 0.0f;

        if (operationIt->second == "+") result = val1 + val2;
        else if (operationIt->second == "-") result = val1 - val2;
        else if (operationIt->second == "*") result = val1 * val2;
        else if (operationIt->second == "/") result = (val2 != 0.0f) ? val1 / val2 : 0.0f;
        else {
            errorMessage = "Unknown operation: " + operationIt->second;
            return false;
        }

        context->SetValue(resultKeyIt->second, result, "context", "Calculate");

        if (m_logger) {
            m_logger->Info("Calculated: " + resultKeyIt->second + " = " +
                         std::to_string(val1) + " " + operationIt->second + " " +
                         std::to_string(val2) + " = " + std::to_string(result),
                         "SequenceContext");
        }

        return true;
    }

    errorMessage = "Unknown method: " + methodName;
    return false;
}
```

## Integration with RecipeWorker/ProgramWorker

```cpp
// RecipeWorker.cpp - Enhanced

void RecipeWorker::run() {
    try {
        emit logMessage("Starting recipe execution...");

        // Create execution context
        auto context = std::make_unique<SequenceContext>();

        // Load recipe
        RecipeManager recipeManager;
        if (!recipeManager.LoadFromFile(m_recipeFilePath)) {
            emit executionCompleted(false, "Failed to load recipe");
            return;
        }

        // Build sequence
        SequenceBuilder* sequence = m_engine->CreateSequence("RecipeExecution");
        buildSequenceFromRecipe(sequence, &recipeManager);

        emit sequenceExecutionStarted();

        // Execute with context
        bool success = m_engine->ExecuteSequence(sequence, context.get());

        if (success) {
            // Export execution summary
            auto summary = context->GetSummary();

            emit logMessage("Execution completed in " +
                          QString::number(summary.endTime - summary.startTime) + " ms");
            emit logMessage("Total steps: " + QString::number(summary.totalSteps));
            emit logMessage("Captured values: " + QString::number(summary.capturedValues.size()));

            // Optional: Export to DataStore for persistence
            // context->ExportAllToDataStore(m_context->dataStore, "Recipe_");

            emit executionCompleted(true, "Recipe completed successfully");
        } else {
            emit executionCompleted(false, "Recipe execution failed");
        }

        delete sequence;

    } catch (const std::exception& e) {
        emit executionCompleted(false, QString("Exception: ") + e.what());
    }
}
```

## Usage Examples

### Example 1: Simple Measurement Capture

```cpp
builder->AddAction("PSU1", "MeasureVoltage", {
    {"captureAs", "V_Measured"}
});

builder->AddAction("context", "LogValue", {
    {"key", "V_Measured"}
});
```

### Example 2: Conditional Based on Measurement

```cpp
// Measure temperature
builder->AddAction("laser-main", "GetTemperature", {
    {"captureAs", "CurrentTemp"}
});

// Check threshold
builder->BeginIf("CurrentTemp", ConditionalBlock::ComparisonType::GREATER_THAN, 30.0f);
    builder->AddAction("system", "Notify", {
        {"title", "Warning"},
        {"message", "Temperature too high!"}
    });
builder->EndIf();
```

### Example 3: Multi-Point Data Collection with Context

```cpp
builder->BeginLoop(10);
    // Measure at each point
    builder->AddAction("PSU1", "MeasureVoltage", {
        {"captureAs", "V_Point_${i}"}  // Creates V_Point_0, V_Point_1, etc.
    });

    builder->AddAction("context", "LogValue", {
        {"key", "V_Point_${i}"}
    });
builder->EndLoop();

// After execution: context has V_Point_0 through V_Point_9
// Can export to DataStore if needed for persistence
```

### Example 4: Calculations in Context

```cpp
// Measure voltage and current
builder->AddAction("PSU1", "MeasureVoltage", {{"captureAs", "V"}});
builder->AddAction("PSU1", "MeasureCurrent", {{"captureAs", "I"}});

// Calculate power: P = V * I
builder->AddAction("context", "Calculate", {
    {"resultKey", "Power"},
    {"key1", "V"},
    {"key2", "I"},
    {"operation", "*"}
});

// Log result
builder->AddAction("context", "LogValue", {{"key", "Power"}});

// Use in conditional
builder->BeginIf("Power", ConditionalBlock::ComparisonType::GREATER_THAN, 10.0f);
    builder->AddAction("system", "Notify", {
        {"title", "Alert"},
        {"message", "Power exceeds 10W!"}
    });
builder->EndIf();
```

## Benefits of This Approach

1. ✅ **Clear Separation**: DataStore for instruments, SequenceContext for execution
2. ✅ **Lifecycle Management**: Context created/destroyed with execution
3. ✅ **No Pollution**: Temporary data doesn't clutter global DataStore
4. ✅ **Rich Metadata**: Timestamps, source tracking, execution stats
5. ✅ **Debugging**: Can save execution context to file for analysis
6. ✅ **Optional Export**: Can selectively export to DataStore when needed
7. ✅ **Thread-Safe**: Works with async execution (RecipeWorker/ProgramWorker)
8. ✅ **Extensible**: Easy to add new context operations

## Next Steps

1. Implement `SequenceContext` class
2. Update `ICommandHandler` interface
3. Update all existing handlers to accept context parameter
4. Implement `SequenceContextCommandHandler`
5. Update `SequenceEngine` execution logic
6. Update `RecipeWorker` and `ProgramWorker` integration
7. Add unit tests for context operations
8. Update documentation

What do you think of this approach?

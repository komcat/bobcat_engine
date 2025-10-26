# SequenceContext Usage Guide

## Overview

The SequenceContext system enables **return value capture** and **conditional logic** in sequence execution, transforming the sequence engine from a simple command executor into an intelligent process controller.

## Quick Start

### 1. Basic Usage in RecipeWorker

```cpp
// In RecipeWorker::run()
void RecipeWorker::run() {
    try {
        // Create execution context
        auto context = std::make_unique<SequenceContext>();

        // Build sequence
        SequenceBuilder* sequence = m_engine->CreateSequence("MyRecipe");
        buildSequenceFromRecipe(sequence, &recipeManager);

        // Execute with context
        bool success = m_engine->ExecuteSequence(sequence, context.get());

        if (success) {
            // Get execution summary
            auto summary = context->GetSummary();
            logMessage("Completed in " + QString::number(summary.endTime - summary.startTime) + " ms");

            // Optional: Export values to DataStore for persistence
            context->ExportToDataStore(m_context->dataStore, "FinalTemperature");
        }

        delete sequence;
    } catch (const std::exception& e) {
        emit executionCompleted(false, QString("Exception: ") + e.what());
    }
}
```

### 2. Capturing Values in Process Sequences

```cpp
// In ProcessSequenceFactory or sequence building code
void buildMeasurementSequence(SequenceBuilder* builder) {
    // Measure voltage and store in context
    builder->AddAction("PSU1", "MeasureVoltage", {
        {"captureAs", "V_Measured"}  // Stores result in context
    });

    // Measure current and store in context
    builder->AddAction("PSU1", "MeasureCurrent", {
        {"captureAs", "I_Measured"}
    });

    // Calculate power: P = V * I
    builder->AddAction("context", "Calculate", {
        {"resultKey", "Power"},
        {"key1", "V_Measured"},
        {"key2", "I_Measured"},
        {"operation", "*"}
    });

    // Log the calculated power
    builder->AddAction("context", "LogValue", {
        {"key", "Power"}
    });
}
```

### 3. Conditional Execution

```cpp
void buildTemperatureCheckSequence(SequenceBuilder* builder) {
    // Measure temperature
    builder->AddAction("laser-main", "GetTemperature", {
        {"captureAs", "CurrentTemp"}
    });

    // Check if temperature is too high
    builder->BeginIf("CurrentTemp",
                    ConditionalBlock::ComparisonType::GREATER_THAN,
                    30.0f);
        // Temperature > 30°C - take action
        builder->AddAction("system", "Notify", {
            {"title", "Warning"},
            {"message", "Temperature too high!"}
        });
        builder->AddAction("laser-main", "SetTemperature", {{"value", "25"}});
    builder->Else();
        // Temperature <= 30°C - normal operation
        builder->AddAction("system", "Notify", {
            {"title", "Info"},
            {"message", "Temperature normal"}
        });
    builder->EndIf();
}
```

## Command Handler Implementation

### Adding Context Support to Handlers

When implementing a new handler method that should support return value capture:

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

        // Perform measurement
        float voltage = psuManager->measureVoltage(deviceName);

        // Auto-capture to context if requested
        if (context && captureAsIt != params.end()) {
            context->SetValue(captureAsIt->second, voltage, deviceName, methodName);

            if (m_logger) {
                m_logger->Info("Measured " + std::to_string(voltage) + " V, " +
                             "stored as '" + captureAsIt->second + "'", "PSUHandler");
            }
        }

        return true;
    }

    // ... other methods
}
```

## Context Operations

### Built-in Context Commands

#### SetValue
```cpp
builder->AddAction("context", "SetValue", {
    {"key", "MyVariable"},
    {"value", "123.45"}
});
```

#### LogValue
```cpp
builder->AddAction("context", "LogValue", {
    {"key", "Temperature"}
});
// Output: Context[Temperature] = 25.5 (from: laser-main.GetTemperature)
```

#### Calculate
```cpp
builder->AddAction("context", "Calculate", {
    {"resultKey", "Sum"},
    {"key1", "Value1"},
    {"key2", "Value2"},
    {"operation", "+"}  // +, -, *, /
});
```

#### Exists
```cpp
builder->AddAction("context", "Exists", {
    {"key", "Temperature"},
    {"resultKey", "TempExists"}  // Stores boolean
});
```

#### Clear / ClearAll
```cpp
builder->AddAction("context", "Clear", {{"key", "OldValue"}});
builder->AddAction("context", "ClearAll", {});
```

## Conditional Types

### Numeric Comparisons
```cpp
builder->BeginIf("Temperature", ComparisonType::GREATER_THAN, 30.0f);
builder->BeginIf("Voltage", ComparisonType::LESS_THAN, 5.0f);
builder->BeginIf("Current", ComparisonType::EQUAL, 1.5f);
builder->BeginIf("Power", ComparisonType::NOT_EQUAL, 0.0f);
builder->BeginIf("Threshold", ComparisonType::GREATER_OR_EQUAL, 10.0f);
builder->BeginIf("Limit", ComparisonType::LESS_OR_EQUAL, 100.0f);
```

### Boolean Checks
```cpp
builder->BeginIf("DeviceReady", ComparisonType::IS_TRUE, 0.0f);
builder->BeginIf("ErrorFlag", ComparisonType::IS_FALSE, 0.0f);
```

### Existence Checks
```cpp
builder->BeginIf("OptionalValue", ComparisonType::EXISTS, 0.0f);
builder->BeginIf("RemovedKey", ComparisonType::NOT_EXISTS, 0.0f);
```

## Advanced Patterns

### Multi-Point Data Collection
```cpp
void buildScanSequence(SequenceBuilder* builder, int numPoints) {
    builder->BeginLoop(numPoints);
        // Move to scan position (would need loop variable substitution)
        builder->AddAction("hex-left", "MoveAbsolute", {
            {"position", "scan_" + std::to_string(i)}  // Would need enhancement
        });

        // Measure at this point
        builder->AddAction("PSU1", "MeasureVoltage", {
            {"captureAs", "Voltage_Point_" + std::to_string(i)}
        });

        // Log the measurement
        builder->AddAction("context", "LogValue", {
            {"key", "Voltage_Point_" + std::to_string(i)}
        });
    builder->EndLoop();
}
```

### Quality Inspection with Routing
```cpp
void buildInspectionSequence(SequenceBuilder* builder) {
    // Capture image
    builder->AddAction("camera", "Capture", {{"cameraId", "camera-top"}});

    // Perform vision inspection
    builder->AddAction("vision", "InspectDefects", {
        {"threshold", "0.95"},
        {"captureAs", "InspectionResult"}  // 1.0 = pass, 0.0 = fail
    });

    // Branch based on result
    builder->BeginIf("InspectionResult", ComparisonType::EQUAL, 1.0f);
        // PASS
        builder->AddAction("system", "Notify", {
            {"title", "Inspection"},
            {"message", "PASS"}
        });
        builder->AddAction("hex-left", "MoveAbsolute", {{"position", "good-bin"}});
    builder->Else();
        // FAIL
        builder->AddAction("system", "Notify", {
            {"title", "Inspection"},
            {"message", "FAIL"}
        });
        builder->AddAction("hex-left", "MoveAbsolute", {{"position", "reject-bin"}});
    builder->EndIf();
}
```

### Temperature Compensation
```cpp
void buildTemperatureCompensatedProcess(SequenceBuilder* builder) {
    // Measure ambient temperature
    builder->AddAction("laser-main", "GetTemperature", {
        {"captureAs", "AmbientTemp"}
    });

    // Adjust process based on temperature
    builder->BeginIf("AmbientTemp", ComparisonType::GREATER_THAN, 25.0f);
        // High temperature - reduce laser power
        builder->AddAction("laser-main", "SetLaserCurrent", {{"value", "45"}});
    builder->Else();
        builder->BeginIf("AmbientTemp", ComparisonType::LESS_THAN, 20.0f);
            // Low temperature - increase laser power
            builder->AddAction("laser-main", "SetLaserCurrent", {{"value", "55"}});
        builder->Else();
            // Normal temperature - standard power
            builder->AddAction("laser-main", "SetLaserCurrent", {{"value", "50"}});
        builder->EndIf();
    builder->EndIf();
}
```

## Exporting to DataStore

Context data is temporary (execution-scoped). To persist important values:

### Export Single Value
```cpp
// After execution
context->ExportToDataStore(dataStore, "FinalTemperature");
// Creates DataStore["FinalTemperature"] from Context["FinalTemperature"]
```

### Export All Values with Prefix
```cpp
context->ExportAllToDataStore(dataStore, "Recipe_");
// Context["Temperature"] → DataStore["Recipe_Temperature"]
// Context["Voltage"] → DataStore["Recipe_Voltage"]
// etc.
```

## Debugging and Logging

### Get Execution Summary
```cpp
auto summary = context->GetSummary();
std::cout << "Sequence: " << summary.sequenceName << std::endl;
std::cout << "Duration: " << (summary.endTime - summary.startTime) << " ms" << std::endl;
std::cout << "Steps: " << summary.totalSteps << std::endl;
std::cout << "Captured values: " << summary.capturedValues.size() << std::endl;

for (const auto& [key, value] : summary.capturedValues) {
    std::cout << "  " << key << " = " << value.AsString()
             << " (from " << value.sourceDevice << "." << value.sourceMethod << ")"
             << std::endl;
}
```

### Save to File
```cpp
// Save execution log as JSON
context->SaveToFile("logs/execution_" + timestamp + ".json");

// Or get JSON string
std::string json = context->ToJson();
```

## Best Practices

1. **Always create context for RecipeWorker/ProgramWorker execution**
   - Context is required, not optional

2. **Use meaningful key names**
   - Good: `"MeasuredVoltage"`, `"CurrentTemp"`, `"InspectionResult"`
   - Bad: `"v1"`, `"temp"`, `"result"`

3. **Export only necessary values to DataStore**
   - Context is temporary, DataStore is persistent
   - Export final results, not intermediate calculations

4. **Log important measurements**
   - Use `context.LogValue()` for visibility during execution

5. **Handle missing values in conditionals**
   - Use `EXISTS` check before numeric comparison if value might not exist

6. **Clear context between independent operations**
   - Use `ClearAll` if reusing context in loops

## Migration from Old System

### Before (Manual DataStore)
```cpp
builder->AddAction("PSU1", "Measure", {});
builder->AddAction("PSU1", "GetLastValue", {});
builder->AddAction("datastore", "Write", {{"channel", "V"}, {"value", "???"}});
```

### After (Automatic Context)
```cpp
builder->AddAction("PSU1", "MeasureVoltage", {{"captureAs", "V"}});
// Optional: Export to DataStore if needed for persistence
// context->ExportToDataStore(dataStore, "V");
```

## Troubleshooting

### Context is null
- Ensure RecipeWorker creates context before calling ExecuteSequence
- Pass context to ExecuteSequence, not nullptr

### Value not found in context
- Check that the capturing action executed successfully
- Verify the key name matches exactly (case-sensitive)
- Use `EXISTS` conditional to check before using

### Conditional always false
- Ensure the context key exists before comparison
- Check the value type (numeric vs boolean vs string)
- Use LogValue to inspect the actual value

### Handler doesn't capture value
- Verify handler checks for "captureAs" parameter
- Ensure handler calls `context->SetValue()` after operation
- Check that context pointer is not null

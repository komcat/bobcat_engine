// SequenceContextCommandHandler.cpp
#include "SequenceContextCommandHandler.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include <iostream>
#include <sstream>

SequenceContextCommandHandler::SequenceContextCommandHandler()
{
    std::cout << "[SequenceContextCommandHandler] Created" << std::endl;
}

bool SequenceContextCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (!context) {
        errorMessage = "SequenceContext is null";
        return false;
    }

    // =========================================================================
    // SetValue - Store a value in context
    // =========================================================================
    if (methodName == "SetValue") {
        auto keyIt = params.find("key");
        auto valueIt = params.find("value");

        if (keyIt == params.end() || valueIt == params.end()) {
            errorMessage = "Missing parameters: key, value";
            return false;
        }

        // Try to parse as number, otherwise store as string
        try {
            float numValue = std::stof(valueIt->second);
            context->SetValue(keyIt->second, numValue, "context", "SetValue");

            if (m_logger) {
                m_logger->Info("Set context[" + keyIt->second + "] = " + std::to_string(numValue),
                              "SequenceContext");
            }
        } catch (...) {
            context->SetValue(keyIt->second, valueIt->second, "context", "SetValue");

            if (m_logger) {
                m_logger->Info("Set context[" + keyIt->second + "] = \"" + valueIt->second + "\"",
                              "SequenceContext");
            }
        }

        return true;
    }

    // =========================================================================
    // SetNumeric - Store a numeric value, resolving context references
    // =========================================================================
    if (methodName == "SetNumeric") {
        auto keyIt = params.find("key");
        auto valueIt = params.find("value");

        if (keyIt == params.end() || valueIt == params.end()) {
            errorMessage = "Missing parameters: key, value";
            return false;
        }

        std::string valueStr = valueIt->second;
        float numValue = 0.0f;

        // Check if value is a context reference (e.g., "context:camera_initial_z")
        if (valueStr.find("context:") == 0) {
            std::string refKey = valueStr.substr(8);  // Remove "context:" prefix
            numValue = context->GetNumeric(refKey, 0.0f);

            if (m_logger) {
                m_logger->Info("Set context[" + keyIt->second + "] = context[" + refKey + "] = " + std::to_string(numValue),
                              "SequenceContext");
            }
        } else {
            // Parse as direct numeric value
            try {
                numValue = std::stof(valueStr);

                if (m_logger) {
                    m_logger->Info("Set context[" + keyIt->second + "] = " + std::to_string(numValue),
                                  "SequenceContext");
                }
            } catch (...) {
                errorMessage = "Invalid numeric value: " + valueStr;
                return false;
            }
        }

        context->SetValue(keyIt->second, numValue, "context", "SetNumeric");
        return true;
    }

    // =========================================================================
    // LogValue - Log a value from context
    // =========================================================================
    if (methodName == "LogValue") {
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

        std::ostringstream message;
        message << "Context[" << keyIt->second << "] = " << value.AsString();

        if (!value.sourceDevice.empty()) {
            message << " (from: " << value.sourceDevice;
            if (!value.sourceMethod.empty()) {
                message << "." << value.sourceMethod;
            }
            message << ")";
        }

        if (m_logger) {
            m_logger->Info(message.str(), "SequenceContext");
        } else {
            std::cout << "[SequenceContext] " << message.str() << std::endl;
        }

        return true;
    }

    // =========================================================================
    // Calculate - Perform arithmetic operations
    // =========================================================================
    if (methodName == "Calculate") {
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

        if (operationIt->second == "+") {
            result = val1 + val2;
        } else if (operationIt->second == "-") {
            result = val1 - val2;
        } else if (operationIt->second == "*") {
            result = val1 * val2;
        } else if (operationIt->second == "/") {
            if (val2 != 0.0f) {
                result = val1 / val2;
            } else {
                errorMessage = "Division by zero";
                return false;
            }
        } else {
            errorMessage = "Unknown operation: " + operationIt->second;
            return false;
        }

        context->SetValue(resultKeyIt->second, result, "context", "Calculate");

        if (m_logger) {
            std::ostringstream message;
            message << "Calculated: " << resultKeyIt->second << " = "
                   << val1 << " " << operationIt->second << " " << val2
                   << " = " << result;
            m_logger->Info(message.str(), "SequenceContext");
        }

        return true;
    }

    // =========================================================================
    // Clear - Clear a value from context
    // =========================================================================
    if (methodName == "Clear") {
        auto keyIt = params.find("key");

        if (keyIt == params.end()) {
            errorMessage = "Missing parameter: key";
            return false;
        }

        context->ClearValue(keyIt->second);

        if (m_logger) {
            m_logger->Info("Cleared context[" + keyIt->second + "]", "SequenceContext");
        }

        return true;
    }

    // =========================================================================
    // ClearAll - Clear all values from context
    // =========================================================================
    if (methodName == "ClearAll") {
        context->ClearAll();

        if (m_logger) {
            m_logger->Info("Cleared all context values", "SequenceContext");
        }

        return true;
    }

    // =========================================================================
    // Exists - Check if key exists (returns boolean to context)
    // =========================================================================
    if (methodName == "Exists") {
        auto keyIt = params.find("key");
        auto resultKeyIt = params.find("resultKey");

        if (keyIt == params.end() || resultKeyIt == params.end()) {
            errorMessage = "Missing parameters: key, resultKey";
            return false;
        }

        bool exists = context->HasValue(keyIt->second);
        context->SetValue(resultKeyIt->second, exists, "context", "Exists");

        if (m_logger) {
            m_logger->Info("Context[" + keyIt->second + "] exists: " + (exists ? "true" : "false"),
                          "SequenceContext");
        }

        return true;
    }

    // =========================================================================
    // CopyValue - Copy value from source key to destination key
    // =========================================================================
    if (methodName == "CopyValue") {
        auto sourceKeyIt = params.find("source_key");
        auto destKeyIt = params.find("dest_key");

        if (sourceKeyIt == params.end() || destKeyIt == params.end()) {
            errorMessage = "Missing parameters: source_key, dest_key";
            return false;
        }

        std::string sourceKey = sourceKeyIt->second;
        std::string destKey = destKeyIt->second;

        if (!context->HasValue(sourceKey)) {
            errorMessage = "Source key not found in context: " + sourceKey;
            return false;
        }

        // Get the source value and copy to destination
        auto value = context->GetValue(sourceKey);

        // Copy based on value type
        if (value.type == SequenceContext::ValueType::NUMERIC) {
            context->SetValue(destKey, value.numericValue, "context", "CopyValue");
        } else if (value.type == SequenceContext::ValueType::BOOLEAN) {
            context->SetValue(destKey, value.booleanValue, "context", "CopyValue");
        } else {
            context->SetValue(destKey, value.stringValue, "context", "CopyValue");
        }

        if (m_logger) {
            m_logger->Info("Copied context[" + sourceKey + "] -> context[" + destKey + "] = " + value.AsString(),
                          "SequenceContext");
        }

        return true;
    }

    errorMessage = "Unknown method: " + methodName;
    return false;
}

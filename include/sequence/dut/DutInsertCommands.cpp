// DutInsertCommands.cpp — see header for description.
#include "DutInsertCommands.h"

#include "../DUTCommandHandler.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "DataStore.h"
#include "dut/DeviceUnderTest.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include <nlohmann/json.hpp>

std::optional<bool> DutInsertCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const DutCommandDeps& deps)
{
  LogManager* m_logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // INSERT STRING
  // ============================================================================
  if (methodName == "InsertString") {
    if (!deps.parent->EnsureDUTActive(errorMessage)) {
      return false;
    }

    std::string key = deps.parent->GetParam(params, "key");
    std::string value = deps.parent->GetParam(params, "value");

    if (key.empty()) {
      errorMessage = "Missing required parameter: key";
      return false;
    }

    // Note: value can be empty string, which is valid

    if (!deps.app->currentDUT->insertEntry(key, value)) {
      errorMessage = "Failed to insert string entry for key: " + key;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Debug("DUT Insert: " + key + " = " + value, "DUT");
    }
    return true;
  }

  // ============================================================================
  // INSERT DOUBLE
  // ============================================================================
  if (methodName == "InsertDouble") {
    if (!deps.parent->EnsureDUTActive(errorMessage)) {
      return false;
    }

    std::string key = deps.parent->GetParam(params, "key");
    std::string valueStr = deps.parent->GetParam(params, "value");

    if (key.empty()) {
      errorMessage = "Missing required parameter: key";
      return false;
    }

    if (valueStr.empty()) {
      errorMessage = "Missing required parameter: value";
      return false;
    }

    try {
      double value = std::stod(valueStr);
      if (!deps.app->currentDUT->insertEntry(key, value)) {
        errorMessage = "Failed to insert double entry for key: " + key;
        if (m_logger) {
          m_logger->Error(errorMessage, "DUT");
        }
        return false;
      }

      if (m_logger) {
        m_logger->Debug("DUT Insert: " + key + " = " + std::to_string(value), "DUT");
      }
      return true;
    }
    catch (const std::invalid_argument&) {
      errorMessage = "Invalid double value: " + valueStr;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }
  }

  // ============================================================================
  // INSERT INTEGER
  // ============================================================================
  if (methodName == "InsertInteger") {
    if (!deps.parent->EnsureDUTActive(errorMessage)) {
      return false;
    }

    std::string key = deps.parent->GetParam(params, "key");
    std::string valueStr = deps.parent->GetParam(params, "value");

    if (key.empty()) {
      errorMessage = "Missing required parameter: key";
      return false;
    }

    if (valueStr.empty()) {
      errorMessage = "Missing required parameter: value";
      return false;
    }

    try {
      int64_t value = std::stoll(valueStr);
      if (!deps.app->currentDUT->insertEntry(key, value)) {
        errorMessage = "Failed to insert integer entry for key: " + key;
        if (m_logger) {
          m_logger->Error(errorMessage, "DUT");
        }
        return false;
      }

      if (m_logger) {
        m_logger->Debug("DUT Insert: " + key + " = " + std::to_string(value), "DUT");
      }
      return true;
    }
    catch (const std::invalid_argument&) {
      errorMessage = "Invalid integer value: " + valueStr;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }
  }

  // ============================================================================
  // INSERT FROM CONTEXT
  // ============================================================================
  if (methodName == "InsertFromContext") {
    if (!deps.parent->EnsureDUTActive(errorMessage)) {
      return false;
    }

    if (!context) {
      errorMessage = "No execution context available";
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    std::string entryKey = deps.parent->GetParam(params, "entry_key");
    std::string contextKey = deps.parent->GetParam(params, "context_key");
    std::string dataType = deps.parent->GetParam(params, "data_type");

    if (entryKey.empty()) {
      errorMessage = "Missing required parameter: entry_key";
      return false;
    }

    if (contextKey.empty()) {
      errorMessage = "Missing required parameter: context_key";
      return false;
    }

    // Default to auto-detection
    if (dataType.empty()) {
      dataType = "auto";
    }

    // Check if context key exists
    if (!context->HasValue(contextKey)) {
      errorMessage = "Context key not found: " + contextKey;
      if (m_logger) {
        m_logger->Error(errorMessage + " (Did you forget to set 'Capture As' in previous step?)", "DUT");
      }
      return false;
    }

    // Get value from context
    auto contextValue = context->GetValue(contextKey);

    // Auto-detect type or use specified type
    bool insertSuccess = false;
    if (dataType == "auto") {
      // Use context's native type
      if (contextValue.type == SequenceContext::ValueType::STRING) {
        insertSuccess = deps.app->currentDUT->insertEntry(entryKey, contextValue.stringValue);
        if (insertSuccess && m_logger) {
          m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + contextValue.stringValue + " (string)", "DUT");
        }
      }
      else if (contextValue.type == SequenceContext::ValueType::NUMERIC) {
        insertSuccess = deps.app->currentDUT->insertEntry(entryKey, static_cast<double>(contextValue.numericValue));
        if (insertSuccess && m_logger) {
          m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + std::to_string(contextValue.numericValue) + " (double)", "DUT");
        }
      }
      else if (contextValue.type == SequenceContext::ValueType::BOOLEAN) {
        // Store boolean as string "true"/"false"
        std::string boolStr = contextValue.booleanValue ? "true" : "false";
        insertSuccess = deps.app->currentDUT->insertEntry(entryKey, boolStr);
        if (insertSuccess && m_logger) {
          m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + boolStr + " (boolean as string)", "DUT");
        }
      }
      else {
        errorMessage = "Unknown value type in context for key: " + contextKey;
        return false;
      }
    }
    else if (dataType == "string") {
      insertSuccess = deps.app->currentDUT->insertEntry(entryKey, contextValue.AsString());
      if (insertSuccess && m_logger) {
        m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + contextValue.AsString() + " (forced string)", "DUT");
      }
    }
    else if (dataType == "double") {
      insertSuccess = deps.app->currentDUT->insertEntry(entryKey, static_cast<double>(contextValue.AsFloat()));
      if (insertSuccess && m_logger) {
        m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + std::to_string(contextValue.AsFloat()) + " (forced double)", "DUT");
      }
    }
    else if (dataType == "integer") {
      insertSuccess = deps.app->currentDUT->insertEntry(entryKey, static_cast<int64_t>(contextValue.AsFloat()));
      if (insertSuccess && m_logger) {
        m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + std::to_string(static_cast<int64_t>(contextValue.AsFloat())) + " (forced integer)", "DUT");
      }
    }
    else {
      errorMessage = "Invalid data_type: " + dataType + " (must be: auto, string, double, or integer)";
      return false;
    }

    if (!insertSuccess) {
      errorMessage = "Failed to insert entry from context for key: " + entryKey;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    return true;
  }

  // ============================================================================
  // BULK INSERT FROM CONTEXT
  // ============================================================================
  if (methodName == "BulkInsertFromContext") {
    if (!deps.parent->EnsureDUTActive(errorMessage)) {
      return false;
    }

    if (!context) {
      errorMessage = "No execution context available";
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    std::string prefix = deps.parent->GetParam(params, "prefix");
    std::string dataType = deps.parent->GetParam(params, "data_type");
    std::string entryName = deps.parent->GetParam(params, "entry_name");

    if (prefix.empty()) {
      errorMessage = "Missing required parameter: prefix";
      return false;
    }

    if (dataType.empty()) {
      dataType = "auto";
    }

    // Use prefix (without trailing dot) as default entry name
    if (entryName.empty()) {
      entryName = prefix;
      // Strip trailing dot if present
      if (!entryName.empty() && entryName.back() == '.') {
        entryName.pop_back();
      }
    }

    // Get all context keys and filter by prefix — build a single JSON object
    auto allKeys = context->GetAllKeys();
    nlohmann::json bulkObject = nlohmann::json::object();
    int fieldCount = 0;

    for (const auto& contextKey : allKeys) {
      // Check if key starts with prefix
      if (contextKey.size() < prefix.size() || contextKey.substr(0, prefix.size()) != prefix) {
        continue;
      }

      // Strip prefix to get the field name within the object
      std::string fieldName = contextKey.substr(prefix.size());
      if (fieldName.empty()) {
        continue;
      }

      // Get value from context
      auto contextValue = context->GetValue(contextKey);

      if (dataType == "auto") {
        if (contextValue.type == SequenceContext::ValueType::NUMERIC) {
          bulkObject[fieldName] = static_cast<double>(contextValue.numericValue);
        }
        else if (contextValue.type == SequenceContext::ValueType::STRING) {
          // Try to parse as number for cleaner output
          char* end = nullptr;
          double numVal = std::strtod(contextValue.stringValue.c_str(), &end);
          if (end != contextValue.stringValue.c_str() && *end == '\0' && !contextValue.stringValue.empty()) {
            bulkObject[fieldName] = numVal;
          }
          else {
            bulkObject[fieldName] = contextValue.stringValue;
          }
        }
        else if (contextValue.type == SequenceContext::ValueType::BOOLEAN) {
          bulkObject[fieldName] = contextValue.booleanValue;
        }
      }
      else if (dataType == "string") {
        bulkObject[fieldName] = contextValue.AsString();
      }
      else if (dataType == "double") {
        bulkObject[fieldName] = static_cast<double>(contextValue.AsFloat());
      }
      else if (dataType == "integer") {
        bulkObject[fieldName] = static_cast<int64_t>(contextValue.AsFloat());
      }

      ++fieldCount;
    }

    if (fieldCount == 0) {
      errorMessage = "No context keys found matching prefix: " + prefix;
      if (m_logger) {
        m_logger->Warning(errorMessage, "DUT");
      }
      return false;
    }

    // Insert as a single object entry
    if (!deps.app->currentDUT->insertEntry(entryName, bulkObject)) {
      errorMessage = "Failed to insert bulk object entry: " + entryName;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("DUT Bulk Insert: '" + entryName + "' with " + std::to_string(fieldCount) + " fields from prefix '" + prefix + "'", "DUT");
    }
    std::cout << "[DUTCommandHandler] Bulk inserted '" << entryName << "' with " << fieldCount << " fields from prefix '" << prefix << "'" << std::endl;

    return true;
  }

  // ============================================================================
  // INSERT ALL FROM DATASTORE
  // Snapshot every channel currently in the DataStore into one grouped DUT
  // entry (nlohmann::json object). Field name = channel name, value = float.
  // Soft no-op if no DUT is active — this lets recipes capture DataStore
  // snapshots without forcing every run to create a DUT first.
  // ============================================================================
  if (methodName == "InsertAllFromDataStore") {
    if (deps.app->currentDUT == nullptr) {
      if (m_logger) {
        m_logger->Info("DUT Insert from DataStore skipped: no active DUT", "DUT");
      }
      std::cout << "[DUTCommandHandler] InsertAllFromDataStore skipped (no active DUT)" << std::endl;
      return true;
    }

    if (!deps.app->dataStore) {
      errorMessage = "DataStore is not available";
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    std::string entryName = deps.parent->GetParam(params, "entry_name");
    std::string prefix = deps.parent->GetParam(params, "prefix");

    if (entryName.empty()) {
      entryName = "datastore";
    }

    auto channels = deps.app->dataStore->GetAvailableChannels();
    nlohmann::json bulkObject = nlohmann::json::object();
    int fieldCount = 0;

    for (const auto& channel : channels) {
      if (!prefix.empty()) {
        if (channel.size() < prefix.size() || channel.substr(0, prefix.size()) != prefix) {
          continue;
        }
      }

      float value = 0.0f;
      if (!deps.app->dataStore->TryGetValue(channel, value)) {
        continue;
      }

      std::string fieldName = channel;
      if (!prefix.empty()) {
        fieldName = channel.substr(prefix.size());
        if (fieldName.empty()) {
          continue;
        }
      }

      bulkObject[fieldName] = static_cast<double>(value);
      ++fieldCount;
    }

    if (fieldCount == 0) {
      errorMessage = prefix.empty()
        ? "DataStore is empty - no channels to insert"
        : "No DataStore channels found matching prefix: " + prefix;
      if (m_logger) {
        m_logger->Warning(errorMessage, "DUT");
      }
      return false;
    }

    if (!deps.app->currentDUT->insertEntry(entryName, bulkObject)) {
      errorMessage = "Failed to insert DataStore snapshot entry: " + entryName;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("DUT Insert from DataStore: '" + entryName + "' with " + std::to_string(fieldCount) + " channels", "DUT");
    }
    std::cout << "[DUTCommandHandler] Inserted DataStore snapshot '" << entryName << "' with " << fieldCount << " channels" << std::endl;

    return true;
  }

  return std::nullopt;  // not our family
}

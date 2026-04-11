// DUTCommandHandler.cpp
#include "DUTCommandHandler.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include "products/product_position_manager.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <nlohmann/json.hpp>

DUTCommandHandler::DUTCommandHandler(ApplicationContext* context)
  : m_context(context)
{
  if (!m_context) {
    std::cerr << "[DUTCommandHandler] Error: ApplicationContext cannot be null" << std::endl;
    m_valid = false;
    return;
  }
  std::cout << "[DUTCommandHandler] Created" << std::endl;
}

bool DUTCommandHandler::ExecuteCommand(
  void* /*manager*/,
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  // Route to appropriate method handler
  if (methodName == "Create") {
    return ExecuteCreate(params, errorMessage, context);
  }
  else if (methodName == "Load") {
    return ExecuteLoad(params, errorMessage, context);
  }
  else if (methodName == "LoadOrCreate") {
    return ExecuteLoadOrCreate(params, errorMessage, context);
  }
  else if (methodName == "Close") {
    return ExecuteClose(errorMessage);
  }
  else if (methodName == "InsertString") {
    return ExecuteInsertString(params, errorMessage);
  }
  else if (methodName == "InsertDouble") {
    return ExecuteInsertDouble(params, errorMessage);
  }
  else if (methodName == "InsertInteger") {
    return ExecuteInsertInteger(params, errorMessage);
  }
  else if (methodName == "InsertFromContext") {
    return ExecuteInsertFromContext(params, errorMessage, context);
  }
  else if (methodName == "BulkInsertFromContext") {
    return ExecuteBulkInsertFromContext(params, errorMessage, context);
  }
  else if (methodName == "DeleteEntry") {
    return ExecuteDeleteEntry(params, errorMessage);
  }
  else if (methodName == "DeleteEntriesByKey") {
    return ExecuteDeleteEntriesByKey(params, errorMessage);
  }

  errorMessage = "Unknown DUT method: " + methodName;
  return false;
}

bool DUTCommandHandler::ExecuteCreate(const std::map<std::string, std::string>& params, std::string& errorMessage, SequenceContext* context)
{
  std::string productName = GetParam(params, "productName");
  std::string serialNumber = GetParam(params, "serialNumber");

  // If productName is empty, try to use loaded product from ProductPositionManager
  if (productName.empty() && m_context->productPositionManager) {
    productName = m_context->productPositionManager->GetCurrentProductName();
    if (!productName.empty()) {
      if (m_logger) {
        m_logger->Debug("Using loaded product name from Position Manager: " + productName, "DUT");
      }
      std::cout << "[DUTCommandHandler] Auto-using product: " << productName << std::endl;
    }
  }

  if (productName.empty()) {
    errorMessage = "Missing product name. Either set 'productName' parameter or load a Product Position File.";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  // If serialNumber is empty, try to read from context (set by AskForInput)
  if (serialNumber.empty() && context) {
    std::string contextSerialNumber = context->GetString("serialNumber", "");
    if (!contextSerialNumber.empty()) {
      serialNumber = contextSerialNumber;
      if (m_logger) {
        m_logger->Info("Using serial number from context: " + serialNumber, "DUT");
      }
    }
  }

  // Final check - serial number must be provided
  if (serialNumber.empty()) {
    errorMessage = "Missing serial number. Please provide serialNumber parameter or use AskForInput to prompt user.";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  // Close existing DUT if one is active
  if (m_context->currentDUT != nullptr) {
    std::cout << "[DUTCommandHandler] Auto-closing existing DUT before creating new one" << std::endl;
    delete m_context->currentDUT;
    m_context->currentDUT = nullptr;
  }

  // Create new DUT and store in context
  auto dutOpt = DeviceUnderTest::create(productName, serialNumber);
  if (!dutOpt.has_value()) {
    errorMessage = "Failed to create DUT: " + productName + " / " + serialNumber;
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  m_context->currentDUT = new DeviceUnderTest(std::move(dutOpt.value()));

  if (m_logger) {
    m_logger->Info("Created DUT: " + productName + " / " + serialNumber, "DUT");
  }
  std::cout << "[DUTCommandHandler] Created DUT: " << productName << " / " << serialNumber << std::endl;
  return true;
}

bool DUTCommandHandler::ExecuteLoad(const std::map<std::string, std::string>& params, std::string& errorMessage, SequenceContext* context)
{
  std::string productName = GetParam(params, "productName");
  std::string serialNumber = GetParam(params, "serialNumber");

  // If productName is empty, try to use loaded product from ProductPositionManager
  if (productName.empty() && m_context->productPositionManager) {
    productName = m_context->productPositionManager->GetCurrentProductName();
    if (!productName.empty()) {
      if (m_logger) {
        m_logger->Debug("Using loaded product name from Position Manager: " + productName, "DUT");
      }
      std::cout << "[DUTCommandHandler] Auto-using product: " << productName << std::endl;
    }
  }

  if (productName.empty()) {
    errorMessage = "Missing product name. Either set 'productName' parameter or load a Product Position File.";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  // If serialNumber is empty, try to read from context (set by AskForInput)
  if (serialNumber.empty() && context) {
    std::string contextSerialNumber = context->GetString("serialNumber", "");
    if (!contextSerialNumber.empty()) {
      serialNumber = contextSerialNumber;
      if (m_logger) {
        m_logger->Info("Using serial number from context: " + serialNumber, "DUT");
      }
    }
  }

  // Final check - serial number must be provided
  if (serialNumber.empty()) {
    errorMessage = "Missing serial number. Please provide serialNumber parameter or use AskForInput to prompt user.";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  // Close existing DUT if one is active
  if (m_context->currentDUT != nullptr) {
    std::cout << "[DUTCommandHandler] Auto-closing existing DUT before loading new one" << std::endl;
    delete m_context->currentDUT;
    m_context->currentDUT = nullptr;
  }

  // Load existing DUT and store in context
  auto dutOpt = DeviceUnderTest::load(productName, serialNumber);
  if (!dutOpt.has_value()) {
    errorMessage = "Failed to load DUT: " + productName + " / " + serialNumber;
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  m_context->currentDUT = new DeviceUnderTest(std::move(dutOpt.value()));

  if (m_logger) {
    m_logger->Info("Loaded DUT: " + productName + " / " + serialNumber +
                   " (" + std::to_string(m_context->currentDUT->getEntryCount()) + " entries)", "DUT");
  }
  std::cout << "[DUTCommandHandler] Loaded DUT: " << productName << " / " << serialNumber
            << " (" << m_context->currentDUT->getEntryCount() << " entries)" << std::endl;
  return true;
}

bool DUTCommandHandler::ExecuteLoadOrCreate(const std::map<std::string, std::string>& params, std::string& errorMessage, SequenceContext* context)
{
  std::string productName = GetParam(params, "productName");
  std::string serialNumber = GetParam(params, "serialNumber");

  // If productName is empty, try to use loaded product from ProductPositionManager
  if (productName.empty() && m_context->productPositionManager) {
    productName = m_context->productPositionManager->GetCurrentProductName();
    if (!productName.empty()) {
      if (m_logger) {
        m_logger->Debug("Using loaded product name from Position Manager: " + productName, "DUT");
      }
      std::cout << "[DUTCommandHandler] Auto-using product: " << productName << std::endl;
    }
  }

  if (productName.empty()) {
    errorMessage = "Missing product name. Either set 'productName' parameter or load a Product Position File.";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  // If serialNumber is empty, try to read from context (set by AskForInput)
  if (serialNumber.empty() && context) {
    std::string contextSerialNumber = context->GetString("serialNumber", "");
    if (!contextSerialNumber.empty()) {
      serialNumber = contextSerialNumber;
      if (m_logger) {
        m_logger->Info("Using serial number from context: " + serialNumber, "DUT");
      }
    }
  }

  // Final check - serial number must be provided
  if (serialNumber.empty()) {
    errorMessage = "Missing serial number. Please provide serialNumber parameter or use AskForInput to prompt user.";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  // Close existing DUT if one is active
  if (m_context->currentDUT != nullptr) {
    std::cout << "[DUTCommandHandler] Auto-closing existing DUT before loading/creating new one" << std::endl;
    delete m_context->currentDUT;
    m_context->currentDUT = nullptr;
  }

  // Check if DUT file exists
  if (DeviceUnderTest::exists(productName, serialNumber)) {
    // Load existing DUT (preserves all previous data)
    auto dutOpt = DeviceUnderTest::load(productName, serialNumber);
    if (!dutOpt.has_value()) {
      errorMessage = "Failed to load existing DUT: " + productName + " / " + serialNumber;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    m_context->currentDUT = new DeviceUnderTest(std::move(dutOpt.value()));

    if (m_logger) {
      m_logger->Info("Loaded existing DUT: " + productName + " / " + serialNumber +
                     " (" + std::to_string(m_context->currentDUT->getEntryCount()) + " existing entries - new data will be appended)", "DUT");
    }
    std::cout << "[DUTCommandHandler] Loaded existing DUT: " << productName << " / " << serialNumber
              << " (" << m_context->currentDUT->getEntryCount() << " existing entries)" << std::endl;
  }
  else {
    // Create new DUT (file doesn't exist yet)
    auto dutOpt = DeviceUnderTest::create(productName, serialNumber);
    if (!dutOpt.has_value()) {
      errorMessage = "Failed to create new DUT: " + productName + " / " + serialNumber;
      if (m_logger) {
        m_logger->Error(errorMessage, "DUT");
      }
      return false;
    }

    m_context->currentDUT = new DeviceUnderTest(std::move(dutOpt.value()));

    if (m_logger) {
      m_logger->Info("Created new DUT: " + productName + " / " + serialNumber, "DUT");
    }
    std::cout << "[DUTCommandHandler] Created new DUT: " << productName << " / " << serialNumber << std::endl;
  }

  return true;
}

bool DUTCommandHandler::ExecuteClose(std::string& errorMessage)
{
  if (m_context->currentDUT == nullptr) {
    errorMessage = "No active DUT to close";
    if (m_logger) {
      m_logger->Warning(errorMessage, "DUT");
    }
    return false;
  }

  std::string serialNumber = m_context->currentDUT->getSerialNumber();
  std::string productName = m_context->currentDUT->getProductName();

  delete m_context->currentDUT;
  m_context->currentDUT = nullptr;

  if (m_logger) {
    m_logger->Info("Closed DUT: " + productName + " / " + serialNumber, "DUT");
  }
  std::cout << "[DUTCommandHandler] Closed DUT: " << productName << " / " << serialNumber << std::endl;
  return true;
}

bool DUTCommandHandler::ExecuteInsertString(const std::map<std::string, std::string>& params, std::string& errorMessage)
{
  if (!EnsureDUTActive(errorMessage)) {
    return false;
  }

  std::string key = GetParam(params, "key");
  std::string value = GetParam(params, "value");

  if (key.empty()) {
    errorMessage = "Missing required parameter: key";
    return false;
  }

  // Note: value can be empty string, which is valid

  if (!m_context->currentDUT->insertEntry(key, value)) {
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

bool DUTCommandHandler::ExecuteInsertDouble(const std::map<std::string, std::string>& params, std::string& errorMessage)
{
  if (!EnsureDUTActive(errorMessage)) {
    return false;
  }

  std::string key = GetParam(params, "key");
  std::string valueStr = GetParam(params, "value");

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
    if (!m_context->currentDUT->insertEntry(key, value)) {
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

bool DUTCommandHandler::ExecuteInsertInteger(const std::map<std::string, std::string>& params, std::string& errorMessage)
{
  if (!EnsureDUTActive(errorMessage)) {
    return false;
  }

  std::string key = GetParam(params, "key");
  std::string valueStr = GetParam(params, "value");

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
    if (!m_context->currentDUT->insertEntry(key, value)) {
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

bool DUTCommandHandler::ExecuteInsertFromContext(const std::map<std::string, std::string>& params, std::string& errorMessage, SequenceContext* context)
{
  if (!EnsureDUTActive(errorMessage)) {
    return false;
  }

  if (!context) {
    errorMessage = "No execution context available";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  std::string entryKey = GetParam(params, "entry_key");
  std::string contextKey = GetParam(params, "context_key");
  std::string dataType = GetParam(params, "data_type");

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
      insertSuccess = m_context->currentDUT->insertEntry(entryKey, contextValue.stringValue);
      if (insertSuccess && m_logger) {
        m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + contextValue.stringValue + " (string)", "DUT");
      }
    }
    else if (contextValue.type == SequenceContext::ValueType::NUMERIC) {
      insertSuccess = m_context->currentDUT->insertEntry(entryKey, static_cast<double>(contextValue.numericValue));
      if (insertSuccess && m_logger) {
        m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + std::to_string(contextValue.numericValue) + " (double)", "DUT");
      }
    }
    else if (contextValue.type == SequenceContext::ValueType::BOOLEAN) {
      // Store boolean as string "true"/"false"
      std::string boolStr = contextValue.booleanValue ? "true" : "false";
      insertSuccess = m_context->currentDUT->insertEntry(entryKey, boolStr);
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
    insertSuccess = m_context->currentDUT->insertEntry(entryKey, contextValue.AsString());
    if (insertSuccess && m_logger) {
      m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + contextValue.AsString() + " (forced string)", "DUT");
    }
  }
  else if (dataType == "double") {
    insertSuccess = m_context->currentDUT->insertEntry(entryKey, static_cast<double>(contextValue.AsFloat()));
    if (insertSuccess && m_logger) {
      m_logger->Debug("DUT Insert from context[" + contextKey + "]: " + entryKey + " = " + std::to_string(contextValue.AsFloat()) + " (forced double)", "DUT");
    }
  }
  else if (dataType == "integer") {
    insertSuccess = m_context->currentDUT->insertEntry(entryKey, static_cast<int64_t>(contextValue.AsFloat()));
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

bool DUTCommandHandler::ExecuteBulkInsertFromContext(const std::map<std::string, std::string>& params, std::string& errorMessage, SequenceContext* context)
{
  if (!EnsureDUTActive(errorMessage)) {
    return false;
  }

  if (!context) {
    errorMessage = "No execution context available";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  std::string prefix = GetParam(params, "prefix");
  std::string dataType = GetParam(params, "data_type");
  std::string entryName = GetParam(params, "entry_name");

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
  if (!m_context->currentDUT->insertEntry(entryName, bulkObject)) {
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

bool DUTCommandHandler::ExecuteDeleteEntry(const std::map<std::string, std::string>& params, std::string& errorMessage)
{
  if (!EnsureDUTActive(errorMessage)) {
    return false;
  }

  std::string timestamp = GetParam(params, "timestamp");

  if (timestamp.empty()) {
    errorMessage = "Missing required parameter: timestamp";
    return false;
  }

  bool deleted = m_context->currentDUT->deleteEntry(timestamp);

  if (!deleted) {
    errorMessage = "Entry not found with timestamp: " + timestamp;
    if (m_logger) {
      m_logger->Warning(errorMessage, "DUT");
    }
    return false;
  }

  if (m_logger) {
    m_logger->Debug("DUT Deleted entry: " + timestamp, "DUT");
  }
  return true;
}

bool DUTCommandHandler::ExecuteDeleteEntriesByKey(const std::map<std::string, std::string>& params, std::string& errorMessage)
{
  if (!EnsureDUTActive(errorMessage)) {
    return false;
  }

  std::string key = GetParam(params, "key");

  if (key.empty()) {
    errorMessage = "Missing required parameter: key";
    return false;
  }

  int deletedCount = m_context->currentDUT->deleteEntriesByKey(key);

  if (deletedCount == -1) {
    errorMessage = "Failed to delete entries by key (save error): " + key;
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }

  if (deletedCount == 0) {
    errorMessage = "No entries found with key: " + key;
    if (m_logger) {
      m_logger->Warning(errorMessage, "DUT");
    }
    return false;
  }

  if (m_logger) {
    m_logger->Debug("DUT Deleted " + std::to_string(deletedCount) + " entries with key: " + key, "DUT");
  }
  return true;
}

bool DUTCommandHandler::EnsureDUTActive(std::string& errorMessage) const
{
  if (m_context->currentDUT == nullptr) {
    errorMessage = "No active DUT. Create or load a DUT first.";
    if (m_logger) {
      m_logger->Error(errorMessage, "DUT");
    }
    return false;
  }
  return true;
}

std::string DUTCommandHandler::GetParam(const std::map<std::string, std::string>& params, const std::string& key) const
{
  auto it = params.find(key);
  if (it != params.end()) {
    return it->second;
  }
  return "";
}

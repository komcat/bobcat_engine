// DUTCommandHandler.h
#pragma once

#include "ICommandHandler.h"
#include "dut/DeviceUnderTest.h"
#include "context/ApplicationContext.h"
#include <string>
#include <map>

/**
 * @brief Command handler for Device Under Test (DUT) operations
 *
 * Manages DUT lifecycle and data entry operations through the sequence engine.
 *
 * Supported Methods:
 * - Create: Create new DUT (params: productName, serialNumber)
 * - Load: Load existing DUT (params: productName, serialNumber)
 * - Close: Close current DUT
 * - InsertString: Insert string value (params: key, value)
 * - InsertDouble: Insert double value (params: key, value)
 * - InsertInteger: Insert integer value (params: key, value)
 * - DeleteEntry: Delete entry by timestamp (params: timestamp)
 * - DeleteEntriesByKey: Delete all entries with key (params: key)
 *
 * Example usage in sequence:
 *   DUT.Create("Product-A", "SN-12345")
 *   DUT.InsertString("status", "PASS")
 *   DUT.InsertDouble("temperature", "25.5")
 *   DUT.Close()
 */
class DUTCommandHandler : public ICommandHandler
{
public:
  DUTCommandHandler(ApplicationContext* context);
  ~DUTCommandHandler() override = default;

  // Validity check
  bool IsValid() const { return m_valid; }

  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context
  ) override;

  std::string GetManagerType() const override { return "dut"; }

private:
  ApplicationContext* m_context;
  bool m_valid = true;

  // Command implementations
  bool ExecuteCreate(const std::map<std::string, std::string>& params, std::string& errorMessage, SequenceContext* context);
  bool ExecuteLoad(const std::map<std::string, std::string>& params, std::string& errorMessage, SequenceContext* context);
  bool ExecuteLoadOrCreate(const std::map<std::string, std::string>& params, std::string& errorMessage, SequenceContext* context);
  bool ExecuteClose(std::string& errorMessage);
  bool ExecuteInsertString(const std::map<std::string, std::string>& params, std::string& errorMessage);
  bool ExecuteInsertDouble(const std::map<std::string, std::string>& params, std::string& errorMessage);
  bool ExecuteInsertInteger(const std::map<std::string, std::string>& params, std::string& errorMessage);
  bool ExecuteInsertFromContext(const std::map<std::string, std::string>& params, std::string& errorMessage, SequenceContext* context);
  bool ExecuteDeleteEntry(const std::map<std::string, std::string>& params, std::string& errorMessage);
  bool ExecuteDeleteEntriesByKey(const std::map<std::string, std::string>& params, std::string& errorMessage);

  // Helper methods
  bool EnsureDUTActive(std::string& errorMessage) const;
  std::string GetParam(const std::map<std::string, std::string>& params, const std::string& key) const;
};

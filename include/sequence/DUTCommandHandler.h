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
 * The actual command logic lives in per-family helpers under
 * include/sequence/dut/ (Lifecycle, Insert, Delete); this class is a thin
 * dispatcher.
 *
 * Supported Methods:
 * - Create: Create new DUT (params: productName, serialNumber)
 * - Load: Load existing DUT (params: productName, serialNumber)
 * - LoadOrCreate: Load if exists, else create
 * - Close: Close current DUT
 * - InsertString: Insert string value (params: key, value)
 * - InsertDouble: Insert double value (params: key, value)
 * - InsertInteger: Insert integer value (params: key, value)
 * - InsertFromContext: Insert value from context (params: entry_key, context_key, data_type)
 * - BulkInsertFromContext: Insert all context vars matching prefix as single object (params: prefix, entry_name, data_type)
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

  // Cross-family utilities — public so per-family helpers under
  // include/sequence/dut/ can reach them via DutCommandDeps::parent->.
  bool EnsureDUTActive(std::string& errorMessage) const;
  std::string GetParam(const std::map<std::string, std::string>& params, const std::string& key) const;

private:
  ApplicationContext* m_context;
  bool m_valid = true;
};

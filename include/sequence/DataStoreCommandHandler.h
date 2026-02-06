// DataStoreCommandHandler.h
#pragma once

#include "ICommandHandler.h"
#include "DataStore.h"
#include <string>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

class DataStoreCommandHandler : public ICommandHandler
{
public:
  DataStoreCommandHandler(DataStore* dataStore);
  ~DataStoreCommandHandler() override = default;

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

  std::string GetManagerType() const override { return "datastore"; }

private:
  DataStore* m_dataStore;
  bool m_valid = true;

  std::string DetectUnit(const std::string& channelName);
  std::string FormatValueWithPrefix(float value, const std::string& unit);
};
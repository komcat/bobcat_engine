// MotionCommandHandler.h
#pragma once

#include "ICommandHandler.h"

// Forward declarations
class DataStore;
class ProductPositionManager;

class MotionCommandHandler : public ICommandHandler
{
public:
  bool ExecuteCommand(
    void* manager,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context
  ) override;

  std::string GetManagerType() const override { return "motion"; }

  // ✅ NEW: Set DataStore for XCAN command
  void SetDataStore(DataStore* dataStore) { m_dataStore = dataStore; }

  // ✅ NEW: Set ProductPositionManager for MoveToGraphNode command
  void SetProductPositionManager(ProductPositionManager* positionManager) { m_positionManager = positionManager; }

private:
  // Helper to resolve context: references or parse direct values
  double ResolveNumericValue(const std::string& valueStr, SequenceContext* context);

  // Target position for verification (optional)
  struct TargetPosition {
    bool hasX = false;
    bool hasY = false;
    bool hasZ = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
  };

  // Helper to wait for motion complete with safety stop checking and position verification
  // Returns false if stop requested, timeout, or position verification failed
  // If targetPos is nullptr, position verification is skipped
  bool WaitForMotionWithStopCheck(
    void* motionManager,
    const std::string& deviceName,
    SequenceContext* context,
    int timeoutMs = 30000,
    const TargetPosition* targetPos = nullptr);

  // ✅ NEW: DataStore for XCAN command
  DataStore* m_dataStore = nullptr;

  // ✅ NEW: ProductPositionManager for MoveToGraphNode command
  ProductPositionManager* m_positionManager = nullptr;
};
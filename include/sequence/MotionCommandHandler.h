// MotionCommandHandler.h
#pragma once

#include "ICommandHandler.h"
#include "SequenceEngine.h"  // For RetraceMotionInfo, RetraceEditCallback
#include <vector>
#include <functional>
#include <atomic>

// Forward declarations
class DataStore;
class ProductPositionManager;
class DeviceLinkManager;
class UnifiedMotionManager;

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

  // Set DeviceLinkManager for linked device commands
  void SetDeviceLinkManager(DeviceLinkManager* linkManager) { m_linkManager = linkManager; }

  // ============================================================================
  // RETRACE EDIT MODE
  // ============================================================================

  /**
   * @brief Enable/disable retrace edit mode
   *
   * When enabled, the handler will invoke the callback after each motion
   * that moves to a named position, allowing position adjustment.
   */
  void SetRetraceEditMode(bool enabled) { m_retraceEditMode.store(enabled); }
  bool IsRetraceEditMode() const { return m_retraceEditMode.load(); }

  /**
   * @brief Set the callback for retrace edit
   *
   * The callback is invoked after motion completes (for named position moves).
   * It should return true to continue, false to stop.
   */
  void SetRetraceEditCallback(RetraceEditCallback callback) { m_retraceEditCallback = callback; }

  /**
   * @brief Get retrace statistics
   */
  int GetRetracePositionsVisited() const { return m_retracePositionsVisited; }
  int GetRetracePositionsSaved() const { return m_retracePositionsSaved; }
  void ResetRetraceStatistics() { m_retracePositionsVisited = 0; m_retracePositionsSaved = 0; }

private:
  // Helper to resolve context: references or parse direct values
  double ResolveNumericValue(const std::string& valueStr, SequenceContext* context);

  // Target position for verification (optional)
  struct TargetPosition {
    bool hasX = false;
    bool hasY = false;
    bool hasZ = false;
    bool hasU = false;  // ✅ NEW: For hexapod rotation axes
    bool hasV = false;  // ✅ NEW: For hexapod rotation axes
    bool hasW = false;  // ✅ NEW: For hexapod rotation axes
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double u = 0.0;  // ✅ NEW: For hexapod rotation axes
    double v = 0.0;  // ✅ NEW: For hexapod rotation axes
    double w = 0.0;  // ✅ NEW: For hexapod rotation axes
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

  // ✅ NEW: Structure for parallel move specification
  struct ParallelMoveSpec {
    std::string deviceName;
    std::string moveType;      // "position" or "graphNode"
    std::string targetName;    // Named position or node name
    std::string graphName;     // Only for graphNode type
  };

  // ✅ NEW: Structure for parallel relative move specification
  struct ParallelRelativeMoveSpec {
    std::string deviceName;
    std::string axis;          // "X", "Y", "Z", "U", "V", "W"
    double distance;           // Distance to move
  };

  // ✅ NEW: Helper to wait for multiple motions to complete with safety stop checking
  // Returns false if stop requested, timeout, or any position verification failed
  bool WaitForMultipleMotionsWithStopCheck(
    void* motionManager,
    const std::vector<ParallelMoveSpec>& moves,
    SequenceContext* context,
    int timeoutMs = 60000);

  // ✅ NEW: Helper to wait for multiple relative motions to complete
  bool WaitForMultipleRelativeMotionsWithStopCheck(
    void* motionManager,
    const std::vector<std::string>& deviceNames,
    SequenceContext* context,
    int timeoutMs = 60000);

  // ✅ NEW: DataStore for XCAN command
  DataStore* m_dataStore = nullptr;

  // ✅ NEW: ProductPositionManager for MoveToGraphNode command
  ProductPositionManager* m_positionManager = nullptr;

  // DeviceLinkManager for linked device commands
  DeviceLinkManager* m_linkManager = nullptr;

  // ✅ Retrace edit mode
  std::atomic<bool> m_retraceEditMode{false};
  RetraceEditCallback m_retraceEditCallback;
  int m_retracePositionsVisited = 0;
  int m_retracePositionsSaved = 0;

  // ✅ Helper to invoke retrace edit callback after motion completes
  // Returns true to continue, false to stop execution
  bool InvokeRetraceEditCallback(
    void* motionManager,
    const std::string& deviceName,
    const std::string& positionName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    double originalX, double originalY, double originalZ,
    double originalU, double originalV, double originalW
  );
};
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
class ScanActivityTracker;

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

  // Set ScanActivityTracker for broadcasting scan-busy state to the TopBar indicator.
  // Null is OK — scan commands no-op the tracker calls if unset.
  void SetScanActivityTracker(ScanActivityTracker* tracker) { m_scanActivityTracker = tracker; }

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
  bool HasRetraceEditCallback() const { return static_cast<bool>(m_retraceEditCallback); }

  /**
   * @brief Get retrace statistics
   */
  int GetRetracePositionsVisited() const { return m_retracePositionsVisited; }
  int GetRetracePositionsSaved() const { return m_retracePositionsSaved; }
  void ResetRetraceStatistics() { m_retracePositionsVisited = 0; m_retracePositionsSaved = 0; }

  // ============================================================================
  // SHARED HELPERS (exposed for split helper classes under include/sequence/motion/)
  // These are public so the per-family helper classes (MotionBasicCommands,
  // MotionParallelCommands, etc.) can call back through deps.parent. Not part
  // of the external API — treat as internal.
  // ============================================================================

  // Helper to resolve context: references or parse direct values
  double ResolveNumericValue(const std::string& valueStr, SequenceContext* context);

  // Target position for verification (optional)
  struct TargetPosition {
    bool hasX = false;
    bool hasY = false;
    bool hasZ = false;
    bool hasU = false;  // For hexapod rotation axes
    bool hasV = false;
    bool hasW = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double u = 0.0;
    double v = 0.0;
    double w = 0.0;
  };

  // (ParallelMoveSpec / ParallelRelativeMoveSpec moved to motion/MotionParallelCommands.h
  //  in Step 10 cleanup — they have no orchestrator-side consumer anymore.)

  // Helper to wait for motion complete with safety stop checking and position verification
  // Returns false if stop requested, timeout, or position verification failed
  // If targetPos is nullptr, position verification is skipped
  bool WaitForMotionWithStopCheck(
    void* motionManager,
    const std::string& deviceName,
    SequenceContext* context,
    int timeoutMs = 30000,
    const TargetPosition* targetPos = nullptr);

  // Helper to wait for multiple relative motions to complete
  bool WaitForMultipleRelativeMotionsWithStopCheck(
    void* motionManager,
    const std::vector<std::string>& deviceNames,
    SequenceContext* context,
    int timeoutMs = 60000);

  // Helper to invoke retrace edit callback after motion completes
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

private:
  // DataStore for XCAN command
  DataStore* m_dataStore = nullptr;

  // ProductPositionManager for MoveToGraphNode command
  ProductPositionManager* m_positionManager = nullptr;

  // DeviceLinkManager for linked device commands
  DeviceLinkManager* m_linkManager = nullptr;

  // Scan-busy broadcaster for TopBar indicator (optional, no-op if null)
  ScanActivityTracker* m_scanActivityTracker = nullptr;

  // Retrace edit mode
  std::atomic<bool> m_retraceEditMode{false};
  RetraceEditCallback m_retraceEditCallback;
  int m_retracePositionsVisited = 0;
  int m_retracePositionsSaved = 0;
};
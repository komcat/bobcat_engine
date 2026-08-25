// RecipeWorker.h
#pragma once

#include <QThread>
#include <QString>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

class SequenceBuilder;
class SequenceEngine;
class RecipeManager;
class ProcessSequenceFactory;
class ProductPositionManager;
class UnifiedMotionManager;
class SequenceContext;  // ✅ Forward declaration for external context
struct PlacementRetraceParams;  // Defined in SystemCommandHandler.h
#include "../sequence/EdgeTransferRetrace.h"
#include "../logger/LogManager.h"  // LogManager::LogCallback (saved UI callback)

/**
 * @brief Worker thread for recipe execution (Self-contained)
 *
 * Handles EVERYTHING for executing a single recipe:
 * - Loading recipe file
 * - Loading product positions
 * - Building device registry
 * - UserPrompt callback setup
 * - Building and executing sequence
 *
 * This is the CORE executor - ProgramWorker uses this internally
 */
class RecipeWorker : public QThread
{
  Q_OBJECT

public:
  RecipeWorker(
    SequenceEngine* engine,
    ProcessSequenceFactory* factory,
    ProductPositionManager* positionManager,
    UnifiedMotionManager* motionManager,
    const std::string& recipeFilePath,
    QObject* parent = nullptr
  );

  ~RecipeWorker();  // Clear callbacks to prevent use-after-free

  void requestStop();  // Request graceful stop

  // Step-by-step execution control
  void setStepByStepMode(bool enabled);  // Enable/disable step-by-step mode
  void requestContinue();                 // Continue to next step
  void requestRunToEnd();                 // Continue running without pausing

  // Set breakpoints (called before starting execution)
  void setBreakpoints(const std::vector<bool>& breakpoints);

  // Set callback for user prompts (optional - if not set, auto-accepts all)
  using UserPromptCallback = std::function<bool(const std::string& title, const std::string& message)>;
  void setUserPromptCallback(UserPromptCallback callback);

  // Set callback for manual adjust (optional - if not set, auto-accepts all)
  using ManualAdjustCallback = std::function<bool(
    const std::string& message,
    const std::string& deviceName,
    double stepSizeUm,
    bool enableX,
    bool enableY,
    bool enableZ,
    const std::vector<std::string>& imagePaths
  )>;
  void setManualAdjustCallback(ManualAdjustCallback callback);

  // Set callback for user input (optional - if not set, returns empty string)
  using UserInputCallback = std::function<std::pair<bool, std::string>(
    const std::string& title,
    const std::string& prompt,
    const std::string& defaultValue
  )>;
  void setUserInputCallback(UserInputCallback callback);

  // Set callback for placement correction retrace (optional - if not set, auto-rejects)
  using PlacementRetraceCallback = std::function<bool(const PlacementRetraceParams& params)>;
  void setPlacementRetraceCallback(PlacementRetraceCallback callback);

  // Set callback for edge transfer retrace (optional - if not set, auto-rejects)
  using EdgeTransferRetraceCallback = std::function<EdgeTransferRetraceOutcome(const EdgeTransferRetraceParams& params)>;
  void setEdgeTransferRetraceCallback(EdgeTransferRetraceCallback callback);

  // ✅ NEW: Set external context (for sharing with ProgramWorker)
  // If set, this context will be used instead of creating a local one
  void setExternalContext(SequenceContext* context);

  // ✅ NEW: Set loop mode (reduces verbose logging during rapid iterations)
  void setLoopMode(bool enabled) { m_loopMode = enabled; }

  // ============================================================================
  // RETRACE EDIT MODE
  // ============================================================================

  /**
   * @brief Enable/disable retrace edit mode for this recipe execution
   *
   * When enabled, the recipe will pause after each motion command (that moves
   * to a named position) to allow position adjustment.
   */
  void setRetraceEditMode(bool enabled) { m_retraceEditMode = enabled; }
  bool isRetraceEditMode() const { return m_retraceEditMode; }

  /**
   * @brief Set the callback for retrace edit pause
   *
   * The callback is invoked after each motion completes to allow adjustment.
   */
  using RetraceEditCallback = std::function<bool(
    const std::string& deviceName,
    const std::string& positionName,
    double originalX, double originalY, double originalZ,
    double originalU, double originalV, double originalW,
    double currentX, double currentY, double currentZ,
    double currentU, double currentV, double currentW,
    bool& saveRequested
  )>;
  void setRetraceEditCallback(RetraceEditCallback callback) { m_retraceEditCallback = callback; }

  /**
   * @brief Get retrace edit statistics
   */
  int getRetracePositionsVisited() const { return m_retracePositionsVisited; }
  int getRetracePositionsSaved() const { return m_retracePositionsSaved; }

signals:
  // ✅ RAW DATA ONLY - No UI concepts
  void executionStarted(int totalEnabledSteps);
  void stepBuildStarted(int stepIndex, QString stepName, QString stepType);
  void stepBuildCompleted(int stepIndex);
  void sequenceExecutionStarted();
  void executionCompleted(bool success, QString message);
  void executionStopped();
  void logMessage(QString message);
  void waitProgress(int remainingMs, int totalMs);  // Wait/Delay countdown

  // Step execution events (from SequenceEngine pub-sub)
  void stepExecutionStarted(int stepIndex, QString stepName, QString stepType);
  void stepExecutionCompleted(int stepIndex, bool success);

  // Action execution event with look-ahead (granular - shows current and upcoming commands)
  void actionStarted(QString current, QString next, QString afterNext);

  // Vision result ready (for real-time UI updates)
  void visionResultReady(QString imagePath);

  // Breakpoint/pause events
  void executionPaused(int stepIndex, QString stepName, bool isBreakpoint);

  // ✅ Retrace edit events
  void retraceEditPause(QString deviceName, QString positionName,
                        double origX, double origY, double origZ,
                        double origU, double origV, double origW,
                        double currX, double currY, double currZ,
                        double currU, double currV, double currW);
  void retraceEditCompleted(int positionsVisited, int positionsSaved);

protected:
  void run() override;

private:
  bool buildSequenceFromRecipe(SequenceBuilder* sequence, RecipeManager* recipeManager);

  // Put back the LogManager UI callback this worker displaced when it started
  // forwarding log lines into the execution log. Idempotent - run() calls it on
  // both the success and the exception path, and the destructor calls it again
  // in case the worker was destroyed without run() completing.
  void restoreLogUICallback();

  SequenceEngine* m_engine;
  ProcessSequenceFactory* m_factory;
  ProductPositionManager* m_positionManager;
  UnifiedMotionManager* m_motionManager;
  std::string m_recipeFilePath;
  std::atomic<bool> m_stopRequested;
  UserPromptCallback m_userPromptCallback;
  ManualAdjustCallback m_manualAdjustCallback;
  UserInputCallback m_userInputCallback;
  PlacementRetraceCallback m_placementRetraceCallback;
  EdgeTransferRetraceCallback m_edgeTransferRetraceCallback;
  SequenceContext* m_externalContext = nullptr;  // ✅ NEW: External context from ProgramWorker
  std::vector<bool> m_breakpoints;               // Breakpoints for each enabled step
  bool m_loopMode = false;                       // ✅ NEW: Loop mode (reduces logging)

  // The application-wide LogManager UI callback (MainWindow's log status bar
  // feed) that this worker displaced while it forwards log lines into the
  // execution log. Restored on every exit path - see restoreLogUICallback().
  // m_logUICallbackSaved distinguishes "saved a null callback" from
  // "never installed ours", so restore is a no-op in the latter case.
  LogManager::LogCallback m_previousLogUICallback;
  std::atomic<bool> m_logUICallbackSaved{false};

  // ✅ Retrace edit mode
  bool m_retraceEditMode = false;
  RetraceEditCallback m_retraceEditCallback;
  int m_retracePositionsVisited = 0;
  int m_retracePositionsSaved = 0;
};
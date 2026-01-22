// RecipeWorker.h
#pragma once

#include <QThread>
#include <QString>
#include <atomic>
#include <functional>

class SequenceBuilder;
class SequenceEngine;
class RecipeManager;
class ProcessSequenceFactory;
class ProductPositionManager;
class UnifiedMotionManager;
class SequenceContext;  // ✅ Forward declaration for external context

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
    bool enableZ
  )>;
  void setManualAdjustCallback(ManualAdjustCallback callback);

  // Set callback for user input (optional - if not set, returns empty string)
  using UserInputCallback = std::function<std::pair<bool, std::string>(
    const std::string& title,
    const std::string& prompt,
    const std::string& defaultValue
  )>;
  void setUserInputCallback(UserInputCallback callback);

  // ✅ NEW: Set external context (for sharing with ProgramWorker)
  // If set, this context will be used instead of creating a local one
  void setExternalContext(SequenceContext* context);

  // ✅ NEW: Set loop mode (reduces verbose logging during rapid iterations)
  void setLoopMode(bool enabled) { m_loopMode = enabled; }

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

protected:
  void run() override;

private:
  void buildSequenceFromRecipe(SequenceBuilder* sequence, RecipeManager* recipeManager);

  SequenceEngine* m_engine;
  ProcessSequenceFactory* m_factory;
  ProductPositionManager* m_positionManager;
  UnifiedMotionManager* m_motionManager;
  std::string m_recipeFilePath;
  std::atomic<bool> m_stopRequested;
  UserPromptCallback m_userPromptCallback;
  ManualAdjustCallback m_manualAdjustCallback;
  UserInputCallback m_userInputCallback;
  SequenceContext* m_externalContext = nullptr;  // ✅ NEW: External context from ProgramWorker
  std::vector<bool> m_breakpoints;               // Breakpoints for each enabled step
  bool m_loopMode = false;                       // ✅ NEW: Loop mode (reduces logging)
};
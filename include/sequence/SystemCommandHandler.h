// SystemCommandHandler.h
#pragma once

#include "ICommandHandler.h"
#include <functional>

// Callback type for user prompts - returns true if user confirmed, false if cancelled
using UserPromptCallback = std::function<bool(const std::string& title, const std::string& message)>;

// Callback type for non-blocking notifications - just shows, doesn't wait for response
using UserNotificationCallback = std::function<void(const std::string& title, const std::string& message)>;

// Callback type for manual adjustment - returns true if user clicked OK, false if cancelled
using ManualAdjustCallback = std::function<bool(
  const std::string& message,
  const std::string& deviceName,
  double stepSizeUm,
  bool enableX,
  bool enableY,
  bool enableZ
)>;

// Callback type for text input - returns pair<accepted, inputText>
using UserInputCallback = std::function<std::pair<bool, std::string>(
  const std::string& title,
  const std::string& prompt,
  const std::string& defaultValue
)>;

// Callback type for manual align point - returns true if user clicked Continue, false if cancelled
using ManualAlignPointCallback = std::function<bool(
  const std::string& pointName,
  const std::string& message,
  bool enableX,
  bool enableY,
  bool enableZ
)>;

// Parameters for placement correction retrace command.
// Passed from recipe plugin -> SystemCommandHandler -> RunProgramPage -> PlacementRetraceWidget.
struct PlacementRetraceParams {
  std::string trainingSetName;    // Name of PlacementCorrectionSet to load
  bool autoStart = false;          // Skip Idle page and begin retrace immediately
  bool autoDetect = false;         // Auto-detect all 4 points with vision (manual fallback on failure)
  bool autoSaveContinue = false;   // Auto-save corrected position, move to target, and continue recipe
};

// Callback for placement correction retrace — blocks the recipe thread.
// The callback shows the PlacementRetraceWidget UI and enters a QEventLoop.
// Returns true if user saved or skipped (recipe continues to next step).
// Returns false if user clicked "End" (recipe execution stops).
// Set by RunProgramPage; see RunProgramPage.cpp "setPlacementRetraceCallback".
using PlacementRetraceCallback = std::function<bool(const PlacementRetraceParams& params)>;

class SystemCommandHandler : public ICommandHandler
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

  std::string GetManagerType() const override { return "system"; }

  // Set the UI callback for blocking user prompts (must be called from main thread)
  void SetUserPromptCallback(UserPromptCallback callback) {
    m_userPromptCallback = callback;
  }

  // Set the UI callback for non-blocking notifications (must be called from main thread)
  void SetUserNotificationCallback(UserNotificationCallback callback) {
    m_userNotificationCallback = callback;
  }

  // Set the UI callback for manual adjustment (must be called from main thread)
  void SetManualAdjustCallback(ManualAdjustCallback callback) {
    m_manualAdjustCallback = callback;
  }

  // Set the UI callback for text input (must be called from main thread)
  void SetUserInputCallback(UserInputCallback callback) {
    m_userInputCallback = callback;
  }

  // Set the UI callback for manual align point (must be called from main thread)
  void SetManualAlignPointCallback(ManualAlignPointCallback callback) {
    m_manualAlignPointCallback = callback;
  }

  // Set the UI callback for placement correction retrace (must be called from main thread)
  void SetPlacementRetraceCallback(PlacementRetraceCallback callback) {
    m_placementRetraceCallback = callback;
  }

  // Getters — needed by ScriptCommandHandler to save/restore callbacks across
  // recipe.run() invocations from Lua (RecipeWorker clears them on destroy).
  UserPromptCallback GetUserPromptCallback() const { return m_userPromptCallback; }
  UserInputCallback  GetUserInputCallback()  const { return m_userInputCallback;  }

private:
  UserPromptCallback m_userPromptCallback;
  UserNotificationCallback m_userNotificationCallback;
  ManualAdjustCallback m_manualAdjustCallback;
  UserInputCallback m_userInputCallback;
  ManualAlignPointCallback m_manualAlignPointCallback;
  PlacementRetraceCallback m_placementRetraceCallback;
};
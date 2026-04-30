// SysDialogCommands.cpp — see header for description.
#include "SysDialogCommands.h"
#include "SysCommandUtils.h"

#include "../SystemCommandHandler.h"
#include "LogManager.h"
#include "SequenceContext.h"

#include <string>
#include <utility>

std::optional<bool> SysDialogCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps)
{
  LogManager* logger = deps.logger;

  // ============================================================================
  // USER PROMPT (configurable blocking/non-blocking)
  // ============================================================================
  if (methodName == "UserPrompt") {
    auto titleIt = params.find("title");
    auto messageIt = params.find("message");
    auto blockingIt = params.find("blocking");
    auto allowCancelIt = params.find("allow_cancel");
    auto autoAcceptIt = params.find("auto_accept");

    if (titleIt == params.end() || messageIt == params.end()) {
      errorMessage = "Missing required parameters: title, message";
      return false;
    }

    // Expand context references in title and message (e.g., "context:offset_x" -> actual value)
    std::string title = SysCommandUtils::ExpandContextReferences(titleIt->second, context);
    std::string message = SysCommandUtils::ExpandContextReferences(messageIt->second, context);
    bool blocking = (blockingIt != params.end() && blockingIt->second == "true");
    bool allowCancel = (allowCancelIt != params.end() && allowCancelIt->second == "true");
    bool autoAccept = (autoAcceptIt != params.end() && autoAcceptIt->second == "true");

    // ✅ LOOP MODE: Auto-accept when in loop execution mode
    if (context && context->IsLoopExecutionMode()) {
      autoAccept = true;
      if (logger) {
        logger->Info("Loop mode: Auto-accepting prompt: " + title, "SystemHandler");
      }
    }

    // ✅ AUTO-ACCEPT MODE: Skip user interaction entirely
    if (autoAccept) {
      if (logger) {
        logger->Info("Auto-accepting prompt (auto_confirm enabled): " + title, "SystemHandler");
      }

      // Store auto-accept result in context
      if (context) {
        context->SetValue("user_prompt_result", "OK", "system", "UserPrompt");
        context->SetValue("user_confirmed", true, "system", "UserPrompt");
      }

      return true;  // Auto-accept and continue
    }

    auto userPromptCallback = deps.parent ? deps.parent->GetUserPromptCallback() : nullptr;
    if (!userPromptCallback) {
      errorMessage = "UserPrompt callback not configured";
      return false;
    }

    if (blocking) {
      // Blocking mode - wait for user response
      if (logger) {
        logger->Info("Showing blocking user prompt: " + title, "SystemHandler");
      }

      // Call the callback - this will be handled by Qt on the main thread
      bool userConfirmed = userPromptCallback(title, message);

      // Store result in context if available
      if (context) {
        std::string result = userConfirmed ? "OK" : "Cancel";
        context->SetValue("user_prompt_result", result, "system", "UserPrompt");
        context->SetValue("user_confirmed", userConfirmed, "system", "UserPrompt");
      }

      if (!userConfirmed) {
        if (allowCancel) {
          // Allow cancel mode - continue sequence even if user cancels
          if (logger) {
            logger->Info("User cancelled prompt (allowed): " + title, "SystemHandler");
          }
          return true;  // Continue sequence
        } else {
          // Traditional mode - stop sequence if user cancels
          errorMessage = "User cancelled operation";
          if (logger) {
            logger->Warning("User cancelled prompt: " + title, "SystemHandler");
          }
          return false;  // Stop sequence
        }
      }

      if (logger) {
        logger->Info("User confirmed prompt: " + title, "SystemHandler");
      }
    }
    else {
      // Non-blocking mode - just show message and continue
      if (logger) {
        logger->Info("Showing non-blocking notification: " + title, "SystemHandler");
      }

      // Still call the callback but ignore the result
      userPromptCallback(title, message);

      if (logger) {
        logger->Info("Non-blocking notification shown", "SystemHandler");
      }
    }

    return true;
  }

  // ============================================================================
  // MANUAL ADJUST (Manual position adjustment with jog controls)
  // ============================================================================
  if (methodName == "ManualAdjust") {
    auto messageIt = params.find("message");
    auto deviceIt = params.find("device_name");
    auto stepSizeIt = params.find("step_size_um");
    auto enableXIt = params.find("enable_x");
    auto enableYIt = params.find("enable_y");
    auto enableZIt = params.find("enable_z");

    if (messageIt == params.end() || deviceIt == params.end() || stepSizeIt == params.end()) {
      errorMessage = "Missing required parameters: message, device_name, step_size_um";
      return false;
    }

    // Expand context references in message
    std::string message = SysCommandUtils::ExpandContextReferences(messageIt->second, context);
    std::string adjustDeviceName = deviceIt->second;
    double stepSizeUm = std::stod(stepSizeIt->second);
    bool enableX = (enableXIt != params.end() && enableXIt->second == "true");
    bool enableY = (enableYIt != params.end() && enableYIt->second == "true");
    bool enableZ = (enableZIt != params.end() && enableZIt->second == "true");

    // ✅ LOOP MODE: Auto-skip manual adjustment when in loop execution mode
    if (context && context->IsLoopExecutionMode()) {
      if (logger) {
        logger->Info("Loop mode: Skipping manual adjustment for: " + adjustDeviceName, "SystemHandler");
      }
      return true;  // Continue without user interaction
    }

    auto manualAdjustCallback = deps.parent ? deps.parent->GetManualAdjustCallback() : nullptr;
    if (!manualAdjustCallback) {
      errorMessage = "ManualAdjust callback not configured";
      return false;
    }

    if (logger) {
      logger->Info("Showing manual adjustment for device: " + adjustDeviceName, "SystemHandler");
    }

    // Call the callback - this will show the ManualAdjustWidget and wait for user
    bool userCompleted = manualAdjustCallback(message, adjustDeviceName, stepSizeUm, enableX, enableY, enableZ);

    if (!userCompleted) {
      errorMessage = "User cancelled manual adjustment";
      if (logger) {
        logger->Warning("User cancelled manual adjustment for: " + adjustDeviceName, "SystemHandler");
      }
      return false;
    }

    if (logger) {
      logger->Info("User completed manual adjustment for: " + adjustDeviceName, "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // ASK FOR INPUT (Text input dialog)
  // ============================================================================
  if (methodName == "AskForInput") {
    auto titleIt = params.find("title");
    auto promptIt = params.find("prompt");
    auto defaultValueIt = params.find("default_value");
    auto contextKeyIt = params.find("context_key");

    if (titleIt == params.end() || promptIt == params.end()) {
      errorMessage = "Missing required parameters: title, prompt";
      return false;
    }

    std::string title = titleIt->second;
    std::string prompt = promptIt->second;
    std::string defaultValue = (defaultValueIt != params.end()) ? defaultValueIt->second : "";
    std::string contextKey = (contextKeyIt != params.end()) ? contextKeyIt->second : "user_input";

    // ✅ LOOP MODE: Auto-accept with default value when in loop execution mode
    if (context && context->IsLoopExecutionMode()) {
      if (logger) {
        logger->Info("Loop mode: Auto-accepting input with default value: " + defaultValue, "SystemHandler");
      }
      // Store default value in context
      context->SetValue(contextKey, defaultValue, "system", "AskForInput");
      return true;  // Continue without user interaction
    }

    auto userInputCallback = deps.parent ? deps.parent->GetUserInputCallback() : nullptr;
    if (!userInputCallback) {
      errorMessage = "UserInput callback not configured";
      return false;
    }

    if (logger) {
      logger->Info("Showing text input dialog: " + title, "SystemHandler");
    }

    // Call the callback - this will show input dialog and wait for user
    auto result = userInputCallback(title, prompt, defaultValue);
    bool accepted = result.first;
    std::string inputText = result.second;

    if (!accepted) {
      errorMessage = "User cancelled input";
      if (logger) {
        logger->Warning("User cancelled input dialog: " + title, "SystemHandler");
      }
      return false;
    }

    // Store result in context
    if (context) {
      context->SetValue(contextKey, inputText, "system", "AskForInput");

      if (logger) {
        logger->Info("User input stored in context[" + contextKey + "]: " + inputText, "SystemHandler");
      }
    }

    if (logger) {
      logger->Info("User provided input: " + inputText, "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // MANUAL ALIGN POINT
  // ============================================================================
  if (methodName == "ManualAlignPoint") {
    auto pointNameIt = params.find("point_name");
    auto messageIt = params.find("message");
    auto enableXIt = params.find("enable_x");
    auto enableYIt = params.find("enable_y");
    auto enableZIt = params.find("enable_z");

    if (pointNameIt == params.end()) {
      errorMessage = "Missing required parameter: point_name";
      return false;
    }

    std::string pointName = pointNameIt->second;
    std::string message = (messageIt != params.end()) ? messageIt->second : "Align the crosshair to the target point";
    bool enableX = (enableXIt != params.end() && (enableXIt->second == "true" || enableXIt->second == "1"));
    bool enableY = (enableYIt != params.end() && (enableYIt->second == "true" || enableYIt->second == "1"));
    bool enableZ = (enableZIt != params.end() && (enableZIt->second == "true" || enableZIt->second == "1"));

    auto manualAlignPointCallback = deps.parent ? deps.parent->GetManualAlignPointCallback() : nullptr;
    if (!manualAlignPointCallback) {
      errorMessage = "ManualAlignPoint callback not configured";
      return false;
    }

    if (logger) {
      logger->Info("Showing manual align point for: " + pointName, "SystemHandler");
    }

    // Call the callback - this will show the ManualAlignPointWidget and wait for user
    bool userCompleted = manualAlignPointCallback(pointName, message, enableX, enableY, enableZ);

    if (!userCompleted) {
      errorMessage = "User cancelled manual alignment";
      if (logger) {
        logger->Warning("User cancelled manual alignment for: " + pointName, "SystemHandler");
      }
      return false;
    }

    if (logger) {
      logger->Info("User completed manual alignment for: " + pointName, "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // PLACEMENT CORRECTION RETRACE
  // ============================================================================
  // Interactive command: blocks the recipe thread and shows the PlacementRetraceWidget
  // in RunProgramPage. The callback is a blocking function that enters a QEventLoop
  // on the UI thread and waits for the user to complete/skip/end the retrace.
  //
  // Returns true if user saved or skipped (recipe continues).
  // Returns false if user ended without saving (recipe stops).
  //
  // The callback is set by RunProgramPage::setPlacementRetraceCallback() during
  // ProgramWorker setup. If running outside RunProgramPage (e.g., RecipePage),
  // the callback may not be configured — handled by the null check below.
  //
  // Plugin source: PlacementCorrectionProcesses.cpp
  // Widget source: PlacementRetraceWidget.h/cpp
  // Callback wiring: RunProgramPage.cpp (search for "setPlacementRetraceCallback")
  // ============================================================================
  if (methodName == "PlacementCorrectionRetrace") {
    auto setNameIt = params.find("training_set_name");
    auto autoStartIt = params.find("auto_start");
    auto autoDetectIt = params.find("auto_detect");
    auto autoSaveContinueIt = params.find("auto_save_continue");

    if (setNameIt == params.end() || setNameIt->second.empty()) {
      errorMessage = "Missing required parameter: training_set_name";
      return false;
    }

    std::string trainingSetName = SysCommandUtils::ExpandContextReferences(setNameIt->second, context);
    bool autoStart = (autoStartIt != params.end() && autoStartIt->second == "true");
    bool autoDetect = (autoDetectIt != params.end() && autoDetectIt->second == "true");
    bool autoSaveContinue = (autoSaveContinueIt != params.end() && autoSaveContinueIt->second == "true");

    // Skip in loop mode — retrace requires operator interaction, not suitable for loops
    if (context && context->IsLoopExecutionMode()) {
      if (logger) {
        logger->Info("Loop mode: Skipping placement retrace for: " + trainingSetName, "SystemHandler");
      }
      return true;
    }

    auto placementRetraceCallback = deps.parent ? deps.parent->GetPlacementRetraceCallback() : nullptr;
    if (!placementRetraceCallback) {
      errorMessage = "PlacementRetrace callback not configured";
      return false;
    }

    if (logger) {
      logger->Info("Showing placement correction retrace for set: " + trainingSetName
                   + " (autoDetect=" + (autoDetect ? "true" : "false")
                   + " autoSave=" + (autoSaveContinue ? "true" : "false") + ")", "SystemHandler");
    }

    PlacementRetraceParams retraceParams;
    retraceParams.trainingSetName = trainingSetName;
    retraceParams.autoStart = autoStart;
    retraceParams.autoDetect = autoDetect;
    retraceParams.autoSaveContinue = autoSaveContinue;

    bool success = placementRetraceCallback(retraceParams);

    if (!success) {
      errorMessage = "User ended placement retrace without saving";
      if (logger) {
        logger->Warning("Placement retrace ended without save for: " + trainingSetName, "SystemHandler");
      }
      return false;
    }

    if (logger) {
      logger->Info("Placement retrace completed for: " + trainingSetName, "SystemHandler");
    }

    return true;
  }

  return std::nullopt;  // not our family
}

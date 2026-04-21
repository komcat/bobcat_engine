// SystemCommandHandler.cpp
#include "SystemCommandHandler.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <iomanip>
#include <QImage>

// Helper function to expand context references in strings
// Replaces "context:variable_name" with actual values from context
static std::string ExpandContextReferences(const std::string& input, SequenceContext* context)
{
  if (!context || input.find("context:") == std::string::npos) {
    return input;  // No context or no references to expand
  }

  std::string result = input;
  std::regex contextPattern("context:([a-zA-Z_][a-zA-Z0-9_]*)");
  std::smatch match;
  std::string::const_iterator searchStart(result.cbegin());

  // Find all context:variable_name patterns and replace them
  std::string expanded = result;
  std::sregex_iterator iter(result.begin(), result.end(), contextPattern);
  std::sregex_iterator end;

  // Collect all matches first to avoid iterator invalidation
  std::vector<std::pair<std::string, std::string>> replacements;
  for (; iter != end; ++iter) {
    std::string fullMatch = (*iter)[0].str();  // e.g., "context:offset_x"
    std::string varName = (*iter)[1].str();    // e.g., "offset_x"

    // Try to get the value from context
    std::string valueStr;
    if (context->HasValue(varName)) {
      // Check if it's numeric or string
      float numValue = context->GetNumeric(varName, 0.0f);
      std::string strValue = context->GetString(varName, "");

      if (!strValue.empty()) {
        valueStr = strValue;
      } else {
        // Format numeric value nicely
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4) << numValue;
        valueStr = oss.str();
        // Remove trailing zeros
        valueStr.erase(valueStr.find_last_not_of('0') + 1, std::string::npos);
        if (valueStr.back() == '.') valueStr.pop_back();
      }
    } else {
      valueStr = "[" + varName + ": N/A]";  // Variable not found
    }

    replacements.push_back({fullMatch, valueStr});
  }

  // Apply replacements in reverse order to maintain string positions
  for (const auto& [from, to] : replacements) {
    size_t pos = expanded.find(from);
    if (pos != std::string::npos) {
      expanded.replace(pos, from.length(), to);
    }
  }

  return expanded;
}

// Helper function to save frame data to file
static bool SaveFrameDataToFile(const CameraFrameData& frameData,
                                const std::string& filepath,
                                std::string& errorMessage)
{
  if (!frameData.IsValid()) {
    errorMessage = "Invalid frame data";
    return false;
  }

  // Determine QImage format based on channels
  QImage::Format format;
  if (frameData.channels == 3) {
    format = QImage::Format_RGB888;
  } else if (frameData.channels == 1) {
    format = QImage::Format_Grayscale8;
  } else if (frameData.channels == 4) {
    format = QImage::Format_RGBA8888;
  } else {
    errorMessage = "Unsupported channel count: " + std::to_string(frameData.channels);
    return false;
  }

  // Create QImage from raw data
  QImage image(
    frameData.imageData.data(),
    frameData.width,
    frameData.height,
    frameData.width * frameData.channels,
    format
  );

  // Save as PNG
  if (!image.save(QString::fromStdString(filepath), "PNG")) {
    errorMessage = "Failed to save image to: " + filepath;
    return false;
  }

  return true;
}

bool SystemCommandHandler::ExecuteCommand(
  void* /*manager*/,
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  // manager is unused for system commands (can be nullptr)

  // Context can be used for capturing user responses, timing data, etc.
  // Example: UserPrompt can store user's choice in context
  auto captureAsIt = params.find("captureAs");

  // ============================================================================
  // WAIT / DELAY
  // ============================================================================
  if (methodName == "Wait" || methodName == "Delay") {
    auto msIt = params.find("milliseconds");

    if (msIt == params.end()) {
      errorMessage = "Missing required parameter: milliseconds";
      return false;
    }

    int totalMs = std::stoi(msIt->second);

    if (m_logger) {
      m_logger->Debug("Waiting for " + std::to_string(totalMs) + "ms", "SystemHandler");
    }

    // Use interruptible wait with progress updates
    const int updateIntervalMs = 100;  // Update UI every 100ms
    int remainingMs = totalMs;

    auto startTime = std::chrono::steady_clock::now();

    while (remainingMs > 0) {
      // Check stop flag - allow immediate cancellation
      if (context && context->IsStopRequestedAny()) {
        if (m_logger) {
          m_logger->Warning("Wait interrupted by user stop request", "SystemHandler");
        }
        return false;  // Stop execution
      }

      // Notify progress callback for UI update
      if (context) {
        context->NotifyWaitProgress(remainingMs, totalMs);
      }

      // Sleep for the smaller of: remaining time or update interval
      int sleepMs = (remainingMs < updateIntervalMs) ? remainingMs : updateIntervalMs;
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));

      // Calculate actual remaining time (more accurate than just subtracting)
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime
      ).count();
      remainingMs = totalMs - static_cast<int>(elapsed);
    }

    // Final notification with 0 remaining
    if (context) {
      context->NotifyWaitProgress(0, totalMs);
    }

    if (m_logger) {
      m_logger->Debug("Wait completed", "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // USER PROMPT (configurable blocking/non-blocking)
  // ============================================================================
  else if (methodName == "UserPrompt") {
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
    std::string title = ExpandContextReferences(titleIt->second, context);
    std::string message = ExpandContextReferences(messageIt->second, context);
    bool blocking = (blockingIt != params.end() && blockingIt->second == "true");
    bool allowCancel = (allowCancelIt != params.end() && allowCancelIt->second == "true");
    bool autoAccept = (autoAcceptIt != params.end() && autoAcceptIt->second == "true");

    // ✅ LOOP MODE: Auto-accept when in loop execution mode
    if (context && context->IsLoopExecutionMode()) {
      autoAccept = true;
      if (m_logger) {
        m_logger->Info("Loop mode: Auto-accepting prompt: " + title, "SystemHandler");
      }
    }

    // ✅ AUTO-ACCEPT MODE: Skip user interaction entirely
    if (autoAccept) {
      if (m_logger) {
        m_logger->Info("Auto-accepting prompt (auto_confirm enabled): " + title, "SystemHandler");
      }

      // Store auto-accept result in context
      if (context) {
        context->SetValue("user_prompt_result", "OK", "system", "UserPrompt");
        context->SetValue("user_confirmed", true, "system", "UserPrompt");
      }

      return true;  // Auto-accept and continue
    }

    if (!m_userPromptCallback) {
      errorMessage = "UserPrompt callback not configured";
      return false;
    }

    if (blocking) {
      // Blocking mode - wait for user response
      if (m_logger) {
        m_logger->Info("Showing blocking user prompt: " + title, "SystemHandler");
      }

      // Call the callback - this will be handled by Qt on the main thread
      bool userConfirmed = m_userPromptCallback(title, message);

      // Store result in context if available
      if (context) {
        std::string result = userConfirmed ? "OK" : "Cancel";
        context->SetValue("user_prompt_result", result, "system", "UserPrompt");
        context->SetValue("user_confirmed", userConfirmed, "system", "UserPrompt");
      }

      if (!userConfirmed) {
        if (allowCancel) {
          // Allow cancel mode - continue sequence even if user cancels
          if (m_logger) {
            m_logger->Info("User cancelled prompt (allowed): " + title, "SystemHandler");
          }
          return true;  // Continue sequence
        } else {
          // Traditional mode - stop sequence if user cancels
          errorMessage = "User cancelled operation";
          if (m_logger) {
            m_logger->Warning("User cancelled prompt: " + title, "SystemHandler");
          }
          return false;  // Stop sequence
        }
      }

      if (m_logger) {
        m_logger->Info("User confirmed prompt: " + title, "SystemHandler");
      }
    }
    else {
      // Non-blocking mode - just show message and continue
      if (m_logger) {
        m_logger->Info("Showing non-blocking notification: " + title, "SystemHandler");
      }

      // Still call the callback but ignore the result
      m_userPromptCallback(title, message);

      if (m_logger) {
        m_logger->Info("Non-blocking notification shown", "SystemHandler");
      }
    }

    return true;
  }

  // ============================================================================
  // MANUAL ADJUST (Manual position adjustment with jog controls)
  // ============================================================================
  else if (methodName == "ManualAdjust") {
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
    std::string message = ExpandContextReferences(messageIt->second, context);
    std::string adjustDeviceName = deviceIt->second;
    double stepSizeUm = std::stod(stepSizeIt->second);
    bool enableX = (enableXIt != params.end() && enableXIt->second == "true");
    bool enableY = (enableYIt != params.end() && enableYIt->second == "true");
    bool enableZ = (enableZIt != params.end() && enableZIt->second == "true");

    // ✅ LOOP MODE: Auto-skip manual adjustment when in loop execution mode
    if (context && context->IsLoopExecutionMode()) {
      if (m_logger) {
        m_logger->Info("Loop mode: Skipping manual adjustment for: " + adjustDeviceName, "SystemHandler");
      }
      return true;  // Continue without user interaction
    }

    if (!m_manualAdjustCallback) {
      errorMessage = "ManualAdjust callback not configured";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Showing manual adjustment for device: " + adjustDeviceName, "SystemHandler");
    }

    // Call the callback - this will show the ManualAdjustWidget and wait for user
    bool userCompleted = m_manualAdjustCallback(message, adjustDeviceName, stepSizeUm, enableX, enableY, enableZ);

    if (!userCompleted) {
      errorMessage = "User cancelled manual adjustment";
      if (m_logger) {
        m_logger->Warning("User cancelled manual adjustment for: " + adjustDeviceName, "SystemHandler");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("User completed manual adjustment for: " + adjustDeviceName, "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // ASK FOR INPUT (Text input dialog)
  // ============================================================================
  else if (methodName == "AskForInput") {
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
      if (m_logger) {
        m_logger->Info("Loop mode: Auto-accepting input with default value: " + defaultValue, "SystemHandler");
      }
      // Store default value in context
      context->SetValue(contextKey, defaultValue, "system", "AskForInput");
      return true;  // Continue without user interaction
    }

    if (!m_userInputCallback) {
      errorMessage = "UserInput callback not configured";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Showing text input dialog: " + title, "SystemHandler");
    }

    // Call the callback - this will show input dialog and wait for user
    auto result = m_userInputCallback(title, prompt, defaultValue);
    bool accepted = result.first;
    std::string inputText = result.second;

    if (!accepted) {
      errorMessage = "User cancelled input";
      if (m_logger) {
        m_logger->Warning("User cancelled input dialog: " + title, "SystemHandler");
      }
      return false;
    }

    // Store result in context
    if (context) {
      context->SetValue(contextKey, inputText, "system", "AskForInput");

      if (m_logger) {
        m_logger->Info("User input stored in context[" + contextKey + "]: " + inputText, "SystemHandler");
      }
    }

    if (m_logger) {
      m_logger->Info("User provided input: " + inputText, "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // LOG MESSAGE
  // ============================================================================
  else if (methodName == "LogMessage" || methodName == "Log") {
    auto messageIt = params.find("message");
    auto logLevelIt = params.find("log_level");

    if (messageIt == params.end()) {
      errorMessage = "Missing required parameter: message";
      return false;
    }

    std::string message = messageIt->second;
    std::string logLevel = (logLevelIt != params.end()) ? logLevelIt->second : "Info";

    if (m_logger) {
      if (logLevel == "Debug") {
        m_logger->Debug(message, "Recipe");
      }
      else if (logLevel == "Warning") {
        m_logger->Warning(message, "Recipe");
      }
      else if (logLevel == "Error") {
        m_logger->Error(message, "Recipe");
      }
      else {
        m_logger->Info(message, "Recipe");
      }
    }

    return true;
  }

  // ============================================================================
  // STOP SEQUENCE
  // ============================================================================
  else if (methodName == "StopSequence") {
    // Request sequence to stop at next checkpoint
    if (context) {
      context->RequestStop();

      if (m_logger) {
        m_logger->Warning("Sequence stop requested by recipe command", "Recipe");
      }

      std::cout << "[SystemCommandHandler] Sequence stop requested" << std::endl;
    }
    else {
      errorMessage = "No execution context available for StopSequence";
      return false;
    }

    return true;
  }

  // ============================================================================
  // SAVE LAST CAPTURE TO FILE
  // ============================================================================
  else if (methodName == "SaveLastCaptureToFile") {
    if (!context) {
      errorMessage = "SequenceContext is null";
      return false;
    }

    // Find the most recently captured image in context
    auto allKeys = context->GetAllKeys();
    std::string mostRecentImageKey;
    auto mostRecentTime = std::chrono::system_clock::time_point::min();

    for (const auto& key : allKeys) {
      if (context->GetValueType(key) == SequenceContext::ValueType::IMAGE_FRAME) {
        SequenceContext::Value val = context->GetValue(key);
        if (val.timestamp > mostRecentTime) {
          mostRecentTime = val.timestamp;
          mostRecentImageKey = key;
        }
      }
    }

    if (mostRecentImageKey.empty()) {
      errorMessage = "No captured images found in context";
      if (m_logger) {
        m_logger->Error("SaveLastCaptureToFile failed: No captured images in context", "SystemHandler");
      }
      return false;
    }

    // Get the frame data
    auto frameData = context->GetFrameData(mostRecentImageKey);
    if (!frameData || !frameData->IsValid()) {
      errorMessage = "Failed to retrieve frame data for key: " + mostRecentImageKey;
      return false;
    }

    // Build save path
    std::string productName;
    auto productIt = params.find("product_name");
    if (productIt != params.end() && !productIt->second.empty()) {
      productName = productIt->second;
    }

    std::string filename;
    auto filenameIt = params.find("filename");
    if (filenameIt != params.end() && !filenameIt->second.empty()) {
      filename = filenameIt->second;
    }
    else {
      // Auto-generate filename with timestamp
      auto now = std::chrono::system_clock::now();
      auto time_t_now = std::chrono::system_clock::to_time_t(now);
      struct tm timeinfo;
      localtime_s(&timeinfo, &time_t_now);
      char buffer[64];
      strftime(buffer, sizeof(buffer), "%H-%M-%S", &timeinfo);
      filename = std::string("last_capture_") + buffer + ".png";
    }

    // Build directory path
    std::string savePath;
    if (!productName.empty()) {
      // Save to product folder
      char dateBuffer[32];
      auto now = std::chrono::system_clock::now();
      auto time_t_now = std::chrono::system_clock::to_time_t(now);
      struct tm timeinfo;
      localtime_s(&timeinfo, &time_t_now);
      strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &timeinfo);

      savePath = "data/captured_images/" + productName + "/" + std::string(dateBuffer) + "/" + filename;
    }
    else {
      // Save to default folder
      char dateBuffer[32];
      auto now = std::chrono::system_clock::now();
      auto time_t_now = std::chrono::system_clock::to_time_t(now);
      struct tm timeinfo;
      localtime_s(&timeinfo, &time_t_now);
      strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &timeinfo);

      savePath = "data/captured_images/" + std::string(dateBuffer) + "/" + filename;
    }

    // Ensure directory exists and save
    std::filesystem::path dirPath = std::filesystem::path(savePath).parent_path();
    if (!std::filesystem::exists(dirPath)) {
      std::filesystem::create_directories(dirPath);
    }

    // Save image
    if (!SaveFrameDataToFile(*frameData, savePath, errorMessage)) {
      if (m_logger) {
        m_logger->Error("Failed to save image: " + errorMessage, "SystemHandler");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("Saved last captured image to: " + savePath, "SystemHandler");
    }

    std::cout << "[SystemCommandHandler] Saved last captured image (from context["
              << mostRecentImageKey << "]) to: " << savePath << std::endl;

    // Store filepath in context if captureAs specified
    auto saveFileCapture = params.find("captureAs");
    if (saveFileCapture != params.end() && !saveFileCapture->second.empty()) {
      context->SetValue(saveFileCapture->second, savePath, "system", "SaveLastCaptureToFile");
    }

    return true;
  }

  // ============================================================================
  // CROP ROI
  // ============================================================================
  else if (methodName == "CropROI") {
    if (!context) {
      errorMessage = "SequenceContext is null";
      return false;
    }

    // Get source context key
    auto sourceKeyIt = params.find("source_context_key");
    if (sourceKeyIt == params.end()) {
      errorMessage = "Missing required parameter: source_context_key";
      return false;
    }
    std::string sourceKey = sourceKeyIt->second;

    // Get output context key
    auto outputKeyIt = params.find("output_context_key");
    if (outputKeyIt == params.end()) {
      errorMessage = "Missing required parameter: output_context_key";
      return false;
    }
    std::string outputKey = outputKeyIt->second;

    // Get width and height percentages
    float widthPercent = 100.0f;
    float heightPercent = 100.0f;

    auto widthIt = params.find("width_percent");
    if (widthIt != params.end()) {
      try {
        widthPercent = std::stof(widthIt->second);
        if (widthPercent <= 0 || widthPercent > 100) {
          errorMessage = "width_percent must be between 0 and 100";
          return false;
        }
      } catch (...) {
        errorMessage = "Invalid width_percent value: " + widthIt->second;
        return false;
      }
    }

    auto heightIt = params.find("height_percent");
    if (heightIt != params.end()) {
      try {
        heightPercent = std::stof(heightIt->second);
        if (heightPercent <= 0 || heightPercent > 100) {
          errorMessage = "height_percent must be between 0 and 100";
          return false;
        }
      } catch (...) {
        errorMessage = "Invalid height_percent value: " + heightIt->second;
        return false;
      }
    }

    // Get source frame data from context
    auto sourceFrameData = context->GetFrameData(sourceKey);
    if (!sourceFrameData || !sourceFrameData->IsValid()) {
      errorMessage = "Source image not found or invalid in context[" + sourceKey + "]";
      if (m_logger) {
        m_logger->Error("CropROI failed: " + errorMessage, "SystemHandler");
      }
      return false;
    }

    // Convert frame data to QImage
    QImage::Format format;
    if (sourceFrameData->channels == 3) {
      format = QImage::Format_RGB888;
    } else if (sourceFrameData->channels == 1) {
      format = QImage::Format_Grayscale8;
    } else if (sourceFrameData->channels == 4) {
      format = QImage::Format_RGBA8888;
    } else {
      errorMessage = "Unsupported channel count: " + std::to_string(sourceFrameData->channels);
      return false;
    }

    QImage sourceImage(
      sourceFrameData->imageData.data(),
      sourceFrameData->width,
      sourceFrameData->height,
      sourceFrameData->width * sourceFrameData->channels,
      format
    );

    // Calculate ROI dimensions (centered)
    int imageWidth = sourceImage.width();
    int imageHeight = sourceImage.height();

    int cropWidth = static_cast<int>(imageWidth * widthPercent / 100.0f);
    int cropHeight = static_cast<int>(imageHeight * heightPercent / 100.0f);

    // Center the crop
    int cropX = (imageWidth - cropWidth) / 2;
    int cropY = (imageHeight - cropHeight) / 2;

    // Perform crop
    QImage croppedImage = sourceImage.copy(cropX, cropY, cropWidth, cropHeight);

    if (croppedImage.isNull()) {
      errorMessage = "Failed to crop image";
      return false;
    }

    // Convert cropped QImage back to CameraFrameData
    CameraFrameData croppedFrameData;
    croppedFrameData.cameraId = sourceFrameData->cameraId + "_cropped";
    croppedFrameData.width = croppedImage.width();
    croppedFrameData.height = croppedImage.height();
    croppedFrameData.channels = sourceFrameData->channels;
    croppedFrameData.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    croppedFrameData.exposureTime = sourceFrameData->exposureTime;
    croppedFrameData.gain = sourceFrameData->gain;
    croppedFrameData.frameNumber = sourceFrameData->frameNumber;

    // Copy pixel data
    size_t dataSize = croppedImage.width() * croppedImage.height() * sourceFrameData->channels;
    croppedFrameData.imageData.resize(dataSize);
    memcpy(croppedFrameData.imageData.data(), croppedImage.bits(), dataSize);

    // Store cropped frame in context
    context->SetFrameData(outputKey, croppedFrameData, true, "system", "CropROI");

    if (m_logger) {
      std::string tempPath = context->GetFrameTempFilePath(outputKey);
      m_logger->Info("Cropped ROI (" + std::to_string((int)widthPercent) + "% x " +
                     std::to_string((int)heightPercent) + "%) from context[" + sourceKey +
                     "] -> context[" + outputKey + "], saved to: " + tempPath, "SystemHandler");
    }

    std::cout << "[SystemCommandHandler] Cropped ROI: " << cropWidth << "x" << cropHeight
              << " (center: " << cropX + cropWidth/2 << "," << cropY + cropHeight/2
              << ") from " << imageWidth << "x" << imageHeight << " image" << std::endl;

    // Optional: Save to file
    auto saveToFileIt = params.find("save_to_file");
    if (saveToFileIt != params.end() && (saveToFileIt->second == "true" || saveToFileIt->second == "1")) {
      std::string filename;
      auto filenameIt = params.find("filename");
      if (filenameIt != params.end() && !filenameIt->second.empty()) {
        filename = filenameIt->second;
      } else {
        // Auto-generate filename
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t_now);
        char buffer[64];
        strftime(buffer, sizeof(buffer), "%H-%M-%S", &timeinfo);
        filename = std::string("cropped_") + buffer + ".png";
      }

      // Build save path
      char dateBuffer[32];
      auto now = std::chrono::system_clock::now();
      auto time_t_now = std::chrono::system_clock::to_time_t(now);
      struct tm timeinfo;
      localtime_s(&timeinfo, &time_t_now);
      strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &timeinfo);

      std::string savePath = "data/captured_images/" + std::string(dateBuffer) + "/" + filename;

      // Ensure directory exists
      std::filesystem::path dirPath = std::filesystem::path(savePath).parent_path();
      if (!std::filesystem::exists(dirPath)) {
        std::filesystem::create_directories(dirPath);
      }

      // Save image
      if (!SaveFrameDataToFile(croppedFrameData, savePath, errorMessage)) {
        if (m_logger) {
          m_logger->Warning("Failed to save cropped image: " + errorMessage, "SystemHandler");
        }
        // Don't fail the whole command if file save fails
      } else {
        if (m_logger) {
          m_logger->Info("Saved cropped image to: " + savePath, "SystemHandler");
        }
      }
    }

    return true;
  }

  // ============================================================================
  // MANUAL ALIGN POINT
  // ============================================================================
  else if (methodName == "ManualAlignPoint") {
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

    if (!m_manualAlignPointCallback) {
      errorMessage = "ManualAlignPoint callback not configured";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Showing manual align point for: " + pointName, "SystemHandler");
    }

    // Call the callback - this will show the ManualAlignPointWidget and wait for user
    bool userCompleted = m_manualAlignPointCallback(pointName, message, enableX, enableY, enableZ);

    if (!userCompleted) {
      errorMessage = "User cancelled manual alignment";
      if (m_logger) {
        m_logger->Warning("User cancelled manual alignment for: " + pointName, "SystemHandler");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("User completed manual alignment for: " + pointName, "SystemHandler");
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
  else if (methodName == "PlacementCorrectionRetrace") {
    auto setNameIt = params.find("training_set_name");
    auto autoStartIt = params.find("auto_start");
    auto autoDetectIt = params.find("auto_detect");
    auto autoSaveContinueIt = params.find("auto_save_continue");

    if (setNameIt == params.end() || setNameIt->second.empty()) {
      errorMessage = "Missing required parameter: training_set_name";
      return false;
    }

    std::string trainingSetName = ExpandContextReferences(setNameIt->second, context);
    bool autoStart = (autoStartIt != params.end() && autoStartIt->second == "true");
    bool autoDetect = (autoDetectIt != params.end() && autoDetectIt->second == "true");
    bool autoSaveContinue = (autoSaveContinueIt != params.end() && autoSaveContinueIt->second == "true");

    // Skip in loop mode — retrace requires operator interaction, not suitable for loops
    if (context && context->IsLoopExecutionMode()) {
      if (m_logger) {
        m_logger->Info("Loop mode: Skipping placement retrace for: " + trainingSetName, "SystemHandler");
      }
      return true;
    }

    if (!m_placementRetraceCallback) {
      errorMessage = "PlacementRetrace callback not configured";
      return false;
    }

    if (m_logger) {
      m_logger->Info("Showing placement correction retrace for set: " + trainingSetName
                     + " (autoDetect=" + (autoDetect ? "true" : "false")
                     + " autoSave=" + (autoSaveContinue ? "true" : "false") + ")", "SystemHandler");
    }

    PlacementRetraceParams retraceParams;
    retraceParams.trainingSetName = trainingSetName;
    retraceParams.autoStart = autoStart;
    retraceParams.autoDetect = autoDetect;
    retraceParams.autoSaveContinue = autoSaveContinue;

    bool success = m_placementRetraceCallback(retraceParams);

    if (!success) {
      errorMessage = "User ended placement retrace without saving";
      if (m_logger) {
        m_logger->Warning("Placement retrace ended without save for: " + trainingSetName, "SystemHandler");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("Placement retrace completed for: " + trainingSetName, "SystemHandler");
    }

    return true;
  }

  // ============================================================================
  // APPEND CSV — append a row to a CSV file (creates file + header on first write)
  // ============================================================================
  else if (methodName == "AppendCsv") {
    auto pathIt = params.find("path");
    auto rowIt = params.find("row");
    if (pathIt == params.end() || pathIt->second.empty()) {
      errorMessage = "AppendCsv requires 'path' parameter";
      return false;
    }
    if (rowIt == params.end()) {
      errorMessage = "AppendCsv requires 'row' parameter";
      return false;
    }

    namespace fs = std::filesystem;
    fs::path target(pathIt->second);

    // Path safety: no ".." components anywhere in the path.
    for (const auto& part : target) {
      if (part == "..") {
        errorMessage = "AppendCsv: path must not contain '..': " + pathIt->second;
        return false;
      }
    }

    // Must live under a "data" directory — either as a relative path starting
    // with "data/" or an absolute path with "data" as a component.
    bool underData = false;
    for (const auto& part : target) {
      if (part == "data") { underData = true; break; }
    }
    if (!underData) {
      errorMessage = "AppendCsv: path must be under a 'data/' directory: " + pathIt->second;
      return false;
    }

    if (pathIt->second.size() > 260) {
      errorMessage = "AppendCsv: path exceeds 260 chars";
      return false;
    }

    std::error_code ec;
    fs::path parent = target.parent_path();
    if (!parent.empty()) {
      fs::create_directories(parent, ec);
      if (ec) {
        errorMessage = "AppendCsv: failed to create directory '" + parent.string() + "': " + ec.message();
        return false;
      }
    }

    const bool newFile = !fs::exists(target, ec);

    std::ofstream ofs(target, std::ios::app | std::ios::binary);
    if (!ofs.is_open()) {
      errorMessage = "AppendCsv: failed to open '" + target.string() + "' for append";
      return false;
    }

    if (newFile) {
      auto headerIt = params.find("header");
      if (headerIt != params.end() && !headerIt->second.empty()) {
        ofs << headerIt->second << "\n";
      }
    }

    ofs << rowIt->second << "\n";
    ofs.flush();
    if (!ofs) {
      errorMessage = "AppendCsv: write failed on '" + target.string() + "'";
      return false;
    }

    if (m_logger) {
      m_logger->Debug("AppendCsv wrote row to " + target.string(), "Recipe");
    }
    return true;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown system method: " + methodName;
    return false;
  }
}
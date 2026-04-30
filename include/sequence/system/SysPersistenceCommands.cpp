// SysPersistenceCommands.cpp — see header for description.
#include "SysPersistenceCommands.h"
#include "SysCommandUtils.h"

#include "CameraFrameData.h"
#include "LogManager.h"
#include "SequenceContext.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

#include <QImage>

std::optional<bool> SysPersistenceCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps)
{
  LogManager* logger = deps.logger;

  // ============================================================================
  // SAVE LAST CAPTURE TO FILE
  // ============================================================================
  if (methodName == "SaveLastCaptureToFile") {
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
      if (logger) {
        logger->Error("SaveLastCaptureToFile failed: No captured images in context", "SystemHandler");
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
    if (!SysCommandUtils::SaveFrameDataToFile(*frameData, savePath, errorMessage)) {
      if (logger) {
        logger->Error("Failed to save image: " + errorMessage, "SystemHandler");
      }
      return false;
    }

    if (logger) {
      logger->Info("Saved last captured image to: " + savePath, "SystemHandler");
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
  if (methodName == "CropROI") {
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
      if (logger) {
        logger->Error("CropROI failed: " + errorMessage, "SystemHandler");
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

    if (logger) {
      std::string tempPath = context->GetFrameTempFilePath(outputKey);
      logger->Info("Cropped ROI (" + std::to_string((int)widthPercent) + "% x " +
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
      if (!SysCommandUtils::SaveFrameDataToFile(croppedFrameData, savePath, errorMessage)) {
        if (logger) {
          logger->Warning("Failed to save cropped image: " + errorMessage, "SystemHandler");
        }
        // Don't fail the whole command if file save fails
      } else {
        if (logger) {
          logger->Info("Saved cropped image to: " + savePath, "SystemHandler");
        }
      }
    }

    return true;
  }

  // ============================================================================
  // APPEND CSV — append a row to a CSV file (creates file + header on first write)
  // ============================================================================
  if (methodName == "AppendCsv") {
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

    if (logger) {
      logger->Debug("AppendCsv wrote row to " + target.string(), "Recipe");
    }
    return true;
  }

  return std::nullopt;  // not our family
}

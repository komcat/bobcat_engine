// CameraCommandHandler.cpp
#include "CameraCommandHandler.h"
#include "CameraManager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "LiveVideoSubscriber.h"
#include "DeviceUnderTest.h"
#include "ApplicationContext.h"
#include <QImage>
#include <QString>
#include <filesystem>
#include <thread>
#include <chrono>
#include <iostream>

namespace fs = std::filesystem;

// Helper function to save frame as PNG
static bool SaveFrameToPNG(const CameraFrameData& frameData, const std::string& filepath) {
    // Create directory if needed
    fs::path path(filepath);
    fs::create_directories(path.parent_path());

    // Create QImage from raw data
    QImage::Format format;
    if (frameData.channels == 3) {
        format = QImage::Format_RGB888;
    } else if (frameData.channels == 1) {
        format = QImage::Format_Grayscale8;
    } else if (frameData.channels == 4) {
        format = QImage::Format_RGBA8888;
    } else {
        std::cerr << "[CameraCommandHandler] Unsupported channel count: " << frameData.channels << std::endl;
        return false;
    }

    QImage image(
        frameData.imageData.data(),
        frameData.width,
        frameData.height,
        frameData.width * frameData.channels,
        format
    );

    // Save as PNG
    return image.save(QString::fromStdString(filepath), "PNG");
}

// Helper function to capture frame from live feed
static bool CaptureFrameFromLiveFeed(CameraManager* cameraManager, const std::string& cameraName,
                                      CameraFrameData& outFrame, std::string& errorMessage) {
    if (!cameraManager) {
        errorMessage = "CameraManager is null";
        return false;
    }

    // Create temporary subscriber to get latest frame (must use shared_ptr for subscription system)
    auto subscriber = std::make_shared<LiveVideoSubscriber>(cameraName, "CameraCommandHandler_Temp");

    // Subscribe to frames
    cameraManager->SubscribeToFrames(subscriber);

    // Initialize status from manager (important if camera already grabbing)
    subscriber->InitializeStatusFromManager(cameraManager);

    // Wait briefly for first frame (camera should already be grabbing)
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Check if frame is available
    if (!subscriber->HasNewFrame()) {
        cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());
        errorMessage = "No frame available from camera: " + cameraName + " (is camera connected and grabbing?)";
        return false;
    }

    // Get latest frame
    outFrame = subscriber->GetLatestFrame();

    // Unsubscribe
    cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());

    // Validate frame
    if (!outFrame.IsValid()) {
        errorMessage = "Invalid frame data from camera: " + cameraName;
        return false;
    }

    return true;
}

// Helper function to generate capture filepath
static std::string GenerateCaptureFilePath(const std::string& productName, const std::string& cameraName, const std::string& prefix = "") {
    // Get current date
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);

    char dateBuffer[32];
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &tm_now);

    char timeBuffer[32];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%H-%M-%S", &tm_now);

    // Build filename: [prefix_][camera]_[time].png
    std::string filename = prefix + cameraName + "_" + timeBuffer + ".png";

    // Build path: data/[product_name]/captured_images/[date]/[filename]
    fs::path capturePath = fs::path("data") / productName / "captured_images" / dateBuffer / filename;

    return capturePath.string();
}

bool CameraCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  CameraManager* cameraManager = static_cast<CameraManager*>(manager);

  // ============================================================================
  // CAPTURE TO FILE
  // ============================================================================
  if (methodName == "CaptureToFile") {
    auto productNameIt = params.find("product_name");
    auto filenameIt = params.find("filename");
    auto prefixIt = params.find("prefix_");

    if (productNameIt == params.end()) {
      errorMessage = "Missing required parameter: product_name";
      return false;
    }

    std::string productName = productNameIt->second;
    std::string prefix = (prefixIt != params.end()) ? prefixIt->second : "";
    std::string filepath;

    // Generate or use provided filepath
    if (filenameIt != params.end() && !filenameIt->second.empty()) {
      // User provided filename - use it (prefix not applied when filename is specified)
      fs::path userPath = fs::path("data") / productName / "captured_images" / filenameIt->second;
      filepath = userPath.string();
    } else {
      // Auto-generate filepath with prefix
      filepath = GenerateCaptureFilePath(productName, deviceName, prefix);
    }

    // Capture frame
    CameraFrameData frameData;
    if (!CaptureFrameFromLiveFeed(cameraManager, deviceName, frameData, errorMessage)) {
      if (m_logger) {
        m_logger->Error("Failed to capture frame: " + errorMessage, "CameraHandler");
      }
      return false;
    }

    // Save to file
    if (!SaveFrameToPNG(frameData, filepath)) {
      errorMessage = "Failed to save frame to: " + filepath;
      if (m_logger) {
        m_logger->Error(errorMessage, "CameraHandler");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("Captured image saved to: " + filepath, "CameraHandler");
    }

    // Store filepath in context if captureAs specified
    auto captureAsIt = params.find("captureAs");
    if (context && captureAsIt != params.end()) {
      context->SetValue(captureAsIt->second, filepath, deviceName, methodName);
    }

    return true;
  }

  // ============================================================================
  // CAPTURE TO CONTEXT
  // ============================================================================
  else if (methodName == "CaptureToContext") {
    auto contextKeyIt = params.find("context_key");

    if (!context) {
      errorMessage = "SequenceContext is null";
      return false;
    }

    if (contextKeyIt == params.end()) {
      errorMessage = "Missing required parameter: context_key";
      return false;
    }

    std::string contextKey = contextKeyIt->second;

    // Capture frame
    CameraFrameData frameData;
    if (!CaptureFrameFromLiveFeed(cameraManager, deviceName, frameData, errorMessage)) {
      if (m_logger) {
        m_logger->Error("Failed to capture frame: " + errorMessage, "CameraHandler");
      }
      return false;
    }

    // Store in context (with temp file save)
    context->SetFrameData(contextKey, frameData, true, deviceName, methodName);

    if (m_logger) {
      std::string tempPath = context->GetFrameTempFilePath(contextKey);
      m_logger->Info("Captured frame stored in context[" + contextKey + "] and saved to: " + tempPath, "CameraHandler");
    }

    return true;
  }

  // ============================================================================
  // CAPTURE TO DUT FOLDER
  // ============================================================================
  else if (methodName == "CaptureToDUT") {
    auto productNameIt = params.find("product_name");
    auto serialNumberIt = params.find("serial_number");
    auto filenameIt = params.find("filename");
    auto prefixIt = params.find("prefix_");

    std::string productName;
    std::string serialNumber;

    // Try to get from parameters first
    if (productNameIt != params.end() && !productNameIt->second.empty()) {
      productName = productNameIt->second;
    }
    if (serialNumberIt != params.end() && !serialNumberIt->second.empty()) {
      serialNumber = serialNumberIt->second;
    }

    // If not in parameters, try to get from current DUT
    if ((productName.empty() || serialNumber.empty()) && m_context && m_context->currentDUT) {
      if (productName.empty()) {
        productName = m_context->currentDUT->getProductName();
      }
      if (serialNumber.empty()) {
        serialNumber = m_context->currentDUT->getSerialNumber();
      }
    }

    // Validate we have both values
    if (productName.empty() || serialNumber.empty()) {
      errorMessage = "Missing product_name and serial_number. Either provide as parameters or ensure a DUT is active.";
      return false;
    }

    // Get DUT image folder path
    std::string imageFolderPath = DeviceUnderTest::getImageFolderPath(productName, serialNumber);

    // Get prefix
    std::string prefix = (prefixIt != params.end()) ? prefixIt->second : "";

    // Generate filename
    std::string filename;
    if (filenameIt != params.end() && !filenameIt->second.empty()) {
      filename = filenameIt->second;  // User provided filename - prefix not applied
    } else {
      // Auto-generate with timestamp and prefix
      auto now = std::chrono::system_clock::now();
      auto time_t_now = std::chrono::system_clock::to_time_t(now);
      std::tm tm_now;
      localtime_s(&tm_now, &time_t_now);

      char timeBuffer[32];
      std::strftime(timeBuffer, sizeof(timeBuffer), "%H-%M-%S", &tm_now);

      // Build filename: [prefix_][camera]_[time].png
      filename = prefix + deviceName + "_" + timeBuffer + ".png";
    }

    std::string filepath = (fs::path(imageFolderPath) / filename).string();

    // Capture frame
    CameraFrameData frameData;
    if (!CaptureFrameFromLiveFeed(cameraManager, deviceName, frameData, errorMessage)) {
      if (m_logger) {
        m_logger->Error("Failed to capture frame: " + errorMessage, "CameraHandler");
      }
      return false;
    }

    // Save to DUT folder
    if (!SaveFrameToPNG(frameData, filepath)) {
      errorMessage = "Failed to save frame to: " + filepath;
      if (m_logger) {
        m_logger->Error(errorMessage, "CameraHandler");
      }
      return false;
    }

    if (m_logger) {
      m_logger->Info("Captured image saved to DUT folder: " + filepath, "CameraHandler");
    }

    // Store filepath in context if captureAs specified
    auto captureAsIt = params.find("captureAs");
    if (context && captureAsIt != params.end()) {
      context->SetValue(captureAsIt->second, filepath, deviceName, methodName);
    }

    return true;
  }

  // ============================================================================
  // CAPTURE AND CONFIRM (requires UI integration)
  // ============================================================================
  else if (methodName == "CaptureAndConfirm") {
    auto messageIt = params.find("message");

    if (messageIt == params.end()) {
      errorMessage = "Missing required parameter: message";
      return false;
    }

    std::string message = messageIt->second;

    // Capture frame
    CameraFrameData frameData;
    if (!CaptureFrameFromLiveFeed(cameraManager, deviceName, frameData, errorMessage)) {
      if (m_logger) {
        m_logger->Error("Failed to capture frame: " + errorMessage, "CameraHandler");
      }
      return false;
    }

    // TODO: Show CaptureConfirmWidget and wait for user response
    // This will be integrated in RunProgramPage similar to ManualAdjustWidget
    // For now, just log and auto-continue
    if (m_logger) {
      m_logger->Warning("CaptureAndConfirm: UI integration pending - auto-continuing", "CameraHandler");
    }

    // Store frame in context for UI to display
    if (context) {
      context->SetFrameData("__capture_confirm_frame__", frameData, false, deviceName, methodName);
      context->SetValue("__capture_confirm_message__", message, deviceName, methodName);
    }

    // TODO: Block and wait for user response via callback
    // For now, return true (auto-confirm)
    return true;
  }

  // ============================================================================
  // LEGACY: Capture (deprecated, use CaptureToFile instead)
  // ============================================================================
  else if (methodName == "Capture") {
    if (m_logger) {
      m_logger->Warning("'Capture' command is deprecated - use 'CaptureToFile' instead", "CameraHandler");
    }
    // Forward to CaptureToFile with default product name
    std::map<std::string, std::string> newParams = params;
    newParams["product_name"] = "default";
    return ExecuteCommand(manager, deviceName, "CaptureToFile", newParams, errorMessage, context);
  }

  // ============================================================================
  // CHECK PRESENT
  // ============================================================================
  else if (methodName == "CheckPresent") {
    if (m_logger) {
      m_logger->Info("Camera check present - TODO: implement", "CameraHandler");
    }
    // TODO: Implement presence check
    return true;
  }

  // ============================================================================
  // UNKNOWN
  // ============================================================================
  else {
    errorMessage = "Unknown camera method: " + methodName;
    return false;
  }
}
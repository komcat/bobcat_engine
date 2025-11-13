// CalibrationCommandHandler.cpp
#include "CalibrationCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "dut/DeviceUnderTest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <chrono>

using json = nlohmann::json;

CalibrationCommandHandler::CalibrationCommandHandler(ApplicationContext* context)
  : m_context(context)
{
}

std::string CalibrationCommandHandler::GetNavigationFilePath(const std::string& uid) const
{
  // ✅ Use SYSTEM folder for all calibration data
  return "config/SYSTEM/navigation_" + uid + ".json";
}

std::string CalibrationCommandHandler::GetNeedleZCalibrationFilePath() const
{
  // ✅ Use SYSTEM folder for all calibration data
  return "config/SYSTEM/needle_z_calibration.json";
}

std::string CalibrationCommandHandler::GetCameraToNeedleOffsetFilePath() const
{
  // ✅ Use SYSTEM folder for factory offset (read-only reference)
  return "config/SYSTEM/needlexyoffset.json";
}

std::string CalibrationCommandHandler::GetCurrentDateTime(bool dateOnly) const
{
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);

  std::tm tm_now;
  localtime_s(&tm_now, &time_t_now);

  std::ostringstream oss;

  if (dateOnly) {
    oss << std::put_time(&tm_now, "%Y-%m-%d");
  } else {
    oss << std::put_time(&tm_now, "%H:%M:%S");
  }

  return oss.str();
}

bool CalibrationCommandHandler::EnsureCalibrationDirectoryExists(std::string& errorMessage)
{
  try {
    // ✅ Use SYSTEM folder for all calibration data
    std::string dirPathStr = "config/SYSTEM";
    std::filesystem::path dirPath = dirPathStr;

    if (!std::filesystem::exists(dirPath)) {
      if (!std::filesystem::create_directories(dirPath)) {
        errorMessage = "Failed to create SYSTEM calibration directory";
        return false;
      }

      if (m_logger) {
        m_logger->Info("Created SYSTEM calibration directory: " + dirPathStr, "CalibrationHandler");
      }
    }

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception creating SYSTEM calibration directory: " + std::string(e.what());
    return false;
  }
}

bool CalibrationCommandHandler::LoadNavigationData(
  const std::string& uid,
  double& camX, double& camY, double& camZ)
{
  try {
    std::string filePath = GetNavigationFilePath(uid);

    if (!std::filesystem::exists(filePath)) {
      return false;  // File doesn't exist, first calibration
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    json j;
    file >> j;
    file.close();

    // Load camera position only
    if (j.contains("camera_position")) {
      camX = j["camera_position"]["x"];
      camY = j["camera_position"]["y"];
      camZ = j["camera_position"]["z"];
    }

    if (m_logger) {
      m_logger->Info(
        "Loaded navigation data for UID: " + uid + " - Camera: (" +
        std::to_string(camX) + ", " + std::to_string(camY) + ", " + std::to_string(camZ) + ")",
        "CalibrationHandler"
      );
    }

    return true;
  }
  catch (const std::exception& e) {
    if (m_logger) {
      m_logger->Warning(
        "Failed to load navigation data: " + std::string(e.what()),
        "CalibrationHandler"
      );
    }
    return false;
  }
}

bool CalibrationCommandHandler::LoadNeedleZCalibrationGlobal(
  double& oldNeedleHeight)
{
  try {
    std::string filePath = GetNeedleZCalibrationFilePath();

    if (!std::filesystem::exists(filePath)) {
      return false;  // File doesn't exist, first calibration
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    json j;
    file >> j;
    file.close();

    // Load old needle height from global file
    if (j.contains("parameters") && j["parameters"].contains("needle_height")) {
      oldNeedleHeight = j["parameters"]["needle_height"];
    }

    if (m_logger) {
      m_logger->Info(
        "Loaded global needle Z calibration: needle_height=" + std::to_string(oldNeedleHeight),
        "CalibrationHandler"
      );
    }

    return true;
  }
  catch (const std::exception& e) {
    if (m_logger) {
      m_logger->Warning(
        "Failed to load global needle Z calibration: " + std::string(e.what()),
        "CalibrationHandler"
      );
    }
    return false;
  }
}

bool CalibrationCommandHandler::LoadCameraToNeedleOffset(
  double& offsetX, double& offsetY, double& offsetZ)
{
  try {
    std::string filePath = GetCameraToNeedleOffsetFilePath();

    if (!std::filesystem::exists(filePath)) {
      return false;  // Factory offset file doesn't exist
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    json j;
    file >> j;
    file.close();

    // Load factory XY offset values (Z is always 0)
    if (j.contains("offset") && j["offset"].contains("x") && j["offset"].contains("y")) {
      offsetX = j["offset"]["x"];
      offsetY = j["offset"]["y"];
      offsetZ = 0.0;  // Z offset is not used in factory calibration
    }

    if (m_logger) {
      m_logger->Info(
        "Loaded factory needle XY offset: (" +
        std::to_string(offsetX) + ", " +
        std::to_string(offsetY) + ")",
        "CalibrationHandler"
      );
    }

    return true;
  }
  catch (const std::exception& e) {
    if (m_logger) {
      m_logger->Warning(
        "Failed to load factory needle XY offset: " + std::string(e.what()),
        "CalibrationHandler"
      );
    }
    return false;
  }
}

bool CalibrationCommandHandler::SaveCameraToNeedleOffset(
  const std::string& deviceName,
  const std::string& pneumaticSlide,
  double needleX, double needleY, double needleZ,
  double cameraX, double cameraY, double cameraZ,
  std::string& errorMessage,
  std::string& resultMessage)
{
  try {
    // Ensure directory exists
    if (!EnsureCalibrationDirectoryExists(errorMessage)) {
      return false;
    }

    // Calculate XY offset only (Z is calibrated separately)
    double offsetX = needleX - cameraX;
    double offsetY = needleY - cameraY;

    // Build JSON structure
    json j;

    // Metadata
    j["metadata"]["calibration_type"] = "Camera to Needle XY Offset";
    j["metadata"]["device"] = deviceName;
    j["metadata"]["last_calibrated_date"] = GetCurrentDateTime(true);
    j["metadata"]["last_calibrated_time"] = GetCurrentDateTime(false);
    j["metadata"]["pneumatic_slide"] = pneumaticSlide;
    j["metadata"]["calibrated_by"] = "System";
    j["metadata"]["description"] = "XY offset from camera center to needle tip (Z calibrated separately)";

    // Offset values (XY only)
    j["offset"]["x"] = offsetX;
    j["offset"]["y"] = offsetY;

    // Store original positions for reference
    j["positions"]["needle_imprint"]["x"] = needleX;
    j["positions"]["needle_imprint"]["y"] = needleY;
    j["positions"]["needle_imprint"]["z"] = needleZ;
    j["positions"]["camera_centered"]["x"] = cameraX;
    j["positions"]["camera_centered"]["y"] = cameraY;
    j["positions"]["camera_centered"]["z"] = cameraZ;

    j["notes"] = "XY offset calculated from needle imprint to camera center. Z offset is handled by separate needle Z calibration.";

    // Write to file
    std::string filePath = GetCameraToNeedleOffsetFilePath();
    std::ofstream file(filePath);

    if (!file.is_open()) {
      errorMessage = "Failed to open offset file for writing: " + filePath;
      return false;
    }

    file << j.dump(2);  // Pretty print with 2-space indent
    file.close();

    // Build result message
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "Camera-to-Needle XY Offset saved:\n";
    oss << "X: " << offsetX << " mm\n";
    oss << "Y: " << offsetY << " mm\n";
    oss << "(Z calibration is separate)";
    resultMessage = oss.str();

    if (m_logger) {
      m_logger->Info(
        "Saved camera-to-needle XY offset: (" +
        std::to_string(offsetX) + ", " +
        std::to_string(offsetY) + ")",
        "CalibrationHandler"
      );
    }

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception saving camera-to-needle XY offset: " + std::string(e.what());
    return false;
  }
}

bool CalibrationCommandHandler::SaveNavigationData(
  const std::string& uid,
  const std::string& deviceName,
  double camX, double camY, double camZ,
  std::string& errorMessage)
{
  try {
    // Ensure directory exists
    if (!EnsureCalibrationDirectoryExists(errorMessage)) {
      return false;
    }

    // Build JSON structure
    json j;

    // Metadata
    j["metadata"]["uid"] = uid;
    j["metadata"]["device"] = deviceName;
    j["metadata"]["last_updated_date"] = GetCurrentDateTime(true);
    j["metadata"]["last_updated_time"] = GetCurrentDateTime(false);
    j["metadata"]["description"] = "Camera position for auto-navigation (SYSTEM-wide)";

    // Camera position only (offsets come from factory file)
    j["camera_position"]["x"] = camX;
    j["camera_position"]["y"] = camY;
    j["camera_position"]["z"] = camZ;

    // Write to file
    std::string filePath = GetNavigationFilePath(uid);
    std::ofstream file(filePath);

    if (!file.is_open()) {
      errorMessage = "Failed to open navigation file for writing: " + filePath;
      return false;
    }

    file << j.dump(2);  // Pretty print with 2-space indent
    file.close();

    if (m_logger) {
      m_logger->Info(
        "Saved navigation data for UID: " + uid + " - Camera: (" +
        std::to_string(camX) + ", " + std::to_string(camY) + ", " + std::to_string(camZ) + ")",
        "CalibrationHandler"
      );
    }

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception saving navigation data: " + std::string(e.what());
    return false;
  }
}

bool CalibrationCommandHandler::SaveNeedleZCalibrationGlobal(
  const std::string& deviceName,
  const std::string& pneumaticSlide,
  double needleX, double needleY, double needleZ,
  std::string& errorMessage,
  std::string& deltaMessage)
{
  try {
    // Ensure directory exists
    if (!EnsureCalibrationDirectoryExists(errorMessage)) {
      return false;
    }

    // Load old calibration data for delta calculation
    double oldNeedleHeight = 0.0;
    bool hadPreviousCalibration = LoadNeedleZCalibrationGlobal(oldNeedleHeight);

    // Calculate delta
    double delta = 0.0;
    if (hadPreviousCalibration) {
      delta = needleZ - oldNeedleHeight;
    }

    // Build JSON structure
    json j;

    // Metadata
    j["metadata"]["calibration_type"] = "Needle Z Position (SYSTEM)";
    j["metadata"]["device"] = deviceName;
    j["metadata"]["calibrated_date"] = GetCurrentDateTime(true);
    j["metadata"]["calibrated_time"] = GetCurrentDateTime(false);
    j["metadata"]["pneumatic_slide"] = pneumaticSlide;
    j["metadata"]["description"] = "SYSTEM-wide needle Z calibration - latest calibration data";

    // Needle touch position
    j["needle_touch_position"]["x"] = needleX;
    j["needle_touch_position"]["y"] = needleY;
    j["needle_touch_position"]["z"] = needleZ;

    // Parameters
    j["parameters"]["needle_height"] = needleZ;

    if (hadPreviousCalibration) {
      j["parameters"]["previous_needle_height"] = oldNeedleHeight;
      j["parameters"]["delta_from_previous"] = delta;
    }

    // Write to global file
    std::string filePath = GetNeedleZCalibrationFilePath();
    std::ofstream file(filePath);

    if (!file.is_open()) {
      errorMessage = "Failed to open global calibration file for writing: " + filePath;
      return false;
    }

    file << j.dump(2);  // Pretty print with 2-space indent
    file.close();

    // Build delta message
    if (hadPreviousCalibration) {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(3);

      if (delta >= 0) {
        oss << " (Δ +" << delta << " mm from previous)";
      } else {
        oss << " (Δ " << delta << " mm from previous)";
      }

      deltaMessage = oss.str();
    } else {
      deltaMessage = " (First calibration)";
    }

    if (m_logger) {
      m_logger->Info(
        "Saved global needle Z calibration: Z=" + std::to_string(needleZ) + deltaMessage,
        "CalibrationHandler"
      );
    }

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception saving global needle Z calibration: " + std::string(e.what());
    return false;
  }
}

bool CalibrationCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  // ============================================================================
  // LOAD EXISTING NAVIGATION (camera position only)
  // ============================================================================
  if (methodName == "LoadExistingCalibration") {
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
      errorMessage = "Missing required parameter: uid";
      return false;
    }

    std::string uid = uidIt->second;

    double camX = 0.0, camY = 0.0, camZ = 0.0;

    bool exists = LoadNavigationData(uid, camX, camY, camZ);

    // Store in context (camera position only, offsets come from system file)
    if (context) {
      context->SetValue("navigation_exists", exists, "calibration", "LoadExistingCalibration");

      if (exists) {
        context->SetValue("camera_x", static_cast<float>(camX), "calibration", "LoadExistingCalibration");
        context->SetValue("camera_y", static_cast<float>(camY), "calibration", "LoadExistingCalibration");
        context->SetValue("camera_z", static_cast<float>(camZ), "calibration", "LoadExistingCalibration");
      }
    }

    return true;
  }

  // ============================================================================
  // CAPTURE CURRENT POSITION
  // ============================================================================
  else if (methodName == "CapturePosition") {
    auto deviceIt = params.find("device");
    auto keyPrefixIt = params.find("key_prefix");

    if (deviceIt == params.end() || keyPrefixIt == params.end()) {
      errorMessage = "Missing required parameters: device, key_prefix";
      return false;
    }

    std::string device = deviceIt->second;
    std::string keyPrefix = keyPrefixIt->second;

    if (!m_context || !m_context->motionManager) {
      errorMessage = "Motion manager not available";
      return false;
    }

    double x = 0.0, y = 0.0, z = 0.0;

    if (!m_context->motionManager->GetPosition(device, "X", x) ||
        !m_context->motionManager->GetPosition(device, "Y", y) ||
        !m_context->motionManager->GetPosition(device, "Z", z)) {
      errorMessage = "Failed to read position from device: " + device;
      return false;
    }

    if (context) {
      context->SetValue(keyPrefix + "_x", static_cast<float>(x), device, methodName);
      context->SetValue(keyPrefix + "_y", static_cast<float>(y), device, methodName);
      context->SetValue(keyPrefix + "_z", static_cast<float>(z), device, methodName);
    }

    if (m_logger) {
      m_logger->Info(
        "Captured position from " + device + ": (" +
        std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")",
        "CalibrationHandler"
      );
    }

    return true;
  }

  // ============================================================================
  // SAVE NAVIGATION DATA (camera position only)
  // ============================================================================
  else if (methodName == "SaveNavigationData") {
    auto uidIt = params.find("uid");
    auto deviceIt = params.find("device");

    if (uidIt == params.end() || deviceIt == params.end()) {
      errorMessage = "Missing required parameters: uid, device";
      return false;
    }

    std::string uid = uidIt->second;
    std::string device = deviceIt->second;

    // Get camera position from context (no offsets - those are system-level)
    if (!context) {
      errorMessage = "SequenceContext is required";
      return false;
    }

    double camX = static_cast<double>(context->GetNumeric("camera_x", 0.0f));
    double camY = static_cast<double>(context->GetNumeric("camera_y", 0.0f));
    double camZ = static_cast<double>(context->GetNumeric("camera_z", 0.0f));

    if (!SaveNavigationData(uid, device, camX, camY, camZ, errorMessage)) {
      return false;
    }

    return true;
  }

  // ============================================================================
  // SAVE NEEDLE Z CALIBRATION (Global)
  // ============================================================================
  else if (methodName == "SaveNeedleZCalibration") {
    auto deviceIt = params.find("device");
    auto pneumaticSlideIt = params.find("pneumatic_slide");

    if (deviceIt == params.end() || pneumaticSlideIt == params.end()) {
      errorMessage = "Missing required parameters: device, pneumatic_slide";
      return false;
    }

    std::string device = deviceIt->second;
    std::string pneumaticSlide = pneumaticSlideIt->second;

    // Get positions from context
    if (!context) {
      errorMessage = "SequenceContext is required";
      return false;
    }

    double needleX = static_cast<double>(context->GetNumeric("needle_x", 0.0f));
    double needleY = static_cast<double>(context->GetNumeric("needle_y", 0.0f));
    double needleZ = static_cast<double>(context->GetNumeric("needle_z", 0.0f));

    std::string deltaMessage;

    if (!SaveNeedleZCalibrationGlobal(
      device, pneumaticSlide,
      needleX, needleY, needleZ,
      errorMessage, deltaMessage)) {
      return false;
    }

    // Store delta message in context for notification
    if (context) {
      context->SetValue("calibration_result",
        "Needle Z calibration saved: Z = " + std::to_string(needleZ) + " mm" + deltaMessage,
        "calibration", "SaveNeedleZCalibration");
    }

    return true;
  }

  // ============================================================================
  // LOAD NEEDLE Z CALIBRATION
  // ============================================================================
  else if (methodName == "LoadNeedleZCalibration") {
    double needleZ = 0.0;

    bool exists = LoadNeedleZCalibrationGlobal(needleZ);

    if (m_logger) {
      if (exists) {
        m_logger->Info(
          "Loaded needle Z calibration: " + std::to_string(needleZ) + " mm",
          "CalibrationHandler"
        );
      } else {
        m_logger->Info(
          "No previous needle Z calibration found",
          "CalibrationHandler"
        );
      }
    }

    // Store in context
    if (context) {
      context->SetValue("needle_z_calibration_exists", exists, "calibration", "LoadNeedleZCalibration");

      if (exists) {
        context->SetValue("previous_needle_z", static_cast<float>(needleZ), "calibration", "LoadNeedleZCalibration");
      }
    }

    return true;
  }

  // ============================================================================
  // LOAD CAMERA-TO-NEEDLE OFFSET
  // ============================================================================
  else if (methodName == "LoadCameraToNeedleOffset") {
    double offsetX = 0.0, offsetY = 0.0, offsetZ = 0.0;
    bool exists = false;
    std::string source = "none";

    // Priority 1: Try product-specific offset if product is loaded
    std::string productName = "";

    // Try to get product name from ProductPositionManager first (most reliable)
    if (m_context && m_context->productPositionManager) {
      productName = m_context->productPositionManager->GetCurrentProductName();
    }

    // Fallback: Try to get from active DUT
    if (productName.empty() && m_context && m_context->currentDUT) {
      productName = m_context->currentDUT->getProductName();
    }

    if (!productName.empty()) {
      std::string productOffsetPath = "config/" + productName + "/calibration/needleXYoffsetCamera.json";

      if (std::filesystem::exists(productOffsetPath)) {
        // Load product-specific offset
        try {
          std::ifstream file(productOffsetPath);
          if (file.is_open()) {
            json j;
            file >> j;
            file.close();

            if (j.contains("offset") && j["offset"].contains("x") && j["offset"].contains("y")) {
              offsetX = j["offset"]["x"];
              offsetY = j["offset"]["y"];
              offsetZ = 0.0;
              exists = true;
              source = "product-specific (" + productName + ")";
            }
          }
        }
        catch (const std::exception& e) {
          if (m_logger) {
            m_logger->Warning("Failed to load product-specific offset: " + std::string(e.what()), "CalibrationHandler");
          }
        }
      } else {
        if (m_logger) {
          m_logger->Info("Product-specific offset not found: " + productOffsetPath, "CalibrationHandler");
        }
      }
    } else {
      if (m_logger) {
        m_logger->Info("No product loaded - will try SYSTEM offset", "CalibrationHandler");
      }
    }

    // Priority 2: Fallback to SYSTEM-level factory offset
    if (!exists) {
      exists = LoadCameraToNeedleOffset(offsetX, offsetY, offsetZ);
      if (exists) {
        source = "SYSTEM factory";
      }
    }

    // Log which offset was loaded
    if (m_logger && exists) {
      m_logger->Info(
        "Loaded " + source + " needle XY offset: (" +
        std::to_string(offsetX) + ", " + std::to_string(offsetY) + ")",
        "CalibrationHandler"
      );
    }

    // Store in context
    if (context) {
      context->SetValue("system_offset_exists", exists, "calibration", "LoadCameraToNeedleOffset");

      if (exists) {
        context->SetValue("system_offset_x", static_cast<float>(offsetX), "calibration", "LoadCameraToNeedleOffset");
        context->SetValue("system_offset_y", static_cast<float>(offsetY), "calibration", "LoadCameraToNeedleOffset");
        context->SetValue("system_offset_z", static_cast<float>(offsetZ), "calibration", "LoadCameraToNeedleOffset");
      }
    }

    return true;
  }

  // ============================================================================
  // SAVE CAMERA-TO-NEEDLE OFFSET
  // ============================================================================
  else if (methodName == "SaveCameraToNeedleOffset") {
    auto deviceIt = params.find("device");
    auto pneumaticSlideIt = params.find("pneumatic_slide");

    if (deviceIt == params.end() || pneumaticSlideIt == params.end()) {
      errorMessage = "Missing required parameters: device, pneumatic_slide";
      return false;
    }

    std::string device = deviceIt->second;
    std::string pneumaticSlide = pneumaticSlideIt->second;

    // Get positions from context
    if (!context) {
      errorMessage = "SequenceContext is required";
      return false;
    }

    double needleX = static_cast<double>(context->GetNumeric("needle_x", 0.0f));
    double needleY = static_cast<double>(context->GetNumeric("needle_y", 0.0f));
    double needleZ = static_cast<double>(context->GetNumeric("needle_z", 0.0f));

    double cameraX = static_cast<double>(context->GetNumeric("camera_x", 0.0f));
    double cameraY = static_cast<double>(context->GetNumeric("camera_y", 0.0f));
    double cameraZ = static_cast<double>(context->GetNumeric("camera_z", 0.0f));

    std::string resultMessage;

    if (!SaveCameraToNeedleOffset(
      device, pneumaticSlide,
      needleX, needleY, needleZ,
      cameraX, cameraY, cameraZ,
      errorMessage, resultMessage)) {
      return false;
    }

    // Store result message in context for notification
    if (context) {
      context->SetValue("offset_result", resultMessage, "calibration", "SaveCameraToNeedleOffset");
    }

    return true;
  }

  // ============================================================================
  // LOAD DISPENSE Z HEIGHT
  // ============================================================================
  else if (methodName == "LoadDispenseZHeight") {
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
      errorMessage = "LoadDispenseZHeight: Missing required parameter 'uid'";
      return false;
    }

    std::string uid = uidIt->second;
    double dispenseZ = 0.0;

    bool exists = LoadDispenseZHeight(uid, dispenseZ);

    if (context) {
      context->SetValue("dispense_z_exists", exists, "calibration", "LoadDispenseZHeight");
      if (exists) {
        context->SetValue("dispense_z", static_cast<float>(dispenseZ), "calibration", "LoadDispenseZHeight");

        if (m_logger) {
          m_logger->Info("Loaded dispense Z height: " + std::to_string(dispenseZ) + " mm for UID: " + uid,
                        "CalibrationHandler");
        }
      } else {
        if (m_logger) {
          m_logger->Info("No saved dispense Z height found for UID: " + uid,
                        "CalibrationHandler");
        }
      }
    }

    return true;
  }

  // ============================================================================
  // SAVE DISPENSE Z HEIGHT
  // ============================================================================
  else if (methodName == "SaveDispenseZHeight") {
    auto uidIt = params.find("uid");
    auto deviceIt = params.find("device");

    if (uidIt == params.end() || deviceIt == params.end()) {
      errorMessage = "SaveDispenseZHeight: Missing required parameters 'uid' and 'device'";
      return false;
    }

    std::string uid = uidIt->second;
    std::string device = deviceIt->second;

    if (!context) {
      errorMessage = "SaveDispenseZHeight: Context not available";
      return false;
    }

    double dispenseZ = static_cast<double>(context->GetNumeric("dispense_z", 0.0f));

    if (!SaveDispenseZHeight(uid, device, dispenseZ, errorMessage)) {
      return false;
    }

    if (m_logger) {
      m_logger->Info("Saved dispense Z height: " + std::to_string(dispenseZ) + " mm for UID: " + uid,
                    "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown calibration method: " + methodName;
    return false;
  }
}

// =============================================================================
// DISPENSE Z HEIGHT HELPER METHODS
// =============================================================================

std::string CalibrationCommandHandler::GetDispenseZFilePath(const std::string& uid) const
{
  // Use SYSTEM folder for dispense Z heights
  return "config/SYSTEM/dispense_z_" + uid + ".json";
}

bool CalibrationCommandHandler::SaveDispenseZHeight(
  const std::string& uid,
  const std::string& deviceName,
  double dispenseZ,
  std::string& errorMessage)
{
  try {
    // Ensure SYSTEM directory exists
    if (!EnsureCalibrationDirectoryExists(errorMessage)) {
      return false;
    }

    std::string filePath = GetDispenseZFilePath(uid);

    // Create JSON structure
    json data;
    data["uid"] = uid;
    data["device"] = deviceName;
    data["dispense_z"] = dispenseZ;
    data["timestamp"] = GetCurrentDateTime(false);
    data["date"] = GetCurrentDateTime(true);

    // Write to file
    std::ofstream file(filePath);
    if (!file.is_open()) {
      errorMessage = "Failed to open file for writing: " + filePath;
      return false;
    }

    file << data.dump(2);  // Pretty print with 2-space indentation
    file.close();

    if (m_logger) {
      m_logger->Info("Saved dispense Z height to: " + filePath, "CalibrationHandler");
    }

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception saving dispense Z height: " + std::string(e.what());
    return false;
  }
}

bool CalibrationCommandHandler::LoadDispenseZHeight(
  const std::string& uid,
  double& dispenseZ)
{
  try {
    std::string filePath = GetDispenseZFilePath(uid);

    if (!std::filesystem::exists(filePath)) {
      return false;  // File doesn't exist
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    json data;
    file >> data;
    file.close();

    // Extract dispense Z height
    if (data.contains("dispense_z")) {
      dispenseZ = data["dispense_z"].get<double>();
      return true;
    }

    return false;
  }
  catch (const std::exception& e) {
    if (m_logger) {
      m_logger->Error("Exception loading dispense Z height: " + std::string(e.what()), "CalibrationHandler");
    }
    return false;
  }
}

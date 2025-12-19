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
#include <cmath>

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
    // Support both formats:
    // 1. New format: {"offset": {"x": ..., "y": ...}}
    // 2. Legacy format: {"x": ..., "y": ...}
    if (j.contains("offset") && j["offset"].contains("x") && j["offset"].contains("y")) {
      // New format with nested "offset" object
      offsetX = j["offset"]["x"];
      offsetY = j["offset"]["y"];
      offsetZ = 0.0;
    } else if (j.contains("x") && j.contains("y")) {
      // Legacy format with direct x/y keys
      offsetX = j["x"];
      offsetY = j["y"];
      offsetZ = 0.0;
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

    // ✅ ALWAYS load from SYSTEM folder only (latest calibration result)
    exists = LoadCameraToNeedleOffset(offsetX, offsetY, offsetZ);
    if (exists) {
      source = "SYSTEM";
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
  // LOAD YAW CORRECTION DATA
  // ============================================================================
  else if (methodName == "LoadYawCorrectionData") {
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
      errorMessage = "LoadYawCorrectionData: Missing required parameter 'uid'";
      return false;
    }

    std::string uid = uidIt->second;
    double corner1X = 0.0, corner1Y = 0.0;
    double corner2X = 0.0, corner2Y = 0.0;
    double previousYawAngle = 0.0;

    bool exists = LoadYawCorrectionData(uid, corner1X, corner1Y, corner2X, corner2Y, previousYawAngle);

    if (context) {
      context->SetValue("yaw_data_exists", exists, "calibration", "LoadYawCorrectionData");
      if (exists) {
        context->SetValue("corner1_x", static_cast<float>(corner1X), "calibration", "LoadYawCorrectionData");
        context->SetValue("corner1_y", static_cast<float>(corner1Y), "calibration", "LoadYawCorrectionData");
        context->SetValue("corner2_x", static_cast<float>(corner2X), "calibration", "LoadYawCorrectionData");
        context->SetValue("corner2_y", static_cast<float>(corner2Y), "calibration", "LoadYawCorrectionData");
        context->SetValue("previous_yaw_angle", static_cast<float>(previousYawAngle), "calibration", "LoadYawCorrectionData");

        if (m_logger) {
          m_logger->Info("Loaded yaw correction data for UID: " + uid +
                        " - Yaw angle: " + std::to_string(previousYawAngle) + " deg",
                        "CalibrationHandler");
        }
      } else {
        if (m_logger) {
          m_logger->Info("No saved yaw correction data found for UID: " + uid, "CalibrationHandler");
        }
      }
    }

    return true;
  }

  // ============================================================================
  // CALCULATE AND SAVE YAW CORRECTION
  // ============================================================================
  else if (methodName == "CalculateAndSaveYawCorrection") {
    auto uidIt = params.find("uid");
    auto gantryDeviceIt = params.find("gantry_device");

    if (uidIt == params.end() || gantryDeviceIt == params.end()) {
      errorMessage = "CalculateAndSaveYawCorrection: Missing required parameters 'uid' and 'gantry_device'";
      return false;
    }

    std::string uid = uidIt->second;
    std::string gantryDevice = gantryDeviceIt->second;

    if (!context) {
      errorMessage = "CalculateAndSaveYawCorrection: Context not available";
      return false;
    }

    // Get corner positions from context
    double corner1X = static_cast<double>(context->GetNumeric("corner1_x", 0.0f));
    double corner1Y = static_cast<double>(context->GetNumeric("corner1_y", 0.0f));
    double corner2X = static_cast<double>(context->GetNumeric("corner2_x", 0.0f));
    double corner2Y = static_cast<double>(context->GetNumeric("corner2_y", 0.0f));

    // Calculate yaw angle
    double yawAngleDegrees = CalculateYawAngle(corner1X, corner1Y, corner2X, corner2Y);

    // Store yaw angle in context
    context->SetValue("yaw_angle_degrees", static_cast<float>(yawAngleDegrees), "calibration", "CalculateAndSaveYawCorrection");

    // Save to file
    if (!SaveYawCorrectionData(uid, gantryDevice, corner1X, corner1Y, corner2X, corner2Y, yawAngleDegrees, errorMessage)) {
      return false;
    }

    if (m_logger) {
      m_logger->Info("Calculated and saved yaw correction: " + std::to_string(yawAngleDegrees) + " deg for UID: " + uid,
                    "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // NAVIGATE TO SAVED CORNER (for yaw correction)
  // ============================================================================
  else if (methodName == "NavigateToSavedCorner") {
    auto deviceIt = params.find("device");
    auto cornerIt = params.find("corner");

    if (deviceIt == params.end() || cornerIt == params.end()) {
      errorMessage = "NavigateToSavedCorner: Missing required parameters 'device' and 'corner'";
      return false;
    }

    std::string device = deviceIt->second;
    std::string corner = cornerIt->second;

    if (!context) {
      errorMessage = "NavigateToSavedCorner: Context not available";
      return false;
    }

    // Get corner position from context
    double x = 0.0, y = 0.0;
    if (corner == "1") {
      x = static_cast<double>(context->GetNumeric("corner1_x", 0.0f));
      y = static_cast<double>(context->GetNumeric("corner1_y", 0.0f));
    } else if (corner == "2") {
      x = static_cast<double>(context->GetNumeric("corner2_x", 0.0f));
      y = static_cast<double>(context->GetNumeric("corner2_y", 0.0f));
    } else {
      errorMessage = "NavigateToSavedCorner: Invalid corner '" + corner + "' (must be '1' or '2')";
      return false;
    }

    // Navigate to saved position
    if (!m_context || !m_context->motionManager) {
      errorMessage = "NavigateToSavedCorner: Motion manager not available";
      return false;
    }

    // Get current Z to maintain height
    double currentZ = 0.0;
    m_context->motionManager->GetPosition(device, "Z", currentZ);

    if (!m_context->motionManager->MoveToAbs(device, x, y, currentZ, 100.0, true)) {
      errorMessage = "NavigateToSavedCorner: Failed to move to corner " + corner;
      return false;
    }

    if (m_logger) {
      m_logger->Info("Navigated to saved corner " + corner + ": (" +
                    std::to_string(x) + ", " + std::to_string(y) + ")",
                    "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // APPLY YAW CORRECTION
  // ============================================================================
  else if (methodName == "ApplyYawCorrection") {
    auto uidIt = params.find("uid");
    auto targetDeviceIt = params.find("target_device");
    auto xDirectionIt = params.find("x_direction");

    if (uidIt == params.end()) {
      errorMessage = "ApplyYawCorrection: Missing required parameter 'uid'";
      return false;
    }

    std::string uid = uidIt->second;
    std::string targetDevice = (targetDeviceIt != params.end()) ? targetDeviceIt->second : "";
    // X direction determines sign: "up" = positive U, "down" = negative U
    std::string xDirection = (xDirectionIt != params.end()) ? xDirectionIt->second : "up";

    if (targetDevice.empty()) {
      errorMessage = "ApplyYawCorrection: Missing target_device parameter";
      return false;
    }

    if (!context) {
      errorMessage = "ApplyYawCorrection: Context not available";
      return false;
    }

    // Get yaw angle from context (may have been loaded or just calculated)
    double yawAngleDegrees = 0.0;

    // First check if yaw_angle_degrees is in context (from CalculateAndSaveYawCorrection)
    if (context->HasValue("yaw_angle_degrees")) {
      yawAngleDegrees = static_cast<double>(context->GetNumeric("yaw_angle_degrees", 0.0f));
    } else {
      // Load from file if not in context
      double corner1X, corner1Y, corner2X, corner2Y, previousYawAngle;
      if (LoadYawCorrectionData(uid, corner1X, corner1Y, corner2X, corner2Y, previousYawAngle)) {
        yawAngleDegrees = previousYawAngle;
        context->SetValue("yaw_angle_degrees", static_cast<float>(yawAngleDegrees), "calibration", "ApplyYawCorrection");
      } else {
        errorMessage = "ApplyYawCorrection: No yaw correction data found for UID: " + uid;
        return false;
      }
    }

    // Apply correction to target device
    if (!m_context || !m_context->motionManager) {
      errorMessage = "ApplyYawCorrection: Motion manager not available";
      return false;
    }

    // Correction is OPPOSITE of measured error (to compensate)
    // Then adjust sign based on X-axis orientation (right-hand rule)
    // "up" = negate once, "down" = negate twice (same as original)
    double uCorrection = -yawAngleDegrees;  // Negate to correct the error
    if (xDirection == "down") {
      uCorrection = -uCorrection;  // Flip for opposite hexapod orientation
    }

    if (!m_context->motionManager->MoveRelative(targetDevice, "U", uCorrection, true)) {
      errorMessage = "ApplyYawCorrection: Failed to move U axis on device: " + targetDevice;
      return false;
    }

    // Store correction in context
    context->SetValue("u_correction", static_cast<float>(uCorrection), "calibration", "ApplyYawCorrection");

    if (m_logger) {
      m_logger->Info("Applied yaw correction to " + targetDevice + ": U = " + std::to_string(uCorrection) +
                    " deg (X " + xDirection + ")", "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // SHOW YAW CORRECTION RESULT
  // ============================================================================
  else if (methodName == "ShowYawCorrectionResult") {
    auto targetDeviceIt = params.find("target_device");

    std::string targetDevice = (targetDeviceIt != params.end()) ? targetDeviceIt->second : "hexapod";

    if (!context) {
      errorMessage = "ShowYawCorrectionResult: Context not available";
      return false;
    }

    // Get values from context
    double corner1X = static_cast<double>(context->GetNumeric("corner1_x", 0.0f));
    double corner1Y = static_cast<double>(context->GetNumeric("corner1_y", 0.0f));
    double corner2X = static_cast<double>(context->GetNumeric("corner2_x", 0.0f));
    double corner2Y = static_cast<double>(context->GetNumeric("corner2_y", 0.0f));
    double yawAngle = static_cast<double>(context->GetNumeric("yaw_angle_degrees", 0.0f));
    double uCorrection = static_cast<double>(context->GetNumeric("u_correction", 0.0f));

    // Format result message
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "Corner1=(" << corner1X << ", " << corner1Y << ") ";
    oss << "Corner2=(" << corner2X << ", " << corner2Y << ") ";
    oss << "YawAngle=" << yawAngle << "deg ";
    oss << "U=" << uCorrection << "deg ";
    oss << "Target=" << targetDevice;

    // Store formatted result in context for display
    context->SetValue("yaw_result_message", oss.str(), "calibration", "ShowYawCorrectionResult");

    if (m_logger) {
      m_logger->Info("✅ Yaw Correction Complete: " + oss.str(), "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // LOAD YAW TO TEMPLATE DATA
  // ============================================================================
  else if (methodName == "LoadYawToTemplateData") {
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
      errorMessage = "LoadYawToTemplateData: Missing required parameter 'uid'";
      return false;
    }

    std::string uid = uidIt->second;

    double tc1x, tc1y, tc2x, tc2y;  // template corners
    double tc3x, tc3y, tc4x, tc4y;  // target corners
    double templateYaw, targetYaw, correctionAngle;

    bool exists = LoadYawToTemplateData(uid, tc1x, tc1y, tc2x, tc2y,
                                         tc3x, tc3y, tc4x, tc4y,
                                         templateYaw, targetYaw, correctionAngle);

    if (context) {
      context->SetValue("yaw_template_data_exists", exists, "calibration", "LoadYawToTemplateData");

      if (exists) {
        // Template corners (1,2)
        context->SetValue("template_corner1_x", static_cast<float>(tc1x), "calibration", "LoadYawToTemplateData");
        context->SetValue("template_corner1_y", static_cast<float>(tc1y), "calibration", "LoadYawToTemplateData");
        context->SetValue("template_corner2_x", static_cast<float>(tc2x), "calibration", "LoadYawToTemplateData");
        context->SetValue("template_corner2_y", static_cast<float>(tc2y), "calibration", "LoadYawToTemplateData");

        // Target corners (3,4)
        context->SetValue("target_corner3_x", static_cast<float>(tc3x), "calibration", "LoadYawToTemplateData");
        context->SetValue("target_corner3_y", static_cast<float>(tc3y), "calibration", "LoadYawToTemplateData");
        context->SetValue("target_corner4_x", static_cast<float>(tc4x), "calibration", "LoadYawToTemplateData");
        context->SetValue("target_corner4_y", static_cast<float>(tc4y), "calibration", "LoadYawToTemplateData");

        // Previous yaw values
        context->SetValue("previous_template_yaw", static_cast<float>(templateYaw), "calibration", "LoadYawToTemplateData");
        context->SetValue("previous_target_yaw", static_cast<float>(targetYaw), "calibration", "LoadYawToTemplateData");
        context->SetValue("previous_correction_angle", static_cast<float>(correctionAngle), "calibration", "LoadYawToTemplateData");
      }
    }

    if (m_logger) {
      if (exists) {
        m_logger->Info("Loaded yaw-to-template data for UID: " + uid, "CalibrationHandler");
      } else {
        m_logger->Info("No previous yaw-to-template data found for UID: " + uid, "CalibrationHandler");
      }
    }

    return true;
  }

  // ============================================================================
  // CALCULATE YAW TO TEMPLATE CORRECTION
  // ============================================================================
  else if (methodName == "CalculateYawToTemplateCorrection") {
    auto uidIt = params.find("uid");
    auto gantryDeviceIt = params.find("gantry_device");

    if (uidIt == params.end()) {
      errorMessage = "CalculateYawToTemplateCorrection: Missing required parameter 'uid'";
      return false;
    }

    std::string uid = uidIt->second;
    std::string gantryDevice = (gantryDeviceIt != params.end()) ? gantryDeviceIt->second : "gantry-main";

    if (!context) {
      errorMessage = "CalculateYawToTemplateCorrection: Context not available";
      return false;
    }

    // Get template corners (1,2) from context
    double tc1x = static_cast<double>(context->GetNumeric("template_corner1_x", 0.0f));
    double tc1y = static_cast<double>(context->GetNumeric("template_corner1_y", 0.0f));
    double tc2x = static_cast<double>(context->GetNumeric("template_corner2_x", 0.0f));
    double tc2y = static_cast<double>(context->GetNumeric("template_corner2_y", 0.0f));

    // Get target corners (3,4) from context
    double tc3x = static_cast<double>(context->GetNumeric("target_corner3_x", 0.0f));
    double tc3y = static_cast<double>(context->GetNumeric("target_corner3_y", 0.0f));
    double tc4x = static_cast<double>(context->GetNumeric("target_corner4_x", 0.0f));
    double tc4y = static_cast<double>(context->GetNumeric("target_corner4_y", 0.0f));

    // Calculate template yaw (from corners 1,2)
    double templateYaw = CalculateYawAngle(tc1x, tc1y, tc2x, tc2y);

    // Calculate target yaw (from corners 3,4)
    double targetYaw = CalculateYawAngle(tc3x, tc3y, tc4x, tc4y);

    // Calculate correction: to bring target to match template
    // correction = template_yaw - target_yaw
    // e.g., target=1, template=-1 => correction = -1 - 1 = -2 (target moves by -2 to match)
    double correctionAngle = templateYaw - targetYaw;

    // Store in context
    context->SetValue("template_yaw_degrees", static_cast<float>(templateYaw), "calibration", "CalculateYawToTemplateCorrection");
    context->SetValue("target_yaw_degrees", static_cast<float>(targetYaw), "calibration", "CalculateYawToTemplateCorrection");
    context->SetValue("yaw_correction_angle", static_cast<float>(correctionAngle), "calibration", "CalculateYawToTemplateCorrection");

    // Save to file
    std::string saveError;
    if (!SaveYawToTemplateData(uid, gantryDevice, tc1x, tc1y, tc2x, tc2y,
                                tc3x, tc3y, tc4x, tc4y,
                                templateYaw, targetYaw, correctionAngle, saveError)) {
      errorMessage = "CalculateYawToTemplateCorrection: Failed to save data - " + saveError;
      return false;
    }

    if (m_logger) {
      m_logger->Info("Calculated yaw-to-template: Template=" + std::to_string(templateYaw) +
                    "deg, Target=" + std::to_string(targetYaw) +
                    "deg, Correction=" + std::to_string(correctionAngle) + "deg",
                    "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // APPLY YAW TO TEMPLATE CORRECTION
  // ============================================================================
  else if (methodName == "ApplyYawToTemplateCorrection") {
    auto targetDeviceIt = params.find("target_device");
    auto xDirectionIt = params.find("x_direction");

    std::string targetDevice = (targetDeviceIt != params.end()) ? targetDeviceIt->second : "";
    std::string xDirection = (xDirectionIt != params.end()) ? xDirectionIt->second : "up";

    if (targetDevice.empty()) {
      errorMessage = "ApplyYawToTemplateCorrection: Missing target_device parameter";
      return false;
    }

    if (!context) {
      errorMessage = "ApplyYawToTemplateCorrection: Context not available";
      return false;
    }

    // Get correction angle from context
    if (!context->HasValue("yaw_correction_angle")) {
      errorMessage = "ApplyYawToTemplateCorrection: No yaw_correction_angle found in context";
      return false;
    }

    double correctionAngle = static_cast<double>(context->GetNumeric("yaw_correction_angle", 0.0f));

    if (!m_context || !m_context->motionManager) {
      errorMessage = "ApplyYawToTemplateCorrection: Motion manager not available";
      return false;
    }

    // Apply correction directly (template_yaw - target_yaw)
    // e.g., target=1, template=-1 => correction = -2 (target moves from 1 to -1)
    // Adjust sign based on X direction (hexapod orientation)
    double uCorrection = correctionAngle;
    if (xDirection == "down") {
      uCorrection = -uCorrection;
    }

    if (!m_context->motionManager->MoveRelative(targetDevice, "U", uCorrection, true)) {
      errorMessage = "ApplyYawToTemplateCorrection: Failed to move U axis on device: " + targetDevice;
      return false;
    }

    // Store correction in context (for SaveYawCorrectionToPosition)
    context->SetValue("u_correction", static_cast<float>(uCorrection), "calibration", "ApplyYawToTemplateCorrection");

    if (m_logger) {
      m_logger->Info("Applied yaw-to-template correction to " + targetDevice + ": U = " +
                    std::to_string(uCorrection) + " deg (X " + xDirection + ")",
                    "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // SHOW YAW TO TEMPLATE RESULT
  // ============================================================================
  else if (methodName == "ShowYawToTemplateResult") {
    auto targetDeviceIt = params.find("target_device");
    std::string targetDevice = (targetDeviceIt != params.end()) ? targetDeviceIt->second : "hexapod";

    if (!context) {
      errorMessage = "ShowYawToTemplateResult: Context not available";
      return false;
    }

    // Get values from context
    double tc1x = static_cast<double>(context->GetNumeric("template_corner1_x", 0.0f));
    double tc1y = static_cast<double>(context->GetNumeric("template_corner1_y", 0.0f));
    double tc2x = static_cast<double>(context->GetNumeric("template_corner2_x", 0.0f));
    double tc2y = static_cast<double>(context->GetNumeric("template_corner2_y", 0.0f));
    double tc3x = static_cast<double>(context->GetNumeric("target_corner3_x", 0.0f));
    double tc3y = static_cast<double>(context->GetNumeric("target_corner3_y", 0.0f));
    double tc4x = static_cast<double>(context->GetNumeric("target_corner4_x", 0.0f));
    double tc4y = static_cast<double>(context->GetNumeric("target_corner4_y", 0.0f));
    double templateYaw = static_cast<double>(context->GetNumeric("template_yaw_degrees", 0.0f));
    double targetYaw = static_cast<double>(context->GetNumeric("target_yaw_degrees", 0.0f));
    double correctionAngle = static_cast<double>(context->GetNumeric("yaw_correction_angle", 0.0f));
    double uCorrection = static_cast<double>(context->GetNumeric("u_correction", 0.0f));

    // Format result message
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "Template: Corner1(" << tc1x << "," << tc1y << ") Corner2(" << tc2x << "," << tc2y << ") Yaw=" << templateYaw << "deg | ";
    oss << "Target: Corner3(" << tc3x << "," << tc3y << ") Corner4(" << tc4x << "," << tc4y << ") Yaw=" << targetYaw << "deg | ";
    oss << "Correction=" << correctionAngle << "deg U=" << uCorrection << "deg Target=" << targetDevice;

    context->SetValue("yaw_template_result_message", oss.str(), "calibration", "ShowYawToTemplateResult");

    if (m_logger) {
      m_logger->Info("Yaw-to-Template Complete: TemplateYaw=" + std::to_string(templateYaw) +
                    "deg, TargetYaw=" + std::to_string(targetYaw) +
                    "deg, U=" + std::to_string(uCorrection) + "deg",
                    "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // SAVE YAW CORRECTION TO NAMED POSITION
  // ============================================================================
  else if (methodName == "SaveYawCorrectionToPosition") {
    auto posDeviceIt = params.find("position_device");
    auto posNameIt = params.find("position_name");

    if (posDeviceIt == params.end() || posNameIt == params.end()) {
      errorMessage = "SaveYawCorrectionToPosition: Missing required parameters 'position_device' and 'position_name'";
      return false;
    }

    std::string positionDevice = posDeviceIt->second;
    std::string positionName = posNameIt->second;

    if (positionName.empty()) {
      errorMessage = "SaveYawCorrectionToPosition: Position name cannot be empty";
      return false;
    }

    if (!context) {
      errorMessage = "SaveYawCorrectionToPosition: Context not available";
      return false;
    }

    // Get U correction from context (set by ApplyYawCorrection)
    if (!context->HasValue("u_correction")) {
      errorMessage = "SaveYawCorrectionToPosition: No u_correction value found in context. Run ApplyYawCorrection first.";
      return false;
    }

    double uCorrection = static_cast<double>(context->GetNumeric("u_correction", 0.0f));

    // Get ProductPositionManager
    if (!m_context || !m_context->productPositionManager) {
      errorMessage = "SaveYawCorrectionToPosition: ProductPositionManager not available";
      return false;
    }

    // Get current position
    PositionStruct currentPos;
    if (!m_context->productPositionManager->GetPosition(positionDevice, positionName, currentPos)) {
      errorMessage = "SaveYawCorrectionToPosition: Position '" + positionName + "' not found for device '" + positionDevice + "'";
      return false;
    }

    // Update only U value (add correction to existing U)
    double newU = currentPos.u + uCorrection;

    // Create updated position
    PositionStruct updatedPos = currentPos;
    updatedPos.u = newU;

    // Save back to ProductPositionManager
    m_context->productPositionManager->AddPosition(positionDevice, positionName, updatedPos);

    // Persist to disk
    if (!m_context->productPositionManager->SaveProduct()) {
      errorMessage = "SaveYawCorrectionToPosition: Failed to save product file";
      return false;
    }

    // Also update the motion manager's cached positions
    if (m_context->motionManager) {
      m_context->motionManager->AddNamedPosition(positionDevice, positionName, updatedPos);
    }

    if (m_logger) {
      m_logger->Info("✅ Saved U correction to position: " + positionDevice + "/" + positionName +
                    " (U: " + std::to_string(currentPos.u) + " -> " + std::to_string(newU) + " deg)",
                    "CalibrationHandler");
    }

    std::cout << "[CalibrationCommandHandler] SaveYawCorrectionToPosition: " << positionDevice
              << "/" << positionName << " U=" << newU << " deg" << std::endl;

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

// =============================================================================
// YAW CORRECTION HELPER METHODS
// =============================================================================

std::string CalibrationCommandHandler::GetYawCorrectionFilePath(const std::string& uid) const
{
  // Use product-specific instances_data folder for yaw correction data
  std::string productName = "default";
  if (m_context && m_context->productPositionManager) {
    std::string currentProduct = m_context->productPositionManager->GetCurrentProductName();
    if (!currentProduct.empty()) {
      productName = currentProduct;
    }
  }
  return "config/" + productName + "/instances_data/yaw_correction_" + uid + ".json";
}

bool CalibrationCommandHandler::LoadYawCorrectionData(
  const std::string& uid,
  double& corner1X, double& corner1Y,
  double& corner2X, double& corner2Y,
  double& previousYawAngle)
{
  try {
    std::string filePath = GetYawCorrectionFilePath(uid);

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

    // Extract corner positions and yaw angle
    if (data.contains("corner1") && data.contains("corner2") && data.contains("yaw_angle_degrees")) {
      corner1X = data["corner1"]["x"].get<double>();
      corner1Y = data["corner1"]["y"].get<double>();
      corner2X = data["corner2"]["x"].get<double>();
      corner2Y = data["corner2"]["y"].get<double>();
      previousYawAngle = data["yaw_angle_degrees"].get<double>();
      return true;
    }

    return false;
  }
  catch (const std::exception& e) {
    if (m_logger) {
      m_logger->Error("Exception loading yaw correction data: " + std::string(e.what()), "CalibrationHandler");
    }
    return false;
  }
}

bool CalibrationCommandHandler::SaveYawCorrectionData(
  const std::string& uid,
  const std::string& gantryDevice,
  double corner1X, double corner1Y,
  double corner2X, double corner2Y,
  double yawAngleDegrees,
  std::string& errorMessage)
{
  try {
    // Ensure instances_data directory exists for current product
    std::string filePath = GetYawCorrectionFilePath(uid);
    std::filesystem::path dirPath = std::filesystem::path(filePath).parent_path();

    if (!std::filesystem::exists(dirPath)) {
      if (!std::filesystem::create_directories(dirPath)) {
        errorMessage = "Failed to create instances_data directory: " + dirPath.string();
        return false;
      }
      if (m_logger) {
        m_logger->Info("Created instances_data directory: " + dirPath.string(), "CalibrationHandler");
      }
    }

    // Calculate distance between corners
    double deltaX = corner2X - corner1X;
    double deltaY = corner2Y - corner1Y;
    double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);

    // Create JSON structure
    json data;
    data["uid"] = uid;
    data["gantry_device"] = gantryDevice;
    data["corner1"]["x"] = corner1X;
    data["corner1"]["y"] = corner1Y;
    data["corner2"]["x"] = corner2X;
    data["corner2"]["y"] = corner2Y;
    data["delta"]["x"] = deltaX;
    data["delta"]["y"] = deltaY;
    data["distance"] = distance;
    data["yaw_angle_degrees"] = yawAngleDegrees;
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
      m_logger->Info("Saved yaw correction data to: " + filePath +
                    " (Yaw: " + std::to_string(yawAngleDegrees) + " deg)",
                    "CalibrationHandler");
    }

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception saving yaw correction data: " + std::string(e.what());
    return false;
  }
}

double CalibrationCommandHandler::CalculateYawAngle(
  double corner1X, double corner1Y,
  double corner2X, double corner2Y)
{
  // Calculate delta between corners
  double deltaX = corner2X - corner1X;
  double deltaY = corner2Y - corner1Y;

  // Calculate yaw angle using atan2
  // The yaw angle is the angle from the Y-axis (vertical line)
  // atan2(deltaX, deltaY) gives angle from Y-axis
  double yawRadians = std::atan2(deltaX, deltaY);

  // Convert to degrees
  double yawDegrees = yawRadians * 180.0 / 3.14159265358979323846;

  if (m_logger) {
    m_logger->Info("Calculated yaw angle: " + std::to_string(yawDegrees) + " deg"
                  " (deltaX=" + std::to_string(deltaX) +
                  ", deltaY=" + std::to_string(deltaY) + ")",
                  "CalibrationHandler");
  }

  return yawDegrees;
}

// =============================================================================
// YAW CORRECTION TO TEMPLATE HELPER METHODS
// =============================================================================

std::string CalibrationCommandHandler::GetYawToTemplateFilePath(const std::string& uid) const
{
  // Use product-specific instances_data folder for yaw-to-template data
  std::string productName = "default";
  if (m_context && m_context->productPositionManager) {
    std::string currentProduct = m_context->productPositionManager->GetCurrentProductName();
    if (!currentProduct.empty()) {
      productName = currentProduct;
    }
  }
  return "config/" + productName + "/instances_data/yaw_to_template_" + uid + ".json";
}

bool CalibrationCommandHandler::LoadYawToTemplateData(
  const std::string& uid,
  double& templateCorner1X, double& templateCorner1Y,
  double& templateCorner2X, double& templateCorner2Y,
  double& targetCorner3X, double& targetCorner3Y,
  double& targetCorner4X, double& targetCorner4Y,
  double& templateYaw, double& targetYaw, double& correctionAngle)
{
  try {
    std::string filePath = GetYawToTemplateFilePath(uid);

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

    // Extract all corner positions and yaw angles
    if (data.contains("template_corner1") && data.contains("template_corner2") &&
        data.contains("target_corner3") && data.contains("target_corner4") &&
        data.contains("template_yaw_degrees") && data.contains("target_yaw_degrees")) {

      templateCorner1X = data["template_corner1"]["x"].get<double>();
      templateCorner1Y = data["template_corner1"]["y"].get<double>();
      templateCorner2X = data["template_corner2"]["x"].get<double>();
      templateCorner2Y = data["template_corner2"]["y"].get<double>();

      targetCorner3X = data["target_corner3"]["x"].get<double>();
      targetCorner3Y = data["target_corner3"]["y"].get<double>();
      targetCorner4X = data["target_corner4"]["x"].get<double>();
      targetCorner4Y = data["target_corner4"]["y"].get<double>();

      templateYaw = data["template_yaw_degrees"].get<double>();
      targetYaw = data["target_yaw_degrees"].get<double>();
      correctionAngle = data["correction_angle_degrees"].get<double>();

      return true;
    }

    return false;
  }
  catch (const std::exception& e) {
    if (m_logger) {
      m_logger->Error("Exception loading yaw-to-template data: " + std::string(e.what()), "CalibrationHandler");
    }
    return false;
  }
}

bool CalibrationCommandHandler::SaveYawToTemplateData(
  const std::string& uid,
  const std::string& gantryDevice,
  double templateCorner1X, double templateCorner1Y,
  double templateCorner2X, double templateCorner2Y,
  double targetCorner3X, double targetCorner3Y,
  double targetCorner4X, double targetCorner4Y,
  double templateYaw, double targetYaw, double correctionAngle,
  std::string& errorMessage)
{
  try {
    // Ensure instances_data directory exists for current product
    std::string filePath = GetYawToTemplateFilePath(uid);
    std::filesystem::path dirPath = std::filesystem::path(filePath).parent_path();

    if (!std::filesystem::exists(dirPath)) {
      if (!std::filesystem::create_directories(dirPath)) {
        errorMessage = "Failed to create instances_data directory: " + dirPath.string();
        return false;
      }
      if (m_logger) {
        m_logger->Info("Created instances_data directory: " + dirPath.string(), "CalibrationHandler");
      }
    }

    // Create JSON structure
    json data;
    data["uid"] = uid;
    data["gantry_device"] = gantryDevice;

    // Template corners (fixed/reference object)
    data["template_corner1"]["x"] = templateCorner1X;
    data["template_corner1"]["y"] = templateCorner1Y;
    data["template_corner2"]["x"] = templateCorner2X;
    data["template_corner2"]["y"] = templateCorner2Y;

    // Target corners (object to be corrected)
    data["target_corner3"]["x"] = targetCorner3X;
    data["target_corner3"]["y"] = targetCorner3Y;
    data["target_corner4"]["x"] = targetCorner4X;
    data["target_corner4"]["y"] = targetCorner4Y;

    // Calculated yaw angles
    data["template_yaw_degrees"] = templateYaw;
    data["target_yaw_degrees"] = targetYaw;
    data["correction_angle_degrees"] = correctionAngle;

    // Timestamp
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
      m_logger->Info("Saved yaw-to-template data to: " + filePath +
                    " (Template: " + std::to_string(templateYaw) + " deg" +
                    ", Target: " + std::to_string(targetYaw) + " deg" +
                    ", Correction: " + std::to_string(correctionAngle) + " deg)",
                    "CalibrationHandler");
    }

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception saving yaw-to-template data: " + std::string(e.what());
    return false;
  }
}

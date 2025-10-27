// CalibrationCommandHandler.cpp
#include "CalibrationCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
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

std::string CalibrationCommandHandler::GetCalibrationFilePath() const
{
  return "config/calibration/CalibrateNeedleZ.json";
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
    std::filesystem::path dirPath = "config/calibration";

    if (!std::filesystem::exists(dirPath)) {
      if (!std::filesystem::create_directories(dirPath)) {
        errorMessage = "Failed to create calibration directory";
        return false;
      }

      if (m_logger) {
        m_logger->Info("Created calibration directory: config/calibration", "CalibrationHandler");
      }
    }

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception creating calibration directory: " + std::string(e.what());
    return false;
  }
}

bool CalibrationCommandHandler::LoadExistingCalibration(
  double& oldNeedleHeight,
  double& camX, double& camY, double& camZ,
  double& offsetX, double& offsetY, double& offsetZ)
{
  try {
    std::string filePath = GetCalibrationFilePath();

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

    // Load old needle height
    if (j.contains("parameters") && j["parameters"].contains("needle_height")) {
      oldNeedleHeight = j["parameters"]["needle_height"];
    }

    // Load camera position
    if (j.contains("camera_position")) {
      camX = j["camera_position"]["x"];
      camY = j["camera_position"]["y"];
      camZ = j["camera_position"]["z"];
    }

    // Load offsets
    if (j.contains("offsets")) {
      offsetX = j["offsets"]["x"];
      offsetY = j["offsets"]["y"];
      offsetZ = j["offsets"]["z"];
    }

    if (m_logger) {
      m_logger->Info(
        "Loaded existing calibration: needle_height=" + std::to_string(oldNeedleHeight),
        "CalibrationHandler"
      );
    }

    return true;
  }
  catch (const std::exception& e) {
    if (m_logger) {
      m_logger->Warning(
        "Failed to load existing calibration: " + std::string(e.what()),
        "CalibrationHandler"
      );
    }
    return false;
  }
}

bool CalibrationCommandHandler::SaveNeedleZCalibration(
  const std::string& deviceName,
  const std::string& pneumaticSlide,
  double camX, double camY, double camZ,
  double offsetX, double offsetY, double offsetZ,
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
    double oldCamX = 0.0, oldCamY = 0.0, oldCamZ = 0.0;
    double oldOffsetX = 0.0, oldOffsetY = 0.0, oldOffsetZ = 0.0;
    bool hadPreviousCalibration = LoadExistingCalibration(
      oldNeedleHeight, oldCamX, oldCamY, oldCamZ, oldOffsetX, oldOffsetY, oldOffsetZ
    );

    // Calculate delta
    double delta = 0.0;
    if (hadPreviousCalibration) {
      delta = needleZ - oldNeedleHeight;
    }

    // Build JSON structure
    json j;

    // Metadata
    j["metadata"]["calibration_type"] = "Needle Z Position";
    j["metadata"]["device"] = deviceName;
    j["metadata"]["calibrated_date"] = GetCurrentDateTime(true);
    j["metadata"]["calibrated_time"] = GetCurrentDateTime(false);
    j["metadata"]["pneumatic_slide"] = pneumaticSlide;

    // Camera position
    j["camera_position"]["x"] = camX;
    j["camera_position"]["y"] = camY;
    j["camera_position"]["z"] = camZ;

    // Offsets
    j["offsets"]["x"] = offsetX;
    j["offsets"]["y"] = offsetY;
    j["offsets"]["z"] = offsetZ;

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

    // Write to file
    std::string filePath = GetCalibrationFilePath();
    std::ofstream file(filePath);

    if (!file.is_open()) {
      errorMessage = "Failed to open calibration file for writing: " + filePath;
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
        "Saved needle Z calibration: Z=" + std::to_string(needleZ) + deltaMessage,
        "CalibrationHandler"
      );
    }

    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception saving calibration: " + std::string(e.what());
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
  // LOAD EXISTING CALIBRATION
  // ============================================================================
  if (methodName == "LoadExistingCalibration") {
    double oldNeedleHeight = 0.0;
    double camX = 0.0, camY = 0.0, camZ = 0.0;
    double offsetX = 0.0, offsetY = 0.0, offsetZ = 0.0;

    bool exists = LoadExistingCalibration(
      oldNeedleHeight, camX, camY, camZ, offsetX, offsetY, offsetZ
    );

    // Store in context
    if (context) {
      context->SetValue("calibration_exists", exists, "calibration", "LoadExistingCalibration");

      if (exists) {
        context->SetValue("camera_x", static_cast<float>(camX), "calibration", "LoadExistingCalibration");
        context->SetValue("camera_y", static_cast<float>(camY), "calibration", "LoadExistingCalibration");
        context->SetValue("camera_z", static_cast<float>(camZ), "calibration", "LoadExistingCalibration");
        context->SetValue("offset_x", static_cast<float>(offsetX), "calibration", "LoadExistingCalibration");
        context->SetValue("offset_y", static_cast<float>(offsetY), "calibration", "LoadExistingCalibration");
        context->SetValue("offset_z", static_cast<float>(offsetZ), "calibration", "LoadExistingCalibration");
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
  // SAVE NEEDLE Z CALIBRATION
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

    double camX = static_cast<double>(context->GetNumeric("camera_x", 0.0f));
    double camY = static_cast<double>(context->GetNumeric("camera_y", 0.0f));
    double camZ = static_cast<double>(context->GetNumeric("camera_z", 0.0f));

    double offsetX = static_cast<double>(context->GetNumeric("offset_x", 0.0f));
    double offsetY = static_cast<double>(context->GetNumeric("offset_y", 0.0f));
    double offsetZ = static_cast<double>(context->GetNumeric("offset_z", 0.0f));

    double needleX = static_cast<double>(context->GetNumeric("needle_x", 0.0f));
    double needleY = static_cast<double>(context->GetNumeric("needle_y", 0.0f));
    double needleZ = static_cast<double>(context->GetNumeric("needle_z", 0.0f));

    std::string deltaMessage;

    if (!SaveNeedleZCalibration(
      device, pneumaticSlide,
      camX, camY, camZ,
      offsetX, offsetY, offsetZ,
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
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown calibration method: " + methodName;
    return false;
  }
}

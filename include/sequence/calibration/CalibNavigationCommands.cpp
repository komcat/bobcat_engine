// CalibNavigationCommands.cpp — Family A: navigation calibration.
// Owns the camera-position-only navigation record:
//   config/SYSTEM/navigation_<uid>.json
// Branches: LoadExistingCalibration, CapturePosition, SaveNavigationData.
#include "CalibNavigationCommands.h"

#include "../CalibrationCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

namespace {

std::string NavigationFilePath(const std::string& uid)
{
  return "config/SYSTEM/navigation_" + uid + ".json";
}

bool LoadNavigationData(
    const std::string& uid,
    double& camX, double& camY, double& camZ,
    LogManager* logger)
{
  try {
    std::string filePath = NavigationFilePath(uid);

    if (!std::filesystem::exists(filePath)) {
      return false;  // First calibration — file not yet written.
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    json j;
    file >> j;
    file.close();

    if (j.contains("camera_position")) {
      camX = j["camera_position"]["x"];
      camY = j["camera_position"]["y"];
      camZ = j["camera_position"]["z"];
    }

    if (logger) {
      logger->Info(
        "Loaded navigation data for UID: " + uid + " - Camera: (" +
        std::to_string(camX) + ", " + std::to_string(camY) + ", " + std::to_string(camZ) + ")",
        "CalibrationHandler"
      );
    }
    return true;
  }
  catch (const std::exception& e) {
    if (logger) {
      logger->Warning(
        "Failed to load navigation data: " + std::string(e.what()),
        "CalibrationHandler"
      );
    }
    return false;
  }
}

bool SaveNavigationData(
    const std::string& uid,
    const std::string& deviceName,
    double camX, double camY, double camZ,
    std::string& errorMessage,
    CalibrationCommandHandler* parent,
    LogManager* logger)
{
  try {
    if (!parent->EnsureCalibrationDirectoryExists(errorMessage)) {
      return false;
    }

    json j;
    j["metadata"]["uid"] = uid;
    j["metadata"]["device"] = deviceName;
    j["metadata"]["last_updated_date"] = parent->GetCurrentDateTime(true);
    j["metadata"]["last_updated_time"] = parent->GetCurrentDateTime(false);
    j["metadata"]["description"] = "Camera position for auto-navigation (SYSTEM-wide)";

    j["camera_position"]["x"] = camX;
    j["camera_position"]["y"] = camY;
    j["camera_position"]["z"] = camZ;

    std::string filePath = NavigationFilePath(uid);
    std::ofstream file(filePath);

    if (!file.is_open()) {
      errorMessage = "Failed to open navigation file for writing: " + filePath;
      return false;
    }

    file << j.dump(2);
    file.close();

    if (logger) {
      logger->Info(
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

}  // namespace

std::optional<bool> CalibNavigationCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

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

    bool exists = LoadNavigationData(uid, camX, camY, camZ, logger);

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
  if (methodName == "CapturePosition") {
    auto deviceIt = params.find("device");
    auto keyPrefixIt = params.find("key_prefix");

    if (deviceIt == params.end() || keyPrefixIt == params.end()) {
      errorMessage = "Missing required parameters: device, key_prefix";
      return false;
    }

    std::string device = deviceIt->second;
    std::string keyPrefix = keyPrefixIt->second;

    if (!deps.app || !deps.app->motionManager) {
      errorMessage = "Motion manager not available";
      return false;
    }

    double x = 0.0, y = 0.0, z = 0.0;

    if (!deps.app->motionManager->GetPosition(device, "X", x) ||
        !deps.app->motionManager->GetPosition(device, "Y", y) ||
        !deps.app->motionManager->GetPosition(device, "Z", z)) {
      errorMessage = "Failed to read position from device: " + device;
      return false;
    }

    if (context) {
      context->SetValue(keyPrefix + "_x", static_cast<float>(x), device, methodName);
      context->SetValue(keyPrefix + "_y", static_cast<float>(y), device, methodName);
      context->SetValue(keyPrefix + "_z", static_cast<float>(z), device, methodName);
    }

    if (logger) {
      logger->Info(
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
  if (methodName == "SaveNavigationData") {
    auto uidIt = params.find("uid");
    auto deviceIt = params.find("device");

    if (uidIt == params.end() || deviceIt == params.end()) {
      errorMessage = "Missing required parameters: uid, device";
      return false;
    }

    std::string uid = uidIt->second;
    std::string device = deviceIt->second;

    if (!context) {
      errorMessage = "SequenceContext is required";
      return false;
    }

    double camX = static_cast<double>(context->GetNumeric("camera_x", 0.0f));
    double camY = static_cast<double>(context->GetNumeric("camera_y", 0.0f));
    double camZ = static_cast<double>(context->GetNumeric("camera_z", 0.0f));

    if (!SaveNavigationData(uid, device, camX, camY, camZ, errorMessage, deps.parent, logger)) {
      return false;
    }

    return true;
  }

  return std::nullopt;
}

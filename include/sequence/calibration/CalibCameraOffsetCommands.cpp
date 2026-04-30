// CalibCameraOffsetCommands.cpp — Family C: camera-to-needle XY offset.
// Owns the local-first override pattern:
//   READ: prefer config/SYSTEM/needlexyoffset.local.json, fall back to
//         config/SYSTEM/needlexyoffset.json (factory default).
//   WRITE: always to .local.json so the factory default is preserved.
// Branches: LoadCameraToNeedleOffset, SaveCameraToNeedleOffset.
// LoadCameraToNeedleOffset reports loadedFromPath so the dispatch branch
// can label the source as FACTORY vs LOCAL in the log line.
#include "CalibCameraOffsetCommands.h"

#include "../CalibrationCommandHandler.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "SequenceContext.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace {

const char* kLocalPath   = "config/SYSTEM/needlexyoffset.local.json";
const char* kFactoryPath = "config/SYSTEM/needlexyoffset.json";

std::string CameraOffsetFilePath(bool forWriting)
{
  if (forWriting) {
    return kLocalPath;
  }
  if (std::filesystem::exists(kLocalPath)) {
    return kLocalPath;
  }
  return kFactoryPath;
}

bool LoadCameraToNeedleOffset(
    double& offsetX, double& offsetY, double& offsetZ,
    std::string& loadedFromPath,
    LogManager* logger)
{
  try {
    std::string filePath = CameraOffsetFilePath(false);
    loadedFromPath = filePath;

    if (!std::filesystem::exists(filePath)) {
      return false;
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    json j;
    file >> j;
    file.close();

    // Two supported formats:
    //   1. {"offset": {"x": ..., "y": ...}}  (current)
    //   2. {"x": ..., "y": ...}              (legacy)
    if (j.contains("offset") && j["offset"].contains("x") && j["offset"].contains("y")) {
      offsetX = j["offset"]["x"];
      offsetY = j["offset"]["y"];
      offsetZ = 0.0;
    } else if (j.contains("x") && j.contains("y")) {
      offsetX = j["x"];
      offsetY = j["y"];
      offsetZ = 0.0;
    }

    if (logger) {
      logger->Info(
        "Loaded factory needle XY offset: (" +
        std::to_string(offsetX) + ", " +
        std::to_string(offsetY) + ")",
        "CalibrationHandler"
      );
    }
    return true;
  }
  catch (const std::exception& e) {
    if (logger) {
      logger->Warning(
        "Failed to load factory needle XY offset: " + std::string(e.what()),
        "CalibrationHandler"
      );
    }
    return false;
  }
}

bool SaveCameraToNeedleOffset(
    const std::string& deviceName,
    const std::string& pneumaticSlide,
    double needleX, double needleY, double needleZ,
    double cameraX, double cameraY, double cameraZ,
    std::string& errorMessage,
    std::string& resultMessage,
    CalibrationCommandHandler* parent,
    LogManager* logger)
{
  try {
    if (!parent->EnsureCalibrationDirectoryExists(errorMessage)) {
      return false;
    }

    double offsetX = needleX - cameraX;
    double offsetY = needleY - cameraY;

    json j;
    j["metadata"]["calibration_type"] = "Camera to Needle XY Offset";
    j["metadata"]["device"] = deviceName;
    j["metadata"]["last_calibrated_date"] = parent->GetCurrentDateTime(true);
    j["metadata"]["last_calibrated_time"] = parent->GetCurrentDateTime(false);
    j["metadata"]["pneumatic_slide"] = pneumaticSlide;
    j["metadata"]["calibrated_by"] = "System";
    j["metadata"]["description"] = "XY offset from camera center to needle tip (Z calibrated separately)";

    j["offset"]["x"] = offsetX;
    j["offset"]["y"] = offsetY;

    j["positions"]["needle_imprint"]["x"] = needleX;
    j["positions"]["needle_imprint"]["y"] = needleY;
    j["positions"]["needle_imprint"]["z"] = needleZ;
    j["positions"]["camera_centered"]["x"] = cameraX;
    j["positions"]["camera_centered"]["y"] = cameraY;
    j["positions"]["camera_centered"]["z"] = cameraZ;

    j["notes"] = "XY offset calculated from needle imprint to camera center. Z offset is handled by separate needle Z calibration.";

    std::string filePath = CameraOffsetFilePath(true);
    std::ofstream file(filePath);

    if (!file.is_open()) {
      errorMessage = "Failed to open offset file for writing: " + filePath;
      return false;
    }

    file << j.dump(2);
    file.close();

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "Camera-to-Needle XY Offset saved:\n";
    oss << "X: " << offsetX << " mm\n";
    oss << "Y: " << offsetY << " mm\n";
    oss << "(Z calibration is separate)";
    resultMessage = oss.str();

    if (logger) {
      logger->Info(
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

}  // namespace

std::optional<bool> CalibCameraOffsetCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // LOAD CAMERA-TO-NEEDLE OFFSET
  // ============================================================================
  if (methodName == "LoadCameraToNeedleOffset") {
    double offsetX = 0.0, offsetY = 0.0, offsetZ = 0.0;
    std::string loadedFromPath;

    bool exists = LoadCameraToNeedleOffset(offsetX, offsetY, offsetZ, loadedFromPath, logger);

    std::string source = "none";
    if (exists) {
      source = (loadedFromPath.find(".local.json") != std::string::npos) ? "LOCAL" : "FACTORY";
    }

    if (logger && exists) {
      logger->Info(
        "Loaded " + source + " needle XY offset from " + loadedFromPath + ": (" +
        std::to_string(offsetX) + ", " + std::to_string(offsetY) + ")",
        "CalibrationHandler"
      );
    }

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
  if (methodName == "SaveCameraToNeedleOffset") {
    auto deviceIt = params.find("device");
    auto pneumaticSlideIt = params.find("pneumatic_slide");

    if (deviceIt == params.end() || pneumaticSlideIt == params.end()) {
      errorMessage = "Missing required parameters: device, pneumatic_slide";
      return false;
    }

    std::string device = deviceIt->second;
    std::string pneumaticSlide = pneumaticSlideIt->second;

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
          errorMessage, resultMessage,
          deps.parent, logger)) {
      return false;
    }

    if (context) {
      context->SetValue("offset_result", resultMessage, "calibration", "SaveCameraToNeedleOffset");
    }

    return true;
  }

  return std::nullopt;
}

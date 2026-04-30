// CalibNeedleZCommands.cpp — Family B: global needle-Z calibration.
// Owns:
//   config/SYSTEM/needle_z_calibration.json
// Branches: SaveNeedleZCalibration, LoadNeedleZCalibration.
// SaveNeedleZCalibrationGlobal performs a delta-vs-previous-calibration
// reporting step (loads the prior height, computes delta, surfaces a
// formatted Δ message back through the deltaMessage out-param).
#include "CalibNeedleZCommands.h"

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

const char* kNeedleZFilePath = "config/SYSTEM/needle_z_calibration.json";

bool LoadNeedleZCalibrationGlobal(
    double& oldNeedleHeight,
    LogManager* logger)
{
  try {
    std::string filePath = kNeedleZFilePath;

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

    if (j.contains("parameters") && j["parameters"].contains("needle_height")) {
      oldNeedleHeight = j["parameters"]["needle_height"];
    }

    if (logger) {
      logger->Info(
        "Loaded global needle Z calibration: needle_height=" + std::to_string(oldNeedleHeight),
        "CalibrationHandler"
      );
    }
    return true;
  }
  catch (const std::exception& e) {
    if (logger) {
      logger->Warning(
        "Failed to load global needle Z calibration: " + std::string(e.what()),
        "CalibrationHandler"
      );
    }
    return false;
  }
}

bool SaveNeedleZCalibrationGlobal(
    const std::string& deviceName,
    const std::string& pneumaticSlide,
    double needleX, double needleY, double needleZ,
    std::string& errorMessage,
    std::string& deltaMessage,
    CalibrationCommandHandler* parent,
    LogManager* logger)
{
  try {
    if (!parent->EnsureCalibrationDirectoryExists(errorMessage)) {
      return false;
    }

    double oldNeedleHeight = 0.0;
    bool hadPreviousCalibration = LoadNeedleZCalibrationGlobal(oldNeedleHeight, logger);

    double delta = 0.0;
    if (hadPreviousCalibration) {
      delta = needleZ - oldNeedleHeight;
    }

    json j;
    j["metadata"]["calibration_type"] = "Needle Z Position (SYSTEM)";
    j["metadata"]["device"] = deviceName;
    j["metadata"]["calibrated_date"] = parent->GetCurrentDateTime(true);
    j["metadata"]["calibrated_time"] = parent->GetCurrentDateTime(false);
    j["metadata"]["pneumatic_slide"] = pneumaticSlide;
    j["metadata"]["description"] = "SYSTEM-wide needle Z calibration - latest calibration data";

    j["needle_touch_position"]["x"] = needleX;
    j["needle_touch_position"]["y"] = needleY;
    j["needle_touch_position"]["z"] = needleZ;

    j["parameters"]["needle_height"] = needleZ;

    if (hadPreviousCalibration) {
      j["parameters"]["previous_needle_height"] = oldNeedleHeight;
      j["parameters"]["delta_from_previous"] = delta;
    }

    std::string filePath = kNeedleZFilePath;
    std::ofstream file(filePath);

    if (!file.is_open()) {
      errorMessage = "Failed to open global calibration file for writing: " + filePath;
      return false;
    }

    file << j.dump(2);
    file.close();

    if (hadPreviousCalibration) {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(3);
      if (delta >= 0) {
        oss << " (\xCE\x94 +" << delta << " mm from previous)";
      } else {
        oss << " (\xCE\x94 " << delta << " mm from previous)";
      }
      deltaMessage = oss.str();
    } else {
      deltaMessage = " (First calibration)";
    }

    if (logger) {
      logger->Info(
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

}  // namespace

std::optional<bool> CalibNeedleZCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // SAVE NEEDLE Z CALIBRATION (Global)
  // ============================================================================
  if (methodName == "SaveNeedleZCalibration") {
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

    std::string deltaMessage;

    if (!SaveNeedleZCalibrationGlobal(
          device, pneumaticSlide,
          needleX, needleY, needleZ,
          errorMessage, deltaMessage,
          deps.parent, logger)) {
      return false;
    }

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
  if (methodName == "LoadNeedleZCalibration") {
    double needleZ = 0.0;

    bool exists = LoadNeedleZCalibrationGlobal(needleZ, logger);

    if (logger) {
      if (exists) {
        logger->Info(
          "Loaded needle Z calibration: " + std::to_string(needleZ) + " mm",
          "CalibrationHandler"
        );
      } else {
        logger->Info(
          "No previous needle Z calibration found",
          "CalibrationHandler"
        );
      }
    }

    if (context) {
      context->SetValue("needle_z_calibration_exists", exists, "calibration", "LoadNeedleZCalibration");

      if (exists) {
        context->SetValue("previous_needle_z", static_cast<float>(needleZ), "calibration", "LoadNeedleZCalibration");
      }
    }

    return true;
  }

  return std::nullopt;
}

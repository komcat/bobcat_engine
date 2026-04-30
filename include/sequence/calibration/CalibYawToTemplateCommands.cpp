// CalibYawToTemplateCommands.cpp — Family F: template-relative yaw correction.
// Owns:
//   config/<product>/instances_data/yaw_to_template_<uid>.json
// Branches: LoadYawToTemplateData, CalculateYawToTemplateCorrection,
//   ApplyYawToTemplateCorrection, ShowYawToTemplateResult.
//
// Yaw math (CalculateYawAngle) is in CalibYawMath.h, shared with Family E.
// Workflow differs from E: instead of comparing one corner pair against a
// previous calibration, this computes correction = template_yaw - target_yaw
// from two independent corner pairs (template + target).
#include "CalibYawToTemplateCommands.h"

#include "../CalibrationCommandHandler.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "CalibYawMath.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

namespace {

std::string YawToTemplateFilePath(ApplicationContext* app, const std::string& uid)
{
  std::string productName = "default";
  if (app && app->productPositionManager) {
    std::string currentProduct = app->productPositionManager->GetCurrentProductName();
    if (!currentProduct.empty()) {
      productName = currentProduct;
    }
  }
  return "config/" + productName + "/instances_data/yaw_to_template_" + uid + ".json";
}

bool LoadYawToTemplateData(
    ApplicationContext* app,
    const std::string& uid,
    double& templateCorner1X, double& templateCorner1Y,
    double& templateCorner2X, double& templateCorner2Y,
    double& targetCorner3X, double& targetCorner3Y,
    double& targetCorner4X, double& targetCorner4Y,
    double& templateYaw, double& targetYaw, double& correctionAngle,
    LogManager* logger)
{
  try {
    std::string filePath = YawToTemplateFilePath(app, uid);

    if (!std::filesystem::exists(filePath)) {
      return false;
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
      return false;
    }

    json data;
    file >> data;
    file.close();

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
    if (logger) {
      logger->Error("Exception loading yaw-to-template data: " + std::string(e.what()), "CalibrationHandler");
    }
    return false;
  }
}

bool SaveYawToTemplateData(
    ApplicationContext* app,
    const std::string& uid,
    const std::string& gantryDevice,
    double templateCorner1X, double templateCorner1Y,
    double templateCorner2X, double templateCorner2Y,
    double targetCorner3X, double targetCorner3Y,
    double targetCorner4X, double targetCorner4Y,
    double templateYaw, double targetYaw, double correctionAngle,
    std::string& errorMessage,
    CalibrationCommandHandler* parent,
    LogManager* logger)
{
  try {
    std::string filePath = YawToTemplateFilePath(app, uid);
    std::filesystem::path dirPath = std::filesystem::path(filePath).parent_path();

    if (!std::filesystem::exists(dirPath)) {
      if (!std::filesystem::create_directories(dirPath)) {
        errorMessage = "Failed to create instances_data directory: " + dirPath.string();
        return false;
      }
      if (logger) {
        logger->Info("Created instances_data directory: " + dirPath.string(), "CalibrationHandler");
      }
    }

    json data;
    data["uid"] = uid;
    data["gantry_device"] = gantryDevice;

    data["template_corner1"]["x"] = templateCorner1X;
    data["template_corner1"]["y"] = templateCorner1Y;
    data["template_corner2"]["x"] = templateCorner2X;
    data["template_corner2"]["y"] = templateCorner2Y;

    data["target_corner3"]["x"] = targetCorner3X;
    data["target_corner3"]["y"] = targetCorner3Y;
    data["target_corner4"]["x"] = targetCorner4X;
    data["target_corner4"]["y"] = targetCorner4Y;

    data["template_yaw_degrees"] = templateYaw;
    data["target_yaw_degrees"] = targetYaw;
    data["correction_angle_degrees"] = correctionAngle;

    data["timestamp"] = parent->GetCurrentDateTime(false);
    data["date"] = parent->GetCurrentDateTime(true);

    std::ofstream file(filePath);
    if (!file.is_open()) {
      errorMessage = "Failed to open file for writing: " + filePath;
      return false;
    }

    file << data.dump(2);
    file.close();

    if (logger) {
      logger->Info("Saved yaw-to-template data to: " + filePath +
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

}  // namespace

std::optional<bool> CalibYawToTemplateCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // LOAD YAW TO TEMPLATE DATA
  // ============================================================================
  if (methodName == "LoadYawToTemplateData") {
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
      errorMessage = "LoadYawToTemplateData: Missing required parameter 'uid'";
      return false;
    }

    std::string uid = uidIt->second;

    double tc1x, tc1y, tc2x, tc2y;
    double tc3x, tc3y, tc4x, tc4y;
    double templateYaw, targetYaw, correctionAngle;

    bool exists = LoadYawToTemplateData(deps.app, uid,
                                         tc1x, tc1y, tc2x, tc2y,
                                         tc3x, tc3y, tc4x, tc4y,
                                         templateYaw, targetYaw, correctionAngle,
                                         logger);

    if (context) {
      context->SetValue("yaw_template_data_exists", exists, "calibration", "LoadYawToTemplateData");

      if (exists) {
        context->SetValue("template_corner1_x", static_cast<float>(tc1x), "calibration", "LoadYawToTemplateData");
        context->SetValue("template_corner1_y", static_cast<float>(tc1y), "calibration", "LoadYawToTemplateData");
        context->SetValue("template_corner2_x", static_cast<float>(tc2x), "calibration", "LoadYawToTemplateData");
        context->SetValue("template_corner2_y", static_cast<float>(tc2y), "calibration", "LoadYawToTemplateData");

        context->SetValue("target_corner3_x", static_cast<float>(tc3x), "calibration", "LoadYawToTemplateData");
        context->SetValue("target_corner3_y", static_cast<float>(tc3y), "calibration", "LoadYawToTemplateData");
        context->SetValue("target_corner4_x", static_cast<float>(tc4x), "calibration", "LoadYawToTemplateData");
        context->SetValue("target_corner4_y", static_cast<float>(tc4y), "calibration", "LoadYawToTemplateData");

        context->SetValue("previous_template_yaw", static_cast<float>(templateYaw), "calibration", "LoadYawToTemplateData");
        context->SetValue("previous_target_yaw", static_cast<float>(targetYaw), "calibration", "LoadYawToTemplateData");
        context->SetValue("previous_correction_angle", static_cast<float>(correctionAngle), "calibration", "LoadYawToTemplateData");
      }
    }

    if (logger) {
      if (exists) {
        logger->Info("Loaded yaw-to-template data for UID: " + uid, "CalibrationHandler");
      } else {
        logger->Info("No previous yaw-to-template data found for UID: " + uid, "CalibrationHandler");
      }
    }

    return true;
  }

  // ============================================================================
  // CALCULATE YAW TO TEMPLATE CORRECTION
  // ============================================================================
  if (methodName == "CalculateYawToTemplateCorrection") {
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

    double tc1x = static_cast<double>(context->GetNumeric("template_corner1_x", 0.0f));
    double tc1y = static_cast<double>(context->GetNumeric("template_corner1_y", 0.0f));
    double tc2x = static_cast<double>(context->GetNumeric("template_corner2_x", 0.0f));
    double tc2y = static_cast<double>(context->GetNumeric("template_corner2_y", 0.0f));

    double tc3x = static_cast<double>(context->GetNumeric("target_corner3_x", 0.0f));
    double tc3y = static_cast<double>(context->GetNumeric("target_corner3_y", 0.0f));
    double tc4x = static_cast<double>(context->GetNumeric("target_corner4_x", 0.0f));
    double tc4y = static_cast<double>(context->GetNumeric("target_corner4_y", 0.0f));

    double templateYaw = calib_yaw::CalculateYawAngle(tc1x, tc1y, tc2x, tc2y, logger);
    double targetYaw   = calib_yaw::CalculateYawAngle(tc3x, tc3y, tc4x, tc4y, logger);

    // Correction brings target to match template:
    //   correction = template_yaw - target_yaw.
    double correctionAngle = templateYaw - targetYaw;

    context->SetValue("template_yaw_degrees", static_cast<float>(templateYaw), "calibration", "CalculateYawToTemplateCorrection");
    context->SetValue("target_yaw_degrees", static_cast<float>(targetYaw), "calibration", "CalculateYawToTemplateCorrection");
    context->SetValue("yaw_correction_angle", static_cast<float>(correctionAngle), "calibration", "CalculateYawToTemplateCorrection");

    std::string saveError;
    if (!SaveYawToTemplateData(deps.app, uid, gantryDevice,
                                tc1x, tc1y, tc2x, tc2y,
                                tc3x, tc3y, tc4x, tc4y,
                                templateYaw, targetYaw, correctionAngle,
                                saveError, deps.parent, logger)) {
      errorMessage = "CalculateYawToTemplateCorrection: Failed to save data - " + saveError;
      return false;
    }

    if (logger) {
      logger->Info("Calculated yaw-to-template: Template=" + std::to_string(templateYaw) +
                   "deg, Target=" + std::to_string(targetYaw) +
                   "deg, Correction=" + std::to_string(correctionAngle) + "deg",
                   "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // APPLY YAW TO TEMPLATE CORRECTION
  // ============================================================================
  if (methodName == "ApplyYawToTemplateCorrection") {
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

    if (!context->HasValue("yaw_correction_angle")) {
      errorMessage = "ApplyYawToTemplateCorrection: No yaw_correction_angle found in context";
      return false;
    }

    double correctionAngle = static_cast<double>(context->GetNumeric("yaw_correction_angle", 0.0f));

    if (!deps.app || !deps.app->motionManager) {
      errorMessage = "ApplyYawToTemplateCorrection: Motion manager not available";
      return false;
    }

    // Apply correction directly (template_yaw - target_yaw); flip sign for
    // hexapods with +X pointing down ("down" = double-negate).
    double uCorrection = correctionAngle;
    if (xDirection == "down") {
      uCorrection = -uCorrection;
    }

    if (!deps.app->motionManager->MoveRelative(targetDevice, "U", uCorrection, true)) {
      errorMessage = "ApplyYawToTemplateCorrection: Failed to move U axis on device: " + targetDevice;
      return false;
    }

    // Stored under the same key as Family E so SaveYawCorrectionToPosition
    // picks it up regardless of which yaw flow ran.
    context->SetValue("u_correction", static_cast<float>(uCorrection), "calibration", "ApplyYawToTemplateCorrection");

    if (logger) {
      logger->Info("Applied yaw-to-template correction to " + targetDevice + ": U = " +
                   std::to_string(uCorrection) + " deg (X " + xDirection + ")",
                   "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // SHOW YAW TO TEMPLATE RESULT
  // ============================================================================
  if (methodName == "ShowYawToTemplateResult") {
    auto targetDeviceIt = params.find("target_device");
    std::string targetDevice = (targetDeviceIt != params.end()) ? targetDeviceIt->second : "hexapod";

    if (!context) {
      errorMessage = "ShowYawToTemplateResult: Context not available";
      return false;
    }

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

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "Template: Corner1(" << tc1x << "," << tc1y << ") Corner2(" << tc2x << "," << tc2y << ") Yaw=" << templateYaw << "deg | ";
    oss << "Target: Corner3(" << tc3x << "," << tc3y << ") Corner4(" << tc4x << "," << tc4y << ") Yaw=" << targetYaw << "deg | ";
    oss << "Correction=" << correctionAngle << "deg U=" << uCorrection << "deg Target=" << targetDevice;

    context->SetValue("yaw_template_result_message", oss.str(), "calibration", "ShowYawToTemplateResult");

    if (logger) {
      logger->Info("Yaw-to-Template Complete: TemplateYaw=" + std::to_string(templateYaw) +
                   "deg, TargetYaw=" + std::to_string(targetYaw) +
                   "deg, U=" + std::to_string(uCorrection) + "deg",
                   "CalibrationHandler");
    }

    return true;
  }

  return std::nullopt;
}

// CalibYawCorrectionCommands.cpp — Family E: corner-pair yaw correction.
// Owns:
//   config/<product>/instances_data/yaw_correction_<uid>.json
// Branches: LoadYawCorrectionData, CalculateAndSaveYawCorrection,
//   NavigateToSavedCorner, ApplyYawCorrection, ShowYawCorrectionResult,
//   SaveYawCorrectionToPosition.
//
// Yaw math is in CalibYawMath.h (shared with Family F — both apply the
// same atan2-from-+Y convention plus a sign-flip-by-orientation rule
// downstream).
#include "CalibYawCorrectionCommands.h"

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
#include <iostream>
#include <cmath>

using json = nlohmann::json;

namespace {

std::string YawCorrectionFilePath(ApplicationContext* app, const std::string& uid)
{
  std::string productName = "default";
  if (app && app->productPositionManager) {
    std::string currentProduct = app->productPositionManager->GetCurrentProductName();
    if (!currentProduct.empty()) {
      productName = currentProduct;
    }
  }
  return "config/" + productName + "/instances_data/yaw_correction_" + uid + ".json";
}

bool LoadYawCorrectionData(
    ApplicationContext* app,
    const std::string& uid,
    double& corner1X, double& corner1Y,
    double& corner2X, double& corner2Y,
    double& previousYawAngle,
    LogManager* logger)
{
  try {
    std::string filePath = YawCorrectionFilePath(app, uid);

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
    if (logger) {
      logger->Error("Exception loading yaw correction data: " + std::string(e.what()), "CalibrationHandler");
    }
    return false;
  }
}

bool SaveYawCorrectionData(
    ApplicationContext* app,
    const std::string& uid,
    const std::string& gantryDevice,
    double corner1X, double corner1Y,
    double corner2X, double corner2Y,
    double yawAngleDegrees,
    std::string& errorMessage,
    CalibrationCommandHandler* parent,
    LogManager* logger)
{
  try {
    std::string filePath = YawCorrectionFilePath(app, uid);
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

    double deltaX = corner2X - corner1X;
    double deltaY = corner2Y - corner1Y;
    double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);

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
      logger->Info("Saved yaw correction data to: " + filePath +
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

}  // namespace

std::optional<bool> CalibYawCorrectionCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // LOAD YAW CORRECTION DATA
  // ============================================================================
  if (methodName == "LoadYawCorrectionData") {
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
      errorMessage = "LoadYawCorrectionData: Missing required parameter 'uid'";
      return false;
    }

    std::string uid = uidIt->second;
    double corner1X = 0.0, corner1Y = 0.0;
    double corner2X = 0.0, corner2Y = 0.0;
    double previousYawAngle = 0.0;

    bool exists = LoadYawCorrectionData(deps.app, uid,
                                         corner1X, corner1Y,
                                         corner2X, corner2Y,
                                         previousYawAngle, logger);

    if (context) {
      context->SetValue("yaw_data_exists", exists, "calibration", "LoadYawCorrectionData");
      if (exists) {
        context->SetValue("corner1_x", static_cast<float>(corner1X), "calibration", "LoadYawCorrectionData");
        context->SetValue("corner1_y", static_cast<float>(corner1Y), "calibration", "LoadYawCorrectionData");
        context->SetValue("corner2_x", static_cast<float>(corner2X), "calibration", "LoadYawCorrectionData");
        context->SetValue("corner2_y", static_cast<float>(corner2Y), "calibration", "LoadYawCorrectionData");
        context->SetValue("previous_yaw_angle", static_cast<float>(previousYawAngle), "calibration", "LoadYawCorrectionData");

        if (logger) {
          logger->Info("Loaded yaw correction data for UID: " + uid +
                       " - Yaw angle: " + std::to_string(previousYawAngle) + " deg",
                       "CalibrationHandler");
        }
      } else {
        if (logger) {
          logger->Info("No saved yaw correction data found for UID: " + uid, "CalibrationHandler");
        }
      }
    }

    return true;
  }

  // ============================================================================
  // CALCULATE AND SAVE YAW CORRECTION
  // ============================================================================
  if (methodName == "CalculateAndSaveYawCorrection") {
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

    double corner1X = static_cast<double>(context->GetNumeric("corner1_x", 0.0f));
    double corner1Y = static_cast<double>(context->GetNumeric("corner1_y", 0.0f));
    double corner2X = static_cast<double>(context->GetNumeric("corner2_x", 0.0f));
    double corner2Y = static_cast<double>(context->GetNumeric("corner2_y", 0.0f));

    double yawAngleDegrees = calib_yaw::CalculateYawAngle(
        corner1X, corner1Y, corner2X, corner2Y, logger);

    context->SetValue("yaw_angle_degrees", static_cast<float>(yawAngleDegrees), "calibration", "CalculateAndSaveYawCorrection");

    if (!SaveYawCorrectionData(deps.app, uid, gantryDevice,
                                corner1X, corner1Y, corner2X, corner2Y,
                                yawAngleDegrees, errorMessage,
                                deps.parent, logger)) {
      return false;
    }

    if (logger) {
      logger->Info("Calculated and saved yaw correction: " + std::to_string(yawAngleDegrees) + " deg for UID: " + uid,
                   "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // NAVIGATE TO SAVED CORNER (for yaw correction)
  // ============================================================================
  if (methodName == "NavigateToSavedCorner") {
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

    if (!deps.app || !deps.app->motionManager) {
      errorMessage = "NavigateToSavedCorner: Motion manager not available";
      return false;
    }

    double currentZ = 0.0;
    if (!deps.app->motionManager->GetPosition(device, "Z", currentZ)) {
      errorMessage = "NavigateToSavedCorner: Failed to read Z position";
      return false;
    }

    if (!deps.app->motionManager->MoveToAbs(device, x, y, currentZ, 100.0, true)) {
      errorMessage = "NavigateToSavedCorner: Failed to move to corner " + corner;
      return false;
    }

    if (logger) {
      logger->Info("Navigated to saved corner " + corner + ": (" +
                   std::to_string(x) + ", " + std::to_string(y) + ")",
                   "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // APPLY YAW CORRECTION
  // ============================================================================
  if (methodName == "ApplyYawCorrection") {
    auto uidIt = params.find("uid");
    auto targetDeviceIt = params.find("target_device");
    auto xDirectionIt = params.find("x_direction");

    if (uidIt == params.end()) {
      errorMessage = "ApplyYawCorrection: Missing required parameter 'uid'";
      return false;
    }

    std::string uid = uidIt->second;
    std::string targetDevice = (targetDeviceIt != params.end()) ? targetDeviceIt->second : "";
    std::string xDirection = (xDirectionIt != params.end()) ? xDirectionIt->second : "up";

    if (targetDevice.empty()) {
      errorMessage = "ApplyYawCorrection: Missing target_device parameter";
      return false;
    }

    if (!context) {
      errorMessage = "ApplyYawCorrection: Context not available";
      return false;
    }

    double yawAngleDegrees = 0.0;

    if (context->HasValue("yaw_angle_degrees")) {
      yawAngleDegrees = static_cast<double>(context->GetNumeric("yaw_angle_degrees", 0.0f));
    } else {
      double corner1X, corner1Y, corner2X, corner2Y, previousYawAngle;
      if (LoadYawCorrectionData(deps.app, uid, corner1X, corner1Y, corner2X, corner2Y, previousYawAngle, logger)) {
        yawAngleDegrees = previousYawAngle;
        context->SetValue("yaw_angle_degrees", static_cast<float>(yawAngleDegrees), "calibration", "ApplyYawCorrection");
      } else {
        errorMessage = "ApplyYawCorrection: No yaw correction data found for UID: " + uid;
        return false;
      }
    }

    if (!deps.app || !deps.app->motionManager) {
      errorMessage = "ApplyYawCorrection: Motion manager not available";
      return false;
    }

    // Correction is the OPPOSITE of the measured error; flip again for
    // hexapod orientations where +X points down ("down" = double-negate).
    double uCorrection = -yawAngleDegrees;
    if (xDirection == "down") {
      uCorrection = -uCorrection;
    }

    if (!deps.app->motionManager->MoveRelative(targetDevice, "U", uCorrection, true)) {
      errorMessage = "ApplyYawCorrection: Failed to move U axis on device: " + targetDevice;
      return false;
    }

    context->SetValue("u_correction", static_cast<float>(uCorrection), "calibration", "ApplyYawCorrection");

    if (logger) {
      logger->Info("Applied yaw correction to " + targetDevice + ": U = " + std::to_string(uCorrection) +
                   " deg (X " + xDirection + ")", "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // SHOW YAW CORRECTION RESULT
  // ============================================================================
  if (methodName == "ShowYawCorrectionResult") {
    auto targetDeviceIt = params.find("target_device");
    std::string targetDevice = (targetDeviceIt != params.end()) ? targetDeviceIt->second : "hexapod";

    if (!context) {
      errorMessage = "ShowYawCorrectionResult: Context not available";
      return false;
    }

    double corner1X = static_cast<double>(context->GetNumeric("corner1_x", 0.0f));
    double corner1Y = static_cast<double>(context->GetNumeric("corner1_y", 0.0f));
    double corner2X = static_cast<double>(context->GetNumeric("corner2_x", 0.0f));
    double corner2Y = static_cast<double>(context->GetNumeric("corner2_y", 0.0f));
    double yawAngle = static_cast<double>(context->GetNumeric("yaw_angle_degrees", 0.0f));
    double uCorrection = static_cast<double>(context->GetNumeric("u_correction", 0.0f));

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "Corner1=(" << corner1X << ", " << corner1Y << ") ";
    oss << "Corner2=(" << corner2X << ", " << corner2Y << ") ";
    oss << "YawAngle=" << yawAngle << "deg ";
    oss << "U=" << uCorrection << "deg ";
    oss << "Target=" << targetDevice;

    context->SetValue("yaw_result_message", oss.str(), "calibration", "ShowYawCorrectionResult");

    if (logger) {
      logger->Info("\xE2\x9C\x85 Yaw Correction Complete: " + oss.str(), "CalibrationHandler");
    }

    return true;
  }

  // ============================================================================
  // SAVE YAW CORRECTION TO NAMED POSITION
  // ============================================================================
  if (methodName == "SaveYawCorrectionToPosition") {
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

    if (!context->HasValue("u_correction")) {
      errorMessage = "SaveYawCorrectionToPosition: No u_correction value found in context. Run ApplyYawCorrection first.";
      return false;
    }

    double uCorrection = static_cast<double>(context->GetNumeric("u_correction", 0.0f));

    if (!deps.app || !deps.app->productPositionManager) {
      errorMessage = "SaveYawCorrectionToPosition: ProductPositionManager not available";
      return false;
    }

    PositionStruct currentPos;
    if (!deps.app->productPositionManager->GetPosition(positionDevice, positionName, currentPos)) {
      errorMessage = "SaveYawCorrectionToPosition: Position '" + positionName + "' not found for device '" + positionDevice + "'";
      return false;
    }

    double newU = currentPos.u + uCorrection;

    PositionStruct updatedPos = currentPos;
    updatedPos.u = newU;

    deps.app->productPositionManager->AddPosition(positionDevice, positionName, updatedPos);

    if (!deps.app->productPositionManager->SaveProduct()) {
      errorMessage = "SaveYawCorrectionToPosition: Failed to save product file";
      return false;
    }

    if (deps.app->motionManager) {
      deps.app->motionManager->AddNamedPosition(positionDevice, positionName, updatedPos);
    }

    if (logger) {
      logger->Info("\xE2\x9C\x85 Saved U correction to position: " + positionDevice + "/" + positionName +
                   " (U: " + std::to_string(currentPos.u) + " -> " + std::to_string(newU) + " deg)",
                   "CalibrationHandler");
    }

    std::cout << "[CalibrationCommandHandler] SaveYawCorrectionToPosition: " << positionDevice
              << "/" << positionName << " U=" << newU << " deg" << std::endl;

    return true;
  }

  return std::nullopt;
}

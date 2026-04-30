// CalibDispenseZCommands.cpp — Family D: per-DUT dispense Z height.
// Owns:
//   config/SYSTEM/dispense_z_<uid>.json
// Branches: LoadDispenseZHeight, SaveDispenseZHeight.
#include "CalibDispenseZCommands.h"

#include "../CalibrationCommandHandler.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "SequenceContext.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

namespace {

std::string DispenseZFilePath(const std::string& uid)
{
  return "config/SYSTEM/dispense_z_" + uid + ".json";
}

bool SaveDispenseZHeight(
    const std::string& uid,
    const std::string& deviceName,
    double dispenseZ,
    std::string& errorMessage,
    CalibrationCommandHandler* parent,
    LogManager* logger)
{
  try {
    if (!parent->EnsureCalibrationDirectoryExists(errorMessage)) {
      return false;
    }

    std::string filePath = DispenseZFilePath(uid);

    json data;
    data["uid"] = uid;
    data["device"] = deviceName;
    data["dispense_z"] = dispenseZ;
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
      logger->Info("Saved dispense Z height to: " + filePath, "CalibrationHandler");
    }
    return true;
  }
  catch (const std::exception& e) {
    errorMessage = "Exception saving dispense Z height: " + std::string(e.what());
    return false;
  }
}

bool LoadDispenseZHeight(
    const std::string& uid,
    double& dispenseZ,
    LogManager* logger)
{
  try {
    std::string filePath = DispenseZFilePath(uid);

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

    if (data.contains("dispense_z")) {
      dispenseZ = data["dispense_z"].get<double>();
      return true;
    }
    return false;
  }
  catch (const std::exception& e) {
    if (logger) {
      logger->Error("Exception loading dispense Z height: " + std::string(e.what()), "CalibrationHandler");
    }
    return false;
  }
}

}  // namespace

std::optional<bool> CalibDispenseZCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps)
{
  LogManager* logger = deps.app ? deps.app->logManager : nullptr;

  // ============================================================================
  // LOAD DISPENSE Z HEIGHT
  // ============================================================================
  if (methodName == "LoadDispenseZHeight") {
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
      errorMessage = "LoadDispenseZHeight: Missing required parameter 'uid'";
      return false;
    }

    std::string uid = uidIt->second;
    double dispenseZ = 0.0;

    bool exists = LoadDispenseZHeight(uid, dispenseZ, logger);

    if (context) {
      context->SetValue("dispense_z_exists", exists, "calibration", "LoadDispenseZHeight");
      if (exists) {
        context->SetValue("dispense_z", static_cast<float>(dispenseZ), "calibration", "LoadDispenseZHeight");

        if (logger) {
          logger->Info("Loaded dispense Z height: " + std::to_string(dispenseZ) + " mm for UID: " + uid,
                       "CalibrationHandler");
        }
      } else {
        if (logger) {
          logger->Info("No saved dispense Z height found for UID: " + uid,
                       "CalibrationHandler");
        }
      }
    }

    return true;
  }

  // ============================================================================
  // SAVE DISPENSE Z HEIGHT
  // ============================================================================
  if (methodName == "SaveDispenseZHeight") {
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

    if (!SaveDispenseZHeight(uid, device, dispenseZ, errorMessage, deps.parent, logger)) {
      return false;
    }

    if (logger) {
      logger->Info("Saved dispense Z height: " + std::to_string(dispenseZ) + " mm for UID: " + uid,
                   "CalibrationHandler");
    }

    return true;
  }

  return std::nullopt;
}

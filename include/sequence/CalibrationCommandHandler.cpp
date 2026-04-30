// CalibrationCommandHandler.cpp
//
// Orchestrator for calibration commands. Per-family logic lives in
// include/sequence/calibration/Calib*Commands.{h,cpp}; this file only holds
// the dispatch ladder + two cross-family utilities (GetCurrentDateTime,
// EnsureCalibrationDirectoryExists) that helpers reach via deps.parent->...
#include "CalibrationCommandHandler.h"

#include "LogManager.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

// Per-family helpers (dispatch order — each TryExecute returns std::nullopt
// when methodName isn't theirs, otherwise true/false handled).
#include "calibration/CalibNavigationCommands.h"      // Family A
#include "calibration/CalibNeedleZCommands.h"         // Family B
#include "calibration/CalibCameraOffsetCommands.h"    // Family C
#include "calibration/CalibDispenseZCommands.h"       // Family D
#include "calibration/CalibYawCorrectionCommands.h"   // Family E
#include "calibration/CalibYawToTemplateCommands.h"   // Family F

CalibrationCommandHandler::CalibrationCommandHandler(ApplicationContext* context)
  : m_context(context)
{
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

bool CalibrationCommandHandler::ExecuteCommand(
  void* /*manager*/,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  // Shared dependency bundle for split helper families. Built once here and
  // reused across every helper TryExecute call below.
  CalibCommandDeps deps{};
  deps.app    = m_context;
  deps.parent = this;

  if (auto r = CalibNavigationCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) return *r;
  if (auto r = CalibNeedleZCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) return *r;
  if (auto r = CalibCameraOffsetCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) return *r;
  if (auto r = CalibDispenseZCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) return *r;
  if (auto r = CalibYawCorrectionCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) return *r;
  if (auto r = CalibYawToTemplateCommands::TryExecute(
        deviceName, methodName, params, errorMessage, context, deps)) return *r;

  errorMessage = "Unknown calibration method: " + methodName;
  return false;
}


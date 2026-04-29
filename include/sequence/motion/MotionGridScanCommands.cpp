// MotionGridScanCommands.cpp
// Family D implementation. See MotionGridScanCommands.h.
//
// Owns the four file-static helpers (FormatSI, MakeTimestamp, FormatTime,
// WriteGridScanFiles) that previously lived at the top of
// MotionCommandHandler.cpp -- sole consumers are the three scan commands
// in this translation unit.
#include "MotionGridScanCommands.h"

#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "DataStore.h"
#include "ScanActivityGuard.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>

static std::string FormatSI(double value) {
  double absVal = std::abs(value);
  const char* suffix;
  double scaled;
  if (absVal < 1e-9) {
    suffix = "p"; scaled = value * 1e12;
  } else if (absVal < 1e-6) {
    suffix = "n"; scaled = value * 1e9;
  } else if (absVal < 1e-3) {
    suffix = "u"; scaled = value * 1e6;
  } else if (absVal < 1.0) {
    suffix = "m"; scaled = value * 1e3;
  } else {
    suffix = ""; scaled = value;
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.3f%s", scaled, suffix);
  return std::string(buf);
}

// Helper to generate timestamp string for filenames (YYYYMMDD_HHMMSS)
static std::string MakeTimestamp() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  struct tm tmBuf {};
  localtime_s(&tmBuf, &t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmBuf);
  return std::string(buf);
}

// Helper to format time_point as "YYYY-MM-DD HH:MM:SS"
static std::string FormatTime(std::chrono::system_clock::time_point tp) {
  auto t = std::chrono::system_clock::to_time_t(tp);
  struct tm tmBuf {};
  localtime_s(&tmBuf, &t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
  return std::string(buf);
}

// Helper to write grid scan JSON + human-readable summary text file
static void WriteGridScanFiles(
    const std::string& processUID,
    const nlohmann::json& resultJson,
    ILogger* logger)
{
  std::string timestamp = MakeTimestamp();
  std::string filePrefix = processUID.empty() ? "grid" : processUID;
  std::string baseName = filePrefix + "_" + timestamp;

  std::filesystem::path outDir = std::filesystem::current_path() / "logs" / "scanning" / "grid";
  std::filesystem::create_directories(outDir);

  // Write JSON data file
  std::filesystem::path jsonPath = outDir / (baseName + ".json");
  std::ofstream jsonOfs(jsonPath);
  if (jsonOfs.is_open()) {
    jsonOfs << resultJson.dump(2);
    jsonOfs.close();
    if (logger) {
      logger->LogInfo("GridScan: Data saved to " + jsonPath.string(), "MotionHandler");
    }
  } else if (logger) {
    logger->LogWarning("GridScan: Failed to save data to " + jsonPath.string(), "MotionHandler");
  }

  // Write human-readable summary text file
  std::filesystem::path txtPath = outDir / (baseName + "_summary.txt");
  std::ofstream txtOfs(txtPath);
  if (txtOfs.is_open()) {
    txtOfs << "========================================\n";
    txtOfs << "  Grid Scan Summary\n";
    txtOfs << "========================================\n\n";

    if (resultJson.contains("process_uid"))
      txtOfs << "Process UID:    " << resultJson["process_uid"].get<std::string>() << "\n";
    if (resultJson.contains("scan_type"))
      txtOfs << "Scan Type:      " << resultJson["scan_type"].get<std::string>() << "\n";
    txtOfs << "Device:         " << resultJson.value("device", "") << "\n";
    txtOfs << "Plane:          " << resultJson.value("plane", "") << "\n";
    txtOfs << "Start Time:     " << resultJson.value("start_time", "") << "\n";
    txtOfs << "End Time:       " << resultJson.value("end_time", "") << "\n";
    txtOfs << "Duration:       " << resultJson.value("duration_seconds", 0) << " s\n\n";

    // Grid info
    if (resultJson.contains("grid")) {
      auto& grid = resultJson["grid"];
      txtOfs << "--- Grid ---\n";
      txtOfs << "Axis 1:         " << grid.value("axis1", "") << "\n";
      txtOfs << "Axis 2:         " << grid.value("axis2", "") << "\n";
      txtOfs << "Points:         " << grid.value("points_axis1", 0) << " x " << grid.value("points_axis2", 0) << "\n";
      txtOfs << "Total Points:   " << grid.value("total_points", 0) << "\n\n";
    }

    // Scan config
    if (resultJson.contains("scan_config")) {
      auto& cfg = resultJson["scan_config"];
      txtOfs << "--- Configuration ---\n";
      if (cfg.contains("mode"))
        txtOfs << "Mode:           " << cfg["mode"].get<std::string>() << "\n";
      if (cfg.contains("range_axis1_um"))
        txtOfs << "Range Axis1:    " << cfg["range_axis1_um"].get<double>() << " um\n";
      if (cfg.contains("range_axis2_um"))
        txtOfs << "Range Axis2:    " << cfg["range_axis2_um"].get<double>() << " um\n";
      if (cfg.contains("step_size_um"))
        txtOfs << "Step Size:      " << cfg["step_size_um"].get<double>() << " um\n";
      if (cfg.contains("settling_time_ms"))
        txtOfs << "Settling Time:  " << cfg["settling_time_ms"].get<int>() << " ms\n";
      if (cfg.contains("channel"))
        txtOfs << "Channel:        " << cfg["channel"].get<std::string>() << "\n";
      if (cfg.contains("channel1"))
        txtOfs << "Channel 1:      " << cfg["channel1"].get<std::string>() << "\n";
      if (cfg.contains("channel2"))
        txtOfs << "Channel 2:      " << cfg["channel2"].get<std::string>() << "\n";
      if (cfg.contains("waveguide_spacing_um"))
        txtOfs << "WG Spacing:     " << cfg["waveguide_spacing_um"].get<double>() << " um\n";
      if (cfg.contains("snake_pattern"))
        txtOfs << "Snake Pattern:  " << (cfg["snake_pattern"].get<bool>() ? "yes" : "no") << "\n";
      if (cfg.contains("return_to_peak"))
        txtOfs << "Return to Peak: " << cfg["return_to_peak"].get<std::string>() << "\n";
      txtOfs << "\n";
    }

    // Center position
    if (resultJson.contains("center_position")) {
      auto& cp = resultJson["center_position"];
      txtOfs << "--- Center Position ---\n";
      char posBuf[128];
      std::snprintf(posBuf, sizeof(posBuf), "X=%.6f  Y=%.6f  Z=%.6f\nU=%.6f  V=%.6f  W=%.6f",
        cp.value("x", 0.0), cp.value("y", 0.0), cp.value("z", 0.0),
        cp.value("u", 0.0), cp.value("v", 0.0), cp.value("w", 0.0));
      txtOfs << posBuf << "\n\n";
    }

    // Results
    if (resultJson.contains("results")) {
      auto& res = resultJson["results"];
      txtOfs << "--- Results ---\n";
      txtOfs << "Points Measured: " << res.value("total_points_measured", 0) << "\n";
      txtOfs << "Early Exit:      " << (res.value("early_exit", false) ? "yes" : "no") << "\n";
      if (res.contains("user_stopped") && res["user_stopped"].get<bool>()) {
        txtOfs << "User Stopped:    yes (returned to peak)\n";
      }

      // Single channel peak
      if (res.contains("peak_value")) {
        txtOfs << "Peak Value:      " << FormatSI(res["peak_value"].get<double>()) << "\n";
        if (res.contains("peak_position")) {
          auto& pp = res["peak_position"];
          txtOfs << "Peak Position:   axis1=" << pp.value("axis1", 0.0)
                 << "  axis2=" << pp.value("axis2", 0.0) << "\n";
        }
      }

      // Dual channel peaks
      if (res.contains("channel1_peak")) {
        auto& p1 = res["channel1_peak"];
        txtOfs << "Ch1 Peak Value:  " << FormatSI(p1["value"].get<double>()) << "\n";
        if (p1.contains("position")) {
          txtOfs << "Ch1 Peak Pos:    axis1=" << p1["position"].value("axis1", 0.0)
                 << "  axis2=" << p1["position"].value("axis2", 0.0) << "\n";
        }
        if (p1.contains("full_position")) {
          auto& fp = p1["full_position"];
          char buf[256];
          std::snprintf(buf, sizeof(buf), "Ch1 Full Pos:    X=%.6f  Y=%.6f  Z=%.6f  U=%.6f  V=%.6f  W=%.6f",
            fp.value("x", 0.0), fp.value("y", 0.0), fp.value("z", 0.0),
            fp.value("u", 0.0), fp.value("v", 0.0), fp.value("w", 0.0));
          txtOfs << buf << "\n";
        }
      }
      if (res.contains("channel2_peak")) {
        auto& p2 = res["channel2_peak"];
        txtOfs << "Ch2 Peak Value:  " << FormatSI(p2["value"].get<double>()) << "\n";
        if (p2.contains("position")) {
          txtOfs << "Ch2 Peak Pos:    axis1=" << p2["position"].value("axis1", 0.0)
                 << "  axis2=" << p2["position"].value("axis2", 0.0) << "\n";
        }
        if (p2.contains("full_position")) {
          auto& fp = p2["full_position"];
          char buf[256];
          std::snprintf(buf, sizeof(buf), "Ch2 Full Pos:    X=%.6f  Y=%.6f  Z=%.6f  U=%.6f  V=%.6f  W=%.6f",
            fp.value("x", 0.0), fp.value("y", 0.0), fp.value("z", 0.0),
            fp.value("u", 0.0), fp.value("v", 0.0), fp.value("w", 0.0));
          txtOfs << buf << "\n";
        }
      }
      if (res.contains("peak_delta_axis1")) {
        txtOfs << "Peak Delta:      " << res["peak_delta_axis1"].get<double>() * 1000.0 << " um"
               << " (" << res["peak_delta_axis1"].get<double>() << " mm)\n";
      }
      if (res.contains("peak_delta_xyzuvw")) {
        auto& d = res["peak_delta_xyzuvw"];
        txtOfs << "\n--- Peak Position Delta (Ch1 - Ch2) ---\n";
        char buf[256];
        std::snprintf(buf, sizeof(buf), "dX=%.4f um  dY=%.4f um  dZ=%.4f um",
          d.value("x_um", 0.0), d.value("y_um", 0.0), d.value("z_um", 0.0));
        txtOfs << buf << "\n";
        std::snprintf(buf, sizeof(buf), "dU=%.6f deg  dV=%.6f deg  dW=%.6f deg",
          d.value("u_deg", 0.0), d.value("v_deg", 0.0), d.value("w_deg", 0.0));
        txtOfs << buf << "\n";
      }
      if (res.contains("returned_to")) {
        txtOfs << "Returned to:     " << res["returned_to"].get<std::string>() << "\n";
      }
    }

    txtOfs << "\n========================================\n";
    txtOfs << "  Data file: " << baseName << ".json\n";
    txtOfs << "========================================\n";
    txtOfs.close();

    if (logger) {
      logger->LogInfo("GridScan: Summary saved to " + txtPath.string(), "MotionHandler");
    }
  }
}

std::optional<bool> MotionGridScanCommands::TryExecute(
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const MotionCommandDeps& deps)
{
  UnifiedMotionManager* motionManager = deps.motion;

  // ============================================================================
  // GRID SCAN - 2D PLANAR GRID SCANNING WITH PRESET MODES
  // ============================================================================
  if (methodName == "GridScan") {
    // Scan-busy indicator — ref-counted Begin on enter, End on every return path.
    ScanActivityGuard _scanBusy(deps.scanAct, "GridScan");

    // ✅ VALIDATE DEVICE NAME (must match hex-*)
    std::regex hexPattern("^hex-.*");
    if (!std::regex_match(deviceName, hexPattern)) {
      errorMessage = "GridScan: Device must be a PI hexapod (hex-*), got: " + deviceName;
      return false;
    }

    // ✅ REQUIRED PARAMETER: mode (coarse/fine/ultrafine)
    auto modeIt = params.find("mode");
    if (modeIt == params.end()) {
      errorMessage = "GridScan: Missing required parameter 'mode' (coarse/fine/ultrafine)";
      return false;
    }
    std::string mode = modeIt->second;

    // Determine grid parameters from mode
    double rangeUm, stepUm;
    if (mode == "coarse") {
      rangeUm = 200.0;  // 200 µm total range
      stepUm = 20.0;    // 20 µm step -> 11x11 = 121 points
    } else if (mode == "fine") {
      rangeUm = 100.0;  // 100 µm total range
      stepUm = 10.0;    // 10 µm step -> 11x11 = 121 points
    } else if (mode == "ultrafine") {
      rangeUm = 50.0;   // 50 µm total range
      stepUm = 5.0;     // 5 µm step -> 11x11 = 121 points
    } else {
      errorMessage = "GridScan: Invalid mode '" + mode + "' (must be 'coarse', 'fine', or 'ultrafine')";
      return false;
    }

    // ✅ REQUIRED PARAMETER: plane (XY/XZ/YZ)
    auto planeIt = params.find("plane");
    if (planeIt == params.end()) {
      errorMessage = "GridScan: Missing required parameter 'plane' (XY/XZ/YZ)";
      return false;
    }
    std::string plane = planeIt->second;

    // Parse plane into axis names
    std::string axis1, axis2;
    if (plane == "XY" || plane == "xy") {
      axis1 = "X"; axis2 = "Y";
    } else if (plane == "XZ" || plane == "xz") {
      axis1 = "X"; axis2 = "Z";
    } else if (plane == "YZ" || plane == "yz") {
      axis1 = "Y"; axis2 = "Z";
    } else {
      errorMessage = "GridScan: Invalid plane '" + plane + "' (must be 'XY', 'XZ', or 'YZ')";
      return false;
    }

    // ✅ REQUIRED PARAMETER: channel
    auto channelIt = params.find("channel");
    if (channelIt == params.end()) {
      errorMessage = "GridScan: Missing required parameter 'channel'";
      return false;
    }
    std::string channel = channelIt->second;
    if (channel.empty()) {
      errorMessage = "GridScan: Data channel cannot be empty - please select a data channel in the recipe configuration";
      return false;
    }

    // ✅ OPTIONAL PARAMETER: settling_time_ms (default 300)
    int settlingTimeMs = 300;
    auto settlingIt = params.find("settling_time_ms");
    if (settlingIt != params.end() && !settlingIt->second.empty()) {
      settlingTimeMs = std::stoi(settlingIt->second);
    }

    // ✅ OPTIONAL PARAMETER: capture_as
    std::string captureAs;
    auto captureAsIt = params.find("capture_as");
    if (captureAsIt != params.end()) {
      captureAs = captureAsIt->second;
    }

    // ✅ OPTIONAL: process_uid
    std::string processUID;
    auto uidItGS = params.find("process_uid");
    if (uidItGS != params.end()) processUID = uidItGS->second;

    // ✅ GET DATA STORE
    if (!deps.dataStore) {
      errorMessage = "GridScan: DataStore not available";
      return false;
    }

    // Convert to mm
    double rangeMm = rangeUm / 1000.0;
    double stepMm = stepUm / 1000.0;
    int gridPoints = static_cast<int>(rangeUm / stepUm) + 1;

    if (deps.logger) {
      deps.logger->Info(
        "GridScan: Starting " + mode + " scan on " + deviceName +
        " (plane=" + plane + ", range=" + std::to_string(rangeUm) + "um" +
        ", step=" + std::to_string(stepUm) + "um" +
        ", grid=" + std::to_string(gridPoints) + "x" + std::to_string(gridPoints) +
        ", channel='" + channel + "', settling=" + std::to_string(settlingTimeMs) + "ms)",
        "MotionHandler"
      );
    }

    // ✅ GET CURRENT POSITION (center of grid)
    double centerAxis1 = 0.0, centerAxis2 = 0.0;
    PositionStruct currentPos = motionManager->GetCurrentPosition(deviceName);
    if (axis1 == "X") centerAxis1 = currentPos.x;
    else if (axis1 == "Y") centerAxis1 = currentPos.y;
    else if (axis1 == "Z") centerAxis1 = currentPos.z;

    if (axis2 == "X") centerAxis2 = currentPos.x;
    else if (axis2 == "Y") centerAxis2 = currentPos.y;
    else if (axis2 == "Z") centerAxis2 = currentPos.z;

    // ✅ DATA COLLECTION
    auto scanStartTimeGS = std::chrono::system_clock::now();
    std::vector<nlohmann::json> scanMeasurementsGS;
    int totalPoints = gridPoints * gridPoints;
    scanMeasurementsGS.reserve(totalPoints);

    // ✅ EXECUTE GRID SCAN
    double peakValue = std::numeric_limits<double>::lowest();
    double peakAxis1 = centerAxis1;
    double peakAxis2 = centerAxis2;
    int currentPoint = 0;
    bool userStopped = false;

    // Start position (corner of grid)
    double startAxis1 = centerAxis1 - rangeMm / 2.0;
    double startAxis2 = centerAxis2 - rangeMm / 2.0;

    for (int row = 0; row < gridPoints && !userStopped; ++row) {
      // Snake pattern: alternate direction each row
      bool reverse = (row % 2 == 1);

      for (int col = 0; col < gridPoints && !userStopped; ++col) {
        // Check stop flag
        if (context && context->IsStopRequestedAny()) {
          if (deps.logger) {
            deps.logger->Info("GridScan: Stop requested, will return to best peak found so far", "MotionHandler");
          }
          userStopped = true;
          break;
        }

        int actualCol = reverse ? (gridPoints - 1 - col) : col;

        double targetAxis1 = startAxis1 + actualCol * stepMm;
        double targetAxis2 = startAxis2 + row * stepMm;

        // Move to position (axis1 first, then axis2 for predictability)
        if (!motionManager->MoveToPosition(deviceName, axis1, targetAxis1, false)) {
          errorMessage = "GridScan: Failed to start move on " + axis1;
          return false;
        }
        // Wait for axis1 move
        auto start = std::chrono::steady_clock::now();
        for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis1); ++w) {
          if (context && context->IsStopRequestedAny()) {
            userStopped = true;
            break;
          }
          if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
            errorMessage = "GridScan: Timeout waiting for " + axis1 + " motion";
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (userStopped) break;

        if (!motionManager->MoveToPosition(deviceName, axis2, targetAxis2, false)) {
          errorMessage = "GridScan: Failed to start move on " + axis2;
          return false;
        }
        // Wait for axis2 move
        start = std::chrono::steady_clock::now();
        for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis2); ++w) {
          if (context && context->IsStopRequestedAny()) {
            userStopped = true;
            break;
          }
          if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
            errorMessage = "GridScan: Timeout waiting for " + axis2 + " motion";
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (userStopped) break;

        // Settling time
        std::this_thread::sleep_for(std::chrono::milliseconds(settlingTimeMs));

        // Read channel value
        double value = static_cast<double>(deps.dataStore->GetValue(channel, 0.0f));

        // Record data point
        {
          PositionStruct fullPos = motionManager->GetCurrentPosition(deviceName);
          nlohmann::json point;
          point["index"] = currentPoint;
          point["row"] = row;
          point["col"] = actualCol;
          point["target_axis1"] = targetAxis1;
          point["target_axis2"] = targetAxis2;
          point["position"] = {
            {"x", fullPos.x}, {"y", fullPos.y}, {"z", fullPos.z},
            {"u", fullPos.u}, {"v", fullPos.v}, {"w", fullPos.w}
          };
          point["value"] = value;
          scanMeasurementsGS.push_back(std::move(point));
        }

        // Track peak (maximize)
        if (value > peakValue) {
          peakValue = value;
          peakAxis1 = targetAxis1;
          peakAxis2 = targetAxis2;
          if (deps.logger) {
            deps.logger->Debug(
              "GridScan: New peak at (" + std::to_string(targetAxis1) + ", " +
              std::to_string(targetAxis2) + ") = " + FormatSI(value),
              "MotionHandler"
            );
          }
        }

        currentPoint++;
        if (deps.logger && (currentPoint % 20 == 0 || currentPoint == totalPoints)) {
          deps.logger->Debug(
            "GridScan: Progress " + std::to_string(currentPoint) + "/" + std::to_string(totalPoints) +
            " (" + std::to_string(100 * currentPoint / totalPoints) + "%)",
            "MotionHandler"
          );
        }
      }
    }

    // ✅ RETURN TO PEAK POSITION (always, even if stopped by user)
    if (deps.logger) {
      deps.logger->Info(
        "GridScan: Returning to peak position (" + std::to_string(peakAxis1) + ", " +
        std::to_string(peakAxis2) + ") with value " + std::to_string(peakValue) +
        (userStopped ? " (stopped by user, returning to best peak found)" : ""),
        "MotionHandler"
      );
    }

    if (!motionManager->MoveToPosition(deviceName, axis1, peakAxis1, false)) {
      errorMessage = "GridScan: Failed to return to peak on " + axis1;
      return false;
    }
    auto startReturn = std::chrono::steady_clock::now();
    for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis1); ++w) {
      if (std::chrono::steady_clock::now() - startReturn > std::chrono::seconds(10)) {
        errorMessage = "GridScan: Timeout returning to peak on " + axis1;
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (!motionManager->MoveToPosition(deviceName, axis2, peakAxis2, false)) {
      errorMessage = "GridScan: Failed to return to peak on " + axis2;
      return false;
    }
    startReturn = std::chrono::steady_clock::now();
    for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis2); ++w) {
      if (std::chrono::steady_clock::now() - startReturn > std::chrono::seconds(10)) {
        errorMessage = "GridScan: Timeout returning to peak on " + axis2;
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // ✅ CAPTURE PEAK VALUE TO CONTEXT IF REQUESTED
    if (!captureAs.empty() && context) {
      context->SetValue(captureAs, static_cast<float>(peakValue), deviceName, "GridScan");
      if (deps.logger) {
        deps.logger->Debug(
          "GridScan: Captured peak value " + std::to_string(peakValue) + " to context:" + captureAs,
          "MotionHandler"
        );
      }
    }

    if (deps.logger) {
      std::string status = userStopped ? "Stopped by user" : "Completed successfully";
      deps.logger->Info(
        "GridScan: " + status + " - Peak value: " + FormatSI(peakValue) +
        " at (" + std::to_string(peakAxis1) + ", " + std::to_string(peakAxis2) + ")" +
        " (" + std::to_string(currentPoint) + "/" + std::to_string(totalPoints) + " points)",
        "MotionHandler"
      );
    }

    // ✅ SAVE GRID SCAN RESULTS TO FILE (always, even partial)
    {
      auto scanEndTimeGS = std::chrono::system_clock::now();
      auto durationSecGS = std::chrono::duration_cast<std::chrono::seconds>(scanEndTimeGS - scanStartTimeGS).count();

      nlohmann::json result;
      result["process_uid"] = processUID;
      result["scan_type"] = "Grid Scan";
      result["device"] = deviceName;
      result["plane"] = plane;
      result["scan_config"] = {
        {"mode", mode},
        {"range_um", rangeUm},
        {"step_size_um", stepUm},
        {"settling_time_ms", settlingTimeMs},
        {"channel", channel},
        {"snake_pattern", true}
      };
      result["grid"] = {
        {"axis1", axis1}, {"axis2", axis2},
        {"points_axis1", gridPoints}, {"points_axis2", gridPoints},
        {"total_points", totalPoints}
      };
      result["center_position"] = {
        {"x", currentPos.x}, {"y", currentPos.y}, {"z", currentPos.z},
        {"u", currentPos.u}, {"v", currentPos.v}, {"w", currentPos.w}
      };
      result["start_time"] = FormatTime(scanStartTimeGS);
      result["end_time"] = FormatTime(scanEndTimeGS);
      result["duration_seconds"] = durationSecGS;
      result["results"] = {
        {"peak_value", peakValue},
        {"peak_position", {{"axis1", peakAxis1}, {"axis2", peakAxis2}}},
        {"total_points_measured", currentPoint},
        {"early_exit", false},
        {"user_stopped", userStopped}
      };
      result["measurements"] = scanMeasurementsGS;

      WriteGridScanFiles(processUID, result, deps.logger);
    }

    if (userStopped) {
      errorMessage = "GridScan: Stopped by user (returned to peak)";
      return false;
    }
    return true;
  }

  // ============================================================================
  // GRID SCAN EXTENDED - FULL CONTROL 2D GRID SCANNING
  // ============================================================================
  if (methodName == "GridScanExtended") {
    // Scan-busy indicator — ref-counted Begin on enter, End on every return path.
    ScanActivityGuard _scanBusy(deps.scanAct, "GridScanExtended");

    // ✅ REQUIRED PARAMETER: plane (XY/XZ/YZ)
    auto planeIt = params.find("plane");
    if (planeIt == params.end()) {
      errorMessage = "GridScanExtended: Missing required parameter 'plane' (XY/XZ/YZ)";
      return false;
    }
    std::string plane = planeIt->second;

    // Parse plane into axis names
    std::string axis1, axis2;
    if (plane == "XY" || plane == "xy") {
      axis1 = "X"; axis2 = "Y";
    } else if (plane == "YX" || plane == "yx") {
      axis1 = "Y"; axis2 = "X";
    } else if (plane == "XZ" || plane == "xz") {
      axis1 = "X"; axis2 = "Z";
    } else if (plane == "YZ" || plane == "yz") {
      axis1 = "Y"; axis2 = "Z";
    } else {
      errorMessage = "GridScanExtended: Invalid plane '" + plane + "' (must be 'XY', 'YX', 'XZ', or 'YZ')";
      return false;
    }

    // ✅ REQUIRED PARAMETER: range_axis1_um
    auto range1It = params.find("range_axis1_um");
    if (range1It == params.end() || range1It->second.empty()) {
      errorMessage = "GridScanExtended: Missing required parameter 'range_axis1_um'";
      return false;
    }
    double rangeAxis1Um = std::stod(range1It->second);
    if (rangeAxis1Um <= 0 || rangeAxis1Um > 1000) {
      errorMessage = "GridScanExtended: range_axis1_um must be between 1 and 1000 um";
      return false;
    }

    // ✅ REQUIRED PARAMETER: range_axis2_um
    auto range2It = params.find("range_axis2_um");
    if (range2It == params.end() || range2It->second.empty()) {
      errorMessage = "GridScanExtended: Missing required parameter 'range_axis2_um'";
      return false;
    }
    double rangeAxis2Um = std::stod(range2It->second);
    if (rangeAxis2Um <= 0 || rangeAxis2Um > 1000) {
      errorMessage = "GridScanExtended: range_axis2_um must be between 1 and 1000 um";
      return false;
    }

    // ✅ REQUIRED PARAMETER: step_size_um
    auto stepIt = params.find("step_size_um");
    if (stepIt == params.end() || stepIt->second.empty()) {
      errorMessage = "GridScanExtended: Missing required parameter 'step_size_um'";
      return false;
    }
    double stepSizeUm = std::stod(stepIt->second);
    if (stepSizeUm < 0.5 || stepSizeUm > 100) {
      errorMessage = "GridScanExtended: step_size_um must be between 0.5 and 100 um";
      return false;
    }

    // ✅ REQUIRED PARAMETER: channel
    auto channelIt = params.find("channel");
    if (channelIt == params.end()) {
      errorMessage = "GridScanExtended: Missing required parameter 'channel'";
      return false;
    }
    std::string channel = channelIt->second;
    if (channel.empty()) {
      errorMessage = "GridScanExtended: Data channel cannot be empty";
      return false;
    }

    // ✅ OPTIONAL PARAMETER: settling_time_ms (default 300)
    int settlingTimeMs = 300;
    auto settlingIt = params.find("settling_time_ms");
    if (settlingIt != params.end() && !settlingIt->second.empty()) {
      settlingTimeMs = std::stoi(settlingIt->second);
      if (settlingTimeMs < 50 || settlingTimeMs > 2000) {
        errorMessage = "GridScanExtended: settling_time_ms must be between 50 and 2000";
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: snake_pattern (default true)
    bool snakePattern = true;
    auto snakeIt = params.find("snake_pattern");
    if (snakeIt != params.end()) {
      snakePattern = (snakeIt->second == "true" || snakeIt->second == "1");
    }

    // ✅ OPTIONAL PARAMETER: return_to_peak (default true)
    bool returnToPeak = true;
    auto returnIt = params.find("return_to_peak");
    if (returnIt != params.end()) {
      returnToPeak = (returnIt->second == "true" || returnIt->second == "1");
    }

    // ✅ OPTIONAL PARAMETER: direction_axis1 (centered/positive/negative)
    std::string directionAxis1 = "centered";
    auto dir1It = params.find("direction_axis1");
    if (dir1It != params.end() && !dir1It->second.empty()) {
      directionAxis1 = dir1It->second;
      if (directionAxis1 != "centered" && directionAxis1 != "positive" && directionAxis1 != "negative") {
        errorMessage = "GridScanExtended: Invalid direction_axis1 '" + directionAxis1 + "' (must be centered/positive/negative)";
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: direction_axis2 (centered/positive/negative)
    std::string directionAxis2 = "centered";
    auto dir2It = params.find("direction_axis2");
    if (dir2It != params.end() && !dir2It->second.empty()) {
      directionAxis2 = dir2It->second;
      if (directionAxis2 != "centered" && directionAxis2 != "positive" && directionAxis2 != "negative") {
        errorMessage = "GridScanExtended: Invalid direction_axis2 '" + directionAxis2 + "' (must be centered/positive/negative)";
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: exit_threshold
    double exitThreshold = std::numeric_limits<double>::max();
    bool hasExitThreshold = false;
    auto exitIt = params.find("exit_threshold");
    if (exitIt != params.end() && !exitIt->second.empty()) {
      try {
        exitThreshold = std::stod(exitIt->second);
        hasExitThreshold = true;
      } catch (...) {
        errorMessage = "GridScanExtended: Invalid exit_threshold value: " + exitIt->second;
        return false;
      }
    }

    // ✅ OPTIONAL PARAMETER: capture_as
    std::string captureAs;
    auto captureAsIt = params.find("capture_as");
    if (captureAsIt != params.end()) {
      captureAs = captureAsIt->second;
    }

    // ✅ OPTIONAL PARAMETER: capture_position_as
    std::string capturePositionAs;
    auto capturePosIt = params.find("capture_position_as");
    if (capturePosIt != params.end()) {
      capturePositionAs = capturePosIt->second;
    }

    // ✅ OPTIONAL PARAMETER: process_uid (for result file naming)
    std::string processUID;
    auto uidIt = params.find("process_uid");
    if (uidIt != params.end()) {
      processUID = uidIt->second;
    }

    // ✅ GET DATA STORE
    if (!deps.dataStore) {
      errorMessage = "GridScanExtended: DataStore not available";
      return false;
    }

    // Convert to mm
    double rangeAxis1Mm = rangeAxis1Um / 1000.0;
    double rangeAxis2Mm = rangeAxis2Um / 1000.0;
    double stepMm = stepSizeUm / 1000.0;

    // Calculate grid dimensions (asymmetric supported)
    int numPoints1 = static_cast<int>(rangeAxis1Um / stepSizeUm) + 1;
    int numPoints2 = static_cast<int>(rangeAxis2Um / stepSizeUm) + 1;
    int totalPoints = numPoints1 * numPoints2;

    if (deps.logger) {
      deps.logger->Info(
        "GridScanExtended: Starting scan on " + deviceName +
        " (plane=" + plane +
        ", axis1=" + std::to_string(rangeAxis1Um) + "um/" + directionAxis1 +
        ", axis2=" + std::to_string(rangeAxis2Um) + "um/" + directionAxis2 +
        ", step=" + std::to_string(stepSizeUm) + "um" +
        ", grid=" + std::to_string(numPoints1) + "x" + std::to_string(numPoints2) +
        " = " + std::to_string(totalPoints) + " points" +
        ", channel='" + channel + "'" +
        ", settling=" + std::to_string(settlingTimeMs) + "ms" +
        ", snake=" + (snakePattern ? "true" : "false") +
        ", returnToPeak=" + (returnToPeak ? "true" : "false") +
        (hasExitThreshold ? ", exitThreshold=" + std::to_string(exitThreshold) : "") + ")",
        "MotionHandler"
      );
    }

    // ✅ GET CURRENT POSITION (center of grid)
    double centerAxis1 = 0.0, centerAxis2 = 0.0;
    PositionStruct currentPosExt = motionManager->GetCurrentPosition(deviceName);
    if (axis1 == "X") centerAxis1 = currentPosExt.x;
    else if (axis1 == "Y") centerAxis1 = currentPosExt.y;
    else if (axis1 == "Z") centerAxis1 = currentPosExt.z;

    if (axis2 == "X") centerAxis2 = currentPosExt.x;
    else if (axis2 == "Y") centerAxis2 = currentPosExt.y;
    else if (axis2 == "Z") centerAxis2 = currentPosExt.z;

    // ✅ GRID SCAN DATA COLLECTION (in-memory, saved to file after scan)
    auto scanStartTime = std::chrono::system_clock::now();
    std::vector<nlohmann::json> scanMeasurements;
    scanMeasurements.reserve(totalPoints);

    // ✅ EXECUTE GRID SCAN
    double peakValue = std::numeric_limits<double>::lowest();
    double peakAxis1 = centerAxis1;
    double peakAxis2 = centerAxis2;
    int currentPoint = 0;
    bool earlyExit = false;
    bool userStopped = false;

    // Calculate start position based on direction
    // centered: current - range/2 to current + range/2
    // positive: current to current + range
    // negative: current - range to current
    double startAxis1, startAxis2;
    if (directionAxis1 == "positive") {
      startAxis1 = centerAxis1;  // Scan from current to current + range
    } else if (directionAxis1 == "negative") {
      startAxis1 = centerAxis1 - rangeAxis1Mm;  // Scan from current - range to current
    } else {  // centered
      startAxis1 = centerAxis1 - rangeAxis1Mm / 2.0;
    }

    if (directionAxis2 == "positive") {
      startAxis2 = centerAxis2;  // Scan from current to current + range
    } else if (directionAxis2 == "negative") {
      startAxis2 = centerAxis2 - rangeAxis2Mm;  // Scan from current - range to current
    } else {  // centered
      startAxis2 = centerAxis2 - rangeAxis2Mm / 2.0;
    }

    if (deps.logger) {
      deps.logger->Debug(
        "GridScanExtended: Axis1(" + axis1 + ") dir=" + directionAxis1 +
        " start=" + std::to_string(startAxis1) +
        ", Axis2(" + axis2 + ") dir=" + directionAxis2 +
        " start=" + std::to_string(startAxis2),
        "MotionHandler"
      );
    }

    for (int row = 0; row < numPoints2 && !earlyExit && !userStopped; ++row) {
      // Snake pattern: alternate direction each row
      bool reverse = snakePattern && (row % 2 == 1);

      for (int col = 0; col < numPoints1 && !earlyExit && !userStopped; ++col) {
        // Check stop flag
        if (context && context->IsStopRequestedAny()) {
          if (deps.logger) {
            deps.logger->Info("GridScanExtended: Stop requested, will return to best peak found so far", "MotionHandler");
          }
          userStopped = true;
          break;
        }

        int actualCol = reverse ? (numPoints1 - 1 - col) : col;

        double targetAxis1 = startAxis1 + actualCol * stepMm;
        double targetAxis2 = startAxis2 + row * stepMm;

        // Move to position (axis1 first, then axis2 for predictability)
        if (!motionManager->MoveToPosition(deviceName, axis1, targetAxis1, false)) {
          errorMessage = "GridScanExtended: Failed to start move on " + axis1;
          return false;
        }
        // Wait for axis1 move
        auto startMove = std::chrono::steady_clock::now();
        for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis1); ++w) {
          if (context && context->IsStopRequestedAny()) {
            userStopped = true;
            break;
          }
          if (std::chrono::steady_clock::now() - startMove > std::chrono::seconds(10)) {
            errorMessage = "GridScanExtended: Timeout waiting for " + axis1 + " motion";
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (userStopped) break;

        if (!motionManager->MoveToPosition(deviceName, axis2, targetAxis2, false)) {
          errorMessage = "GridScanExtended: Failed to start move on " + axis2;
          return false;
        }
        // Wait for axis2 move
        startMove = std::chrono::steady_clock::now();
        for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis2); ++w) {
          if (context && context->IsStopRequestedAny()) {
            userStopped = true;
            break;
          }
          if (std::chrono::steady_clock::now() - startMove > std::chrono::seconds(10)) {
            errorMessage = "GridScanExtended: Timeout waiting for " + axis2 + " motion";
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (userStopped) break;

        // Settling time
        std::this_thread::sleep_for(std::chrono::milliseconds(settlingTimeMs));

        // Read channel value
        double value = static_cast<double>(deps.dataStore->GetValue(channel, 0.0f));

        // Record data point with full XYZUVW position
        {
          PositionStruct fullPos = motionManager->GetCurrentPosition(deviceName);
          nlohmann::json point;
          point["index"] = currentPoint;
          point["row"] = row;
          point["col"] = actualCol;
          point["target_axis1"] = targetAxis1;
          point["target_axis2"] = targetAxis2;
          point["position"] = {
            {"x", fullPos.x}, {"y", fullPos.y}, {"z", fullPos.z},
            {"u", fullPos.u}, {"v", fullPos.v}, {"w", fullPos.w}
          };
          point["value"] = value;
          scanMeasurements.push_back(std::move(point));
        }

        // Track peak (maximize)
        if (value > peakValue) {
          peakValue = value;
          peakAxis1 = targetAxis1;
          peakAxis2 = targetAxis2;
          if (deps.logger) {
            deps.logger->Debug(
              "GridScanExtended: New peak at (" + std::to_string(targetAxis1) + ", " +
              std::to_string(targetAxis2) + ") = " + FormatSI(value),
              "MotionHandler"
            );
          }
        }

        // Check exit threshold
        if (hasExitThreshold && value > exitThreshold) {
          earlyExit = true;
          if (deps.logger) {
            deps.logger->Info(
              "GridScanExtended: Exit threshold reached (" + std::to_string(value) +
              " > " + std::to_string(exitThreshold) + "), stopping scan",
              "MotionHandler"
            );
          }
        }

        currentPoint++;
        if (deps.logger && (currentPoint % 20 == 0 || currentPoint == totalPoints)) {
          deps.logger->Debug(
            "GridScanExtended: Progress " + std::to_string(currentPoint) + "/" + std::to_string(totalPoints) +
            " (" + std::to_string(100 * currentPoint / totalPoints) + "%)",
            "MotionHandler"
          );
        }
      }
    }

    // ✅ RETURN TO PEAK POSITION (always when enabled, even if stopped by user)
    if (returnToPeak || userStopped) {
      if (deps.logger) {
        deps.logger->Info(
          "GridScanExtended: Returning to peak position (" + std::to_string(peakAxis1) + ", " +
          std::to_string(peakAxis2) + ") with value " + std::to_string(peakValue) +
          (userStopped ? " (stopped by user, returning to best peak found)" : ""),
          "MotionHandler"
        );
      }

      if (!motionManager->MoveToPosition(deviceName, axis1, peakAxis1, false)) {
        errorMessage = "GridScanExtended: Failed to return to peak on " + axis1;
        return false;
      }
      auto startReturnExt = std::chrono::steady_clock::now();
      for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis1); ++w) {
        if (std::chrono::steady_clock::now() - startReturnExt > std::chrono::seconds(10)) {
          errorMessage = "GridScanExtended: Timeout returning to peak on " + axis1;
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      if (!motionManager->MoveToPosition(deviceName, axis2, peakAxis2, false)) {
        errorMessage = "GridScanExtended: Failed to return to peak on " + axis2;
        return false;
      }
      startReturnExt = std::chrono::steady_clock::now();
      for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis2); ++w) {
        if (std::chrono::steady_clock::now() - startReturnExt > std::chrono::seconds(10)) {
          errorMessage = "GridScanExtended: Timeout returning to peak on " + axis2;
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    // ✅ CAPTURE PEAK VALUE TO CONTEXT IF REQUESTED
    if (!captureAs.empty() && context) {
      context->SetValue(captureAs, static_cast<float>(peakValue), deviceName, "GridScanExtended");
      if (deps.logger) {
        deps.logger->Debug(
          "GridScanExtended: Captured peak value " + std::to_string(peakValue) + " to context:" + captureAs,
          "MotionHandler"
        );
      }
    }

    // ✅ CAPTURE PEAK POSITION TO CONTEXT IF REQUESTED
    if (!capturePositionAs.empty() && context) {
      std::string positionStr = std::to_string(peakAxis1) + "," + std::to_string(peakAxis2);
      context->SetValue(capturePositionAs, positionStr, deviceName, "GridScanExtended");
      if (deps.logger) {
        deps.logger->Debug(
          "GridScanExtended: Captured peak position " + positionStr + " to context:" + capturePositionAs,
          "MotionHandler"
        );
      }
    }

    if (deps.logger) {
      std::string status = userStopped ? "Stopped by user" :
        (earlyExit ? "Completed (early exit - threshold reached)" : "Completed successfully");
      deps.logger->Info(
        "GridScanExtended: " + status +
        " - Peak value: " + std::to_string(peakValue) +
        " at (" + std::to_string(peakAxis1) + ", " + std::to_string(peakAxis2) + ")" +
        " (" + std::to_string(currentPoint) + "/" + std::to_string(totalPoints) + " points)",
        "MotionHandler"
      );
    }

    // ✅ SAVE GRID SCAN RESULTS TO FILE (always, even partial)
    {
      auto scanEndTime = std::chrono::system_clock::now();
      auto durationSec = std::chrono::duration_cast<std::chrono::seconds>(scanEndTime - scanStartTime).count();

      nlohmann::json result;
      result["process_uid"] = processUID;
      result["scan_type"] = "Grid Scan Extended";
      result["device"] = deviceName;
      result["plane"] = plane;
      result["scan_config"] = {
        {"range_axis1_um", rangeAxis1Um},
        {"direction_axis1", directionAxis1},
        {"range_axis2_um", rangeAxis2Um},
        {"direction_axis2", directionAxis2},
        {"step_size_um", stepSizeUm},
        {"settling_time_ms", settlingTimeMs},
        {"channel", channel},
        {"snake_pattern", snakePattern}
      };
      result["grid"] = {
        {"axis1", axis1}, {"axis2", axis2},
        {"points_axis1", numPoints1}, {"points_axis2", numPoints2},
        {"total_points", totalPoints}
      };
      result["center_position"] = {
        {"x", currentPosExt.x}, {"y", currentPosExt.y}, {"z", currentPosExt.z},
        {"u", currentPosExt.u}, {"v", currentPosExt.v}, {"w", currentPosExt.w}
      };
      result["start_time"] = FormatTime(scanStartTime);
      result["end_time"] = FormatTime(scanEndTime);
      result["duration_seconds"] = durationSec;
      result["results"] = {
        {"peak_value", peakValue},
        {"peak_position", {{"axis1", peakAxis1}, {"axis2", peakAxis2}}},
        {"total_points_measured", currentPoint},
        {"early_exit", earlyExit},
        {"user_stopped", userStopped}
      };
      result["measurements"] = scanMeasurements;

      WriteGridScanFiles(processUID, result, deps.logger);
    }

    if (userStopped) {
      errorMessage = "GridScanExtended: Stopped by user (returned to peak)";
      return false;
    }
    return true;
  }

  // ============================================================================
  // GRID SCAN DUAL CHANNEL - Two-channel grid scan for PIC waveguide alignment
  // ============================================================================
  if (methodName == "GridScanDualChannel") {
    // Scan-busy indicator — ref-counted Begin on enter, End on every return path.
    ScanActivityGuard _scanBusy(deps.scanAct, "GridScanDualChannel");

    // ✅ REQUIRED PARAMETER: plane (XY/XZ/YZ)
    auto planeItDC = params.find("plane");
    if (planeItDC == params.end()) {
      errorMessage = "GridScanDualChannel: Missing required parameter 'plane'";
      return false;
    }
    std::string plane = planeItDC->second;

    std::string axis1, axis2;
    if (plane == "XY" || plane == "xy") {
      axis1 = "X"; axis2 = "Y";
    } else if (plane == "XZ" || plane == "xz") {
      axis1 = "X"; axis2 = "Z";
    } else if (plane == "YZ" || plane == "yz") {
      axis1 = "Y"; axis2 = "Z";
    } else {
      errorMessage = "GridScanDualChannel: Invalid plane '" + plane + "'";
      return false;
    }

    // ✅ REQUIRED: range_axis1_um
    auto range1ItDC = params.find("range_axis1_um");
    if (range1ItDC == params.end() || range1ItDC->second.empty()) {
      errorMessage = "GridScanDualChannel: Missing 'range_axis1_um'";
      return false;
    }
    double rangeAxis1Um = std::stod(range1ItDC->second);
    if (rangeAxis1Um <= 0 || rangeAxis1Um > 1000) {
      errorMessage = "GridScanDualChannel: range_axis1_um must be 1-1000";
      return false;
    }

    // ✅ REQUIRED: range_axis2_um
    auto range2ItDC = params.find("range_axis2_um");
    if (range2ItDC == params.end() || range2ItDC->second.empty()) {
      errorMessage = "GridScanDualChannel: Missing 'range_axis2_um'";
      return false;
    }
    double rangeAxis2Um = std::stod(range2ItDC->second);
    if (rangeAxis2Um <= 0 || rangeAxis2Um > 1000) {
      errorMessage = "GridScanDualChannel: range_axis2_um must be 1-1000";
      return false;
    }

    // ✅ REQUIRED: step_size_um
    auto stepItDC = params.find("step_size_um");
    if (stepItDC == params.end() || stepItDC->second.empty()) {
      errorMessage = "GridScanDualChannel: Missing 'step_size_um'";
      return false;
    }
    double stepSizeUm = std::stod(stepItDC->second);
    if (stepSizeUm < 0.5 || stepSizeUm > 100) {
      errorMessage = "GridScanDualChannel: step_size_um must be 0.5-100";
      return false;
    }

    // ✅ REQUIRED: channel1
    auto ch1It = params.find("channel1");
    if (ch1It == params.end() || ch1It->second.empty()) {
      errorMessage = "GridScanDualChannel: Missing 'channel1'";
      return false;
    }
    std::string channel1 = ch1It->second;

    // ✅ REQUIRED: channel2
    auto ch2It = params.find("channel2");
    if (ch2It == params.end() || ch2It->second.empty()) {
      errorMessage = "GridScanDualChannel: Missing 'channel2'";
      return false;
    }
    std::string channel2 = ch2It->second;

    // ✅ OPTIONAL: waveguide_spacing_um (default 2500)
    double waveguideSpacingUm = 2500.0;
    auto wgIt = params.find("waveguide_spacing_um");
    if (wgIt != params.end() && !wgIt->second.empty()) {
      waveguideSpacingUm = std::stod(wgIt->second);
    }

    // ✅ OPTIONAL: settling_time_ms (default 300)
    int settlingTimeMs = 300;
    auto settlingItDC = params.find("settling_time_ms");
    if (settlingItDC != params.end() && !settlingItDC->second.empty()) {
      settlingTimeMs = std::stoi(settlingItDC->second);
      if (settlingTimeMs < 50 || settlingTimeMs > 2000) {
        errorMessage = "GridScanDualChannel: settling_time_ms must be 50-2000";
        return false;
      }
    }

    // ✅ OPTIONAL: snake_pattern (default true)
    bool snakePattern = true;
    auto snakeItDC = params.find("snake_pattern");
    if (snakeItDC != params.end()) {
      snakePattern = (snakeItDC->second == "true" || snakeItDC->second == "1");
    }

    // ✅ OPTIONAL: return_to_peak (channel1/channel2/none, default channel1)
    std::string returnToPeak = "channel1";
    auto returnItDC = params.find("return_to_peak");
    if (returnItDC != params.end() && !returnItDC->second.empty()) {
      returnToPeak = returnItDC->second;
    }

    // ✅ OPTIONAL: direction_axis1
    std::string directionAxis1 = "centered";
    auto dir1ItDC = params.find("direction_axis1");
    if (dir1ItDC != params.end() && !dir1ItDC->second.empty()) {
      directionAxis1 = dir1ItDC->second;
      if (directionAxis1 != "centered" && directionAxis1 != "positive" && directionAxis1 != "negative") {
        errorMessage = "GridScanDualChannel: Invalid direction_axis1 '" + directionAxis1 + "'";
        return false;
      }
    }

    // ✅ OPTIONAL: direction_axis2
    std::string directionAxis2 = "centered";
    auto dir2ItDC = params.find("direction_axis2");
    if (dir2ItDC != params.end() && !dir2ItDC->second.empty()) {
      directionAxis2 = dir2ItDC->second;
      if (directionAxis2 != "centered" && directionAxis2 != "positive" && directionAxis2 != "negative") {
        errorMessage = "GridScanDualChannel: Invalid direction_axis2 '" + directionAxis2 + "'";
        return false;
      }
    }

    // ✅ OPTIONAL: exit_threshold
    double exitThreshold = std::numeric_limits<double>::max();
    bool hasExitThreshold = false;
    auto exitItDC = params.find("exit_threshold");
    if (exitItDC != params.end() && !exitItDC->second.empty()) {
      try {
        exitThreshold = std::stod(exitItDC->second);
        hasExitThreshold = true;
      } catch (...) {
        errorMessage = "GridScanDualChannel: Invalid exit_threshold: " + exitItDC->second;
        return false;
      }
    }

    // ✅ OPTIONAL: capture params
    std::string capturePeak1As, capturePeak2As, capturePeak1PosAs, capturePeak2PosAs, capturePeakDeltaAs;
    auto cp1 = params.find("capture_peak1_as");
    if (cp1 != params.end()) capturePeak1As = cp1->second;
    auto cp2 = params.find("capture_peak2_as");
    if (cp2 != params.end()) capturePeak2As = cp2->second;
    auto cp1p = params.find("capture_peak1_position_as");
    if (cp1p != params.end()) capturePeak1PosAs = cp1p->second;
    auto cp2p = params.find("capture_peak2_position_as");
    if (cp2p != params.end()) capturePeak2PosAs = cp2p->second;
    auto cpd = params.find("capture_peak_delta_as");
    if (cpd != params.end()) capturePeakDeltaAs = cpd->second;

    // ✅ OPTIONAL: process_uid
    std::string processUID;
    auto uidItDC = params.find("process_uid");
    if (uidItDC != params.end()) processUID = uidItDC->second;

    // ✅ GET DATA STORE
    if (!deps.dataStore) {
      errorMessage = "GridScanDualChannel: DataStore not available";
      return false;
    }

    // Convert to mm
    double rangeAxis1Mm = rangeAxis1Um / 1000.0;
    double rangeAxis2Mm = rangeAxis2Um / 1000.0;
    double stepMm = stepSizeUm / 1000.0;

    // Grid dimensions
    int numPoints1 = static_cast<int>(rangeAxis1Um / stepSizeUm) + 1;
    int numPoints2 = static_cast<int>(rangeAxis2Um / stepSizeUm) + 1;
    int totalPoints = numPoints1 * numPoints2;

    if (deps.logger) {
      deps.logger->Info(
        "GridScanDualChannel: Starting dual-channel scan on " + deviceName +
        " (plane=" + plane +
        ", grid=" + std::to_string(numPoints1) + "x" + std::to_string(numPoints2) +
        " = " + std::to_string(totalPoints) + " points" +
        ", ch1='" + channel1 + "', ch2='" + channel2 + "'" +
        ", waveguide=" + std::to_string(waveguideSpacingUm) + "um" +
        ", returnTo=" + returnToPeak + ")",
        "MotionHandler"
      );
    }

    // Get current position (center of grid)
    PositionStruct currentPosDC = motionManager->GetCurrentPosition(deviceName);
    double centerAxis1 = 0.0, centerAxis2 = 0.0;
    if (axis1 == "X") centerAxis1 = currentPosDC.x;
    else if (axis1 == "Y") centerAxis1 = currentPosDC.y;
    else if (axis1 == "Z") centerAxis1 = currentPosDC.z;

    if (axis2 == "X") centerAxis2 = currentPosDC.x;
    else if (axis2 == "Y") centerAxis2 = currentPosDC.y;
    else if (axis2 == "Z") centerAxis2 = currentPosDC.z;

    // Data collection
    auto scanStartTimeDC = std::chrono::system_clock::now();
    std::vector<nlohmann::json> scanMeasurementsDC;
    scanMeasurementsDC.reserve(totalPoints);

    // Dual peak tracking
    double peakValue1 = std::numeric_limits<double>::lowest();
    double peakAxis1_ch1 = centerAxis1, peakAxis2_ch1 = centerAxis2;
    PositionStruct peakFullPos_ch1 = currentPosDC;
    double peakValue2 = std::numeric_limits<double>::lowest();
    double peakAxis1_ch2 = centerAxis1, peakAxis2_ch2 = centerAxis2;
    PositionStruct peakFullPos_ch2 = currentPosDC;
    int currentPoint = 0;
    bool earlyExit = false;

    // Calculate start positions
    double startAxis1, startAxis2;
    if (directionAxis1 == "positive") startAxis1 = centerAxis1;
    else if (directionAxis1 == "negative") startAxis1 = centerAxis1 - rangeAxis1Mm;
    else startAxis1 = centerAxis1 - rangeAxis1Mm / 2.0;

    if (directionAxis2 == "positive") startAxis2 = centerAxis2;
    else if (directionAxis2 == "negative") startAxis2 = centerAxis2 - rangeAxis2Mm;
    else startAxis2 = centerAxis2 - rangeAxis2Mm / 2.0;

    // ✅ EXECUTE GRID SCAN (dual channel)
    bool userStopped = false;
    for (int row = 0; row < numPoints2 && !earlyExit && !userStopped; ++row) {
      bool reverse = snakePattern && (row % 2 == 1);

      for (int col = 0; col < numPoints1 && !earlyExit && !userStopped; ++col) {
        if (context && context->IsStopRequestedAny()) {
          if (deps.logger) deps.logger->Info("GridScanDualChannel: Stop requested, will return to best peak found so far", "MotionHandler");
          userStopped = true;
          break;
        }

        int actualCol = reverse ? (numPoints1 - 1 - col) : col;
        double targetAxis1 = startAxis1 + actualCol * stepMm;
        double targetAxis2 = startAxis2 + row * stepMm;

        // Move axis1
        if (!motionManager->MoveToPosition(deviceName, axis1, targetAxis1, false)) {
          errorMessage = "GridScanDualChannel: Failed to move " + axis1;
          return false;
        }
        auto startMoveDC = std::chrono::steady_clock::now();
        for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis1); ++w) {
          if (context && context->IsStopRequestedAny()) {
            userStopped = true;
            break;
          }
          if (std::chrono::steady_clock::now() - startMoveDC > std::chrono::seconds(10)) {
            errorMessage = "GridScanDualChannel: Timeout waiting for " + axis1;
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (userStopped) break;

        // Move axis2
        if (!motionManager->MoveToPosition(deviceName, axis2, targetAxis2, false)) {
          errorMessage = "GridScanDualChannel: Failed to move " + axis2;
          return false;
        }
        startMoveDC = std::chrono::steady_clock::now();
        for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis2); ++w) {
          if (context && context->IsStopRequestedAny()) {
            userStopped = true;
            break;
          }
          if (std::chrono::steady_clock::now() - startMoveDC > std::chrono::seconds(10)) {
            errorMessage = "GridScanDualChannel: Timeout waiting for " + axis2;
            return false;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (userStopped) break;

        // Settling time
        std::this_thread::sleep_for(std::chrono::milliseconds(settlingTimeMs));

        // Read both channels
        double value1 = static_cast<double>(deps.dataStore->GetValue(channel1, 0.0f));
        double value2 = static_cast<double>(deps.dataStore->GetValue(channel2, 0.0f));

        // Record data point with full XYZUVW
        PositionStruct fullPos = motionManager->GetCurrentPosition(deviceName);
        {
          nlohmann::json point;
          point["index"] = currentPoint;
          point["row"] = row;
          point["col"] = actualCol;
          point["target_axis1"] = targetAxis1;
          point["target_axis2"] = targetAxis2;
          point["position"] = {
            {"x", fullPos.x}, {"y", fullPos.y}, {"z", fullPos.z},
            {"u", fullPos.u}, {"v", fullPos.v}, {"w", fullPos.w}
          };
          point["value1"] = value1;
          point["value2"] = value2;
          scanMeasurementsDC.push_back(std::move(point));
        }

        // Track ch1 peak
        if (value1 > peakValue1) {
          peakValue1 = value1;
          peakAxis1_ch1 = targetAxis1;
          peakAxis2_ch1 = targetAxis2;
          peakFullPos_ch1 = fullPos;
          if (deps.logger) {
            deps.logger->Debug(
              "GridScanDualChannel: New Ch1 peak at (" + std::to_string(targetAxis1) + ", " +
              std::to_string(targetAxis2) + ") = " + FormatSI(value1),
              "MotionHandler"
            );
          }
        }

        // Track ch2 peak
        if (value2 > peakValue2) {
          peakValue2 = value2;
          peakAxis1_ch2 = targetAxis1;
          peakAxis2_ch2 = targetAxis2;
          peakFullPos_ch2 = fullPos;
          if (deps.logger) {
            deps.logger->Debug(
              "GridScanDualChannel: New Ch2 peak at (" + std::to_string(targetAxis1) + ", " +
              std::to_string(targetAxis2) + ") = " + FormatSI(value2),
              "MotionHandler"
            );
          }
        }

        // Exit threshold (either channel)
        if (hasExitThreshold && (value1 > exitThreshold || value2 > exitThreshold)) {
          earlyExit = true;
          if (deps.logger) {
            deps.logger->Info(
              "GridScanDualChannel: Exit threshold reached (ch1=" + FormatSI(value1) +
              ", ch2=" + FormatSI(value2) + " > " + FormatSI(exitThreshold) + ")",
              "MotionHandler"
            );
          }
        }

        currentPoint++;
        if (deps.logger && (currentPoint % 20 == 0 || currentPoint == totalPoints)) {
          deps.logger->Debug(
            "GridScanDualChannel: Progress " + std::to_string(currentPoint) + "/" + std::to_string(totalPoints) +
            " (" + std::to_string(100 * currentPoint / totalPoints) + "%)",
            "MotionHandler"
          );
        }
      }
    }

    // ✅ COMPUTE PEAK DELTA (axis1 difference between ch1 and ch2 peaks)
    double peakDeltaAxis1 = peakAxis1_ch1 - peakAxis1_ch2;

    if (deps.logger) {
      deps.logger->Info(
        "GridScanDualChannel: Peak delta axis1 = " + std::to_string(peakDeltaAxis1 * 1000.0) + " um" +
        " (ch1@" + std::to_string(peakAxis1_ch1) + " - ch2@" + std::to_string(peakAxis1_ch2) + ")" +
        ", waveguide spacing=" + std::to_string(waveguideSpacingUm) + "um",
        "MotionHandler"
      );
    }

    // ✅ RETURN TO PEAK POSITION (always when configured, even if stopped by user)
    // On user stop: default to channel1 peak if return_to_peak was "none"
    std::string effectiveReturnTo = returnToPeak;
    if (userStopped && effectiveReturnTo == "none") {
      effectiveReturnTo = "channel1";
    }
    if (effectiveReturnTo == "channel1" || effectiveReturnTo == "channel2") {
      double returnAxis1 = (effectiveReturnTo == "channel1") ? peakAxis1_ch1 : peakAxis1_ch2;
      double returnAxis2 = (effectiveReturnTo == "channel1") ? peakAxis2_ch1 : peakAxis2_ch2;
      double returnVal = (effectiveReturnTo == "channel1") ? peakValue1 : peakValue2;

      if (deps.logger) {
        deps.logger->Info(
          "GridScanDualChannel: Returning to " + effectiveReturnTo + " peak (" +
          std::to_string(returnAxis1) + ", " + std::to_string(returnAxis2) +
          ") value=" + FormatSI(returnVal) +
          (userStopped ? " (stopped by user, returning to best peak found)" : ""),
          "MotionHandler"
        );
      }

      if (!motionManager->MoveToPosition(deviceName, axis1, returnAxis1, false)) {
        errorMessage = "GridScanDualChannel: Failed to return to peak on " + axis1;
        return false;
      }
      auto startReturnDC = std::chrono::steady_clock::now();
      for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis1); ++w) {
        if (std::chrono::steady_clock::now() - startReturnDC > std::chrono::seconds(10)) {
          errorMessage = "GridScanDualChannel: Timeout returning to peak on " + axis1;
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      if (!motionManager->MoveToPosition(deviceName, axis2, returnAxis2, false)) {
        errorMessage = "GridScanDualChannel: Failed to return to peak on " + axis2;
        return false;
      }
      startReturnDC = std::chrono::steady_clock::now();
      for (int w = 0; w < 1000 && motionManager->IsMoving(deviceName, axis2); ++w) {
        if (std::chrono::steady_clock::now() - startReturnDC > std::chrono::seconds(10)) {
          errorMessage = "GridScanDualChannel: Timeout returning to peak on " + axis2;
          return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    // ✅ CAPTURE RESULTS TO CONTEXT
    if (!capturePeak1As.empty() && context) {
      context->SetValue(capturePeak1As, static_cast<float>(peakValue1), deviceName, "GridScanDualChannel");
      if (deps.logger) deps.logger->Debug("GridScanDualChannel: Captured ch1 peak " + std::to_string(peakValue1) + " to context:" + capturePeak1As, "MotionHandler");
    }
    if (!capturePeak2As.empty() && context) {
      context->SetValue(capturePeak2As, static_cast<float>(peakValue2), deviceName, "GridScanDualChannel");
      if (deps.logger) deps.logger->Debug("GridScanDualChannel: Captured ch2 peak " + std::to_string(peakValue2) + " to context:" + capturePeak2As, "MotionHandler");
    }
    if (!capturePeak1PosAs.empty() && context) {
      std::string pos1Str = std::to_string(peakAxis1_ch1) + "," + std::to_string(peakAxis2_ch1);
      context->SetValue(capturePeak1PosAs, pos1Str, deviceName, "GridScanDualChannel");
      if (deps.logger) deps.logger->Debug("GridScanDualChannel: Captured ch1 peak position " + pos1Str + " to context:" + capturePeak1PosAs, "MotionHandler");
    }
    if (!capturePeak2PosAs.empty() && context) {
      std::string pos2Str = std::to_string(peakAxis1_ch2) + "," + std::to_string(peakAxis2_ch2);
      context->SetValue(capturePeak2PosAs, pos2Str, deviceName, "GridScanDualChannel");
      if (deps.logger) deps.logger->Debug("GridScanDualChannel: Captured ch2 peak position " + pos2Str + " to context:" + capturePeak2PosAs, "MotionHandler");
    }
    if (!capturePeakDeltaAs.empty() && context) {
      context->SetValue(capturePeakDeltaAs, static_cast<float>(peakDeltaAxis1), deviceName, "GridScanDualChannel");
      if (deps.logger) deps.logger->Debug("GridScanDualChannel: Captured peak delta " + std::to_string(peakDeltaAxis1) + " mm to context:" + capturePeakDeltaAs, "MotionHandler");
    }

    if (deps.logger) {
      std::string status = userStopped ? "Stopped by user" :
        (earlyExit ? "Completed (early exit)" : "Completed");
      double dxUm = (peakFullPos_ch1.x - peakFullPos_ch2.x) * 1000.0;
      double dyUm = (peakFullPos_ch1.y - peakFullPos_ch2.y) * 1000.0;
      double dzUm = (peakFullPos_ch1.z - peakFullPos_ch2.z) * 1000.0;
      double duDeg = peakFullPos_ch1.u - peakFullPos_ch2.u;
      double dvDeg = peakFullPos_ch1.v - peakFullPos_ch2.v;
      double dwDeg = peakFullPos_ch1.w - peakFullPos_ch2.w;
      deps.logger->Info(
        "GridScanDualChannel: " + status +
        " (" + std::to_string(currentPoint) + "/" + std::to_string(totalPoints) + " points)" +
        " - Ch1 peak: " + FormatSI(peakValue1) + " at (" + std::to_string(peakAxis1_ch1) + ", " + std::to_string(peakAxis2_ch1) + ")" +
        " - Ch2 peak: " + FormatSI(peakValue2) + " at (" + std::to_string(peakAxis1_ch2) + ", " + std::to_string(peakAxis2_ch2) + ")" +
        " - Delta XYZUVW: dX=" + std::to_string(dxUm) + "um dY=" + std::to_string(dyUm) + "um dZ=" + std::to_string(dzUm) + "um" +
        " dU=" + std::to_string(duDeg) + "deg dV=" + std::to_string(dvDeg) + "deg dW=" + std::to_string(dwDeg) + "deg",
        "MotionHandler"
      );
    }

    // ✅ SAVE RESULTS TO FILE
    {
      auto scanEndTimeDC = std::chrono::system_clock::now();
      auto durationSecDC = std::chrono::duration_cast<std::chrono::seconds>(scanEndTimeDC - scanStartTimeDC).count();

      nlohmann::json result;
      result["process_uid"] = processUID;
      result["scan_type"] = "Grid Scan Dual Channel";
      result["device"] = deviceName;
      result["plane"] = plane;
      result["scan_config"] = {
        {"range_axis1_um", rangeAxis1Um},
        {"direction_axis1", directionAxis1},
        {"range_axis2_um", rangeAxis2Um},
        {"direction_axis2", directionAxis2},
        {"step_size_um", stepSizeUm},
        {"settling_time_ms", settlingTimeMs},
        {"channel1", channel1},
        {"channel2", channel2},
        {"waveguide_spacing_um", waveguideSpacingUm},
        {"snake_pattern", snakePattern},
        {"return_to_peak", returnToPeak}
      };
      result["grid"] = {
        {"axis1", axis1}, {"axis2", axis2},
        {"points_axis1", numPoints1}, {"points_axis2", numPoints2},
        {"total_points", totalPoints}
      };
      result["center_position"] = {
        {"x", currentPosDC.x}, {"y", currentPosDC.y}, {"z", currentPosDC.z},
        {"u", currentPosDC.u}, {"v", currentPosDC.v}, {"w", currentPosDC.w}
      };
      result["start_time"] = FormatTime(scanStartTimeDC);
      result["end_time"] = FormatTime(scanEndTimeDC);
      result["duration_seconds"] = durationSecDC;
      result["results"] = {
        {"channel1_peak", {
          {"value", peakValue1},
          {"position", {{"axis1", peakAxis1_ch1}, {"axis2", peakAxis2_ch1}}},
          {"full_position", {
            {"x", peakFullPos_ch1.x}, {"y", peakFullPos_ch1.y}, {"z", peakFullPos_ch1.z},
            {"u", peakFullPos_ch1.u}, {"v", peakFullPos_ch1.v}, {"w", peakFullPos_ch1.w}
          }}
        }},
        {"channel2_peak", {
          {"value", peakValue2},
          {"position", {{"axis1", peakAxis1_ch2}, {"axis2", peakAxis2_ch2}}},
          {"full_position", {
            {"x", peakFullPos_ch2.x}, {"y", peakFullPos_ch2.y}, {"z", peakFullPos_ch2.z},
            {"u", peakFullPos_ch2.u}, {"v", peakFullPos_ch2.v}, {"w", peakFullPos_ch2.w}
          }}
        }},
        {"peak_delta_axis1", peakDeltaAxis1},
        {"peak_delta_axis1_um", peakDeltaAxis1 * 1000.0},
        {"peak_delta_xyzuvw", {
          {"x_mm", peakFullPos_ch1.x - peakFullPos_ch2.x},
          {"y_mm", peakFullPos_ch1.y - peakFullPos_ch2.y},
          {"z_mm", peakFullPos_ch1.z - peakFullPos_ch2.z},
          {"u_deg", peakFullPos_ch1.u - peakFullPos_ch2.u},
          {"v_deg", peakFullPos_ch1.v - peakFullPos_ch2.v},
          {"w_deg", peakFullPos_ch1.w - peakFullPos_ch2.w},
          {"x_um", (peakFullPos_ch1.x - peakFullPos_ch2.x) * 1000.0},
          {"y_um", (peakFullPos_ch1.y - peakFullPos_ch2.y) * 1000.0},
          {"z_um", (peakFullPos_ch1.z - peakFullPos_ch2.z) * 1000.0}
        }},
        {"waveguide_spacing_um", waveguideSpacingUm},
        {"total_points_measured", currentPoint},
        {"early_exit", earlyExit},
        {"user_stopped", userStopped},
        {"returned_to", effectiveReturnTo}
      };
      result["measurements"] = scanMeasurementsDC;

      WriteGridScanFiles(processUID, result, deps.logger);
    }

    if (userStopped) {
      errorMessage = "GridScanDualChannel: Stopped by user (returned to peak)";
      return false;
    }
    return true;
  }

  return std::nullopt;
}

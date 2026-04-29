// CustomServerFitCommands.cpp
// Fit family — TCP-based fit operations + state management.
#include "CustomServerFitCommands.h"

#include "ApplicationContext.h"
#include "../../customserver/BeamServerClient.h"
#include "LogManager.h"
#include "SequenceContext.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

// Exception-free numeric conversions (std::stod/stoi throw on empty/invalid).
double safeStod(const std::string& str, double defaultVal = 0.0) {
  if (str.empty()) return defaultVal;
  char* end = nullptr;
  double val = std::strtod(str.c_str(), &end);
  if (end == str.c_str()) return defaultVal;
  return val;
}

int safeStoi(const std::string& str, int defaultVal = 0) {
  if (str.empty()) return defaultVal;
  char* end = nullptr;
  long val = std::strtol(str.c_str(), &end, 10);
  if (end == str.c_str()) return defaultVal;
  return static_cast<int>(val);
}

}  // namespace

bool CustomServerFitCommands::HandleFit(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const CustomServerCommandDeps& deps)
{
  if (!deps.app->beamServerClient->IsConnected()) {
    errorMessage = "Beam server not connected";
    return false;
  }

  double umPerPxX = 0.25;
  auto uxIt = params.find("um_per_px_x");
  if (uxIt != params.end() && !uxIt->second.empty()) {
    umPerPxX = safeStod(uxIt->second);
  }

  double umPerPxY = 0.25;
  auto uyIt = params.find("um_per_px_y");
  if (uyIt != params.end() && !uyIt->second.empty()) {
    umPerPxY = safeStod(uyIt->second);
  }

  double wavelengthM = 1550e-9;  // default 1550nm
  auto wlIt = params.find("wavelength_m");
  if (wlIt != params.end() && !wlIt->second.empty()) {
    wavelengthM = safeStod(wlIt->second);
  }

  std::string captureAs = "beam";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  if (deps.app->logManager) {
    deps.app->logManager->Info("Sending fit request (wavelength=" +
                   std::to_string(wavelengthM) + "m)", "CustomServer");
  }

  auto result = deps.app->beamServerClient->SendFit(umPerPxX, umPerPxY, wavelengthM);

  if (result.empty()) {
    errorMessage = "Fit failed: " + deps.app->beamServerClient->GetLastError();
    if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
    return false;
  }

  if (result.contains("error")) {
    errorMessage = "Server error: " + result["error"].get<std::string>();
    if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
    return false;
  }

  // Store results in sequence context
  if (context) {
    int numBeams = result.value("num_beams", 0);
    int numZSteps = result.value("num_z_steps", 0);
    context->SetValue(captureAs + ".num_beams", std::to_string(numBeams));
    context->SetValue(captureAs + ".num_z_steps", std::to_string(numZSteps));

    // Per-beam fit results
    if (result.contains("beams") && result["beams"].is_array()) {
      int beamIdx = 0;
      for (const auto& beam : result["beams"]) {
        std::string prefix = captureAs + "." + std::to_string(beamIdx);

        // Store side label (TX/RX)
        if (beam.contains("side"))
          context->SetValue(prefix + ".side", beam["side"].get<std::string>());

        auto storeDouble = [&](const std::string& key) {
          if (beam.contains(key))
            context->SetValue(prefix + "." + key, std::to_string(beam[key].get<double>()));
        };

        storeDouble("beam_loc_x_um");
        storeDouble("beam_loc_y_um");
        storeDouble("waist_x_um");
        storeDouble("waist_y_um");
        storeDouble("centroid_z_um");
        storeDouble("poynt_x_deg");
        storeDouble("poynt_y_deg");
        storeDouble("m2_x");
        storeDouble("m2_y");
        storeDouble("fit_error_x");
        storeDouble("fit_error_y");

        ++beamIdx;
      }
    }

    // TX-RX vector
    if (result.contains("tx_rx_vector_x_um"))
      context->SetValue(captureAs + ".tx_rx_vector_x_um",
        std::to_string(result["tx_rx_vector_x_um"].get<double>()));
    if (result.contains("tx_rx_vector_y_um"))
      context->SetValue(captureAs + ".tx_rx_vector_y_um",
        std::to_string(result["tx_rx_vector_y_um"].get<double>()));

    if (deps.app->logManager) {
      deps.app->logManager->Info("Fit result: " + std::to_string(numBeams) + " beams, " +
                     std::to_string(numZSteps) + " z-steps", "CustomServer");
    }
  }

  return true;
}
bool CustomServerFitCommands::HandleSave(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const CustomServerCommandDeps& deps)
{
  if (!deps.app->beamServerClient->IsConnected()) {
    errorMessage = "Beam server not connected";
    return false;
  }

  std::string name;
  auto nameIt = params.find("name");
  if (nameIt != params.end() && !nameIt->second.empty()) {
    name = nameIt->second;
    if (context && name.substr(0, 8) == "context:") {
      name = context->GetString(name.substr(8));
    }
  }

  if (deps.app->logManager) {
    deps.app->logManager->Info("Sending save request" +
                   (name.empty() ? "" : " (name=" + name + ")"), "CustomServer");
  }

  auto result = deps.app->beamServerClient->SendSave(name);

  if (result.empty()) {
    errorMessage = "Save failed: " + deps.app->beamServerClient->GetLastError();
    if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
    return false;
  }

  if (result.contains("error")) {
    errorMessage = "Server error: " + result["error"].get<std::string>();
    if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
    return false;
  }

  // Store save directory in context
  std::string captureAs = "beam";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  if (context && result.contains("save_dir")) {
    context->SetValue(captureAs + ".save_dir", result["save_dir"].get<std::string>());
  }

  if (deps.app->logManager) {
    std::string saveDir = result.value("save_dir", "unknown");
    int numImages = result.value("num_images", 0);
    deps.app->logManager->Info("Save complete: " + std::to_string(numImages) +
                   " images saved to " + saveDir, "CustomServer");
  }

  return true;
}
bool CustomServerFitCommands::HandleReset(
  std::string& errorMessage,
  const CustomServerCommandDeps& deps)
{
  // No-op if not connected — allows Reset before Connect in recipes
  if (!deps.app->beamServerClient->IsConnected()) {
    if (deps.app->logManager) deps.app->logManager->Info("Beam server not connected, skipping reset", "CustomServer");
    return true;
  }

  if (!deps.app->beamServerClient->SendReset()) {
    errorMessage = "Reset failed: " + deps.app->beamServerClient->GetLastError();
    if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
    return false;
  }

  if (deps.app->logManager) deps.app->logManager->Info("Beam server reset", "CustomServer");
  return true;
}

std::optional<bool> CustomServerFitCommands::TryExecute(
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const CustomServerCommandDeps& deps)
{
  if (methodName == "Fit")   return HandleFit(params, errorMessage, context, deps);
  if (methodName == "Save")  return HandleSave(params, errorMessage, context, deps);
  if (methodName == "Reset") return HandleReset(errorMessage, deps);
  return std::nullopt;
}

// CustomServerDetectionCommands.cpp
// Detection family — TCP-based beam detection (live + file + folder).
#include "CustomServerDetectionCommands.h"

#include "ApplicationContext.h"
#include "../../customserver/BeamServerClient.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "CameraFrameData.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include "unified_motion_manager.h"  // GetCurrentPosition() used in detection metadata

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <regex>
#include <string>
#include <thread>

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

bool CustomServerDetectionCommands::HandleDetect(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const CustomServerCommandDeps& deps)
{
  if (!deps.app->beamServerClient->IsConnected()) {
    errorMessage = "Beam server not connected";
    return false;
  }

  // Inbounce delay — settle time before starting detection
  int inbounceDelayMs = 500;
  auto ibIt = params.find("inbounce_delay_ms");
  if (ibIt != params.end() && !ibIt->second.empty()) {
    inbounceDelayMs = safeStoi(ibIt->second, 500);
    if (inbounceDelayMs < 0) inbounceDelayMs = 0;
  }
  if (inbounceDelayMs > 0) {
    if (deps.app->logManager) deps.app->logManager->Info("Detect: inbounce delay " + std::to_string(inbounceDelayMs) + "ms", "CustomServer");
    std::this_thread::sleep_for(std::chrono::milliseconds(inbounceDelayMs));
  }

  // Parse parameters
  auto cameraIt = params.find("camera_device");
  if (cameraIt == params.end() || cameraIt->second.empty()) {
    errorMessage = "Missing required parameter: camera_device";
    return false;
  }
  std::string cameraDevice = cameraIt->second;

  // Resolve z_um: prefer live device reading, fall back to param/context value
  double z_um = 0.0;
  bool useDeviceZ = false;
  auto udzIt = params.find("use_device_z");
  if (udzIt != params.end()) {
    useDeviceZ = (udzIt->second == "true" || udzIt->second == "1");
  }

  if (useDeviceZ && deps.app->motionManager) {
    std::string zDevice = "gantry-main";
    auto zdIt = params.find("z_device");
    if (zdIt != params.end() && !zdIt->second.empty()) {
      zDevice = zdIt->second;
    }
    std::string zAxis = "Z";
    auto zaIt = params.find("z_axis");
    if (zaIt != params.end() && !zaIt->second.empty()) {
      zAxis = zaIt->second;
    }

    double positionMm = 0.0;
    if (deps.app->motionManager->GetPosition(zDevice, zAxis, positionMm)) {
      z_um = positionMm * 1000.0;  // mm -> um
      if (deps.app->logManager) {
        deps.app->logManager->Info("Read Z from " + zDevice + "." + zAxis + " = " +
                       std::to_string(positionMm) + " mm (" + std::to_string(z_um) + " um)",
                       "CustomServer");
      }
    } else {
      if (deps.app->logManager) {
        deps.app->logManager->Warning("Failed to read Z from " + zDevice + "." + zAxis +
                          ", falling back to z_um parameter", "CustomServer");
      }
      useDeviceZ = false;  // fall through to param/context below
    }
  }

  if (!useDeviceZ) {
    auto zIt = params.find("z_um");
    if (zIt != params.end() && !zIt->second.empty()) {
      std::string zVal = zIt->second;
      if (context && zVal.substr(0, 8) == "context:") {
        std::string contextKey = zVal.substr(8);
        zVal = context->GetString(contextKey);
        if (zVal.empty() && deps.app->logManager) {
          deps.app->logManager->Warning("Context key '" + contextKey + "' not found or empty, z_um will be 0",
                            "CustomServer");
        }
      }
      z_um = safeStod(zVal);
    }
  }

  bool rotate180 = false;
  auto rotIt = params.find("rotate_180");
  if (rotIt != params.end()) {
    rotate180 = (rotIt->second == "true" || rotIt->second == "1");
  }

  double umPerPxX = 1.0;
  auto uxIt = params.find("um_per_px_x");
  if (uxIt != params.end() && !uxIt->second.empty()) {
    umPerPxX = safeStod(uxIt->second);
  }

  double umPerPxY = 1.0;
  auto uyIt = params.find("um_per_px_y");
  if (uyIt != params.end() && !uyIt->second.empty()) {
    umPerPxY = safeStod(uyIt->second);
  }

  std::string captureAs = "beam";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  // Grab frame from camera
  if (!deps.app->cameraManager) {
    errorMessage = "CameraManager not available";
    return false;
  }

  auto status = deps.app->cameraManager->GetCameraStatus(cameraDevice);
  if (!status.connected) {
    errorMessage = "Camera not connected: " + cameraDevice;
    return false;
  }
  if (!status.grabbing) {
    errorMessage = "Camera not grabbing: " + cameraDevice;
    return false;
  }

  int numFrames = 5;
  auto nfIt = params.find("num_frames");
  if (nfIt != params.end() && !nfIt->second.empty()) {
    numFrames = safeStoi(nfIt->second, 5);
    if (numFrames < 1) numFrames = 1;
    if (numFrames > 20) numFrames = 20;
  }

  // Use subscriber to capture frames for server-side averaging
  auto subscriber = std::make_shared<LiveVideoSubscriber>(cameraDevice, "BeamDetect_Temp");
  deps.app->cameraManager->SubscribeToFrames(subscriber);
  subscriber->InitializeStatusFromManager(deps.app->cameraManager);
  constexpr int maxWaitPerFrameMs = 3000;
  constexpr int checkIntervalMs = 10;

  int width = 0;
  int height = 0;
  int channels = 0;
  size_t framePixels = 0;
  std::vector<uint8_t> allFrames;  // all frames concatenated: frame0|frame1|...
  int framesCollected = 0;

  for (int f = 0; f < numFrames; ++f) {
    // Wait for a new frame
    subscriber->MarkFrameConsumed();
    for (int i = 0; i < maxWaitPerFrameMs / checkIntervalMs; ++i) {
      if (subscriber->HasNewFrame()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
    }

    if (!subscriber->HasNewFrame()) {
      if (framesCollected == 0) {
        deps.app->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());
        errorMessage = "Timeout waiting for frame from camera: " + cameraDevice;
        return false;
      }
      break;  // use however many frames we got
    }

    CameraFrameData frameData = subscriber->GetLatestFrame();
    if (!frameData.IsValid()) continue;

    if (framesCollected == 0) {
      width = static_cast<int>(frameData.width);
      height = static_cast<int>(frameData.height);
      channels = static_cast<int>(frameData.channels);
      framePixels = static_cast<size_t>(width) * static_cast<size_t>(height);
      allFrames.reserve(framePixels * static_cast<size_t>(numFrames));
    }

    // Convert to grayscale and append this frame
    size_t frameStart = allFrames.size();
    allFrames.resize(frameStart + framePixels);
    for (size_t pixIdx = 0; pixIdx < framePixels; ++pixIdx) {
      size_t srcIdx = pixIdx * static_cast<size_t>(channels);
      uint8_t gray = 0;
      if (channels == 1) {
        gray = frameData.imageData[srcIdx];
      } else {
        int sum = 0;
        for (int c = 0; c < channels; ++c) {
          sum += frameData.imageData[srcIdx + static_cast<size_t>(c)];
        }
        gray = static_cast<uint8_t>(sum / channels);
      }
      allFrames[frameStart + pixIdx] = gray;
    }
    ++framesCollected;
  }

  deps.app->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());

  if (framesCollected == 0) {
    errorMessage = "No valid frames captured from camera: " + cameraDevice;
    return false;
  }

  if (deps.app->logManager) {
    deps.app->logManager->Info("Sending detect: " + std::to_string(width) + "x" +
                   std::to_string(height) + " z=" + std::to_string(z_um) + "um" +
                   " (" + std::to_string(framesCollected) + " frames)",
                   "CustomServer");
  }

  // Send all raw frames to beam server for server-side detection and averaging
  auto result = deps.app->beamServerClient->SendDetect(
    allFrames, width, height, z_um, rotate180, umPerPxX, umPerPxY, framesCollected);

  if (result.empty()) {
    errorMessage = "Detect failed: " + deps.app->beamServerClient->GetLastError();
    if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
    return false;
  }

  // Check for error in response
  if (result.contains("error")) {
    errorMessage = "Server error: " + result["error"].get<std::string>();
    if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
    return false;
  }

  // Store results in sequence context
  if (context) {
    // Store num_beams
    int numBeams = result.value("num_beams", 0);
    context->SetValue(captureAs + ".num_beams", std::to_string(numBeams));
    context->SetValue(captureAs + ".z_um", std::to_string(z_um));

    // Store per-beam results
    if (result.contains("beams") && result["beams"].is_array()) {
      int beamIdx = 0;
      for (const auto& beam : result["beams"]) {
        std::string prefix = captureAs + "." + std::to_string(beamIdx);

        if (beam.contains("side"))
          context->SetValue(prefix + ".side", beam["side"].get<std::string>());
        if (beam.contains("centroid_x_px"))
          context->SetValue(prefix + ".centroid_x_px", std::to_string(beam["centroid_x_px"].get<double>()));
        if (beam.contains("centroid_y_px"))
          context->SetValue(prefix + ".centroid_y_px", std::to_string(beam["centroid_y_px"].get<double>()));
        if (beam.contains("centroid_x_um"))
          context->SetValue(prefix + ".centroid_x_um", std::to_string(beam["centroid_x_um"].get<double>()));
        if (beam.contains("centroid_y_um"))
          context->SetValue(prefix + ".centroid_y_um", std::to_string(beam["centroid_y_um"].get<double>()));
        if (beam.contains("d4sigma_x_um"))
          context->SetValue(prefix + ".d4sigma_x_um", std::to_string(beam["d4sigma_x_um"].get<double>()));
        if (beam.contains("d4sigma_y_um"))
          context->SetValue(prefix + ".d4sigma_y_um", std::to_string(beam["d4sigma_y_um"].get<double>()));
        if (beam.contains("d4sigma_x_px"))
          context->SetValue(prefix + ".d4sigma_x_px", std::to_string(beam["d4sigma_x_px"].get<double>()));
        if (beam.contains("d4sigma_y_px"))
          context->SetValue(prefix + ".d4sigma_y_px", std::to_string(beam["d4sigma_y_px"].get<double>()));
        if (beam.contains("peak_intensity"))
          context->SetValue(prefix + ".peak_intensity", std::to_string(beam["peak_intensity"].get<double>()));

        ++beamIdx;
      }
    }

    if (deps.app->logManager) {
      deps.app->logManager->Info("Detect result: " + std::to_string(numBeams) + " beams at z=" +
                     std::to_string(z_um) + "um", "CustomServer");
    }
  }

  // Outbounce delay — settle time after detection completes
  int outbounceDelayMs = 500;
  auto obIt = params.find("outbounce_delay_ms");
  if (obIt != params.end() && !obIt->second.empty()) {
    outbounceDelayMs = safeStoi(obIt->second, 500);
    if (outbounceDelayMs < 0) outbounceDelayMs = 0;
  }
  if (outbounceDelayMs > 0) {
    if (deps.app->logManager) deps.app->logManager->Info("Detect: outbounce delay " + std::to_string(outbounceDelayMs) + "ms", "CustomServer");
    std::this_thread::sleep_for(std::chrono::milliseconds(outbounceDelayMs));
  }

  return true;
}
bool CustomServerDetectionCommands::HandleDetectN(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const CustomServerCommandDeps& deps)
{
  if (!deps.app->beamServerClient->IsConnected()) {
    errorMessage = "Beam server not connected";
    return false;
  }

  // Inbounce delay — settle time before starting detection
  int inbounceDelayMs = 500;
  {
    auto ibIt = params.find("inbounce_delay_ms");
    if (ibIt != params.end() && !ibIt->second.empty()) {
      inbounceDelayMs = safeStoi(ibIt->second, 500);
      if (inbounceDelayMs < 0) inbounceDelayMs = 0;
    }
  }
  if (inbounceDelayMs > 0) {
    if (deps.app->logManager) deps.app->logManager->Info("DetectN: inbounce delay " + std::to_string(inbounceDelayMs) + "ms", "CustomServer");
    std::this_thread::sleep_for(std::chrono::milliseconds(inbounceDelayMs));
  }

  // Parse repeat_count and delay_between_ms
  int repeatCount = 5;
  auto rcIt = params.find("repeat_count");
  if (rcIt != params.end() && !rcIt->second.empty()) {
    repeatCount = safeStoi(rcIt->second, 5);
    if (repeatCount < 1) repeatCount = 1;
    if (repeatCount > 100) repeatCount = 100;
  }

  int delayBetweenMs = 100;
  auto dbIt = params.find("delay_between_ms");
  if (dbIt != params.end() && !dbIt->second.empty()) {
    delayBetweenMs = safeStoi(dbIt->second, 100);
    if (delayBetweenMs < 0) delayBetweenMs = 0;
  }

  // Parse parameters (same as HandleDetect)
  auto cameraIt = params.find("camera_device");
  if (cameraIt == params.end() || cameraIt->second.empty()) {
    errorMessage = "Missing required parameter: camera_device";
    return false;
  }
  std::string cameraDevice = cameraIt->second;

  // Resolve z_um
  double z_um = 0.0;
  bool useDeviceZ = false;
  auto udzIt = params.find("use_device_z");
  if (udzIt != params.end()) {
    useDeviceZ = (udzIt->second == "true" || udzIt->second == "1");
  }

  if (useDeviceZ && deps.app->motionManager) {
    std::string zDevice = "gantry-main";
    auto zdIt = params.find("z_device");
    if (zdIt != params.end() && !zdIt->second.empty()) {
      zDevice = zdIt->second;
    }
    std::string zAxis = "Z";
    auto zaIt = params.find("z_axis");
    if (zaIt != params.end() && !zaIt->second.empty()) {
      zAxis = zaIt->second;
    }

    double positionMm = 0.0;
    if (deps.app->motionManager->GetPosition(zDevice, zAxis, positionMm)) {
      z_um = positionMm * 1000.0;
      if (deps.app->logManager) {
        deps.app->logManager->Info("DetectN: Read Z from " + zDevice + "." + zAxis + " = " +
                       std::to_string(positionMm) + " mm (" + std::to_string(z_um) + " um)",
                       "CustomServer");
      }
    } else {
      if (deps.app->logManager) {
        deps.app->logManager->Warning("DetectN: Failed to read Z from " + zDevice + "." + zAxis +
                          ", falling back to z_um parameter", "CustomServer");
      }
      useDeviceZ = false;
    }
  }

  if (!useDeviceZ) {
    auto zIt = params.find("z_um");
    if (zIt != params.end() && !zIt->second.empty()) {
      std::string zVal = zIt->second;
      if (context && zVal.substr(0, 8) == "context:") {
        std::string contextKey = zVal.substr(8);
        zVal = context->GetString(contextKey);
        if (zVal.empty() && deps.app->logManager) {
          deps.app->logManager->Warning("Context key '" + contextKey + "' not found or empty, z_um will be 0",
                            "CustomServer");
        }
      }
      z_um = safeStod(zVal);
    }
  }

  bool rotate180 = false;
  auto rotIt = params.find("rotate_180");
  if (rotIt != params.end()) {
    rotate180 = (rotIt->second == "true" || rotIt->second == "1");
  }

  double umPerPxX = 1.0;
  auto uxIt = params.find("um_per_px_x");
  if (uxIt != params.end() && !uxIt->second.empty()) {
    umPerPxX = safeStod(uxIt->second);
  }

  double umPerPxY = 1.0;
  auto uyIt = params.find("um_per_px_y");
  if (uyIt != params.end() && !uyIt->second.empty()) {
    umPerPxY = safeStod(uyIt->second);
  }

  std::string captureAs = "beam";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  int numFrames = 5;
  auto nfIt = params.find("num_frames");
  if (nfIt != params.end() && !nfIt->second.empty()) {
    numFrames = safeStoi(nfIt->second, 5);
    if (numFrames < 1) numFrames = 1;
    if (numFrames > 20) numFrames = 20;
  }

  // Verify camera
  if (!deps.app->cameraManager) {
    errorMessage = "CameraManager not available";
    return false;
  }

  auto status = deps.app->cameraManager->GetCameraStatus(cameraDevice);
  if (!status.connected) {
    errorMessage = "Camera not connected: " + cameraDevice;
    return false;
  }
  if (!status.grabbing) {
    errorMessage = "Camera not grabbing: " + cameraDevice;
    return false;
  }

  if (deps.app->logManager) {
    deps.app->logManager->Info("DetectN: Starting " + std::to_string(repeatCount) +
                   " repeated detections at z=" + std::to_string(z_um) + "um" +
                   " (" + std::to_string(numFrames) + " frames each, " +
                   std::to_string(delayBetweenMs) + "ms delay)",
                   "CustomServer");
  }

  // Bounded loop: capture and send N independent detections
  for (int rep = 0; rep < repeatCount; ++rep) {
    // Capture frames using subscriber
    auto subscriber = std::make_shared<LiveVideoSubscriber>(cameraDevice, "BeamDetectN_Temp");
    deps.app->cameraManager->SubscribeToFrames(subscriber);
    subscriber->InitializeStatusFromManager(deps.app->cameraManager);
    constexpr int maxWaitPerFrameMs = 3000;
    constexpr int checkIntervalMs = 10;

    int width = 0;
    int height = 0;
    int channels = 0;
    size_t framePixels = 0;
    std::vector<uint8_t> allFrames;
    int framesCollected = 0;

    for (int f = 0; f < numFrames; ++f) {
      subscriber->MarkFrameConsumed();
      for (int i = 0; i < maxWaitPerFrameMs / checkIntervalMs; ++i) {
        if (subscriber->HasNewFrame()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
      }

      if (!subscriber->HasNewFrame()) {
        if (framesCollected == 0) {
          deps.app->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());
          errorMessage = "DetectN rep " + std::to_string(rep) +
                         ": Timeout waiting for frame from camera: " + cameraDevice;
          return false;
        }
        break;
      }

      CameraFrameData frameData = subscriber->GetLatestFrame();
      if (!frameData.IsValid()) continue;

      if (framesCollected == 0) {
        width = static_cast<int>(frameData.width);
        height = static_cast<int>(frameData.height);
        channels = static_cast<int>(frameData.channels);
        framePixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        allFrames.reserve(framePixels * static_cast<size_t>(numFrames));
      }

      size_t frameStart = allFrames.size();
      allFrames.resize(frameStart + framePixels);
      for (size_t pixIdx = 0; pixIdx < framePixels; ++pixIdx) {
        size_t srcIdx = pixIdx * static_cast<size_t>(channels);
        uint8_t gray = 0;
        if (channels == 1) {
          gray = frameData.imageData[srcIdx];
        } else {
          int sum = 0;
          for (int c = 0; c < channels; ++c) {
            sum += frameData.imageData[srcIdx + static_cast<size_t>(c)];
          }
          gray = static_cast<uint8_t>(sum / channels);
        }
        allFrames[frameStart + pixIdx] = gray;
      }
      ++framesCollected;
    }

    deps.app->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());

    if (framesCollected == 0) {
      errorMessage = "DetectN rep " + std::to_string(rep) +
                     ": No valid frames captured from camera: " + cameraDevice;
      return false;
    }

    // Send detect to server
    if (deps.app->logManager) {
      deps.app->logManager->Info("DetectN rep " + std::to_string(rep + 1) + "/" +
                     std::to_string(repeatCount) + ": Sending " +
                     std::to_string(framesCollected) + " frames",
                     "CustomServer");
    }

    auto result = deps.app->beamServerClient->SendDetect(
      allFrames, width, height, z_um, rotate180, umPerPxX, umPerPxY, framesCollected);

    if (result.empty()) {
      errorMessage = "DetectN rep " + std::to_string(rep) +
                     ": Detect failed: " + deps.app->beamServerClient->GetLastError();
      if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
      return false;
    }

    if (result.contains("error")) {
      errorMessage = "DetectN rep " + std::to_string(rep) +
                     ": Server error: " + result["error"].get<std::string>();
      if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
      return false;
    }

    // Store last iteration's results in context (same format as HandleDetect)
    if (context && rep == repeatCount - 1) {
      int numBeams = result.value("num_beams", 0);
      context->SetValue(captureAs + ".num_beams", std::to_string(numBeams));
      context->SetValue(captureAs + ".z_um", std::to_string(z_um));
      context->SetValue(captureAs + ".repeat_count", std::to_string(repeatCount));

      if (result.contains("beams") && result["beams"].is_array()) {
        int beamIdx = 0;
        for (const auto& beam : result["beams"]) {
          std::string prefix = captureAs + "." + std::to_string(beamIdx);

          if (beam.contains("side"))
            context->SetValue(prefix + ".side", beam["side"].get<std::string>());
          if (beam.contains("centroid_x_px"))
            context->SetValue(prefix + ".centroid_x_px", std::to_string(beam["centroid_x_px"].get<double>()));
          if (beam.contains("centroid_y_px"))
            context->SetValue(prefix + ".centroid_y_px", std::to_string(beam["centroid_y_px"].get<double>()));
          if (beam.contains("centroid_x_um"))
            context->SetValue(prefix + ".centroid_x_um", std::to_string(beam["centroid_x_um"].get<double>()));
          if (beam.contains("centroid_y_um"))
            context->SetValue(prefix + ".centroid_y_um", std::to_string(beam["centroid_y_um"].get<double>()));
          if (beam.contains("d4sigma_x_um"))
            context->SetValue(prefix + ".d4sigma_x_um", std::to_string(beam["d4sigma_x_um"].get<double>()));
          if (beam.contains("d4sigma_y_um"))
            context->SetValue(prefix + ".d4sigma_y_um", std::to_string(beam["d4sigma_y_um"].get<double>()));
          if (beam.contains("d4sigma_x_px"))
            context->SetValue(prefix + ".d4sigma_x_px", std::to_string(beam["d4sigma_x_px"].get<double>()));
          if (beam.contains("d4sigma_y_px"))
            context->SetValue(prefix + ".d4sigma_y_px", std::to_string(beam["d4sigma_y_px"].get<double>()));
          if (beam.contains("peak_intensity"))
            context->SetValue(prefix + ".peak_intensity", std::to_string(beam["peak_intensity"].get<double>()));

          ++beamIdx;
        }
      }
    }

    // Delay between iterations (skip after last)
    if (rep < repeatCount - 1 && delayBetweenMs > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delayBetweenMs));
    }
  }

  if (deps.app->logManager) {
    deps.app->logManager->Info("DetectN: Completed " + std::to_string(repeatCount) +
                   " detections at z=" + std::to_string(z_um) + "um",
                   "CustomServer");
  }

  // Outbounce delay — settle time after all detections complete
  int outbounceDelayMs = 500;
  {
    auto obIt = params.find("outbounce_delay_ms");
    if (obIt != params.end() && !obIt->second.empty()) {
      outbounceDelayMs = safeStoi(obIt->second, 500);
      if (outbounceDelayMs < 0) outbounceDelayMs = 0;
    }
  }
  if (outbounceDelayMs > 0) {
    if (deps.app->logManager) deps.app->logManager->Info("DetectN: outbounce delay " + std::to_string(outbounceDelayMs) + "ms", "CustomServer");
    std::this_thread::sleep_for(std::chrono::milliseconds(outbounceDelayMs));
  }

  return true;
}
bool CustomServerDetectionCommands::HandleDetectFile(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const CustomServerCommandDeps& deps)
{
  if (!deps.app->beamServerClient->IsConnected()) {
    errorMessage = "Beam server not connected";
    return false;
  }

  // file_path (required)
  auto fpIt = params.find("file_path");
  if (fpIt == params.end() || fpIt->second.empty()) {
    errorMessage = "Missing required parameter: file_path";
    return false;
  }
  std::string filePath = fpIt->second;
  // Support context: references
  if (context && filePath.substr(0, 8) == "context:") {
    filePath = context->GetString(filePath.substr(8));
  }

  double z_um = 0.0;
  auto zIt = params.find("z_um");
  if (zIt != params.end() && !zIt->second.empty()) {
    std::string zVal = zIt->second;
    if (context && zVal.substr(0, 8) == "context:") {
      std::string contextKey = zVal.substr(8);
      zVal = context->GetString(contextKey);
      if (zVal.empty() && deps.app->logManager) {
        deps.app->logManager->Warning("Context key '" + contextKey + "' not found or empty, z_um will be 0",
                          "CustomServer");
      }
    }
    z_um = safeStod(zVal);
  }

  std::optional<bool> rotate180 = std::nullopt;
  auto rotIt = params.find("rotate_180");
  if (rotIt != params.end()) {
    rotate180 = (rotIt->second == "true" || rotIt->second == "1");
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

  std::string captureAs = "beam";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  if (deps.app->logManager) {
    deps.app->logManager->Info("Sending detect file: " + filePath + " z=" + std::to_string(z_um) + "um",
                   "CustomServer");
  }

  auto result = deps.app->beamServerClient->SendDetectFile(
    filePath, z_um, rotate180, umPerPxX, umPerPxY);

  if (result.empty()) {
    errorMessage = "DetectFile failed: " + deps.app->beamServerClient->GetLastError();
    if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
    return false;
  }

  if (result.contains("error")) {
    errorMessage = "Server error: " + result["error"].get<std::string>();
    if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
    return false;
  }

  // Store results in sequence context (same format as Detect)
  if (context) {
    int numBeams = result.value("num_beams", 0);
    context->SetValue(captureAs + ".num_beams", std::to_string(numBeams));
    context->SetValue(captureAs + ".z_um", std::to_string(z_um));

    if (result.contains("beams") && result["beams"].is_array()) {
      int beamIdx = 0;
      for (const auto& beam : result["beams"]) {
        std::string prefix = captureAs + "." + std::to_string(beamIdx);

        if (beam.contains("side"))
          context->SetValue(prefix + ".side", beam["side"].get<std::string>());
        if (beam.contains("centroid_x_px"))
          context->SetValue(prefix + ".centroid_x_px", std::to_string(beam["centroid_x_px"].get<double>()));
        if (beam.contains("centroid_y_px"))
          context->SetValue(prefix + ".centroid_y_px", std::to_string(beam["centroid_y_px"].get<double>()));
        if (beam.contains("centroid_x_um"))
          context->SetValue(prefix + ".centroid_x_um", std::to_string(beam["centroid_x_um"].get<double>()));
        if (beam.contains("centroid_y_um"))
          context->SetValue(prefix + ".centroid_y_um", std::to_string(beam["centroid_y_um"].get<double>()));
        if (beam.contains("d4sigma_x_um"))
          context->SetValue(prefix + ".d4sigma_x_um", std::to_string(beam["d4sigma_x_um"].get<double>()));
        if (beam.contains("d4sigma_y_um"))
          context->SetValue(prefix + ".d4sigma_y_um", std::to_string(beam["d4sigma_y_um"].get<double>()));
        if (beam.contains("d4sigma_x_px"))
          context->SetValue(prefix + ".d4sigma_x_px", std::to_string(beam["d4sigma_x_px"].get<double>()));
        if (beam.contains("d4sigma_y_px"))
          context->SetValue(prefix + ".d4sigma_y_px", std::to_string(beam["d4sigma_y_px"].get<double>()));
        if (beam.contains("peak_intensity"))
          context->SetValue(prefix + ".peak_intensity", std::to_string(beam["peak_intensity"].get<double>()));

        ++beamIdx;
      }
    }

    if (deps.app->logManager) {
      deps.app->logManager->Info("DetectFile result: " + std::to_string(result.value("num_beams", 0)) +
                     " beams from " + filePath, "CustomServer");
    }
  }

  return true;
}
bool CustomServerDetectionCommands::HandleDetectFolder(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const CustomServerCommandDeps& deps)
{
  if (!deps.app->beamServerClient->IsConnected()) {
    errorMessage = "Beam server not connected";
    return false;
  }

  // folder_path (required, supports context:)
  auto fpIt = params.find("folder_path");
  if (fpIt == params.end() || fpIt->second.empty()) {
    errorMessage = "Missing required parameter: folder_path";
    return false;
  }
  std::string folderPath = fpIt->second;
  if (context && folderPath.substr(0, 8) == "context:") {
    folderPath = context->GetString(folderPath.substr(8));
  }

  // filename_filter — substring filter to include only matching files (e.g., "ch0")
  std::string filenameFilter;
  auto ffIt = params.find("filename_filter");
  if (ffIt != params.end()) {
    filenameFilter = ffIt->second;
  }

  // z_regex — regex to extract z-height from filename, group 1 = value in um
  std::string zRegex = R"(zheight([\d.]+)um)";
  auto zrIt = params.find("z_regex");
  if (zrIt != params.end() && !zrIt->second.empty()) {
    zRegex = zrIt->second;
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

  int delayMs = 150;
  auto delIt = params.find("delay_ms");
  if (delIt != params.end() && !delIt->second.empty()) {
    delayMs = safeStoi(delIt->second);
  }

  std::string captureAs = "beam";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  // Verify folder exists
  namespace fs = std::filesystem;
  if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
    errorMessage = "Folder does not exist: " + folderPath;
    return false;
  }

  // Collect image files (.png, .bmp, .tif, .tiff, .jpg, .jpeg)
  struct ImageEntry {
    std::string path;
    double z_um;
  };
  std::vector<ImageEntry> entries;
  std::regex re(zRegex, std::regex::icase);

  for (const auto& entry : fs::directory_iterator(folderPath)) {
    if (!entry.is_regular_file()) continue;

    std::string ext = entry.path().extension().string();
    // Convert to lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext != ".png" && ext != ".bmp" && ext != ".tif" &&
        ext != ".tiff" && ext != ".jpg" && ext != ".jpeg") {
      continue;
    }

    std::string filename = entry.path().filename().string();

    // Apply filename filter (substring match, case-insensitive)
    if (!filenameFilter.empty()) {
      std::string filenameLower = filename;
      std::string filterLower = filenameFilter;
      std::transform(filenameLower.begin(), filenameLower.end(), filenameLower.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (filenameLower.find(filterLower) == std::string::npos) {
        continue;  // Skip files that don't match the filter
      }
    }

    std::smatch match;
    double z_um = 0.0;
    if (std::regex_search(filename, match, re) && match.size() > 1) {
      z_um = safeStod(match[1].str());
    }

    entries.push_back({entry.path().string(), z_um});
  }

  if (entries.empty()) {
    errorMessage = "No image files found matching filter in: " + folderPath;
    return false;
  }

  // Sort by z_um ascending
  std::sort(entries.begin(), entries.end(),
            [](const ImageEntry& a, const ImageEntry& b) { return a.z_um < b.z_um; });

  if (deps.app->logManager) {
    deps.app->logManager->Info("DetectFolder: " + std::to_string(entries.size()) +
                   " images in " + folderPath, "CustomServer");
  }

  // Send each file as DetectFile with delay between
  int successCount = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& img = entries[i];

    if (deps.app->logManager) {
      deps.app->logManager->Info("DetectFolder [" + std::to_string(i + 1) + "/" +
                     std::to_string(entries.size()) + "]: " +
                     fs::path(img.path).filename().string() +
                     " z=" + std::to_string(img.z_um) + "um", "CustomServer");
    }

    // rotate_180 not set — let server use its config default
    auto result = deps.app->beamServerClient->SendDetectFile(
      img.path, img.z_um, std::nullopt, umPerPxX, umPerPxY);

    if (result.empty()) {
      errorMessage = "DetectFile failed for " + img.path + ": " +
                     deps.app->beamServerClient->GetLastError();
      if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
      return false;
    }

    if (result.contains("error")) {
      errorMessage = "Server error for " + img.path + ": " +
                     result["error"].get<std::string>();
      if (deps.app->logManager) deps.app->logManager->Error(errorMessage, "CustomServer");
      return false;
    }

    ++successCount;

    // Store last detection result in context
    if (context) {
      int numBeams = result.value("num_beams", 0);
      context->SetValue(captureAs + ".num_beams", std::to_string(numBeams));
      context->SetValue(captureAs + ".z_um", std::to_string(img.z_um));
    }

    // Delay between images (skip after last)
    if (i + 1 < entries.size() && delayMs > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
  }

  if (deps.app->logManager) {
    deps.app->logManager->Info("DetectFolder complete: " + std::to_string(successCount) +
                   " images sent", "CustomServer");
  }

  // Store total count in context
  if (context) {
    context->SetValue(captureAs + ".folder_images_sent", std::to_string(successCount));
  }

  return true;
}

std::optional<bool> CustomServerDetectionCommands::TryExecute(
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const CustomServerCommandDeps& deps)
{
  if (methodName == "Detect")       return HandleDetect(params, errorMessage, context, deps);
  if (methodName == "DetectN")      return HandleDetectN(params, errorMessage, context, deps);
  if (methodName == "DetectFile")   return HandleDetectFile(params, errorMessage, context, deps);
  if (methodName == "DetectFolder") return HandleDetectFolder(params, errorMessage, context, deps);
  return std::nullopt;
}

// CustomServerCommandHandler.cpp
// Routes Connect/Disconnect/Detect/Fit/Reset commands to BeamServerClient.
#include "CustomServerCommandHandler.h"
#include "customserver/BeamServerClient.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "CameraFrameData.h"
#include "SequenceContext.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "LevenbergMarquardt.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <optional>
#include <filesystem>
#include <regex>
#include <algorithm>
#include <cmath>
#include <fstream>

#ifdef HAS_OPENCV
#include "../vision/VisionManager.h"
#include "../vision/VisionResult.h"
#include "../vision/VisionROI.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>
#endif

bool CustomServerCommandHandler::ExecuteCommand(
  void* /*manager*/,
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (!m_context) {
    errorMessage = "ApplicationContext not available";
    return false;
  }

  // Native handlers (no TCP server needed)
  if (methodName == "FitSlices")           return HandleFitSlices(params, errorMessage, context);
  if (methodName == "D4SigmaZScanFolder") return HandleD4SigmaZScanFolder(params, errorMessage, context);

  // TCP-based handlers require beam server client
  if (!m_context->beamServerClient) {
    errorMessage = "BeamServerClient not available";
    return false;
  }

  if (methodName == "Connect")      return HandleConnect(params, errorMessage);
  if (methodName == "Disconnect")   return HandleDisconnect(errorMessage);
  if (methodName == "Detect")       return HandleDetect(params, errorMessage, context);
  if (methodName == "DetectFile")   return HandleDetectFile(params, errorMessage, context);
  if (methodName == "DetectFolder") return HandleDetectFolder(params, errorMessage, context);
  if (methodName == "Fit")          return HandleFit(params, errorMessage, context);
  if (methodName == "Save")         return HandleSave(params, errorMessage, context);
  if (methodName == "Reset")        return HandleReset(errorMessage);

  errorMessage = "Unknown method: " + methodName;
  return false;
}

bool CustomServerCommandHandler::HandleConnect(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage)
{
  std::string host = "127.0.0.1";
  int port = 5555;

  auto hostIt = params.find("host");
  if (hostIt != params.end() && !hostIt->second.empty()) {
    host = hostIt->second;
  }

  auto portIt = params.find("port");
  if (portIt != params.end() && !portIt->second.empty()) {
    port = std::stoi(portIt->second);
  }

  if (m_logger) {
    m_logger->Info("Connecting to beam server at " + host + ":" + std::to_string(port),
                   "CustomServer");
  }

  if (!m_context->beamServerClient->Connect(host, port)) {
    errorMessage = "Failed to connect to beam server: " +
                   m_context->beamServerClient->GetLastError();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
    return false;
  }

  if (m_logger) m_logger->Info("Connected to beam server", "CustomServer");
  return true;
}

bool CustomServerCommandHandler::HandleDisconnect(std::string& /*errorMessage*/)
{
  m_context->beamServerClient->Disconnect();
  if (m_logger) m_logger->Info("Disconnected from beam server", "CustomServer");
  return true;
}

bool CustomServerCommandHandler::HandleDetect(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (!m_context->beamServerClient->IsConnected()) {
    errorMessage = "Beam server not connected";
    return false;
  }

  // Parse parameters
  auto cameraIt = params.find("camera_device");
  if (cameraIt == params.end() || cameraIt->second.empty()) {
    errorMessage = "Missing required parameter: camera_device";
    return false;
  }
  std::string cameraDevice = cameraIt->second;

  double z_um = 0.0;
  auto zIt = params.find("z_um");
  if (zIt != params.end() && !zIt->second.empty()) {
    // Support context: references
    std::string zVal = zIt->second;
    if (context && zVal.substr(0, 8) == "context:") {
      zVal = context->GetString(zVal.substr(8));
    }
    z_um = std::stod(zVal);
  }

  bool rotate180 = false;
  auto rotIt = params.find("rotate_180");
  if (rotIt != params.end()) {
    rotate180 = (rotIt->second == "true" || rotIt->second == "1");
  }

  double umPerPxX = 1.0;
  auto uxIt = params.find("um_per_px_x");
  if (uxIt != params.end() && !uxIt->second.empty()) {
    umPerPxX = std::stod(uxIt->second);
  }

  double umPerPxY = 1.0;
  auto uyIt = params.find("um_per_px_y");
  if (uyIt != params.end() && !uyIt->second.empty()) {
    umPerPxY = std::stod(uyIt->second);
  }

  std::string captureAs = "beam";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  // Grab frame from camera
  if (!m_context->cameraManager) {
    errorMessage = "CameraManager not available";
    return false;
  }

  auto status = m_context->cameraManager->GetCameraStatus(cameraDevice);
  if (!status.connected) {
    errorMessage = "Camera not connected: " + cameraDevice;
    return false;
  }
  if (!status.grabbing) {
    errorMessage = "Camera not grabbing: " + cameraDevice;
    return false;
  }

  // Use subscriber to get latest frame
  auto subscriber = std::make_shared<LiveVideoSubscriber>(cameraDevice, "BeamDetect_Temp");
  m_context->cameraManager->SubscribeToFrames(subscriber);
  subscriber->InitializeStatusFromManager(m_context->cameraManager);

  // Wait for frame (max 3 seconds)
  const int maxWaitMs = 3000;
  const int checkIntervalMs = 100;
  int waitedMs = 0;
  for (int i = 0; i < maxWaitMs / checkIntervalMs; ++i) {
    if (subscriber->HasNewFrame()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
    waitedMs += checkIntervalMs;
  }

  if (!subscriber->HasNewFrame()) {
    m_context->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());
    errorMessage = "Timeout waiting for frame from camera: " + cameraDevice;
    return false;
  }

  CameraFrameData frameData = subscriber->GetLatestFrame();
  m_context->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());

  if (!frameData.IsValid()) {
    errorMessage = "Invalid frame data from camera: " + cameraDevice;
    return false;
  }

  // Convert 8-bit image to uint16 (multiply by 256)
  // If multi-channel, convert to grayscale by averaging
  int width = static_cast<int>(frameData.width);
  int height = static_cast<int>(frameData.height);
  int channels = static_cast<int>(frameData.channels);

  std::vector<uint16_t> imageU16(static_cast<size_t>(width) * static_cast<size_t>(height));

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      size_t pixIdx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
      size_t srcIdx = pixIdx * static_cast<size_t>(channels);

      uint8_t gray = 0;
      if (channels == 1) {
        gray = frameData.imageData[srcIdx];
      } else {
        // Average RGB channels for grayscale
        int sum = 0;
        for (int c = 0; c < channels; ++c) {
          sum += frameData.imageData[srcIdx + static_cast<size_t>(c)];
        }
        gray = static_cast<uint8_t>(sum / channels);
      }

      imageU16[pixIdx] = static_cast<uint16_t>(gray) * 256;
    }
  }

  if (m_logger) {
    m_logger->Info("Sending detect: " + std::to_string(width) + "x" +
                   std::to_string(height) + " z=" + std::to_string(z_um) + "um",
                   "CustomServer");
  }

  // Send to beam server
  auto result = m_context->beamServerClient->SendDetect(
    imageU16, width, height, z_um, rotate180, umPerPxX, umPerPxY);

  if (result.empty()) {
    errorMessage = "Detect failed: " + m_context->beamServerClient->GetLastError();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
    return false;
  }

  // Check for error in response
  if (result.contains("error")) {
    errorMessage = "Server error: " + result["error"].get<std::string>();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
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

        ++beamIdx;
      }
    }

    if (m_logger) {
      m_logger->Info("Detect result: " + std::to_string(numBeams) + " beams at z=" +
                     std::to_string(z_um) + "um", "CustomServer");
    }
  }

  return true;
}

bool CustomServerCommandHandler::HandleDetectFile(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (!m_context->beamServerClient->IsConnected()) {
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
      zVal = context->GetString(zVal.substr(8));
    }
    z_um = std::stod(zVal);
  }

  std::optional<bool> rotate180 = std::nullopt;
  auto rotIt = params.find("rotate_180");
  if (rotIt != params.end()) {
    rotate180 = (rotIt->second == "true" || rotIt->second == "1");
  }

  double umPerPxX = 0.25;
  auto uxIt = params.find("um_per_px_x");
  if (uxIt != params.end() && !uxIt->second.empty()) {
    umPerPxX = std::stod(uxIt->second);
  }

  double umPerPxY = 0.25;
  auto uyIt = params.find("um_per_px_y");
  if (uyIt != params.end() && !uyIt->second.empty()) {
    umPerPxY = std::stod(uyIt->second);
  }

  std::string captureAs = "beam";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  if (m_logger) {
    m_logger->Info("Sending detect file: " + filePath + " z=" + std::to_string(z_um) + "um",
                   "CustomServer");
  }

  auto result = m_context->beamServerClient->SendDetectFile(
    filePath, z_um, rotate180, umPerPxX, umPerPxY);

  if (result.empty()) {
    errorMessage = "DetectFile failed: " + m_context->beamServerClient->GetLastError();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
    return false;
  }

  if (result.contains("error")) {
    errorMessage = "Server error: " + result["error"].get<std::string>();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
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

        ++beamIdx;
      }
    }

    if (m_logger) {
      m_logger->Info("DetectFile result: " + std::to_string(result.value("num_beams", 0)) +
                     " beams from " + filePath, "CustomServer");
    }
  }

  return true;
}

bool CustomServerCommandHandler::HandleFit(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (!m_context->beamServerClient->IsConnected()) {
    errorMessage = "Beam server not connected";
    return false;
  }

  double umPerPxX = 0.25;
  auto uxIt = params.find("um_per_px_x");
  if (uxIt != params.end() && !uxIt->second.empty()) {
    umPerPxX = std::stod(uxIt->second);
  }

  double umPerPxY = 0.25;
  auto uyIt = params.find("um_per_px_y");
  if (uyIt != params.end() && !uyIt->second.empty()) {
    umPerPxY = std::stod(uyIt->second);
  }

  double wavelengthM = 1550e-9;  // default 1550nm
  auto wlIt = params.find("wavelength_m");
  if (wlIt != params.end() && !wlIt->second.empty()) {
    wavelengthM = std::stod(wlIt->second);
  }

  std::string captureAs = "beam";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  if (m_logger) {
    m_logger->Info("Sending fit request (wavelength=" +
                   std::to_string(wavelengthM) + "m)", "CustomServer");
  }

  auto result = m_context->beamServerClient->SendFit(umPerPxX, umPerPxY, wavelengthM);

  if (result.empty()) {
    errorMessage = "Fit failed: " + m_context->beamServerClient->GetLastError();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
    return false;
  }

  if (result.contains("error")) {
    errorMessage = "Server error: " + result["error"].get<std::string>();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
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

    if (m_logger) {
      m_logger->Info("Fit result: " + std::to_string(numBeams) + " beams, " +
                     std::to_string(numZSteps) + " z-steps", "CustomServer");
    }
  }

  return true;
}

bool CustomServerCommandHandler::HandleReset(std::string& errorMessage)
{
  // No-op if not connected — allows Reset before Connect in recipes
  if (!m_context->beamServerClient->IsConnected()) {
    if (m_logger) m_logger->Info("Beam server not connected, skipping reset", "CustomServer");
    return true;
  }

  if (!m_context->beamServerClient->SendReset()) {
    errorMessage = "Reset failed: " + m_context->beamServerClient->GetLastError();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
    return false;
  }

  if (m_logger) m_logger->Info("Beam server reset", "CustomServer");
  return true;
}

bool CustomServerCommandHandler::HandleSave(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (!m_context->beamServerClient->IsConnected()) {
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

  if (m_logger) {
    m_logger->Info("Sending save request" +
                   (name.empty() ? "" : " (name=" + name + ")"), "CustomServer");
  }

  auto result = m_context->beamServerClient->SendSave(name);

  if (result.empty()) {
    errorMessage = "Save failed: " + m_context->beamServerClient->GetLastError();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
    return false;
  }

  if (result.contains("error")) {
    errorMessage = "Server error: " + result["error"].get<std::string>();
    if (m_logger) m_logger->Error(errorMessage, "CustomServer");
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

  if (m_logger) {
    std::string saveDir = result.value("save_dir", "unknown");
    int numImages = result.value("num_images", 0);
    m_logger->Info("Save complete: " + std::to_string(numImages) +
                   " images saved to " + saveDir, "CustomServer");
  }

  return true;
}

bool CustomServerCommandHandler::HandleDetectFolder(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (!m_context->beamServerClient->IsConnected()) {
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
    umPerPxX = std::stod(uxIt->second);
  }

  double umPerPxY = 0.25;
  auto uyIt = params.find("um_per_px_y");
  if (uyIt != params.end() && !uyIt->second.empty()) {
    umPerPxY = std::stod(uyIt->second);
  }

  int delayMs = 150;
  auto delIt = params.find("delay_ms");
  if (delIt != params.end() && !delIt->second.empty()) {
    delayMs = std::stoi(delIt->second);
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
      z_um = std::stod(match[1].str());
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

  if (m_logger) {
    m_logger->Info("DetectFolder: " + std::to_string(entries.size()) +
                   " images in " + folderPath, "CustomServer");
  }

  // Send each file as DetectFile with delay between
  int successCount = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& img = entries[i];

    if (m_logger) {
      m_logger->Info("DetectFolder [" + std::to_string(i + 1) + "/" +
                     std::to_string(entries.size()) + "]: " +
                     fs::path(img.path).filename().string() +
                     " z=" + std::to_string(img.z_um) + "um", "CustomServer");
    }

    // rotate_180 not set — let server use its config default
    auto result = m_context->beamServerClient->SendDetectFile(
      img.path, img.z_um, std::nullopt, umPerPxX, umPerPxY);

    if (result.empty()) {
      errorMessage = "DetectFile failed for " + img.path + ": " +
                     m_context->beamServerClient->GetLastError();
      if (m_logger) m_logger->Error(errorMessage, "CustomServer");
      return false;
    }

    if (result.contains("error")) {
      errorMessage = "Server error for " + img.path + ": " +
                     result["error"].get<std::string>();
      if (m_logger) m_logger->Error(errorMessage, "CustomServer");
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

  if (m_logger) {
    m_logger->Info("DetectFolder complete: " + std::to_string(successCount) +
                   " images sent", "CustomServer");
  }

  // Store total count in context
  if (context) {
    context->SetValue(captureAs + ".folder_images_sent", std::to_string(successCount));
  }

  return true;
}

//=============================================================================
// HandleFitSlices - Native Gaussian M^2 beam fit from context data
//=============================================================================

bool CustomServerCommandHandler::HandleFitSlices(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (!context) {
    errorMessage = "FitSlices requires a sequence context";
    return false;
  }

  // Parse parameters
  std::string prefix = "splice";
  auto prefIt = params.find("capture_as_prefix");
  if (prefIt != params.end() && !prefIt->second.empty()) {
    prefix = prefIt->second;
  }

  // num_slices: 0 or "auto" means auto-detect from context
  int numSlicesParam = 0;
  auto nsIt = params.find("num_slices");
  if (nsIt != params.end() && !nsIt->second.empty() && nsIt->second != "auto") {
    numSlicesParam = std::stoi(nsIt->second);
  }

  double wavelengthM = 1550e-9;
  auto wlIt = params.find("wavelength_m");
  if (wlIt != params.end() && !wlIt->second.empty()) {
    wavelengthM = std::stod(wlIt->second);
  }
  double wavelengthUm = wavelengthM * 1e6;  // Convert to um for calculations

  double umPerPxX = 0.25;
  auto uxIt = params.find("um_per_px_x");
  if (uxIt != params.end() && !uxIt->second.empty()) {
    umPerPxX = std::stod(uxIt->second);
  }

  double umPerPxY = 0.25;
  auto uyIt = params.find("um_per_px_y");
  if (uyIt != params.end() && !uyIt->second.empty()) {
    umPerPxY = std::stod(uyIt->second);
  }

  std::string captureAs = "beam_fit";
  auto capIt = params.find("captureAs");
  if (capIt != params.end() && !capIt->second.empty()) {
    captureAs = capIt->second;
  }

  // Auto-detect number of slices from context if not specified
  int numSlices = numSlicesParam;
  if (numSlices <= 0) {
    for (int i = 1; i <= 200; ++i) {
      std::string sliceKey = prefix + std::to_string(i);
      if (context->HasValue(sliceKey + ".z_um") ||
          context->HasValue(sliceKey + ".detected.device_z_mm")) {
        numSlices = i;
      } else {
        break;
      }
    }
  }

  if (numSlices <= 0) {
    errorMessage = "FitSlices: No slice data found with prefix '" + prefix + "'";
    return false;
  }

  if (m_logger) {
    m_logger->Info("FitSlices: prefix=" + prefix + ", slices=" + std::to_string(numSlices) +
                   (numSlicesParam <= 0 ? " (auto-detected)" : "") +
                   ", wavelength=" + std::to_string(wavelengthUm) + "um", "CustomServer");
  }

  // Read z-heights from context for each slice
  std::vector<double> z_um_all;
  for (int i = 1; i <= numSlices; ++i) {
    std::string sliceKey = prefix + std::to_string(i);
    double z = 0.0;
    bool found = false;

    // Try from-folder mode first: prefix1.z_um
    if (context->HasValue(sliceKey + ".z_um")) {
      z = static_cast<double>(context->GetNumeric(sliceKey + ".z_um", 0.0f));
      found = true;
    }
    // Try live mode: prefix1.detected.device_z_mm (convert mm -> um)
    else if (context->HasValue(sliceKey + ".detected.device_z_mm")) {
      z = static_cast<double>(context->GetNumeric(sliceKey + ".detected.device_z_mm", 0.0f)) * 1000.0;
      found = true;
    }

    if (!found) {
      if (m_logger) {
        m_logger->Warning("FitSlices: No z-height for slice " + sliceKey + ", stopping at " +
                         std::to_string(i - 1) + " slices", "CustomServer");
      }
      break;
    }
    z_um_all.push_back(z);
  }

  numSlices = static_cast<int>(z_um_all.size());
  if (numSlices < 3) {
    errorMessage = "FitSlices: Need at least 3 slices, found " + std::to_string(numSlices);
    return false;
  }

  // Determine beam count from first slice
  int numBeams = 0;
  std::string firstSlice = prefix + "1";
  // Try beam_count measurement (from D4Sigma plugin)
  if (context->HasValue(firstSlice + ".result.beam_count")) {
    numBeams = static_cast<int>(context->GetNumeric(firstSlice + ".result.beam_count", 0.0f));
  }
  // Try vision.beam.count style (from StoreResultInContext)
  else if (context->HasValue(firstSlice + ".beam.count")) {
    numBeams = static_cast<int>(context->GetNumeric(firstSlice + ".beam.count", 0.0f));
  }
  // Probe incrementally
  else {
    for (int j = 0; j < 20; ++j) {
      std::string testKey = firstSlice + ".result.beam_" + std::to_string(j) + "_d4sigma_x_px";
      if (!context->HasValue(testKey)) break;
      numBeams = j + 1;
    }
  }

  if (numBeams == 0) {
    errorMessage = "FitSlices: No beam data found in context for " + firstSlice;
    return false;
  }

  if (m_logger) {
    m_logger->Info("FitSlices: Found " + std::to_string(numBeams) + " beams, " +
                   std::to_string(numSlices) + " z-steps", "CustomServer");
  }

  // Gaussian M^2 beam model:
  // d(z) = 2*w0 * sqrt(1 + ((z - z0) * M2 * lambda / (pi * w0^2))^2)
  // Parameters: [z0, w0, M2]
  // w0 and M2 are clamped to positive values during fitting
  auto beamModel = [wavelengthUm](double z, const std::vector<double>& p) -> double {
    double z0 = p[0];
    double w0 = std::max(p[1], 0.1);   // Clamp w0 > 0.1 um
    double M2 = std::max(p[2], 0.3);   // Clamp M2 > 0.3
    double zR = M_PI * w0 * w0 / (M2 * wavelengthUm);
    double ratio = (z - z0) / zR;
    return 2.0 * w0 * std::sqrt(1.0 + ratio * ratio);
  };

  // Collect beam centroid locations for TX→RX vector computation
  std::vector<std::pair<double, double>> beamLocations;

  // For each beam, collect data across all slices and fit
  for (int b = 0; b < numBeams; ++b) {
    std::string beamStr = std::to_string(b);
    std::vector<double> z_um;
    std::vector<double> d4x_um, d4y_um;
    std::vector<double> cx_um, cy_um;

    for (int i = 1; i <= numSlices; ++i) {
      std::string sliceKey = prefix + std::to_string(i);
      std::string mBase = sliceKey + ".result.beam_" + beamStr + "_";

      // D4Sigma values (in pixels)
      float d4x_px = context->GetNumeric(mBase + "d4sigma_x_px", -1.0f);
      float d4y_px = context->GetNumeric(mBase + "d4sigma_y_px", -1.0f);

      if (d4x_px <= 0.0f || d4y_px <= 0.0f) {
        // Skip slices with missing or zero data
        if (m_logger) {
          m_logger->Warning("FitSlices: Beam " + beamStr + " slice " + std::to_string(i) +
                           " missing d4sigma data, skipping", "CustomServer");
        }
        continue;
      }

      z_um.push_back(z_um_all[static_cast<size_t>(i - 1)]);
      d4x_um.push_back(static_cast<double>(d4x_px) * umPerPxX);
      d4y_um.push_back(static_cast<double>(d4y_px) * umPerPxY);

      // Centroid values (in pixels)
      float cx_px = context->GetNumeric(mBase + "centroid_x_px", 0.0f);
      float cy_px = context->GetNumeric(mBase + "centroid_y_px", 0.0f);
      cx_um.push_back(static_cast<double>(cx_px) * umPerPxX);
      cy_um.push_back(static_cast<double>(cy_px) * umPerPxY);
    }

    if (z_um.size() < 3) {
      if (m_logger) {
        m_logger->Warning("FitSlices: Beam " + beamStr + " has only " +
                         std::to_string(z_um.size()) + " valid slices (need >= 3), skipping",
                         "CustomServer");
      }
      continue;
    }

    // Log raw data for debugging
    if (m_logger) {
      for (size_t di = 0; di < z_um.size(); ++di) {
        m_logger->Info("FitSlices beam " + beamStr + " z=" +
          std::to_string(z_um[di]) + " d4x=" + std::to_string(d4x_um[di]) +
          " d4y=" + std::to_string(d4y_um[di]) + " um", "CustomServer");
      }
    }

    // Smart initial guesses: find z of minimum d4sigma (closest to waist)
    auto computeInitialGuess = [](const std::vector<double>& z,
                                   const std::vector<double>& d_um) {
      // Find minimum d4sigma and its z position
      size_t minIdx = 0;
      double minD = d_um[0];
      for (size_t i = 1; i < d_um.size(); ++i) {
        if (d_um[i] < minD) {
          minD = d_um[i];
          minIdx = i;
        }
      }
      double z0 = z[minIdx];
      double w0 = minD / 2.0;  // d4sigma = 2*w0 at waist
      if (w0 < 0.5) w0 = 0.5;  // Minimum reasonable waist
      double M2 = 1.0;  // Start at diffraction-limited (matches reference algorithm)
      return std::vector<double>{z0, w0, M2};
    };

    // Fit X
    auto guessX = computeInitialGuess(z_um, d4x_um);
    if (m_logger) {
      m_logger->Info("FitSlices beam " + beamStr + " X guess: z0=" +
        std::to_string(guessX[0]) + " w0=" + std::to_string(guessX[1]) +
        " M2=" + std::to_string(guessX[2]), "CustomServer");
    }
    auto fitX = LMFit::Solve(z_um, d4x_um, guessX, beamModel);

    // Fit Y
    auto guessY = computeInitialGuess(z_um, d4y_um);
    if (m_logger) {
      m_logger->Info("FitSlices beam " + beamStr + " Y guess: z0=" +
        std::to_string(guessY[0]) + " w0=" + std::to_string(guessY[1]) +
        " M2=" + std::to_string(guessY[2]), "CustomServer");
    }
    auto fitY = LMFit::Solve(z_um, d4y_um, guessY, beamModel);

    // Linear fit for centroid trajectory: centroid(z) = a*z + b
    auto linearFit = [](const std::vector<double>& x, const std::vector<double>& y)
        -> std::pair<double, double> {
      double N = static_cast<double>(x.size());
      double sx = 0, sy = 0, sxy = 0, sx2 = 0;
      for (size_t i = 0; i < x.size(); ++i) {
        sx += x[i]; sy += y[i]; sxy += x[i] * y[i]; sx2 += x[i] * x[i];
      }
      double denom = N * sx2 - sx * sx;
      if (std::abs(denom) < 1e-30) return {0.0, sy / N};
      double a = (N * sxy - sx * sy) / denom;
      double b = (sy - a * sx) / N;
      return {a, b};
    };

    auto [ax, bx] = linearFit(z_um, cx_um);
    auto [ay, by_] = linearFit(z_um, cy_um);

    // Full 3D Poynting angle (accounts for direction vector normalization)
    double n_norm = std::sqrt(1.0 + ax * ax + ay * ay);
    double poyntXDeg = std::atan(ax / n_norm) * 180.0 / M_PI;
    double poyntYDeg = -std::atan(ay / n_norm) * 180.0 / M_PI;  // Negate Y: image Y-axis is inverted vs physical convention

    // Extract fitted parameters (ensure physical validity)
    double w0X = std::max(std::abs(fitX.params[1]), 0.1);
    double w0Y = std::max(std::abs(fitY.params[1]), 0.1);
    double m2X = std::max(std::abs(fitX.params[2]), 0.3);
    double m2Y = std::max(std::abs(fitY.params[2]), 0.3);
    double z0X = fitX.params[0];
    double z0Y = fitY.params[0];
    double waistXUm = w0X;  // w0 half-width (matches reference algorithm)
    double waistYUm = w0Y;
    double centroidZ = (z0X + z0Y) / 2.0;

    // Interpolate beam centroid location at the waist z-position
    // Uses linear interpolation of centroid trajectory: loc = a*z0 + b
    double beamLocXUm = ax * centroidZ + bx;
    double beamLocYUm = ay * centroidZ + by_;

    // Store results in context
    std::string outPrefix = captureAs + "." + beamStr;

    context->SetValue(outPrefix + ".waist_x_um", static_cast<float>(waistXUm));
    context->SetValue(outPrefix + ".waist_y_um", static_cast<float>(waistYUm));
    context->SetValue(outPrefix + ".centroid_z_um", static_cast<float>(centroidZ));
    context->SetValue(outPrefix + ".z0_x_um", static_cast<float>(z0X));
    context->SetValue(outPrefix + ".z0_y_um", static_cast<float>(z0Y));
    context->SetValue(outPrefix + ".m2_x", static_cast<float>(m2X));
    context->SetValue(outPrefix + ".m2_y", static_cast<float>(m2Y));
    context->SetValue(outPrefix + ".poynt_x_deg", static_cast<float>(poyntXDeg));
    context->SetValue(outPrefix + ".poynt_y_deg", static_cast<float>(poyntYDeg));
    context->SetValue(outPrefix + ".beam_loc_x_um", static_cast<float>(beamLocXUm));
    context->SetValue(outPrefix + ".beam_loc_y_um", static_cast<float>(beamLocYUm));
    context->SetValue(outPrefix + ".fit_error_x", static_cast<float>(fitX.residualSum));
    context->SetValue(outPrefix + ".fit_error_y", static_cast<float>(fitY.residualSum));
    context->SetValue(outPrefix + ".fit_converged_x", fitX.converged);
    context->SetValue(outPrefix + ".fit_converged_y", fitY.converged);

    // Collect beam locations for TX→RX vector computation
    beamLocations.push_back({beamLocXUm, beamLocYUm});

    if (m_logger) {
      m_logger->Info("FitSlices beam " + beamStr +
        ": waist_x=" + std::to_string(waistXUm) + "um" +
        ", waist_y=" + std::to_string(waistYUm) + "um" +
        ", M2_x=" + std::to_string(m2X) +
        ", M2_y=" + std::to_string(m2Y) +
        ", poynt=(" + std::to_string(poyntXDeg) + "," + std::to_string(poyntYDeg) + ")deg" +
        ", loc=(" + std::to_string(beamLocXUm) + "," + std::to_string(beamLocYUm) + ")um" +
        ", iters_x=" + std::to_string(fitX.iterations) +
        ", iters_y=" + std::to_string(fitY.iterations),
        "CustomServer");
    }
  }

  // Compute TX→RX vector if we have at least 2 beams (beam 0 = TX, beam 1 = RX)
  if (beamLocations.size() >= 2) {
    double vecXUm = beamLocations[1].first - beamLocations[0].first;
    double vecYUm = beamLocations[1].second - beamLocations[0].second;
    double distance = std::sqrt(vecXUm * vecXUm + vecYUm * vecYUm);

    context->SetValue(captureAs + ".tx_rx_vector_x_um", static_cast<float>(vecXUm));
    context->SetValue(captureAs + ".tx_rx_vector_y_um", static_cast<float>(vecYUm));
    context->SetValue(captureAs + ".tx_rx_distance_um", static_cast<float>(distance));

    if (m_logger) {
      m_logger->Info("FitSlices TX->RX vector: X=" + std::to_string(vecXUm) +
                     "um, Y=" + std::to_string(vecYUm) +
                     "um, distance=" + std::to_string(distance) + "um", "CustomServer");
    }
  }

  // Store summary
  context->SetValue(captureAs + ".num_beams", static_cast<float>(numBeams));
  context->SetValue(captureAs + ".num_z_steps", static_cast<float>(numSlices));

  if (m_logger) {
    m_logger->Info("FitSlices complete: " + std::to_string(numBeams) + " beams, " +
                   std::to_string(numSlices) + " z-steps fitted", "CustomServer");
  }

  return true;
}

#ifdef HAS_OPENCV
// File-local helper: D4sigma on a cropped sub-image using OpenCV
struct D4CropResult { double cx, cy, d4x, d4y; };

static D4CropResult d4sigma_crop(const cv::Mat& img, int r0, int c0, int r1, int c1,
                                  double noiseFloor = 0.0) {
    // Two-pass: intensity-weighted centroid, then variance
    // noiseFloor: pixels below this value (after bg subtraction) are treated as zero
    //             to prevent background noise from inflating the second moment.
    //             Matches the effect of minSignalAboveBg in the Halcon detection pass.
    double total = 0, sx = 0, sy = 0;
    for (int r = r0; r < r1; r++) {
        for (int c = c0; c < c1; c++) {
            double I = static_cast<double>(img.at<float>(r, c));
            if (I < noiseFloor) I = 0;
            total += I;
            sx += I * c;
            sy += I * r;
        }
    }
    if (total == 0) return {0, 0, 0, 0};
    double cx = sx / total;
    double cy = sy / total;

    double vx = 0, vy = 0;
    for (int r = r0; r < r1; r++) {
        for (int c = c0; c < c1; c++) {
            double I = static_cast<double>(img.at<float>(r, c));
            if (I < noiseFloor) I = 0;
            vx += I * (c - cx) * (c - cx);
            vy += I * (r - cy) * (r - cy);
        }
    }
    vx /= total;
    vy /= total;

    return {cx, cy, 4.0 * std::sqrt(vx), 4.0 * std::sqrt(vy)};  // d4sigma = 4*sqrt(var)
}
#endif // HAS_OPENCV

//=============================================================================
// HandleD4SigmaZScanFolder - Process folder of images through vision pipeline
//=============================================================================

bool CustomServerCommandHandler::HandleD4SigmaZScanFolder(
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
#ifndef HAS_OPENCV
  errorMessage = "D4SigmaZScanFolder requires OpenCV (HAS_OPENCV not defined)";
  return false;
#else
  if (!context) {
    errorMessage = "D4SigmaZScanFolder requires a sequence context";
    return false;
  }

  // Parse parameters
  auto profIt = params.find("profile_path");
  if (profIt == params.end() || profIt->second.empty()) {
    errorMessage = "Missing required parameter: profile_path";
    return false;
  }
  std::string profilePath = profIt->second;

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
  auto ffIt2 = params.find("filename_filter");
  if (ffIt2 != params.end()) {
    filenameFilter = ffIt2->second;
  }

  std::string zRegex = R"(zheight([\d.]+)um)";
  auto zrIt = params.find("z_regex");
  if (zrIt != params.end() && !zrIt->second.empty()) {
    zRegex = zrIt->second;
  }

  std::string prefix = "splice";
  auto prefIt = params.find("capture_as_prefix");
  if (prefIt != params.end() && !prefIt->second.empty()) {
    prefix = prefIt->second;
  }

  bool rotate180 = true;  // Default matches Python convention
  auto rotIt = params.find("rotate_180");
  if (rotIt != params.end()) {
    rotate180 = (rotIt->second == "true" || rotIt->second == "1");
  }

  // Fixed-crop second pass parameters
  bool fixedCropEnabled = true;
  auto fceIt = params.find("fixed_crop_enabled");
  if (fceIt != params.end()) {
    fixedCropEnabled = (fceIt->second == "true" || fceIt->second == "1");
  }

  double cropWidthFactor = 2.5;
  auto cwfIt = params.find("crop_width_factor");
  if (cwfIt != params.end() && !cwfIt->second.empty()) {
    cropWidthFactor = std::stod(cwfIt->second);
  }

  // Verify folder exists
  namespace fs = std::filesystem;
  if (!fs::exists(folderPath) || !fs::is_directory(folderPath)) {
    errorMessage = "Folder does not exist: " + folderPath;
    return false;
  }

  // Collect image files and extract z-heights
  struct ImageEntry {
    std::string path;
    double z_um;
  };
  std::vector<ImageEntry> entries;
  std::regex re(zRegex, std::regex::icase);

  for (const auto& entry : fs::directory_iterator(folderPath)) {
    if (!entry.is_regular_file()) continue;

    std::string ext = entry.path().extension().string();
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
      z_um = std::stod(match[1].str());
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

  if (m_logger) {
    m_logger->Info("D4SigmaZScanFolder: " + std::to_string(entries.size()) +
                   " images in " + folderPath, "CustomServer");
  }

  // Load vision profile JSON to get plugin name and parameters
  std::string pluginName;
  int bgBoxSize = 25;  // Default for fixed-crop background subtraction
  try {
    std::ifstream file(profilePath);
    if (!file.is_open()) {
      errorMessage = "Cannot open vision profile: " + profilePath;
      return false;
    }

    nlohmann::json j;
    file >> j;
    file.close();

    // Set ROI if present
    if (j.contains("roi")) {
      auto roiJson = j["roi"];
      int startX = roiJson.value("startX", 0);
      int startY = roiJson.value("startY", 0);
      int endX = roiJson.value("endX", 640);
      int endY = roiJson.value("endY", 480);
      bool enabled = roiJson.value("enabled", false);

      VisionROI roi(startX, startY, endX, endY);
      roi.setEnabled(enabled);
      VisionManager::instance().setROI(roi);
    }

    // Load plugin name and parameters
    if (j.contains("plugin")) {
      auto pluginJson = j["plugin"];
      pluginName = pluginJson.value("name", "");

      if (!pluginName.empty()) {
        auto plugin = VisionManager::instance().getPlugin(QString::fromStdString(pluginName));
        if (plugin && pluginJson.contains("parameters")) {
          plugin->setParameters(pluginJson["parameters"]);
          // Extract bgBoxSize for fixed-crop second pass background subtraction
          if (pluginJson["parameters"].contains("bgBoxSize")) {
            bgBoxSize = pluginJson["parameters"]["bgBoxSize"].get<int>();
          }
        } else if (!plugin) {
          errorMessage = "Vision plugin not found: " + pluginName;
          return false;
        }
      }
    }

    if (pluginName.empty()) {
      errorMessage = "No plugin specified in vision profile: " + profilePath;
      return false;
    }
  } catch (const std::exception& e) {
    errorMessage = "Error loading vision profile: " + std::string(e.what());
    return false;
  }

  // Process each image through the vision pipeline
  QString qPluginName = QString::fromStdString(pluginName);
  int successCount = 0;

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& img = entries[i];
    std::string captureAs = prefix + std::to_string(i + 1);

    if (m_logger) {
      m_logger->Info("D4SigmaZScanFolder [" + std::to_string(i + 1) + "/" +
                     std::to_string(entries.size()) + "]: " +
                     fs::path(img.path).filename().string() +
                     " z=" + std::to_string(img.z_um) + "um", "CustomServer");
    }

    // Load image
    cv::Mat image = cv::imread(img.path, cv::IMREAD_UNCHANGED);
    if (image.empty()) {
      if (m_logger) {
        m_logger->Warning("Failed to load image: " + img.path, "CustomServer");
      }
      continue;
    }

    // Rotate 180° if requested (matches Python convention: rotate_180=True)
    if (rotate180) {
      cv::flip(image, image, -1);  // flipCode=-1 = 180° rotation
    }

    // Set as input and execute vision plugin
    VisionManager::instance().setInputImage(image);
    VisionResult result = VisionManager::instance().executePlugin(qPluginName);

    if (!result.success) {
      if (m_logger) {
        m_logger->Warning("Vision execution failed for " + img.path + ": " +
                         result.errorMessage, "CustomServer");
      }
      continue;
    }

    // Store measurements in context (same format as StoreResultInContext)
    for (const auto& kv : result.measurements) {
      std::string key = captureAs + ".result." + kv.first;
      context->SetValue(key, static_cast<float>(kv.second));
    }

    // Store metadata
    for (const auto& kv : result.metadata) {
      std::string key = captureAs + ".metadata." + kv.first;
      context->SetValue(key, kv.second);
    }

    // Store beam results
    int beamIndex = 0;
    for (const auto& beam : result.beams) {
      std::string bPrefix = captureAs + ".beam." + std::to_string(beamIndex) + ".";
      context->SetValue(bPrefix + "center_x", static_cast<float>(beam.centerPx.x));
      context->SetValue(bPrefix + "center_y", static_cast<float>(beam.centerPx.y));
      context->SetValue(bPrefix + "center_mm_x", static_cast<float>(beam.centerMm.x));
      context->SetValue(bPrefix + "center_mm_y", static_cast<float>(beam.centerMm.y));
      context->SetValue(bPrefix + "fwhm_major_px", static_cast<float>(beam.fwhmMajorPx));
      context->SetValue(bPrefix + "fwhm_minor_px", static_cast<float>(beam.fwhmMinorPx));
      context->SetValue(bPrefix + "fwhm_major_mm", static_cast<float>(beam.fwhmMajorMm));
      context->SetValue(bPrefix + "fwhm_minor_mm", static_cast<float>(beam.fwhmMinorMm));
      context->SetValue(bPrefix + "mean_intensity", static_cast<float>(beam.meanIntensity));
      context->SetValue(bPrefix + "max_intensity", static_cast<float>(beam.maxIntensity));
      context->SetValue(bPrefix + "is_saturated", beam.isSaturated);
      context->SetValue(bPrefix + "angle_deg", static_cast<float>(beam.angleDeg));
      context->SetValue(bPrefix + "ellipticity", static_cast<float>(beam.ellipticity));
      context->SetValue(bPrefix + "area_px", static_cast<float>(beam.areaPx));
      if (!beam.name.empty()) {
        context->SetValue(bPrefix + "name", beam.name);
      }
      beamIndex++;
    }
    context->SetValue(captureAs + ".beam.count", static_cast<float>(beamIndex));

    // Store z-height and success flag
    context->SetValue(captureAs + ".z_um", static_cast<float>(img.z_um));
    context->SetValue(captureAs + ".result.success", result.success);
    context->SetValue(captureAs + ".result.plugin", result.pluginName);

    ++successCount;
  }

  // =========================================================================
  // Second pass: Fixed-crop d4sigma (eliminates threshold-dependent wing clipping)
  // At defocused z-positions, beams are dimmer/larger so more wing pixels fall
  // below the noise threshold — causing d4sigma underestimation. The fixed crop
  // uses the middle slice (best SNR) as reference to define a consistent
  // integration region across all z-steps.
  // =========================================================================
  if (fixedCropEnabled && successCount >= 3) {
    // Collect indices of successful slices
    std::vector<size_t> successIndices;
    for (size_t i = 0; i < entries.size(); ++i) {
      std::string sliceKey = prefix + std::to_string(i + 1);
      if (context->HasValue(sliceKey + ".result.success")) {
        successIndices.push_back(i);
      }
    }

    if (successIndices.size() >= 3) {
      // Pick middle slice as reference (best SNR, near focus)
      size_t midSuccessIdx = successIndices.size() / 2;
      size_t midEntryIdx = successIndices[midSuccessIdx];
      std::string midKey = prefix + std::to_string(midEntryIdx + 1);

      // Get beam count from middle slice
      int numBeams = static_cast<int>(context->GetNumeric(midKey + ".result.beam_count", 0.0f));
      if (numBeams <= 0) {
        // Probe incrementally
        for (int j = 0; j < 20; ++j) {
          if (!context->HasValue(midKey + ".result.beam_" + std::to_string(j) + "_d4sigma_x_px")) break;
          numBeams = j + 1;
        }
      }

      if (numBeams > 0) {
        if (m_logger) {
          m_logger->Info("D4SigmaZScanFolder: Starting fixed-crop second pass (factor=" +
                         std::to_string(cropWidthFactor) + ", bgBox=" + std::to_string(bgBoxSize) +
                         ", ref slice=" + std::to_string(midEntryIdx + 1) + ", beams=" +
                         std::to_string(numBeams) + ")", "CustomServer");
        }

        // Read reference image to get dimensions
        cv::Mat refImage = cv::imread(entries[midEntryIdx].path, cv::IMREAD_UNCHANGED);
        if (!refImage.empty()) {
          if (rotate180) cv::flip(refImage, refImage, -1);
          int imgRows = refImage.rows;
          int imgCols = refImage.cols;

          // Compute per-beam crop rectangles from middle slice
          struct CropRect { int r0, c0, r1, c1; };
          std::vector<CropRect> crops(static_cast<size_t>(numBeams));
          bool cropsValid = true;

          for (int b = 0; b < numBeams; ++b) {
            std::string bKey = midKey + ".result.beam_" + std::to_string(b) + "_";
            float cx_px = context->GetNumeric(bKey + "centroid_x_px", -1.0f);
            float cy_px = context->GetNumeric(bKey + "centroid_y_px", -1.0f);
            float d4x_px = context->GetNumeric(bKey + "d4sigma_x_px", -1.0f);
            float d4y_px = context->GetNumeric(bKey + "d4sigma_y_px", -1.0f);

            if (cx_px < 0 || cy_px < 0 || d4x_px <= 0 || d4y_px <= 0) {
              if (m_logger) {
                m_logger->Warning("D4SigmaZScanFolder: Beam " + std::to_string(b) +
                  " missing data in middle slice, skipping fixed-crop pass", "CustomServer");
              }
              cropsValid = false;
              break;
            }

            double cropW = static_cast<double>(d4x_px) * cropWidthFactor;
            double cropH = static_cast<double>(d4y_px) * cropWidthFactor;
            int r0 = std::max(0, static_cast<int>(static_cast<double>(cy_px) - cropH));
            int c0 = std::max(0, static_cast<int>(static_cast<double>(cx_px) - cropW));
            int r1 = std::min(imgRows, static_cast<int>(static_cast<double>(cy_px) + cropH));
            int c1 = std::min(imgCols, static_cast<int>(static_cast<double>(cx_px) + cropW));

            crops[static_cast<size_t>(b)] = {r0, c0, r1, c1};

            if (m_logger) {
              m_logger->Info("D4SigmaZScanFolder: Beam " + std::to_string(b) +
                " crop: [" + std::to_string(r0) + "," + std::to_string(c0) + "]->[" +
                std::to_string(r1) + "," + std::to_string(c1) + "] from centroid=(" +
                std::to_string(cx_px) + "," + std::to_string(cy_px) + ") d4s=(" +
                std::to_string(d4x_px) + "," + std::to_string(d4y_px) + ")", "CustomServer");
            }
          }

          if (cropsValid) {
            // Process each successful slice with fixed crops
            for (size_t si = 0; si < successIndices.size(); ++si) {
              size_t idx = successIndices[si];
              std::string sliceKey = prefix + std::to_string(idx + 1);

              // Reload image
              cv::Mat image = cv::imread(entries[idx].path, cv::IMREAD_UNCHANGED);
              if (image.empty()) continue;
              if (rotate180) cv::flip(image, image, -1);

              // Convert to single-channel float
              cv::Mat gray;
              if (image.channels() == 3) {
                cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
              } else if (image.channels() == 4) {
                cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
              } else {
                gray = image;
              }
              cv::Mat floatImg;
              gray.convertTo(floatImg, CV_32F);

              // 4-corner background subtraction (matches Halcon plugin's approach)
              int box = std::min(bgBoxSize, std::min(floatImg.rows, floatImg.cols) / 2);
              double bgSum = 0;
              int bgPixels = 0;
              // Top-left
              for (int r = 0; r < box; ++r)
                for (int c = 0; c < box; ++c) {
                  bgSum += static_cast<double>(floatImg.at<float>(r, c));
                  bgPixels++;
                }
              // Top-right
              for (int r = 0; r < box; ++r)
                for (int c = floatImg.cols - box; c < floatImg.cols; ++c) {
                  bgSum += static_cast<double>(floatImg.at<float>(r, c));
                  bgPixels++;
                }
              // Bottom-left
              for (int r = floatImg.rows - box; r < floatImg.rows; ++r)
                for (int c = 0; c < box; ++c) {
                  bgSum += static_cast<double>(floatImg.at<float>(r, c));
                  bgPixels++;
                }
              // Bottom-right
              for (int r = floatImg.rows - box; r < floatImg.rows; ++r)
                for (int c = floatImg.cols - box; c < floatImg.cols; ++c) {
                  bgSum += static_cast<double>(floatImg.at<float>(r, c));
                  bgPixels++;
                }
              float bgMean = (bgPixels > 0) ? static_cast<float>(bgSum / bgPixels) : 0.0f;
              floatImg -= bgMean;

              // For each beam, compute d4sigma in fixed crop and overwrite context
              for (int b = 0; b < numBeams; ++b) {
                const auto& crop = crops[static_cast<size_t>(b)];
                // Use noiseFloor=0 (clip negatives only) — matches Python calc_beam_params(threshold=0).
                // The fixed crop from the middle slice is already tight enough to limit background noise.
                D4CropResult d4 = d4sigma_crop(floatImg, crop.r0, crop.c0, crop.r1, crop.c1, 0.0);

                if (d4.d4x > 0 && d4.d4y > 0) {
                  std::string bKey = sliceKey + ".result.beam_" + std::to_string(b) + "_";
                  context->SetValue(bKey + "d4sigma_x_px", static_cast<float>(d4.d4x));
                  context->SetValue(bKey + "d4sigma_y_px", static_cast<float>(d4.d4y));
                  context->SetValue(bKey + "centroid_x_px", static_cast<float>(d4.cx));
                  context->SetValue(bKey + "centroid_y_px", static_cast<float>(d4.cy));
                }
              }
            }

            if (m_logger) {
              m_logger->Info("D4SigmaZScanFolder: Fixed-crop second pass complete (" +
                             std::to_string(successIndices.size()) + " slices, " +
                             std::to_string(numBeams) + " beams updated)", "CustomServer");
            }
          }
        } else {
          if (m_logger) {
            m_logger->Warning("D4SigmaZScanFolder: Could not load reference image for fixed-crop pass",
                             "CustomServer");
          }
        }
      }
    }
  }

  if (successCount == 0) {
    errorMessage = "D4SigmaZScanFolder: No images were successfully processed";
    return false;
  }

  // Store summary in context
  context->SetValue(prefix + ".total_slices", static_cast<float>(successCount));

  if (m_logger) {
    m_logger->Info("D4SigmaZScanFolder complete: " + std::to_string(successCount) +
                   "/" + std::to_string(entries.size()) + " images processed", "CustomServer");
  }

  return true;
#endif // HAS_OPENCV
}

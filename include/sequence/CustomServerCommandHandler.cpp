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
#include <iostream>
#include <thread>
#include <chrono>
#include <optional>
#include <filesystem>
#include <regex>
#include <algorithm>

bool CustomServerCommandHandler::ExecuteCommand(
  void* /*manager*/,
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  if (!m_context || !m_context->beamServerClient) {
    errorMessage = "BeamServerClient not available";
    return false;
  }

  if (methodName == "Connect")      return HandleConnect(params, errorMessage);
  if (methodName == "Disconnect")   return HandleDisconnect(errorMessage);
  if (methodName == "Detect")       return HandleDetect(params, errorMessage, context);
  if (methodName == "DetectFile")   return HandleDetectFile(params, errorMessage, context);
  if (methodName == "DetectFolder") return HandleDetectFolder(params, errorMessage, context);
  if (methodName == "Fit")          return HandleFit(params, errorMessage, context);
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

  std::string name;
  auto nameIt = params.find("name");
  if (nameIt != params.end() && !nameIt->second.empty()) {
    name = nameIt->second;
    if (context && name.substr(0, 8) == "context:") {
      name = context->GetString(name.substr(8));
    }
  }

  if (m_logger) {
    m_logger->Info("Sending fit request (wavelength=" +
                   std::to_string(wavelengthM) + "m" +
                   (name.empty() ? "" : ", name=" + name) + ")", "CustomServer");
  }

  auto result = m_context->beamServerClient->SendFit(umPerPxX, umPerPxY, wavelengthM, name);

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

    // Save directory (returned by server when images are saved)
    if (result.contains("save_dir"))
      context->SetValue(captureAs + ".save_dir", result["save_dir"].get<std::string>());

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
    std::smatch match;
    double z_um = 0.0;
    if (std::regex_search(filename, match, re) && match.size() > 1) {
      z_um = std::stod(match[1].str());
    }

    entries.push_back({entry.path().string(), z_um});
  }

  if (entries.empty()) {
    errorMessage = "No image files found in: " + folderPath;
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

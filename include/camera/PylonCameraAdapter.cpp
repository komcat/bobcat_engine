// PylonCameraAdapter.cpp
#include "PylonCameraAdapter.h"
#include "CameraManager.h"  // For CameraInfo struct
#include <iostream>

PylonCameraAdapter::PylonCameraAdapter(const std::string& cameraId)
  : m_cameraId(cameraId)
  , m_pylonCamera(std::make_unique<PylonCamera>())
  , m_frameCallback(nullptr)
  , m_connectionMethod(ConnectionMethod::AUTO)
  , m_deviceIndex(0) {

  // Initialize format converter once
  m_formatConverter.OutputPixelFormat = Pylon::PixelType_RGB8packed;
  m_formatConverter.OutputBitAlignment = Pylon::OutputBitAlignment_MsbAligned;

  std::cout << "PylonCameraAdapter created for camera: " << cameraId << " (auto-connect mode)" << std::endl;
}

PylonCameraAdapter::PylonCameraAdapter(const CameraInfo& cameraInfo)
  : m_cameraId(cameraInfo.id)
  , m_pylonCamera(std::make_unique<PylonCamera>())
  , m_frameCallback(nullptr)
  , m_deviceIndex(cameraInfo.deviceIndex) {

  // Copy connection info from CameraInfo
  switch (cameraInfo.connectionMethod) {
  case CameraInfo::ConnectionMethod::IP_ADDRESS:
    m_connectionMethod = ConnectionMethod::IP_ADDRESS;
    m_ipAddress = cameraInfo.ipAddress;
    std::cout << "PylonCameraAdapter created for camera: " << cameraInfo.id
      << " (IP: " << m_ipAddress << ")" << std::endl;
    break;
  case CameraInfo::ConnectionMethod::SERIAL_NUMBER:
    m_connectionMethod = ConnectionMethod::SERIAL_NUMBER;
    m_serialNumber = cameraInfo.serialNumber;
    std::cout << "PylonCameraAdapter created for camera: " << cameraInfo.id
      << " (Serial: " << m_serialNumber << ")" << std::endl;
    break;
  case CameraInfo::ConnectionMethod::DEVICE_INDEX:
    m_connectionMethod = ConnectionMethod::DEVICE_INDEX;
    m_deviceIndex = cameraInfo.deviceIndex;
    std::cout << "PylonCameraAdapter created for camera: " << cameraInfo.id
      << " (Index: " << m_deviceIndex << ")" << std::endl;
    break;
  case CameraInfo::ConnectionMethod::AUTO:
  default:
    m_connectionMethod = ConnectionMethod::AUTO;
    std::cout << "PylonCameraAdapter created for camera: " << cameraInfo.id
      << " (auto-connect mode)" << std::endl;
    break;
  }

  // Initialize format converter once
  m_formatConverter.OutputPixelFormat = Pylon::PixelType_RGB8packed;
  m_formatConverter.OutputBitAlignment = Pylon::OutputBitAlignment_MsbAligned;
}

PylonCameraAdapter::~PylonCameraAdapter() {
  try {
    // Clear cached images before destroying
    if (m_cachedPylonImage.IsValid()) {
      m_cachedPylonImage.Release();
    }
    if (m_cachedConvertedImage.IsValid()) {
      m_cachedConvertedImage.Release();
    }

    if (IsGrabbing()) {
      StopGrabbing();
    }
    if (IsConnected()) {
      Disconnect();
    }
  }
  catch (...) {
    // Ignore exceptions during destruction
  }

  std::cout << "PylonCameraAdapter destroyed for camera: " << m_cameraId << std::endl;
}

bool PylonCameraAdapter::Initialize() {
  try {
    if (!m_pylonCamera) {
      SetError("Pylon camera instance is null");
      return false;
    }

    bool result = m_pylonCamera->Initialize();
    if (!result) {
      SetError("Failed to initialize Pylon camera");
    }
    else {
      ClearLastError();
      SetupFrameCallbackIntegration();
    }

    return result;
  }
  catch (const std::exception& e) {
    SetError("Exception during initialization: " + std::string(e.what()));
    return false;
  }
}

bool PylonCameraAdapter::Connect() {
  try {
    if (!m_pylonCamera) {
      SetError("Pylon camera instance is null");
      return false;
    }

    bool result = false;

    // Use the appropriate connection method based on stored info
    switch (m_connectionMethod) {
    case ConnectionMethod::IP_ADDRESS:
      std::cout << "PylonCameraAdapter [" << m_cameraId << "]: Connecting by IP: " << m_ipAddress << std::endl;
      result = m_pylonCamera->ConnectToIP(m_ipAddress);
      break;

    case ConnectionMethod::SERIAL_NUMBER:
      std::cout << "PylonCameraAdapter [" << m_cameraId << "]: Connecting by Serial: " << m_serialNumber << std::endl;
      result = m_pylonCamera->ConnectToSerial(m_serialNumber);
      break;

    case ConnectionMethod::DEVICE_INDEX:
      std::cout << "PylonCameraAdapter [" << m_cameraId << "]: Connecting by Index: " << m_deviceIndex << std::endl;
      result = m_pylonCamera->ConnectToIndex(m_deviceIndex);
      break;

    case ConnectionMethod::AUTO:
    default:
      std::cout << "PylonCameraAdapter [" << m_cameraId << "]: Connecting to first available camera" << std::endl;
      result = m_pylonCamera->Connect();
      break;
    }

    if (!result) {
      SetError("Failed to connect Pylon camera");
    }
    else {
      ClearLastError();
      std::cout << "PylonCameraAdapter [" << m_cameraId << "]: Successfully connected" << std::endl;
    }

    return result;
  }
  catch (const std::exception& e) {
    SetError("Exception during connection: " + std::string(e.what()));
    return false;
  }
}

bool PylonCameraAdapter::Disconnect() {
  try {
    if (!m_pylonCamera) {
      return true; // Already disconnected
    }

    // Stop grabbing first if active
    if (IsGrabbing()) {
      if (!StopGrabbing()) {
        SetError("Failed to stop grabbing during disconnect");
        // Continue with disconnect anyway
      }
    }

    bool result = m_pylonCamera->DisconnectWithResult();
    if (!result) {
      SetError("Failed to disconnect Pylon camera");
    }
    else {
      ClearLastError();
    }

    return result;
  }
  catch (const std::exception& e) {
    SetError("Exception during disconnection: " + std::string(e.what()));
    return false;
  }
}

bool PylonCameraAdapter::StartGrabbing() {
  try {
    if (!m_pylonCamera) {
      SetError("Pylon camera instance is null");
      return false;
    }

    if (!IsConnected()) {
      SetError("Camera not connected");
      return false;
    }

    bool result = m_pylonCamera->StartGrabbing();
    if (!result) {
      SetError("Failed to start grabbing");
    }
    else {
      ClearLastError();
    }

    return result;
  }
  catch (const std::exception& e) {
    SetError("Exception during start grabbing: " + std::string(e.what()));
    return false;
  }
}

bool PylonCameraAdapter::StopGrabbing() {
  try {
    if (!m_pylonCamera) {
      return true; // Already stopped
    }

    bool result = m_pylonCamera->StopGrabbingWithResult();
    if (!result) {
      SetError("Failed to stop grabbing");
    }
    else {
      ClearLastError();
    }

    return result;
  }
  catch (const std::exception& e) {
    SetError("Exception during stop grabbing: " + std::string(e.what()));
    return false;
  }
}

bool PylonCameraAdapter::IsConnected() const {
  if (!m_pylonCamera) {
    return false;
  }

  try {
    return m_pylonCamera->IsConnected();
  }
  catch (...) {
    return false;
  }
}

bool PylonCameraAdapter::IsGrabbing() const {
  if (!m_pylonCamera) {
    return false;
  }

  try {
    return m_pylonCamera->IsGrabbing();
  }
  catch (...) {
    return false;
  }
}

bool PylonCameraAdapter::IsDeviceRemoved() const {
  if (!m_pylonCamera) {
    return true;
  }

  try {
    return m_pylonCamera->IsCameraDeviceRemoved();
  }
  catch (...) {
    return true;
  }
}

bool PylonCameraAdapter::CaptureFrame(CameraFrameData& frameData) {
  try {
    if (!m_pylonCamera) {
      SetError("Pylon camera instance is null");
      return false;
    }

    if (!IsConnected()) {
      SetError("Camera not connected");
      return false;
    }

    // FIXED: Check if we're already grabbing continuously
    if (IsGrabbing()) {
      // Use the current frame from continuous grabbing instead of trying to retrieve a new one

      // Try to get the latest frame data from PylonCamera's internal buffer
      uint8_t* imageData = nullptr;
      uint32_t width = 0, height = 0;
      bool newFrame = false;

      // Use PylonCamera's method to get the latest frame data (if available)
      // This avoids conflicts with the continuous grabbing thread
      if (m_pylonCamera->GetLatestFrameData(imageData, width, height, newFrame)) {
        // Fill the CameraFrameData structure
        frameData.cameraId = m_cameraId;
        frameData.width = width;
        frameData.height = height;
        frameData.channels = 3; // RGB
        frameData.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count();

        // Get exposure settings for metadata
        auto exposureSettings = GetExposureSettings();
        frameData.exposureTime = exposureSettings.exposure_time;
        frameData.gain = exposureSettings.gain;

        // Copy the image data (already in RGB format from PylonCamera)
        size_t dataSize = width * height * 3;
        frameData.imageData.resize(dataSize);
        std::memcpy(frameData.imageData.data(), imageData, dataSize);

        ClearLastError();
        return true;
      }
      else {
        SetError("No valid frame available from continuous grabbing");
        return false;
      }
    }
    else {
      // Camera is not grabbing continuously
      auto& internalCamera = m_pylonCamera->GetInternalCamera();

      Pylon::CGrabResultPtr grabResult;
      if (!internalCamera.RetrieveResult(1000, grabResult, Pylon::TimeoutHandling_Return) ||
        !grabResult || !grabResult->GrabSucceeded()) {
        SetError("Failed to retrieve frame - camera not grabbing");
        return false;
      }

      // Use the cached converter with thread safety
      std::lock_guard<std::mutex> lock(m_conversionMutex);

      frameData.cameraId = m_cameraId;
      frameData.width = grabResult->GetWidth();
      frameData.height = grabResult->GetHeight();
      frameData.channels = 3;
      frameData.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

      auto exposureSettings = GetExposureSettings();
      frameData.exposureTime = exposureSettings.exposure_time;
      frameData.gain = exposureSettings.gain;

      // Check pixel type first
      Pylon::EPixelType pixelType = grabResult->GetPixelType();

      if (pixelType == Pylon::PixelType_RGB8packed) {
        // Direct copy
        size_t dataSize = grabResult->GetImageSize();
        frameData.imageData.resize(dataSize);
        const uint8_t* buffer = static_cast<const uint8_t*>(grabResult->GetBuffer());
        std::memcpy(frameData.imageData.data(), buffer, dataSize);
      }
      else {
        // Use cached converter
        if (m_cachedPylonImage.IsValid()) {
          m_cachedPylonImage.Release();
        }
        m_cachedPylonImage.AttachGrabResultBuffer(grabResult);

        if (m_cachedConvertedImage.IsValid()) {
          m_cachedConvertedImage.Release();
        }
        m_formatConverter.Convert(m_cachedConvertedImage, m_cachedPylonImage);

        size_t dataSize = m_cachedConvertedImage.GetImageSize();
        frameData.imageData.resize(dataSize);
        const uint8_t* buffer = static_cast<const uint8_t*>(m_cachedConvertedImage.GetBuffer());
        std::memcpy(frameData.imageData.data(), buffer, dataSize);
      }

      ClearLastError();
      return true;
    }
  }
  catch (const std::exception& e) {
    SetError("Exception during frame capture: " + std::string(e.what()));
    return false;
  }
}

bool PylonCameraAdapter::HasNewFrame() const {
  if (!m_pylonCamera) {
    return false;
  }

  try {
    // For PylonCamera, we can check if there's a frame available
    return m_pylonCamera->IsGrabbing();
  }
  catch (...) {
    return false;
  }
}

std::string PylonCameraAdapter::GetModelName() const {
  if (!m_pylonCamera || !IsConnected()) {
    return "Unknown Pylon Camera";
  }

  try {
    std::string deviceInfo = m_pylonCamera->GetDeviceInfo();
    // Extract model name from device info string
    size_t modelPos = deviceInfo.find("Camera: ");
    if (modelPos != std::string::npos) {
      size_t start = modelPos + 8; // Skip "Camera: "
      size_t end = deviceInfo.find(",", start);
      if (end != std::string::npos) {
        return deviceInfo.substr(start, end - start);
      }
    }
    return "Pylon Camera";
  }
  catch (...) {
    return "Pylon Camera";
  }
}

std::string PylonCameraAdapter::GetSerialNumber() const {
  if (!m_pylonCamera || !IsConnected()) {
    return "";
  }

  try {
    std::string deviceInfo = m_pylonCamera->GetDeviceInfo();
    // Extract serial number from device info string
    size_t snPos = deviceInfo.find("S/N: ");
    if (snPos != std::string::npos) {
      size_t start = snPos + 5; // Skip "S/N: "
      return deviceInfo.substr(start);
    }
    return "";
  }
  catch (...) {
    return "";
  }
}

std::string PylonCameraAdapter::GetVendorName() const {
  return "Basler";
}

bool PylonCameraAdapter::SetExposureSettings(const ExposureSettings& settings) {
  try {
    if (!m_pylonCamera) {
      SetError("Pylon camera instance is null");
      return false;
    }

    // Convert common settings to Pylon format
    PylonCamera::ExposureSettings pylonSettings;
    ConvertCommonExposureToPylon(settings, pylonSettings);

    bool result = m_pylonCamera->ApplyExposureSettings(pylonSettings);
    if (!result) {
      SetError("Failed to set exposure settings");
    }
    else {
      ClearLastError();
    }

    return result;
  }
  catch (const std::exception& e) {
    SetError("Exception setting exposure: " + std::string(e.what()));
    return false;
  }
}

ICameraHardware::ExposureSettings PylonCameraAdapter::GetExposureSettings() const {
  ExposureSettings settings;

  try {
    if (!m_pylonCamera) {
      return settings;
    }

    PylonCamera::ExposureSettings pylonSettings = m_pylonCamera->GetCurrentExposureSettings();
    ConvertPylonExposureToCommon(pylonSettings, settings);

  }
  catch (...) {
    // Return default settings on error
  }

  return settings;
}

void PylonCameraAdapter::SetFrameCallback(std::function<void(const CameraFrameData&)> callback) {
  m_frameCallback = callback;
  std::cout << "Frame callback set for Pylon camera: " << m_cameraId << std::endl;
}

void PylonCameraAdapter::ClearFrameCallback() {
  m_frameCallback = nullptr;
  std::cout << "Frame callback cleared for Pylon camera: " << m_cameraId << std::endl;
}

bool PylonCameraAdapter::SetConfiguration(const std::string& key, const std::string& /*value*/) {
  try {
    if (!m_pylonCamera || !IsConnected()) {
      SetError("Camera not connected");
      return false;
    }

    // Handle common Pylon-specific configurations
    if (key == "TriggerMode") {
      // Set trigger mode configuration
      return true; // Implement based on your PylonCamera class
    }
    else if (key == "PixelFormat") {
      // Set pixel format
      return true; // Implement based on your PylonCamera class
    }

    SetError("Unknown configuration key: " + key);
    return false;
  }
  catch (const std::exception& e) {
    SetError("Exception setting configuration: " + std::string(e.what()));
    return false;
  }
}

std::string PylonCameraAdapter::GetConfiguration(const std::string& key) const {
  try {
    if (!m_pylonCamera || !IsConnected()) {
      return "";
    }

    // Handle common Pylon-specific configurations
    if (key == "TriggerMode") {
      return "Off"; // Get from PylonCamera
    }
    else if (key == "PixelFormat") {
      return "RGB8"; // Get from PylonCamera
    }

    return "";
  }
  catch (...) {
    return "";
  }
}

// Private helper methods

void PylonCameraAdapter::SetError(const std::string& error) {
  m_lastError = error;
  std::cerr << "PylonCameraAdapter [" << m_cameraId << "]: " << error << std::endl;
}

void PylonCameraAdapter::ConvertPylonExposureToCommon(
  const PylonCamera::ExposureSettings& pylonSettings,
  ExposureSettings& commonSettings) const {

  commonSettings.exposure_time = pylonSettings.exposure_time;
  commonSettings.gain = pylonSettings.gain;
  commonSettings.auto_exposure = pylonSettings.exposure_auto;
  commonSettings.auto_gain = pylonSettings.gain_auto;
  commonSettings.frame_rate = pylonSettings.frame_rate;
}

void PylonCameraAdapter::ConvertCommonExposureToPylon(
  const ExposureSettings& commonSettings,
  PylonCamera::ExposureSettings& pylonSettings) const {

  pylonSettings.exposure_time = commonSettings.exposure_time;
  pylonSettings.gain = commonSettings.gain;
  pylonSettings.exposure_auto = commonSettings.auto_exposure;
  pylonSettings.gain_auto = commonSettings.auto_gain;
  pylonSettings.frame_rate = commonSettings.frame_rate;
}

void PylonCameraAdapter::SetupFrameCallbackIntegration() {
  // Set up the frame callback integration with PylonCamera
  if (m_pylonCamera) {
    auto callback = [this](const Pylon::CGrabResultPtr& grabResult) {
      OnPylonFrameReceived(grabResult);
    };
    m_pylonCamera->SetNewFrameCallback(callback);
  }

  std::cout << "Frame callback integration setup for camera: " << m_cameraId << std::endl;
}

void PylonCameraAdapter::OnPylonFrameReceived(const Pylon::CGrabResultPtr& grabResult) {
  // Early exit if no callback or invalid grab result
  if (!m_frameCallback || !grabResult || !grabResult->GrabSucceeded()) {
    return;
  }

  try {
    // Lock for thread safety during conversion
    std::lock_guard<std::mutex> lock(m_conversionMutex);

    CameraFrameData frameData;
    frameData.cameraId = m_cameraId;
    frameData.width = grabResult->GetWidth();
    frameData.height = grabResult->GetHeight();
    frameData.channels = 3; // RGB
    frameData.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();

    // Check if the grabbed image is already RGB to avoid unnecessary conversion
    Pylon::EPixelType pixelType = grabResult->GetPixelType();

    if (pixelType == Pylon::PixelType_RGB8packed) {
      // Already in the format we want, copy directly
      size_t dataSize = grabResult->GetImageSize();
      frameData.imageData.resize(dataSize);
      const uint8_t* buffer = static_cast<const uint8_t*>(grabResult->GetBuffer());
      std::memcpy(frameData.imageData.data(), buffer, dataSize);
    }
    else {
      // Need format conversion - use cached objects

      // Release and reattach the cached pylon image
      if (m_cachedPylonImage.IsValid()) {
        m_cachedPylonImage.Release();
      }
      m_cachedPylonImage.AttachGrabResultBuffer(grabResult);

      // Release previous converted image
      if (m_cachedConvertedImage.IsValid()) {
        m_cachedConvertedImage.Release();
      }

      // Convert using the pre-initialized converter
      m_formatConverter.Convert(m_cachedConvertedImage, m_cachedPylonImage);

      // Copy the converted image data
      size_t dataSize = m_cachedConvertedImage.GetImageSize();
      frameData.imageData.resize(dataSize);
      const uint8_t* buffer = static_cast<const uint8_t*>(m_cachedConvertedImage.GetBuffer());
      std::memcpy(frameData.imageData.data(), buffer, dataSize);
    }

    // Call the frame callback outside the lock
    m_frameCallback(frameData);
  }
  catch (const std::exception& e) {
    SetError("Exception in frame callback: " + std::string(e.what()));
  }
}
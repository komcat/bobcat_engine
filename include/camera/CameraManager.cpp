// CameraManager.cpp - Complete implementation with clean architecture
// Business logic only - no UI, no test class dependencies, no logger
#include "CameraManager.h"
#include "ICameraHardware.h"
#include "CameraHardwareFactory.h"

// SDK includes for discovery only
#include <pylon/PylonIncludes.h>
#include "../external/uEye/include/ueye.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <iomanip>
#include <sstream>

// ============================================================================
// BMP FILE STRUCTURES
// ============================================================================

#pragma pack(push, 1)
struct BMPFileHeader {
  uint16_t type = 0x4D42; // "BM"
  uint32_t size;
  uint16_t reserved1 = 0;
  uint16_t reserved2 = 0;
  uint32_t offset = 54;
};

struct BMPInfoHeader {
  uint32_t size = 40;
  int32_t width;
  int32_t height;
  uint16_t planes = 1;
  uint16_t bitCount = 24;
  uint32_t compression = 0;
  uint32_t imageSize;
  int32_t xPixelsPerMeter = 2835; // 72 DPI
  int32_t yPixelsPerMeter = 2835; // 72 DPI
  uint32_t colorsUsed = 0;
  uint32_t colorsImportant = 0;
};
#pragma pack(pop)

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

CameraManager::CameraManager()
  : m_broadcastSystemRunning(false)
  , m_shouldStopBroadcast(false)
  , m_globalBroadcastRate(30.0f) {

  std::cout << "[INFO] CameraManager created" << std::endl;
}

CameraManager::~CameraManager() {
  StopBroadcastSystem();
  StopGrabbingAll();
  m_cameras.clear();

  std::cout << "[INFO] CameraManager destroyed" << std::endl;
}

// ============================================================================
// MANAGED CAMERA IMPLEMENTATION
// ============================================================================

CameraManager::ManagedCamera::ManagedCamera(const ExtendedCameraInfo& cameraInfo)
  : info(cameraInfo) {

  // Create camera using factory with full connection info
  // This passes IP address, serial number, or device index to the adapter
  camera = CameraHardwareFactory::CreateCamera(cameraInfo.type, static_cast<const CameraInfo&>(cameraInfo));

  std::string typeName = CameraHardwareFactory::GetCameraTypeName(cameraInfo.type);
  if (camera) {
    std::cout << "[INFO] Created " << typeName << " camera: " << cameraInfo.id
      << " (" << cameraInfo.GetConnectionInfo() << ")" << std::endl;
  }
  else {
    std::cout << "[ERROR] Failed to create " << typeName << " camera: " << cameraInfo.id << std::endl;
  }
}

// ============================================================================
// CAMERA MANAGEMENT - ADD/REMOVE CAMERAS
// ============================================================================

bool CameraManager::AddCamera(const CameraInfo& cameraInfo) {
  // Convert CameraInfo to ExtendedCameraInfo with default Pylon type for backward compatibility
  ExtendedCameraInfo extendedInfo(cameraInfo, ICameraHardware::CameraType::PYLON);
  return AddCamera(extendedInfo);
}

bool CameraManager::AddCamera(const ExtendedCameraInfo& cameraInfo) {
  std::lock_guard<std::mutex> lock(m_camerasMutex);

  // Check if camera already exists
  if (m_cameras.find(cameraInfo.id) != m_cameras.end()) {
    std::cout << "[WARNING] Camera already exists: " << cameraInfo.id << std::endl;
    return false;
  }

  try {
    auto managedCamera = std::make_unique<ManagedCamera>(cameraInfo);
    if (!managedCamera->camera) {
      std::cout << "[ERROR] Failed to create camera instance for: " << cameraInfo.id << std::endl;
      return false;
    }

    // Set up frame callback for broadcasting
    managedCamera->camera->SetFrameCallback([this](const CameraFrameData& frameData) {
      BroadcastFrame(frameData);
    });

    m_cameras[cameraInfo.id] = std::move(managedCamera);

    std::string typeStr = CameraHardwareFactory::GetCameraTypeName(cameraInfo.type);
    std::cout << "[INFO] Added " << typeStr << " camera: " << cameraInfo.id << std::endl;

    return true;

  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] Exception adding camera [" << cameraInfo.id << "]: " << e.what() << std::endl;
    return false;
  }
}

bool CameraManager::AddPylonCamera(const CameraInfo& cameraInfo) {
  ExtendedCameraInfo extendedInfo(cameraInfo, ICameraHardware::CameraType::PYLON);
  return AddCamera(extendedInfo);
}

bool CameraManager::AddIDSCamera(const CameraInfo& cameraInfo, int deviceId) {
  ExtendedCameraInfo extendedInfo(cameraInfo, ICameraHardware::CameraType::IDS, std::to_string(deviceId));
  return AddCamera(extendedInfo);
}

bool CameraManager::AddCameraAutoDetect(const CameraInfo& cameraInfo, const std::string& deviceInfo) {
  ICameraHardware::CameraType detectedType = CameraHardwareFactory::DetectCameraType(deviceInfo);

  if (detectedType == ICameraHardware::CameraType::UNKNOWN) {
    std::cout << "[WARNING] Could not auto-detect camera type for: " << cameraInfo.id << ", defaulting to Pylon" << std::endl;
    detectedType = ICameraHardware::CameraType::PYLON;
  }

  ExtendedCameraInfo extendedInfo(cameraInfo, detectedType, deviceInfo);
  return AddCamera(extendedInfo);
}

bool CameraManager::RemoveCamera(const std::string& cameraId) {
  std::lock_guard<std::mutex> lock(m_camerasMutex);

  auto it = m_cameras.find(cameraId);
  if (it != m_cameras.end()) {
    // Stop camera if running
    if (it->second->camera) {
      if (it->second->camera->IsGrabbing()) {
        it->second->camera->StopGrabbing();
      }
      if (it->second->camera->IsConnected()) {
        it->second->camera->Disconnect();
      }
    }

    m_cameras.erase(it);

    std::cout << "[INFO] Removed camera: " << cameraId << std::endl;
    return true;
  }

  return false;
}

// ============================================================================
// CAMERA ACCESS METHODS
// ============================================================================

ICameraHardware* CameraManager::GetCameraHardware(const std::string& cameraId) {
  std::lock_guard<std::mutex> lock(m_camerasMutex);

  auto it = m_cameras.find(cameraId);
  if (it != m_cameras.end()) {
    return it->second->camera.get();
  }
  return nullptr;
}

std::vector<std::string> CameraManager::GetCameraIds() const {
  std::lock_guard<std::mutex> lock(m_camerasMutex);

  std::vector<std::string> ids;
  for (const auto& [id, camera] : m_cameras) {
    ids.push_back(id);
  }
  return ids;
}

std::vector<std::string> CameraManager::GetCameraIdsByType(ICameraHardware::CameraType type) const {
  std::lock_guard<std::mutex> lock(m_camerasMutex);

  std::vector<std::string> result;
  for (const auto& [id, camera] : m_cameras) {
    if (camera->info.type == type) {
      result.push_back(id);
    }
  }
  return result;
}

// ============================================================================
// CAMERA OPERATIONS
// ============================================================================

bool CameraManager::InitializeAllCameras() {
  std::lock_guard<std::mutex> lock(m_camerasMutex);

  bool allSuccess = true;
  for (const auto& [id, managedCamera] : m_cameras) {
    if (managedCamera->camera) {
      if (!managedCamera->camera->Initialize()) {
        allSuccess = false;
        std::cout << "[ERROR] Failed to initialize camera: " << id << std::endl;
        if (!managedCamera->camera->Connect())
        {
          std::cout << "[ERROR] Failed to connect camera: " << id << std::endl;
        }
        // Try to connect anyway for error reporting

      }
    }
  }

  return allSuccess;
}

bool CameraManager::ConnectCamera(const std::string& cameraId) {
  // Get camera without holding lock to avoid deadlock
  ICameraHardware* camera = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_camerasMutex);
    auto it = m_cameras.find(cameraId);
    if (it != m_cameras.end()) {
      camera = it->second->camera.get();
    }
  }

  if (camera) {
    return camera->Initialize() && camera->Connect();
  }
  return false;
}

bool CameraManager::DisconnectCamera(const std::string& cameraId) {
  // Get camera without holding lock to avoid deadlock
  ICameraHardware* camera = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_camerasMutex);
    auto it = m_cameras.find(cameraId);
    if (it != m_cameras.end()) {
      camera = it->second->camera.get();
    }
  }

  if (camera) {
    return camera->Disconnect();
  }
  return false;
}

bool CameraManager::ConnectAllCameras() {
  // Get list of camera IDs first (without holding lock during connection)
  std::vector<std::string> cameraIds;
  {
    std::lock_guard<std::mutex> lock(m_camerasMutex);
    for (const auto& [id, managedCamera] : m_cameras) {
      cameraIds.push_back(id);
    }
  }

  bool allSuccess = true;
  for (const auto& cameraId : cameraIds) {
    std::cout << "[INFO] Connecting camera: " << cameraId << std::endl;

    if (!ConnectCamera(cameraId)) {
      allSuccess = false;
      std::cout << "[ERROR] Failed to connect camera: " << cameraId << std::endl;
    }
    else {
      std::cout << "[INFO] Successfully connected camera: " << cameraId << std::endl;
    }

    // Add 500ms delay between camera connections
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  return allSuccess;
}

bool CameraManager::StartGrabbing(const std::string& cameraId) {
  // Get camera without holding lock to avoid deadlock
  ICameraHardware* camera = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_camerasMutex);
    auto it = m_cameras.find(cameraId);
    if (it != m_cameras.end()) {
      camera = it->second->camera.get();
    }
  }

  if (camera) {
    return camera->StartGrabbing();
  }
  return false;
}

bool CameraManager::StopGrabbing(const std::string& cameraId) {
  // Get camera without holding lock to avoid deadlock
  ICameraHardware* camera = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_camerasMutex);
    auto it = m_cameras.find(cameraId);
    if (it != m_cameras.end()) {
      camera = it->second->camera.get();
    }
  }

  if (camera) {
    return camera->StopGrabbing();
  }
  return false;
}

bool CameraManager::StartGrabbingAll() {
  std::lock_guard<std::mutex> lock(m_camerasMutex);

  bool allSuccess = true;
  for (const auto& [id, managedCamera] : m_cameras) {
    if (managedCamera->camera && managedCamera->camera->IsConnected()) {
      if (!managedCamera->camera->StartGrabbing()) {
        allSuccess = false;
        std::cout << "[ERROR] Failed to start grabbing for camera: " << id << std::endl;
      }
    }
  }

  return allSuccess;
}

bool CameraManager::StopGrabbingAll() {
  std::lock_guard<std::mutex> lock(m_camerasMutex);

  bool allSuccess = true;
  for (const auto& [id, managedCamera] : m_cameras) {
    if (managedCamera->camera && managedCamera->camera->IsGrabbing()) {
      if (!managedCamera->camera->StopGrabbing()) {
        allSuccess = false;
        std::cout << "[ERROR] Failed to stop grabbing for camera: " << id << std::endl;
      }
    }
  }

  return allSuccess;
}

bool CameraManager::StartGrabbingWithBroadcast(const std::string& cameraId) {
  // Start broadcasting system if not running
  if (!m_broadcastSystemRunning.load()) {
    StartBroadcastSystem();
  }

  return StartGrabbing(cameraId);
}

// ============================================================================
// EXPOSURE CONTROL - Interface-based only
// ============================================================================

bool CameraManager::ApplyExposureSettings(const std::string& cameraId, const ICameraHardware::ExposureSettings& settings) {
  auto camera = GetCameraHardware(cameraId);
  if (camera) {
    std::cout << "[INFO] Applying exposure settings to camera: " << cameraId << std::endl;
    std::cout << "[INFO]   Exposure: " << settings.exposure_time << " μs" << std::endl;
    std::cout << "[INFO]   Gain: " << settings.gain << std::endl;
    std::cout << "[INFO]   Auto Exposure: " << (settings.auto_exposure ? "On" : "Off") << std::endl;
    std::cout << "[INFO]   Auto Gain: " << (settings.auto_gain ? "On" : "Off") << std::endl;
    return camera->SetExposureSettings(settings);
  }
  return false;
}

ICameraHardware::ExposureSettings CameraManager::GetExposureSettings(const std::string& cameraId) const {
  auto camera = const_cast<CameraManager*>(this)->GetCameraHardware(cameraId);
  if (camera) {
    return camera->GetExposureSettings();
  }
  return ICameraHardware::ExposureSettings(); // Return default settings
}

// ============================================================================
// CAMERA STATUS
// ============================================================================

CameraManager::CameraStatus CameraManager::GetCameraStatus(const std::string& cameraId) const {
  CameraStatus status;
  status.id = cameraId;
  status.type = ICameraHardware::CameraType::UNKNOWN;
  status.connected = false;
  status.grabbing = false;
  status.deviceRemoved = true;

  std::lock_guard<std::mutex> lock(m_camerasMutex);
  auto it = m_cameras.find(cameraId);
  if (it != m_cameras.end()) {
    const auto& managedCamera = it->second;
    const auto& camera = managedCamera->camera;

    status.type = managedCamera->info.type;
    status.deviceInfo = managedCamera->info.deviceInfo;

    if (camera) {
      status.connected = camera->IsConnected();
      status.grabbing = camera->IsGrabbing();
      status.deviceRemoved = camera->IsDeviceRemoved();
      status.modelName = camera->GetModelName();
      status.serialNumber = camera->GetSerialNumber();
      status.currentExposure = camera->GetExposureSettings();
    }
  }

  return status;
}

std::vector<CameraManager::CameraStatus> CameraManager::GetAllCameraStatus() const {
  std::vector<CameraStatus> statuses;

  std::lock_guard<std::mutex> lock(m_camerasMutex);

  // Direct iteration without calling GetCameraStatus to avoid nested locking
  for (const auto& [id, managedCamera] : m_cameras) {
    CameraStatus status;
    status.id = id;
    status.type = ICameraHardware::CameraType::UNKNOWN;
    status.connected = false;
    status.grabbing = false;
    status.deviceRemoved = true;

    if (managedCamera) {
      const auto& camera = managedCamera->camera;

      status.type = managedCamera->info.type;
      status.deviceInfo = managedCamera->info.deviceInfo;

      if (camera) {
        status.connected = camera->IsConnected();
        status.grabbing = camera->IsGrabbing();
        status.deviceRemoved = camera->IsDeviceRemoved();
        status.modelName = camera->GetModelName();
        status.serialNumber = camera->GetSerialNumber();
        status.currentExposure = camera->GetExposureSettings();
      }
    }

    statuses.push_back(status);
  }

  return statuses;
}

// ============================================================================
// CAMERA CONFIGURATION
// ============================================================================

bool CameraManager::SetCameraConfiguration(const std::string& cameraId, const std::string& key, const std::string& value) {
  auto camera = GetCameraHardware(cameraId);
  if (camera) {
    return camera->SetConfiguration(key, value);
  }
  return false;
}

std::string CameraManager::GetCameraConfiguration(const std::string& cameraId, const std::string& key) const {
  auto camera = const_cast<CameraManager*>(this)->GetCameraHardware(cameraId);
  if (camera) {
    return camera->GetConfiguration(key);
  }
  return "";
}

// ============================================================================
// BROADCASTING SYSTEM
// ============================================================================

void CameraManager::StartBroadcastSystem() {
  if (m_broadcastSystemRunning.exchange(true)) {
    return; // Already running
  }

  m_shouldStopBroadcast.store(false);
  m_broadcastThread = std::thread(&CameraManager::BroadcastThreadFunction, this);

  std::cout << "[INFO] Camera broadcasting system started" << std::endl;
}

void CameraManager::StopBroadcastSystem() {
  if (!m_broadcastSystemRunning.exchange(false)) {
    return; // Already stopped
  }

  m_shouldStopBroadcast.store(true);

  if (m_broadcastThread.joinable()) {
    m_broadcastThread.join();
  }

  std::cout << "[INFO] Camera broadcasting system stopped" << std::endl;
}

void CameraManager::SubscribeToFrames(std::shared_ptr<CameraFrameSubscriber> subscriber) {
  if (!subscriber) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_subscribersMutex);
  m_subscribers[subscriber->GetSubscriberId()] = SubscriberInfo(subscriber);

  std::cout << "[INFO] Subscriber added: " << subscriber->GetSubscriberId() << std::endl;
}

void CameraManager::UnsubscribeFromFrames(const std::string& subscriberId) {
  std::lock_guard<std::mutex> lock(m_subscribersMutex);

  auto it = m_subscribers.find(subscriberId);
  if (it != m_subscribers.end()) {
    m_subscribers.erase(it);

    std::cout << "[INFO] Subscriber removed: " << subscriberId << std::endl;
  }
}

void CameraManager::SetGlobalBroadcastRate(float fps) {
  m_globalBroadcastRate = fps;
}

size_t CameraManager::GetSubscriberCount() const {
  std::lock_guard<std::mutex> lock(m_subscribersMutex);
  return m_subscribers.size();
}

std::vector<std::string> CameraManager::GetSubscriberIds() const {
  std::lock_guard<std::mutex> lock(m_subscribersMutex);

  std::vector<std::string> ids;
  for (const auto& [id, info] : m_subscribers) {
    ids.push_back(id);
  }
  return ids;
}

void CameraManager::BroadcastFrame(const CameraFrameData& frameData) {
  std::lock_guard<std::mutex> lock(m_subscribersMutex);

  auto now = std::chrono::steady_clock::now();

  for (auto& [id, subscriberInfo] : m_subscribers) {
    auto subscriber = subscriberInfo.subscriber;
    if (!subscriber) {
      continue;
    }

    // Check if subscriber wants frames from this camera
    if (!subscriber->WantsFramesFromCamera(frameData.cameraId)) {
      continue;
    }

    // Check rate limiting
    int minInterval = subscriber->GetMinFrameIntervalMs();
    if (minInterval > 0) {
      auto timeSinceLastBroadcast = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - subscriberInfo.lastBroadcast).count();

      if (timeSinceLastBroadcast < minInterval) {
        continue; // Skip this frame for rate limiting
      }
    }

    // Send frame to subscriber
    try {
      subscriber->OnNewFrame(frameData);
      subscriberInfo.lastBroadcast = now;
    }
    catch (const std::exception& e) {
      std::cout << "[ERROR] Exception in subscriber [" << id << "]: " << e.what() << std::endl;
    }
  }
}

void CameraManager::BroadcastThreadFunction() {
  std::cout << "[INFO] Camera broadcast thread started" << std::endl;

  while (!m_shouldStopBroadcast.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // The actual broadcasting is triggered by frame callbacks
    // This thread just keeps the system alive
  }

  std::cout << "[INFO] Camera broadcast thread stopped" << std::endl;
}

// ============================================================================
// CAMERA DISCOVERY - Using SDKs directly (no test classes)
// ============================================================================

std::vector<CameraInfo> CameraManager::DiscoverPylonCameras() {
  std::vector<CameraInfo> pylonCameras;

  std::cout << "[INFO] Discovering Pylon cameras..." << std::endl;

  try {
    // Initialize Pylon if not already done
    Pylon::PylonInitialize();

    // Get the transport layer factory
    Pylon::CTlFactory& tlFactory = Pylon::CTlFactory::GetInstance();

    // Enumerate all devices
    Pylon::DeviceInfoList_t devices;
    size_t numDevices = tlFactory.EnumerateDevices(devices);

    std::cout << "[INFO] Found " << numDevices << " Pylon cameras" << std::endl;

    for (size_t i = 0; i < numDevices; i++) {
      const auto& device = devices[i];

      std::string modelName = device.GetModelName().c_str();
      std::string serialNumber = device.GetSerialNumber().c_str();
      std::string deviceClass = device.GetDeviceClass().c_str();

      // Create camera info
      std::string cameraId = "Pylon_" + modelName + "_" + serialNumber;
      std::string description = modelName + " (" + deviceClass + ")";

      CameraInfo cameraInfo(cameraId, serialNumber, description);

      // Determine connection method based on device class
      if (deviceClass.find("GigE") != std::string::npos) {
        // For GigE cameras, try to get IP address
        try {
          // Create temporary camera to get IP info
          Pylon::CInstantCamera tempCamera;
          tempCamera.Attach(tlFactory.CreateDevice(device));
          tempCamera.Open();

          if (tempCamera.GetTLNodeMap().GetNode("GevCurrentIPAddress")) {
            Pylon::CIntegerParameter ipParam(tempCamera.GetTLNodeMap(), "GevCurrentIPAddress");
            if (ipParam.IsReadable()) {
              int64_t ipValue = ipParam.GetValue();
              int a = (ipValue >> 24) & 0xFF;
              int b = (ipValue >> 16) & 0xFF;
              int c = (ipValue >> 8) & 0xFF;
              int d = ipValue & 0xFF;
              std::string ipAddress = std::to_string(a) + "." + std::to_string(b) + "." +
                std::to_string(c) + "." + std::to_string(d);

              // Create IP-based camera info
              cameraInfo = CameraInfo::CreateByIP(cameraId, ipAddress, description);
            }
          }

          tempCamera.Close();
        }
        catch (...) {
          std::cout << "[WARNING] Failed to get IP for GigE camera " << cameraId << ", using serial" << std::endl;
        }
      }

      pylonCameras.push_back(cameraInfo);

      std::cout << "[INFO] Discovered Pylon camera: " << cameraId << " [" << cameraInfo.GetConnectionInfo() << "]" << std::endl;
    }
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] Exception during Pylon camera discovery: " << e.what() << std::endl;
  }

  return pylonCameras;
}

std::vector<ExtendedCameraInfo> CameraManager::DiscoverIDSCameras() {
  std::vector<ExtendedCameraInfo> idsCameras;

  std::cout << "[INFO] Discovering IDS cameras..." << std::endl;

  try {
    // Use IDS SDK directly - no test class
    INT numCameras = 0;
    is_GetNumberOfCameras(&numCameras);

    std::cout << "[INFO] Found " << numCameras << " IDS cameras" << std::endl;

    if (numCameras > 0) {
      UEYE_CAMERA_LIST* cameraList = (UEYE_CAMERA_LIST*)malloc(
        sizeof(DWORD) + numCameras * sizeof(UEYE_CAMERA_INFO));
      cameraList->dwCount = numCameras;

      if (is_GetCameraList(cameraList) == IS_SUCCESS) {
        for (DWORD i = 0; i < cameraList->dwCount; i++) {
          int deviceId = cameraList->uci[i].dwCameraID;
          std::string serial = cameraList->uci[i].SerNo;

          // Create camera info
          CameraInfo baseInfo("IDS_Camera_" + std::to_string(deviceId),
            "IDS Camera (ID: " + std::to_string(deviceId) + ")");

          ExtendedCameraInfo cameraInfo(baseInfo, ICameraHardware::CameraType::IDS,
            std::to_string(deviceId));
          cameraInfo.serialNumber = serial;

          idsCameras.push_back(cameraInfo);

          std::cout << "[INFO] Discovered IDS camera: ID=" << deviceId << ", Serial=" << serial << std::endl;
        }
      }
      free(cameraList);
    }
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] Exception during IDS camera discovery: " << e.what() << std::endl;
  }

  return idsCameras;
}

std::vector<ExtendedCameraInfo> CameraManager::DiscoverAllCameras() {
  std::vector<ExtendedCameraInfo> allCameras;

  std::cout << "[INFO] Discovering all cameras..." << std::endl;

  // Discover Pylon cameras (convert CameraInfo to ExtendedCameraInfo)
  try {
    auto pylonCameras = DiscoverPylonCameras();
    for (const auto& pylonCamera : pylonCameras) {
      ExtendedCameraInfo extendedInfo(pylonCamera, ICameraHardware::CameraType::PYLON);
      allCameras.push_back(extendedInfo);
    }
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] Error discovering Pylon cameras: " << e.what() << std::endl;
  }

  // Discover IDS cameras
  try {
    auto idsCameras = DiscoverIDSCameras();
    allCameras.insert(allCameras.end(), idsCameras.begin(), idsCameras.end());
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] Error discovering IDS cameras: " << e.what() << std::endl;
  }

  std::cout << "[INFO] Total cameras discovered: " << allCameras.size() << std::endl;

  return allCameras;
}

bool CameraManager::DiscoverAndAddAllCameras() {
  std::cout << "[INFO] Starting camera discovery and addition..." << std::endl;

  auto discoveredCameras = DiscoverAllCameras();

  bool allSuccess = true;
  for (const auto& cameraInfo : discoveredCameras) {
    if (!AddCamera(cameraInfo)) {
      allSuccess = false;
      std::cout << "[WARNING] Failed to add discovered camera: " << cameraInfo.id << std::endl;
    }
  }

  std::cout << "[INFO] Camera discovery completed. Added " << GetCameraCount() << " cameras." << std::endl;

  return allSuccess;
}

// ============================================================================
// IMAGE CAPTURE AND SAVING
// ============================================================================

bool CameraManager::SaveFrameToBMP(const CameraFrameData& frameData, const std::string& filePath) {
  if (!frameData.IsValid()) {
    std::cout << "[ERROR] Cannot save invalid frame data to BMP" << std::endl;
    return false;
  }

  // Only support RGB images for now
  if (frameData.channels != 3) {
    std::cout << "[ERROR] BMP save only supports RGB images (3 channels), got " << frameData.channels << " channels" << std::endl;
    return false;
  }

  try {
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
      std::cout << "[ERROR] Failed to open file for writing: " << filePath << std::endl;
      return false;
    }

    // Calculate padded row size (BMP rows must be aligned to 4 bytes)
    int rowSize = ((frameData.width * 3 + 3) / 4) * 4;
    int paddingSize = rowSize - (frameData.width * 3);
    uint32_t imageSize = rowSize * frameData.height;

    // Prepare BMP headers
    BMPFileHeader fileHeader;
    fileHeader.size = sizeof(BMPFileHeader) + sizeof(BMPInfoHeader) + imageSize;

    BMPInfoHeader infoHeader;
    infoHeader.width = frameData.width;
    infoHeader.height = frameData.height;
    infoHeader.imageSize = imageSize;

    // Write headers
    file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    file.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));

    // Write pixel data (BMP stores images bottom-to-top, BGR format)
    std::vector<uint8_t> padding(paddingSize, 0);

    for (int y = frameData.height - 1; y >= 0; y--) {
      for (int x = 0; x < static_cast<int>(frameData.width); x++) {
        int idx = (y * frameData.width + x) * 3;
        // Convert RGB to BGR for BMP format
        uint8_t b = frameData.imageData[idx + 2];
        uint8_t g = frameData.imageData[idx + 1];
        uint8_t r = frameData.imageData[idx];

        file.write(reinterpret_cast<const char*>(&b), 1);
        file.write(reinterpret_cast<const char*>(&g), 1);
        file.write(reinterpret_cast<const char*>(&r), 1);
      }
      // Write padding bytes
      if (paddingSize > 0) {
        file.write(reinterpret_cast<const char*>(padding.data()), paddingSize);
      }
    }

    file.close();

    std::cout << "[INFO] Saved image to: " << filePath << std::endl;

    return true;
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] Exception saving BMP file: " << e.what() << std::endl;
    return false;
  }
}

std::string CameraManager::GenerateImageFilename(const std::string& cameraId, const std::string& extension) {
  // Get current time for timestamp
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  ss << cameraId << "_";
  std::tm timeinfo;
  localtime_s(&timeinfo, &time_t);
  ss << std::put_time(&timeinfo, "%Y%m%d_%H%M%S");

  // Add milliseconds for uniqueness
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    now.time_since_epoch()) % 1000;
  ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
  ss << "." << extension;

  return ss.str();
}

std::string CameraManager::GetImageOutputDirectory() {
  // Check if custom directory is set
  if (!m_imageOutputDirectory.empty()) {
    return m_imageOutputDirectory;
  }

  // Default to "captured_images" folder in current working directory
  std::string defaultDir = "captured_images";

  // Create directory if it doesn't exist
  try {
    if (!std::filesystem::exists(defaultDir)) {
      std::filesystem::create_directories(defaultDir);

      std::cout << "[INFO] Created image output directory: " << defaultDir << std::endl;
    }
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] Failed to create output directory: " << e.what() << std::endl;
    // Fall back to current directory
    return ".";
  }

  return defaultDir;
}

void CameraManager::SetImageOutputDirectory(const std::string& directory) {
  m_imageOutputDirectory = directory;

  // Try to create the directory if it doesn't exist
  try {
    if (!std::filesystem::exists(directory)) {
      std::filesystem::create_directories(directory);

      std::cout << "[INFO] Created image output directory: " << directory << std::endl;
    }
  }
  catch (const std::exception& e) {
    std::cout << "[ERROR] Failed to create output directory: " << e.what() << std::endl;
  }
}

bool CameraManager::CaptureImage(const std::string& cameraId) {
  auto camera = GetCameraHardware(cameraId);
  if (!camera) {
    std::cout << "[ERROR] Camera not found: " << cameraId << std::endl;
    return false;
  }

  // Capture frame from camera
  CameraFrameData frameData;
  frameData.cameraId = cameraId;

  if (!camera->CaptureFrame(frameData)) {
    std::cout << "[ERROR] Failed to capture frame from camera: " << cameraId << std::endl;
    return false;
  }

  // Generate filename with timestamp
  std::string filename = GenerateImageFilename(cameraId, "bmp");
  std::string directory = GetImageOutputDirectory();
  std::string fullPath = directory + "/" + filename;

  // Save to BMP file
  if (!SaveFrameToBMP(frameData, fullPath)) {
    std::cout << "[ERROR] Failed to save captured image to file" << std::endl;
    return false;
  }

  // Store the last captured image path for reference
  m_lastCapturedImagePath = fullPath;

  std::cout << "[INFO] Image captured and saved: " << fullPath << std::endl;

  return true;
}

bool CameraManager::CaptureImage(const std::string& cameraId, const std::string& customFilename) {
  auto camera = GetCameraHardware(cameraId);
  if (!camera) {
    return false;
  }

  CameraFrameData frameData;
  frameData.cameraId = cameraId;

  if (!camera->CaptureFrame(frameData)) {
    return false;
  }

  std::string directory = GetImageOutputDirectory();
  std::string fullPath = directory + "/" + customFilename;

  if (!SaveFrameToBMP(frameData, fullPath)) {
    return false;
  }

  m_lastCapturedImagePath = fullPath;

  std::cout << "[INFO] Image captured with custom name: " << fullPath << std::endl;

  return true;
}

bool CameraManager::CaptureImageAll() {
  std::lock_guard<std::mutex> lock(m_camerasMutex);

  bool allSuccess = true;
  int captureCount = 0;

  for (const auto& [id, managedCamera] : m_cameras) {
    if (managedCamera->camera && managedCamera->camera->IsConnected()) {
      CameraFrameData frameData;
      frameData.cameraId = id;

      if (managedCamera->camera->CaptureFrame(frameData)) {
        // Generate unique filename for each camera
        std::string filename = GenerateImageFilename(id, "bmp");
        std::string directory = GetImageOutputDirectory();
        std::string fullPath = directory + "/" + filename;

        if (SaveFrameToBMP(frameData, fullPath)) {
          captureCount++;
          std::cout << "[INFO] Captured image from " << id << ": " << fullPath << std::endl;
        }
        else {
          allSuccess = false;
        }
      }
      else {
        allSuccess = false;
        std::cout << "[ERROR] Failed to capture frame from camera: " << id << std::endl;
      }
    }
  }

  std::cout << "[INFO] Captured " << captureCount << " images from all cameras" << std::endl;

  return allSuccess;
}
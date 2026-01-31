#pragma once

#include "CameraFrameData.h"
#include "ICameraHardware.h"
#include "CameraHardwareFactory.h"
#include "ILogger.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <set>
#include <mutex>

// Forward declaration
class CameraFrameSubscriber;

// Camera identification structure
struct CameraInfo {
  std::string id;
  std::string serialNumber;
  std::string ipAddress;
  std::string description;
  bool autoConnect;

  enum class ConnectionMethod {
    AUTO,
    SERIAL_NUMBER,
    IP_ADDRESS,
    DEVICE_INDEX
  };

  ConnectionMethod connectionMethod;
  int deviceIndex = 0;

  CameraInfo(const std::string& cameraId, const std::string& desc = "", bool autoConn = true)
    : id(cameraId), description(desc), autoConnect(autoConn), connectionMethod(ConnectionMethod::AUTO) {
  }

  CameraInfo(const std::string& cameraId, const std::string& serial, const std::string& desc, bool autoConn = true)
    : id(cameraId), serialNumber(serial), description(desc), autoConnect(autoConn),
    connectionMethod(ConnectionMethod::SERIAL_NUMBER) {
  }

  static CameraInfo CreateByIP(const std::string& cameraId, const std::string& ip,
    const std::string& desc = "", bool autoConn = true) {
    CameraInfo info;
    info.id = cameraId;
    info.ipAddress = ip;
    info.description = desc;
    info.autoConnect = autoConn;
    info.connectionMethod = ConnectionMethod::IP_ADDRESS;
    return info;
  }

  static CameraInfo CreateByIndex(const std::string& cameraId, int index,
    const std::string& desc = "", bool autoConn = true) {
    CameraInfo info;
    info.id = cameraId;
    info.deviceIndex = index;
    info.description = desc;
    info.autoConnect = autoConn;
    info.connectionMethod = ConnectionMethod::DEVICE_INDEX;
    return info;
  }

  bool IsIPConnection() const { return connectionMethod == ConnectionMethod::IP_ADDRESS; }
  bool IsSerialConnection() const { return connectionMethod == ConnectionMethod::SERIAL_NUMBER; }
  bool IsIndexConnection() const { return connectionMethod == ConnectionMethod::DEVICE_INDEX; }
  bool IsAutoConnection() const { return connectionMethod == ConnectionMethod::AUTO; }

  std::string GetConnectionInfo() const {
    switch (connectionMethod) {
    case ConnectionMethod::IP_ADDRESS:
      return "IP: " + ipAddress;
    case ConnectionMethod::SERIAL_NUMBER:
      return "Serial: " + serialNumber;
    case ConnectionMethod::DEVICE_INDEX:
      return "Index: " + std::to_string(deviceIndex);
    default:
      return "Auto";
    }
  }

private:
  CameraInfo() : autoConnect(true), connectionMethod(ConnectionMethod::AUTO), deviceIndex(0) {}
};

// Extended camera info with type support
struct ExtendedCameraInfo : public CameraInfo {
  ICameraHardware::CameraType type = ICameraHardware::CameraType::UNKNOWN;
  std::string deviceInfo;

  ExtendedCameraInfo() = default;
  ExtendedCameraInfo(const CameraInfo& baseInfo, ICameraHardware::CameraType camType, const std::string& devInfo = "")
    : CameraInfo(baseInfo), type(camType), deviceInfo(devInfo) {
  }
};

/**
 * @brief Multi-camera manager - Business Logic Layer
 *
 * Pure business logic for camera management. No UI code, no test dependencies.
 * Uses ICameraHardware interface for hardware abstraction.
 */
class CameraManager {
public:
  CameraManager();
  ~CameraManager();

  // Logger support
  void SetLogger(ILogger* logger) { m_logger = logger; }

  // ============================================================================
  // CAMERA MANAGEMENT
  // ============================================================================

  bool AddCamera(const CameraInfo& cameraInfo);
  bool AddCamera(const ExtendedCameraInfo& cameraInfo);
  bool AddPylonCamera(const CameraInfo& cameraInfo);
  bool AddIDSCamera(const CameraInfo& cameraInfo, int deviceId = 0);
  bool AddCameraAutoDetect(const CameraInfo& cameraInfo, const std::string& deviceInfo);
  bool RemoveCamera(const std::string& cameraId);

  ICameraHardware* GetCameraHardware(const std::string& cameraId);

  std::vector<std::string> GetCameraIds() const;
  std::vector<std::string> GetCameraIdsByType(ICameraHardware::CameraType type) const;
  size_t GetCameraCount() const { return m_cameras.size(); }

  // ============================================================================
  // CAMERA OPERATIONS
  // ============================================================================

  bool InitializeAllCameras();
  bool ConnectCamera(const std::string& cameraId);
  bool ConnectAllCameras();
  bool DisconnectCamera(const std::string& cameraId);
  bool StartGrabbingAll();
  bool StopGrabbingAll();
  bool StartGrabbing(const std::string& cameraId);
  bool StopGrabbing(const std::string& cameraId);
  bool StartGrabbingWithBroadcast(const std::string& cameraId);

  // ============================================================================
  // EXPOSURE CONTROL
  // ============================================================================

  bool ApplyExposureSettings(const std::string& cameraId, const ICameraHardware::ExposureSettings& settings);
  ICameraHardware::ExposureSettings GetExposureSettings(const std::string& cameraId) const;

  // ============================================================================
  // CAMERA STATUS
  // ============================================================================

  struct CameraStatus {
    std::string id;
    ICameraHardware::CameraType type;
    bool connected;
    bool grabbing;
    bool deviceRemoved;
    std::string deviceInfo;
    std::string modelName;
    std::string serialNumber;
    ICameraHardware::ExposureSettings currentExposure;
  };

  CameraStatus GetCameraStatus(const std::string& cameraId) const;
  std::vector<CameraStatus> GetAllCameraStatus() const;

  // ============================================================================
  // CONFIGURATION
  // ============================================================================

  bool SetCameraConfiguration(const std::string& cameraId, const std::string& key, const std::string& value);
  std::string GetCameraConfiguration(const std::string& cameraId, const std::string& key) const;

  // ============================================================================
  // BROADCASTING SYSTEM
  // ============================================================================

  void StartBroadcastSystem();
  void StopBroadcastSystem();
  void SubscribeToFrames(std::shared_ptr<CameraFrameSubscriber> subscriber);
  void UnsubscribeFromFrames(const std::string& subscriberId);
  void SetGlobalBroadcastRate(float fps);
  size_t GetSubscriberCount() const;
  std::vector<std::string> GetSubscriberIds() const;

  // ============================================================================
  // CAMERA DISCOVERY
  // ============================================================================

  static std::vector<ExtendedCameraInfo> DiscoverAllCameras();
  static std::vector<CameraInfo> DiscoverPylonCameras();
  static std::vector<ExtendedCameraInfo> DiscoverIDSCameras();
  bool DiscoverAndAddAllCameras();

  // ============================================================================
  // IMAGE CAPTURE
  // ============================================================================

  bool CaptureImage(const std::string& cameraId);
  bool CaptureImage(const std::string& cameraId, const std::string& customFilename);
  bool CaptureImageAll();
  void SetImageOutputDirectory(const std::string& directory);
  std::string GetImageOutputDirectory();
  std::string GetLastCapturedImagePath() const { return m_lastCapturedImagePath; }

private:
  // ============================================================================
  // INTERNAL STRUCTURES
  // ============================================================================

  struct ManagedCamera {
    ExtendedCameraInfo info;
    std::unique_ptr<ICameraHardware> camera;

    ManagedCamera(const ExtendedCameraInfo& cameraInfo);
  };

  struct SubscriberInfo {
    std::shared_ptr<CameraFrameSubscriber> subscriber;
    std::chrono::steady_clock::time_point lastBroadcast;
    std::set<std::string> interestedCameras;

    SubscriberInfo()
      : subscriber(nullptr), lastBroadcast(std::chrono::steady_clock::now()) {
    }

    SubscriberInfo(std::shared_ptr<CameraFrameSubscriber> sub)
      : subscriber(sub), lastBroadcast(std::chrono::steady_clock::now()) {
    }

    bool WantsFramesFrom(const std::string& cameraId) const {
      return subscriber && subscriber->WantsFramesFromCamera(cameraId);
    }
  };

  // ============================================================================
  // MEMBER VARIABLES
  // ============================================================================

  mutable std::mutex m_camerasMutex;
  std::unordered_map<std::string, std::unique_ptr<ManagedCamera>> m_cameras;

  mutable std::mutex m_subscribersMutex;
  std::unordered_map<std::string, SubscriberInfo> m_subscribers;
  std::atomic<bool> m_broadcastSystemRunning;
  std::atomic<bool> m_shouldStopBroadcast;
  std::thread m_broadcastThread;
  float m_globalBroadcastRate;

  std::string m_imageOutputDirectory;
  std::string m_lastCapturedImagePath;

  // Logger for spdlog integration
  ILogger* m_logger = nullptr;

  // ============================================================================
  // HELPER METHODS
  // ============================================================================

  void BroadcastFrame(const CameraFrameData& frameData);
  void BroadcastThreadFunction();

  bool SaveFrameToBMP(const CameraFrameData& frameData, const std::string& filePath);
  std::string GenerateImageFilename(const std::string& cameraId, const std::string& extension);
};
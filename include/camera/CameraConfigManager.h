#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

// Forward declarations
class CameraManager;
class Logger;
struct CameraInfo;           // This is the existing CameraInfo from CameraManager.h
struct ExtendedCameraInfo;   // This is the ExtendedCameraInfo from CameraManager.h

// Structure to hold camera configuration data (renamed to avoid conflict with CameraManager::CameraInfo)
struct CameraConfigData {
  std::string id;
  std::string displayName;
  std::string connectionType;  // "ip", "auto", "serial", "index"
  std::string cameraType;      // "PYLON" (or "BASLER") or "IDS" - NEW FIELD
  std::string ipAddress;
  std::string serialNumber;    // NEW: Add explicit serial number field
  int port;
  bool enabled;
  bool autoConnect;
  std::string description;

  // Optional camera-specific settings
  int exposureTime;
  double gain;
  int width;
  int height;

  // Default constructor
  CameraConfigData()
    : port(0), enabled(true), autoConnect(true),
    exposureTime(1000), gain(1.0), width(0), height(0),
    cameraType("PYLON") {  // Default to PYLON for backward compatibility
  }

  // Helper methods
  bool IsPylonCamera() const { return cameraType == "PYLON" || cameraType == "BASLER"; }
  bool IsIDSCamera() const { return cameraType == "IDS"; }
};

// Structure to hold manager-level settings
struct CameraManagerSettings {
  bool autoConnect;
  int defaultExposureTime;
  double defaultGain;
  bool broadcastEnabled;
  int maxSubscribers;
  int defaultWidth;
  int defaultHeight;
  std::string logLevel;

  // Default constructor
  CameraManagerSettings()
    : autoConnect(true), defaultExposureTime(1000), defaultGain(1.0),
    broadcastEnabled(true), maxSubscribers(10), defaultWidth(0),
    defaultHeight(0), logLevel("INFO") {
  }
};

class CameraConfigManager {
public:
  // Constructor
  explicit CameraConfigManager(const std::string& configFilePath = "camera_config.json");

  // Destructor
  ~CameraConfigManager() = default;

  // Core functionality
  bool LoadConfig();
  bool LoadConfig(const std::string& configFilePath);
  bool SaveConfig() const;
  bool SaveConfig(const std::string& configFilePath) const;

  // Initialize CameraManager with loaded configuration
  bool InitializeCameraManager(CameraManager& cameraManager) const;

  // Configuration access
  const std::vector<CameraConfigData>& GetCameraConfigs() const { return cameraConfigs_; }
  const CameraManagerSettings& GetManagerSettings() const { return managerSettings_; }

  // Individual camera config access
  bool GetCameraConfig(const std::string& cameraId, CameraConfigData& config) const;
  bool UpdateCameraConfig(const std::string& cameraId, const CameraConfigData& config);
  bool AddCameraConfig(const CameraConfigData& config);
  bool RemoveCameraConfig(const std::string& cameraId);

  // Manager settings access
  void SetManagerSettings(const CameraManagerSettings& settings) { managerSettings_ = settings; }

  // Utility functions
  std::vector<std::string> GetEnabledCameraIds() const;
  std::vector<std::string> GetAutoConnectCameraIds() const;
  int GetEnabledCameraCount() const;
  bool IsCameraEnabled(const std::string& cameraId) const;

  // Convenience methods for common operations
  bool SetCameraIPAddress(const std::string& cameraId, const std::string& ipAddress);
  bool EnableCamera(const std::string& cameraId, bool enabled = true);
  bool SetCameraAutoConnect(const std::string& cameraId, bool autoConnect);

  // Validation
  bool ValidateConfig() const;
  std::vector<std::string> GetValidationErrors() const;

  // Status and logging
  void SetLogger(Logger* logger) { logger_ = logger; }
  std::string GetConfigFilePath() const { return configFilePath_; }
  bool IsConfigLoaded() const { return configLoaded_; }

  // Create default configuration
  void CreateDefaultConfig();

  // Helper function to convert CameraConfigData to ExtendedCameraInfo (for CameraManager compatibility)
  // This converts from our config format TO the ExtendedCameraInfo struct
  ExtendedCameraInfo ConvertToExtendedCameraInfo(const CameraConfigData& configData) const;

  // Legacy method for backward compatibility
  CameraInfo ConvertToCameraInfo(const CameraConfigData& configData) const;

private:
  // Helper methods
  bool LoadFromJson(const nlohmann::json& jsonConfig);
  nlohmann::json SaveToJson() const;
  void LogInfo(const std::string& message) const;
  void LogWarning(const std::string& message) const;
  void LogError(const std::string& message) const;

  CameraConfigData ParseCameraConfig(const nlohmann::json& camJson) const;
  CameraManagerSettings ParseManagerSettings(const nlohmann::json& settingsJson) const;

  nlohmann::json CameraConfigToJson(const CameraConfigData& config) const;
  nlohmann::json ManagerSettingsToJson(const CameraManagerSettings& settings) const;

  bool ValidateCameraConfig(const CameraConfigData& config, std::vector<std::string>& errors) const;
  bool ValidateManagerSettings(const CameraManagerSettings& settings, std::vector<std::string>& errors) const;

  // Member variables
  std::string configFilePath_;
  std::vector<CameraConfigData> cameraConfigs_;
  CameraManagerSettings managerSettings_;
  Logger* logger_;
  bool configLoaded_;

  mutable std::vector<std::string> validationErrors_;
};
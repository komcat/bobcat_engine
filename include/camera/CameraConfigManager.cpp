#include <CameraConfigManager.h>
//#include "../include/camera/CameraManager.h"
#include <CameraManager.h>
#include <ICameraHardware.h>
#include <fstream>
#include <algorithm>
#include <iostream>

CameraConfigManager::CameraConfigManager(const std::string& configFilePath)
  : configFilePath_(configFilePath), logger_(nullptr), configLoaded_(false) {
}

bool CameraConfigManager::LoadConfig() {
  // Try local override first
  std::string localPath = "config/camera_config.local.json";
  if (std::filesystem::exists(localPath)) {
    return LoadConfig(localPath);
  }

  // Fall back to default
  return LoadConfig(configFilePath_);
}

bool CameraConfigManager::LoadConfig(const std::string& configFilePath) {
  configFilePath_ = configFilePath;

  try {
    std::ifstream configFile(configFilePath);
    if (!configFile.is_open()) {
      std::cerr << "[CameraConfig] Camera config file not found: " << configFilePath
        << ", creating default configuration" << std::endl;
      CreateDefaultConfig();
      return SaveConfig();
    }

    nlohmann::json jsonConfig;
    configFile >> jsonConfig;
    configFile.close();

    std::cout << "[CameraConfig] Loading camera configuration from: " << configFilePath << std::endl;

    bool success = LoadFromJson(jsonConfig);
    if (success) {
      configLoaded_ = true;
      std::cout << "[CameraConfig] Camera configuration loaded successfully" << std::endl;

      // Validate the loaded configuration
      if (!ValidateConfig()) {
        std::cerr << "[CameraConfig] Camera configuration has validation issues" << std::endl;
        auto errors = GetValidationErrors();
        for (const auto& error : errors) {
          std::cerr << "[CameraConfig] Validation error: " << error << std::endl;
        }
      }
    }

    return success;
  }
  catch (const std::exception& e) {
    std::cerr << "[CameraConfig] Failed to load camera configuration: " << e.what() << std::endl;
    return false;
  }
}

bool CameraConfigManager::SaveConfig() const {
  return SaveConfig(configFilePath_);
}

bool CameraConfigManager::SaveConfig(const std::string& configFilePath) const {
  try {
    nlohmann::json jsonConfig = SaveToJson();

    std::ofstream configFile(configFilePath);
    if (!configFile.is_open()) {
      std::cerr << "[CameraConfig] Failed to open config file for writing: " << configFilePath << std::endl;
      return false;
    }

    configFile << jsonConfig.dump(2);
    configFile.close();

    std::cout << "[CameraConfig] Camera configuration saved to: " << configFilePath << std::endl;
    return true;
  }
  catch (const std::exception& e) {
    std::cerr << "[CameraConfig] Failed to save camera configuration: " << e.what() << std::endl;
    return false;
  }
}

bool CameraConfigManager::InitializeCameraManager(CameraManager& cameraManager) const {
  if (!configLoaded_) {
    std::cerr << "[CameraConfig] Cannot initialize CameraManager: configuration not loaded" << std::endl;
    return false;
  }

  std::cout << "[CameraConfig] Initializing CameraManager with configuration" << std::endl;

  try {
    // Apply manager settings
    std::cout << "[CameraConfig] Applying camera manager settings:" << std::endl;
    std::cout << "[CameraConfig]   - Auto connect: "
      << (managerSettings_.autoConnect ? "enabled" : "disabled") << std::endl;
    std::cout << "[CameraConfig]   - Broadcasting: "
      << (managerSettings_.broadcastEnabled ? "enabled" : "disabled") << std::endl;
    std::cout << "[CameraConfig]   - Max subscribers: " << managerSettings_.maxSubscribers << std::endl;
    std::cout << "[CameraConfig]   - Default exposure: " << managerSettings_.defaultExposureTime << "μs" << std::endl;
    std::cout << "[CameraConfig]   - Default gain: " << managerSettings_.defaultGain << std::endl;

    // Add cameras to manager
    int addedCameras = 0;
    int enabledCameras = 0;

    for (const auto& camConfig : cameraConfigs_) {
      if (!camConfig.enabled) {
        std::cout << "[CameraConfig] Skipping disabled camera: " << camConfig.id << std::endl;
        continue;
      }

      enabledCameras++;

      try {
        ExtendedCameraInfo extendedCamera = ConvertToExtendedCameraInfo(camConfig);

        std::cout << "[CameraConfig] Camera " << camConfig.id << " configured as: "
          << (camConfig.IsIDSCamera() ? "IDS" : "PYLON") << " type" << std::endl;

        std::cout << "[CameraConfig] Adding " << camConfig.connectionType << " camera: " << camConfig.id
          << " (" << (camConfig.ipAddress.empty() ? "auto-detect" : camConfig.ipAddress) << ") - "
          << camConfig.displayName << std::endl;

        cameraManager.AddCamera(extendedCamera);
        addedCameras++;

        std::cout << "[CameraConfig] Successfully added camera: " << camConfig.id << std::endl;

      }
      catch (const std::exception& e) {
        std::cerr << "[CameraConfig] Failed to add camera " << camConfig.id << ": " << e.what() << std::endl;
      }
    }

    std::cout << "[CameraConfig] Camera configuration summary:" << std::endl;
    std::cout << "[CameraConfig]   - Total cameras in config: " << cameraConfigs_.size() << std::endl;
    std::cout << "[CameraConfig]   - Enabled cameras: " << enabledCameras << std::endl;
    std::cout << "[CameraConfig]   - Successfully added: " << addedCameras << std::endl;

    // Initialize all cameras if auto_connect is enabled
    if (managerSettings_.autoConnect && addedCameras > 0) {
      std::cout << "[CameraConfig] Auto-initializing cameras..." << std::endl;
      cameraManager.InitializeAllCameras();

      // Log connection results
      for (const auto& camConfig : cameraConfigs_) {
        if (camConfig.enabled) {
          auto status = cameraManager.GetCameraStatus(camConfig.id);
          if (status.connected) {
            std::cout << "[CameraConfig] ✓ Camera " << camConfig.id << " connected successfully" << std::endl;
          }
          else {
            std::cerr << "[CameraConfig] ✗ Camera " << camConfig.id << " failed to connect" << std::endl;
          }
        }
      }
    }

    return addedCameras > 0;
  }
  catch (const std::exception& e) {
    std::cerr << "[CameraConfig] Exception during CameraManager initialization: " << e.what() << std::endl;
    return false;
  }
}

bool CameraConfigManager::GetCameraConfig(const std::string& cameraId, CameraConfigData& config) const {
  auto it = std::find_if(cameraConfigs_.begin(), cameraConfigs_.end(),
    [&cameraId](const CameraConfigData& cfg) { return cfg.id == cameraId; });

  if (it != cameraConfigs_.end()) {
    config = *it;
    return true;
  }
  return false;
}

bool CameraConfigManager::UpdateCameraConfig(const std::string& cameraId, const CameraConfigData& config) {
  auto it = std::find_if(cameraConfigs_.begin(), cameraConfigs_.end(),
    [&cameraId](const CameraConfigData& cfg) { return cfg.id == cameraId; });

  if (it != cameraConfigs_.end()) {
    *it = config;
    std::cout << "[CameraConfig] Updated camera configuration: " << cameraId << std::endl;
    return true;
  }
  return false;
}

bool CameraConfigManager::AddCameraConfig(const CameraConfigData& config) {
  if (std::any_of(cameraConfigs_.begin(), cameraConfigs_.end(),
    [&config](const CameraConfigData& cfg) { return cfg.id == config.id; })) {
    std::cerr << "[CameraConfig] Camera ID already exists: " << config.id << std::endl;
    return false;
  }

  cameraConfigs_.push_back(config);
  std::cout << "[CameraConfig] Added camera configuration: " << config.id << std::endl;
  return true;
}

bool CameraConfigManager::RemoveCameraConfig(const std::string& cameraId) {
  auto it = std::find_if(cameraConfigs_.begin(), cameraConfigs_.end(),
    [&cameraId](const CameraConfigData& cfg) { return cfg.id == cameraId; });

  if (it != cameraConfigs_.end()) {
    cameraConfigs_.erase(it);
    std::cout << "[CameraConfig] Removed camera configuration: " << cameraId << std::endl;
    return true;
  }
  return false;
}

std::vector<std::string> CameraConfigManager::GetEnabledCameraIds() const {
  std::vector<std::string> enabledIds;
  for (const auto& config : cameraConfigs_) {
    if (config.enabled) {
      enabledIds.push_back(config.id);
    }
  }
  return enabledIds;
}

std::vector<std::string> CameraConfigManager::GetAutoConnectCameraIds() const {
  std::vector<std::string> autoConnectIds;
  for (const auto& config : cameraConfigs_) {
    if (config.enabled && config.autoConnect) {
      autoConnectIds.push_back(config.id);
    }
  }
  return autoConnectIds;
}

int CameraConfigManager::GetEnabledCameraCount() const {
  return static_cast<int>(std::count_if(cameraConfigs_.begin(), cameraConfigs_.end(),
    [](const CameraConfigData& cfg) { return cfg.enabled; }));
}

bool CameraConfigManager::IsCameraEnabled(const std::string& cameraId) const {
  auto it = std::find_if(cameraConfigs_.begin(), cameraConfigs_.end(),
    [&cameraId](const CameraConfigData& cfg) { return cfg.id == cameraId; });

  return it != cameraConfigs_.end() && it->enabled;
}

bool CameraConfigManager::ValidateConfig() const {
  validationErrors_.clear();

  ValidateManagerSettings(managerSettings_, validationErrors_);

  for (const auto& config : cameraConfigs_) {
    ValidateCameraConfig(config, validationErrors_);
  }

  std::vector<std::string> ids;
  for (const auto& config : cameraConfigs_) {
    if (std::find(ids.begin(), ids.end(), config.id) != ids.end()) {
      validationErrors_.push_back("Duplicate camera ID: " + config.id);
    }
    ids.push_back(config.id);
  }

  return validationErrors_.empty();
}

std::vector<std::string> CameraConfigManager::GetValidationErrors() const {
  return validationErrors_;
}

void CameraConfigManager::CreateDefaultConfig() {
  std::cout << "[CameraConfig] Creating default camera configuration" << std::endl;

  managerSettings_ = CameraManagerSettings();
  cameraConfigs_.clear();

  CameraConfigData mainCamera;
  mainCamera.id = "main_camera";
  mainCamera.displayName = "Top view camera";
  mainCamera.connectionType = "ip";
  mainCamera.cameraType = "PYLON";
  mainCamera.ipAddress = "192.168.0.68";
  mainCamera.port = 0;
  mainCamera.enabled = true;
  mainCamera.autoConnect = true;
  mainCamera.description = "Primary overhead camera for top-down imaging";
  cameraConfigs_.push_back(mainCamera);

  CameraConfigData auxCamera;
  auxCamera.id = "aux_camera";
  auxCamera.displayName = "Auxiliary Camera";
  auxCamera.connectionType = "ip";
  auxCamera.cameraType = "PYLON";
  auxCamera.ipAddress = "192.168.0.69";
  auxCamera.port = 0;
  auxCamera.enabled = true;
  auxCamera.autoConnect = true;
  auxCamera.description = "Secondary camera for auxiliary imaging";
  cameraConfigs_.push_back(auxCamera);

  CameraConfigData idsCamera;
  idsCamera.id = "ids_camera_1";
  idsCamera.displayName = "IDS Test Camera";
  idsCamera.connectionType = "index";
  idsCamera.cameraType = "IDS";
  idsCamera.port = 0;
  idsCamera.enabled = false;
  idsCamera.autoConnect = false;
  idsCamera.description = "IDS camera connected via USB";
  cameraConfigs_.push_back(idsCamera);

  configLoaded_ = true;
}

bool CameraConfigManager::LoadFromJson(const nlohmann::json& jsonConfig) {
  try {
    if (jsonConfig.contains("manager_settings")) {
      managerSettings_ = ParseManagerSettings(jsonConfig["manager_settings"]);
    }

    cameraConfigs_.clear();
    if (jsonConfig.contains("cameras")) {
      for (const auto& camJson : jsonConfig["cameras"]) {
        CameraConfigData config = ParseCameraConfig(camJson);
        cameraConfigs_.push_back(config);
      }
    }

    return true;
  }
  catch (const std::exception& e) {
    std::cerr << "[CameraConfig] Error parsing JSON configuration: " << e.what() << std::endl;
    return false;
  }
}

nlohmann::json CameraConfigManager::SaveToJson() const {
  nlohmann::json jsonConfig;

  jsonConfig["manager_settings"] = ManagerSettingsToJson(managerSettings_);

  jsonConfig["cameras"] = nlohmann::json::array();
  for (const auto& config : cameraConfigs_) {
    jsonConfig["cameras"].push_back(CameraConfigToJson(config));
  }

  return jsonConfig;
}

CameraConfigData CameraConfigManager::ParseCameraConfig(const nlohmann::json& camJson) const {
  CameraConfigData config;

  config.id = camJson.value("id", "");
  config.displayName = camJson.value("display_name", "Unknown Camera");
  config.connectionType = camJson.value("connection_type", "auto");
  config.cameraType = camJson.value("camera_type", "PYLON");
  config.ipAddress = camJson.value("ip_address", "");
  config.serialNumber = camJson.value("serial_number", "");
  config.port = camJson.value("port", 0);
  config.enabled = camJson.value("enabled", true);
  config.autoConnect = camJson.value("auto_connect", true);
  config.description = camJson.value("description", "");

  config.exposureTime = camJson.value("exposure_time", managerSettings_.defaultExposureTime);
  config.gain = camJson.value("gain", managerSettings_.defaultGain);
  config.width = camJson.value("width", managerSettings_.defaultWidth);
  config.height = camJson.value("height", managerSettings_.defaultHeight);

  return config;
}

CameraManagerSettings CameraConfigManager::ParseManagerSettings(const nlohmann::json& settingsJson) const {
  CameraManagerSettings settings;

  settings.autoConnect = settingsJson.value("auto_connect", true);
  settings.defaultExposureTime = settingsJson.value("default_exposure_time", 1000);
  settings.defaultGain = settingsJson.value("default_gain", 1.0);
  settings.broadcastEnabled = settingsJson.value("broadcast_enabled", true);
  settings.maxSubscribers = settingsJson.value("max_subscribers", 10);
  settings.defaultWidth = settingsJson.value("default_width", 0);
  settings.defaultHeight = settingsJson.value("default_height", 0);
  settings.logLevel = settingsJson.value("log_level", "INFO");

  return settings;
}

nlohmann::json CameraConfigManager::CameraConfigToJson(const CameraConfigData& config) const {
  nlohmann::json camJson;

  camJson["id"] = config.id;
  camJson["display_name"] = config.displayName;
  camJson["connection_type"] = config.connectionType;
  camJson["camera_type"] = config.cameraType;
  camJson["ip_address"] = config.ipAddress;
  camJson["serial_number"] = config.serialNumber;
  camJson["port"] = config.port;
  camJson["enabled"] = config.enabled;
  camJson["auto_connect"] = config.autoConnect;
  camJson["description"] = config.description;

  if (config.exposureTime != managerSettings_.defaultExposureTime) {
    camJson["exposure_time"] = config.exposureTime;
  }
  if (config.gain != managerSettings_.defaultGain) {
    camJson["gain"] = config.gain;
  }
  if (config.width != managerSettings_.defaultWidth) {
    camJson["width"] = config.width;
  }
  if (config.height != managerSettings_.defaultHeight) {
    camJson["height"] = config.height;
  }

  return camJson;
}

nlohmann::json CameraConfigManager::ManagerSettingsToJson(const CameraManagerSettings& settings) const {
  nlohmann::json settingsJson;

  settingsJson["auto_connect"] = settings.autoConnect;
  settingsJson["default_exposure_time"] = settings.defaultExposureTime;
  settingsJson["default_gain"] = settings.defaultGain;
  settingsJson["broadcast_enabled"] = settings.broadcastEnabled;
  settingsJson["max_subscribers"] = settings.maxSubscribers;
  settingsJson["default_width"] = settings.defaultWidth;
  settingsJson["default_height"] = settings.defaultHeight;
  settingsJson["log_level"] = settings.logLevel;

  return settingsJson;
}

bool CameraConfigManager::ValidateCameraConfig(const CameraConfigData& config, std::vector<std::string>& errors) const {
  bool isValid = true;

  if (config.id.empty()) {
    errors.push_back("Camera ID cannot be empty");
    isValid = false;
  }

  if (config.connectionType == "ip" && config.ipAddress.empty()) {
    errors.push_back("IP camera " + config.id + " missing IP address");
    isValid = false;
  }

  if (config.exposureTime < 0) {
    errors.push_back("Camera " + config.id + " has invalid exposure time");
    isValid = false;
  }

  if (config.gain < 0) {
    errors.push_back("Camera " + config.id + " has invalid gain");
    isValid = false;
  }

  if (config.cameraType != "PYLON" && config.cameraType != "BASLER" && config.cameraType != "IDS") {
    errors.push_back("Camera " + config.id + " has invalid camera type: " + config.cameraType);
    isValid = false;
  }

  return isValid;
}

bool CameraConfigManager::ValidateManagerSettings(const CameraManagerSettings& settings, std::vector<std::string>& errors) const {
  bool isValid = true;

  if (settings.defaultExposureTime < 0) {
    errors.push_back("Default exposure time cannot be negative");
    isValid = false;
  }

  if (settings.defaultGain < 0) {
    errors.push_back("Default gain cannot be negative");
    isValid = false;
  }

  if (settings.maxSubscribers < 1) {
    errors.push_back("Max subscribers must be at least 1");
    isValid = false;
  }

  return isValid;
}

void CameraConfigManager::LogInfo(const std::string& message) const {
  std::cout << "[CameraConfig] " << message << std::endl;
}

void CameraConfigManager::LogWarning(const std::string& message) const {
  std::cerr << "[CameraConfig] " << message << std::endl;
}

void CameraConfigManager::LogError(const std::string& message) const {
  std::cerr << "[CameraConfig] " << message << std::endl;
}

ExtendedCameraInfo CameraConfigManager::ConvertToExtendedCameraInfo(const CameraConfigData& configData) const {
  ICameraHardware::CameraType cameraType;
  if (configData.IsIDSCamera()) {
    cameraType = ICameraHardware::CameraType::IDS;
  }
  else {
    cameraType = ICameraHardware::CameraType::PYLON;
  }

  if (configData.connectionType == "ip") {
    CameraInfo baseInfo = CameraInfo::CreateByIP(configData.id, configData.ipAddress, configData.displayName, configData.autoConnect);
    return ExtendedCameraInfo(baseInfo, cameraType, configData.description);
  }
  else if (configData.connectionType == "serial") {
    CameraInfo baseInfo(configData.id, configData.serialNumber, configData.displayName, configData.autoConnect);
    return ExtendedCameraInfo(baseInfo, cameraType, configData.description);
  }
  else if (configData.connectionType == "index") {
    CameraInfo baseInfo = CameraInfo::CreateByIndex(configData.id, configData.port, configData.displayName, configData.autoConnect);
    return ExtendedCameraInfo(baseInfo, cameraType, configData.description);
  }
  else {
    CameraInfo baseInfo(configData.id, configData.displayName, configData.autoConnect);
    return ExtendedCameraInfo(baseInfo, cameraType, configData.description);
  }
}

CameraInfo CameraConfigManager::ConvertToCameraInfo(const CameraConfigData& configData) const {
  ExtendedCameraInfo extended = ConvertToExtendedCameraInfo(configData);
  return static_cast<CameraInfo>(extended);
}

bool CameraConfigManager::SetCameraIPAddress(const std::string& cameraId, const std::string& ipAddress) {
  auto it = std::find_if(cameraConfigs_.begin(), cameraConfigs_.end(),
    [&cameraId](CameraConfigData& cfg) { return cfg.id == cameraId; });

  if (it != cameraConfigs_.end()) {
    it->ipAddress = ipAddress;
    it->connectionType = "ip";
    std::cout << "[CameraConfig] Updated IP address for camera " << cameraId << " to: " << ipAddress << std::endl;
    return true;
  }

  std::cerr << "[CameraConfig] Camera " << cameraId << " not found for IP address update" << std::endl;
  return false;
}

bool CameraConfigManager::EnableCamera(const std::string& cameraId, bool enabled) {
  auto it = std::find_if(cameraConfigs_.begin(), cameraConfigs_.end(),
    [&cameraId](CameraConfigData& cfg) { return cfg.id == cameraId; });

  if (it != cameraConfigs_.end()) {
    it->enabled = enabled;
    std::cout << "[CameraConfig] " << (enabled ? "Enabled" : "Disabled")
      << " camera: " << cameraId << std::endl;
    return true;
  }

  std::cerr << "[CameraConfig] Camera " << cameraId << " not found for enable/disable operation" << std::endl;
  return false;
}

bool CameraConfigManager::SetCameraAutoConnect(const std::string& cameraId, bool autoConnect) {
  auto it = std::find_if(cameraConfigs_.begin(), cameraConfigs_.end(),
    [&cameraId](CameraConfigData& cfg) { return cfg.id == cameraId; });

  if (it != cameraConfigs_.end()) {
    it->autoConnect = autoConnect;
    std::cout << "[CameraConfig] Set auto-connect for camera " << cameraId
      << " to: " << (autoConnect ? "true" : "false") << std::endl;
    return true;
  }

  std::cerr << "[CameraConfig] Camera " << cameraId << " not found for auto-connect setting" << std::endl;
  return false;
}
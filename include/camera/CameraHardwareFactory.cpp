// CameraHardwareFactory.cpp - Complete implementation
#include "CameraHardwareFactory.h"
#include "CameraManager.h"  // For CameraInfo struct
#include "PylonCameraAdapter.h"
#include "IDSCameraAdapter.h"
#include <algorithm>
#include <cctype>
#include <iostream>

std::unique_ptr<ICameraHardware> CameraHardwareFactory::CreateCamera(
  ICameraHardware::CameraType type,
  const std::string& cameraId,
  const std::string& deviceInfo) {

  try {
    switch (type) {
    case ICameraHardware::CameraType::PYLON: {
      std::cout << "Creating Pylon camera adapter for: " << cameraId << " (no connection info)" << std::endl;
      auto camera = std::make_unique<PylonCameraAdapter>(cameraId);
      if (camera) {
        std::cout << "Pylon camera adapter created successfully" << std::endl;
      }
      return camera;
    }

    case ICameraHardware::CameraType::IDS: {
      std::cout << "Creating IDS camera adapter for: " << cameraId << std::endl;

      // Extract device ID from deviceInfo if available
      int deviceId = 0;
      if (!deviceInfo.empty()) {
        try {
          deviceId = std::stoi(deviceInfo);
        }
        catch (...) {
          std::cerr << "Failed to parse device ID from: " << deviceInfo << ", using 0" << std::endl;
          deviceId = 0;
        }
      }

      auto camera = std::make_unique<IDSCameraAdapter>(cameraId, deviceId);
      if (camera) {
        std::cout << "IDS camera adapter created successfully (Device ID: " << deviceId << ")" << std::endl;
      }
      return camera;
    }

    case ICameraHardware::CameraType::UNKNOWN:
    default: {
      std::cerr << "Unknown camera type requested for: " << cameraId << std::endl;
      return nullptr;
    }
    }
  }
  catch (const std::exception& e) {
    std::cerr << "Exception creating camera [" << cameraId << "]: " << e.what() << std::endl;
    return nullptr;
  }
}

std::unique_ptr<ICameraHardware> CameraHardwareFactory::CreateCamera(
  ICameraHardware::CameraType type,
  const CameraInfo& cameraInfo) {

  try {
    switch (type) {
    case ICameraHardware::CameraType::PYLON: {
      std::cout << "Creating Pylon camera adapter for: " << cameraInfo.id
        << " with " << cameraInfo.GetConnectionInfo() << std::endl;
      auto camera = std::make_unique<PylonCameraAdapter>(cameraInfo);
      if (camera) {
        std::cout << "Pylon camera adapter created successfully with connection info" << std::endl;
      }
      return camera;
    }

    case ICameraHardware::CameraType::IDS: {
      std::cout << "Creating IDS camera adapter for: " << cameraInfo.id << std::endl;

      // For IDS cameras, use deviceIndex
      auto camera = std::make_unique<IDSCameraAdapter>(cameraInfo.id, cameraInfo.deviceIndex);
      if (camera) {
        std::cout << "IDS camera adapter created successfully (Device ID: " << cameraInfo.deviceIndex << ")" << std::endl;
      }
      return camera;
    }

    case ICameraHardware::CameraType::UNKNOWN:
    default: {
      std::cerr << "Unknown camera type requested for: " << cameraInfo.id << std::endl;
      return nullptr;
    }
    }
  }
  catch (const std::exception& e) {
    std::cerr << "Exception creating camera [" << cameraInfo.id << "]: " << e.what() << std::endl;
    return nullptr;
  }
}

ICameraHardware::CameraType CameraHardwareFactory::DetectCameraType(const std::string& deviceInfo) {
  if (deviceInfo.empty()) {
    return ICameraHardware::CameraType::UNKNOWN;
  }

  // Convert to lowercase for case-insensitive comparison
  std::string lowerDeviceInfo = deviceInfo;
  std::transform(lowerDeviceInfo.begin(), lowerDeviceInfo.end(), lowerDeviceInfo.begin(), ::tolower);

  // Check for Pylon/Basler indicators
  if (lowerDeviceInfo.find("basler") != std::string::npos ||
    lowerDeviceInfo.find("pylon") != std::string::npos ||
    lowerDeviceInfo.find("ace") != std::string::npos ||
    lowerDeviceInfo.find("dart") != std::string::npos ||
    lowerDeviceInfo.find("pulse") != std::string::npos ||
    lowerDeviceInfo.find("gige") != std::string::npos) {

    std::cout << "Detected Pylon camera from device info: " << deviceInfo << std::endl;
    return ICameraHardware::CameraType::PYLON;
  }

  // Check for IDS indicators
  if (lowerDeviceInfo.find("ids") != std::string::npos ||
    lowerDeviceInfo.find("ueye") != std::string::npos ||
    lowerDeviceInfo.find("imaging development systems") != std::string::npos ||
    lowerDeviceInfo.find("ui-") != std::string::npos ||
    lowerDeviceInfo.find("le") != std::string::npos ||
    lowerDeviceInfo.find("cp") != std::string::npos ||
    lowerDeviceInfo.find("ml") != std::string::npos) {

    std::cout << "Detected IDS camera from device info: " << deviceInfo << std::endl;
    return ICameraHardware::CameraType::IDS;
  }

  // Default to unknown if no match found
  std::cerr << "Could not detect camera type from device info: " << deviceInfo << std::endl;

  return ICameraHardware::CameraType::UNKNOWN;
}

std::unique_ptr<ICameraHardware> CameraHardwareFactory::CreateCameraAutoDetect(
  const std::string& cameraId,
  const std::string& deviceInfo) {

  ICameraHardware::CameraType detectedType = DetectCameraType(deviceInfo);

  if (detectedType == ICameraHardware::CameraType::UNKNOWN) {
    std::cerr << "Auto-detection failed for camera: " << cameraId << ", defaulting to Pylon" << std::endl;
    detectedType = ICameraHardware::CameraType::PYLON; // Default fallback
  }

  return CreateCamera(detectedType, cameraId, deviceInfo);
}

std::string CameraHardwareFactory::GetCameraTypeName(ICameraHardware::CameraType type) {
  switch (type) {
  case ICameraHardware::CameraType::PYLON:
    return "Pylon/Basler";
  case ICameraHardware::CameraType::IDS:
    return "IDS uEye";
  case ICameraHardware::CameraType::UNKNOWN:
  default:
    return "Unknown";
  }
}

bool CameraHardwareFactory::IsCameraTypeSupported(ICameraHardware::CameraType type) {
  switch (type) {
  case ICameraHardware::CameraType::PYLON:
  case ICameraHardware::CameraType::IDS:
    return true;
  case ICameraHardware::CameraType::UNKNOWN:
  default:
    return false;
  }
}
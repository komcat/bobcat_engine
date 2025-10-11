// CameraHardwareFactory.h
// Factory for creating camera hardware instances
#pragma once

#include "ICameraHardware.h"
#include <memory>
#include <string>

/**
 * @brief Factory class for creating camera hardware instances
 *
 * This factory creates the appropriate camera adapter (Pylon or IDS)
 * based on the camera type specified, providing a unified way to
 * instantiate different camera hardware implementations.
 */
class CameraHardwareFactory {
public:
  /**
   * @brief Create a camera hardware instance
   * @param type The type of camera to create (PYLON, IDS, etc.)
   * @param cameraId Unique identifier for the camera
   * @param deviceInfo Optional device-specific information (e.g., device ID for IDS cameras)
   * @return Unique pointer to the created camera hardware, or nullptr if creation failed
   */
  static std::unique_ptr<ICameraHardware> CreateCamera(
    ICameraHardware::CameraType type,
    const std::string& cameraId,
    const std::string& deviceInfo = ""
  );

  /**
   * @brief Auto-detect camera type from device information string
   * @param deviceInfo Device information string containing vendor/model info
   * @return Detected camera type, or UNKNOWN if detection failed
   *
   * This method analyzes device information strings to automatically
   * determine whether a camera is a Pylon/Basler camera, IDS camera, etc.
   *
   * Examples of deviceInfo strings:
   * - "Basler acA1920-40gm" -> PYLON
   * - "IDS UI-3240CP" -> IDS
   * - "uEye LE" -> IDS
   */
  static ICameraHardware::CameraType DetectCameraType(const std::string& deviceInfo);

  /**
   * @brief Create camera with auto-detection
   * @param cameraId Unique identifier for the camera
   * @param deviceInfo Device information for type detection
   * @return Unique pointer to created camera, or nullptr if failed
   */
  static std::unique_ptr<ICameraHardware> CreateCameraAutoDetect(
    const std::string& cameraId,
    const std::string& deviceInfo
  );

  /**
   * @brief Get human-readable name for camera type
   * @param type Camera type enum value
   * @return String representation of the camera type
   */
  static std::string GetCameraTypeName(ICameraHardware::CameraType type);

  /**
   * @brief Check if a camera type is supported by this factory
   * @param type Camera type to check
   * @return True if the camera type can be created by this factory
   */
  static bool IsCameraTypeSupported(ICameraHardware::CameraType type);

private:
  // Static class - no instances allowed
  CameraHardwareFactory() = delete;
  ~CameraHardwareFactory() = delete;
  CameraHardwareFactory(const CameraHardwareFactory&) = delete;
  CameraHardwareFactory& operator=(const CameraHardwareFactory&) = delete;
};
// ICameraHardware.h
// Common interface for both Pylon and IDS camera hardware
#pragma once

#include "CameraFrameData.h"
#include <string>
#include <functional>
#include <memory>

/**
 * @brief Abstract interface for camera hardware implementations
 *
 * This interface provides a common API for different camera types (Pylon, IDS)
 * while maintaining compatibility with the existing broadcasting system.
 */
class ICameraHardware {
public:
  virtual ~ICameraHardware() = default;

  // Core camera operations
  virtual bool Initialize() = 0;
  virtual bool Connect() = 0;
  virtual bool Disconnect() = 0;
  virtual bool StartGrabbing() = 0;
  virtual bool StopGrabbing() = 0;

  // Status queries
  virtual bool IsConnected() const = 0;
  virtual bool IsGrabbing() const = 0;
  virtual bool IsDeviceRemoved() const = 0;

  // Frame capture
  virtual bool CaptureFrame(CameraFrameData& frameData) = 0;
  virtual bool HasNewFrame() const = 0;

  // Camera information
  virtual std::string GetCameraId() const = 0;
  virtual std::string GetModelName() const = 0;
  virtual std::string GetSerialNumber() const = 0;
  virtual std::string GetVendorName() const = 0;

  // Exposure settings (common interface)
  struct ExposureSettings {
    double exposure_time = 10000.0; // microseconds (matches existing code)
    double gain = 1.0;              // gain value
    bool auto_exposure = false;     // auto exposure enabled
    bool auto_gain = false;         // auto gain enabled

    ExposureSettings() = default;
    ExposureSettings(double exp, double g, bool autoExp = false, bool autoG = false)
      : exposure_time(exp), gain(g), auto_exposure(autoExp), auto_gain(autoG) {
    }
  };

  virtual bool SetExposureSettings(const ExposureSettings& settings) = 0;
  virtual ExposureSettings GetExposureSettings() const = 0;

  // Frame callback for broadcasting system
  virtual void SetFrameCallback(std::function<void(const CameraFrameData&)> callback) = 0;
  virtual void ClearFrameCallback() = 0;

  // Camera type identification
  enum class CameraType {
    PYLON,
    IDS,
    UNKNOWN
  };
  virtual CameraType GetCameraType() const = 0;

  // Error handling
  virtual std::string GetLastError() const = 0;
  virtual void ClearLastError() = 0;

  // Optional: Camera-specific configuration
  virtual bool SetConfiguration(const std::string& key, const std::string& value) { return false; }
  virtual std::string GetConfiguration(const std::string& key) const { return ""; }
};

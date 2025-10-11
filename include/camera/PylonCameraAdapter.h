// PylonCameraAdapter.h
// Adapter wrapping existing PylonCamera for the common interface
#pragma once

#include "ICameraHardware.h"
#include "pylon_camera.h"
#include <memory>
#include <string>
#include <functional>

/**
 * @brief Adapter that wraps PylonCamera to implement ICameraHardware
 *
 * This adapter provides minimal wrapping around the existing PylonCamera
 * implementation, maintaining compatibility with current code while adding
 * the common interface.
 */
class PylonCameraAdapter : public ICameraHardware {
public:
  explicit PylonCameraAdapter(const std::string& cameraId);
  virtual ~PylonCameraAdapter();

  // Disable copy/move to prevent issues with callbacks
  PylonCameraAdapter(const PylonCameraAdapter&) = delete;
  PylonCameraAdapter& operator=(const PylonCameraAdapter&) = delete;
  PylonCameraAdapter(PylonCameraAdapter&&) = delete;
  PylonCameraAdapter& operator=(PylonCameraAdapter&&) = delete;

  // ICameraHardware interface implementation
  bool Initialize() override;
  bool Connect() override;
  bool Disconnect() override;
  bool StartGrabbing() override;
  bool StopGrabbing() override;

  bool IsConnected() const override;
  bool IsGrabbing() const override;
  bool IsDeviceRemoved() const override;

  bool CaptureFrame(CameraFrameData& frameData) override;
  bool HasNewFrame() const override;

  std::string GetCameraId() const override { return m_cameraId; }
  std::string GetModelName() const override;
  std::string GetSerialNumber() const override;
  std::string GetVendorName() const override;

  bool SetExposureSettings(const ExposureSettings& settings) override;
  ExposureSettings GetExposureSettings() const override;

  void SetFrameCallback(std::function<void(const CameraFrameData&)> callback) override;
  void ClearFrameCallback() override;

  CameraType GetCameraType() const override { return CameraType::PYLON; }

  std::string GetLastError() const override { return m_lastError; }
  void ClearLastError() override { m_lastError.clear(); }

  // Pylon-specific configuration
  bool SetConfiguration(const std::string& key, const std::string& value) override;
  std::string GetConfiguration(const std::string& key) const override;

  // Access to underlying Pylon camera for backward compatibility
  PylonCamera* GetPylonCamera() { return m_pylonCamera.get(); }
  const PylonCamera* GetPylonCamera() const { return m_pylonCamera.get(); }

private:
  std::string m_cameraId;
  std::unique_ptr<PylonCamera> m_pylonCamera;
  std::function<void(const CameraFrameData&)> m_frameCallback;
  std::string m_lastError;

  // ADD THESE NEW MEMBERS:
  mutable Pylon::CImageFormatConverter m_formatConverter;
  mutable Pylon::CPylonImage m_cachedPylonImage;
  mutable Pylon::CPylonImage m_cachedConvertedImage;
  mutable std::mutex m_conversionMutex;  // For thread safety

  // Internal helper methods
  void SetError(const std::string& error);
  void ConvertPylonExposureToCommon(const PylonCamera::ExposureSettings& pylonSettings,
    ExposureSettings& commonSettings) const;
  void ConvertCommonExposureToPylon(const ExposureSettings& commonSettings,
    PylonCamera::ExposureSettings& pylonSettings) const;

  // Frame callback integration with existing PylonCamera
  void SetupFrameCallbackIntegration();
  void OnPylonFrameReceived(const Pylon::CGrabResultPtr& grabResult);
};

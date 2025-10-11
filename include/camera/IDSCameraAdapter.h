// IDSCameraAdapter.h
// Simplified adapter implementing ICameraHardware for IDS uEye cameras
#pragma once

#include "ICameraHardware.h"
#include "ueye.h"  // IDS uEye SDK header
#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>

/**
 * @brief Simplified adapter implementing ICameraHardware for IDS uEye cameras
 *
 * Focuses on core functionality: connect/disconnect, grab, exposure/gain control
 * Now includes frame grabbing thread for continuous live video broadcasting
 */
class IDSCameraAdapter : public ICameraHardware {
public:
  explicit IDSCameraAdapter(const std::string& cameraId, int deviceId = 0);
  virtual ~IDSCameraAdapter();

  // Disable copy/move
  IDSCameraAdapter(const IDSCameraAdapter&) = delete;
  IDSCameraAdapter& operator=(const IDSCameraAdapter&) = delete;
  IDSCameraAdapter(IDSCameraAdapter&&) = delete;
  IDSCameraAdapter& operator=(IDSCameraAdapter&&) = delete;

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
  std::string GetVendorName() const override { return "IDS Imaging"; }

  bool SetExposureSettings(const ExposureSettings& settings) override;
  ExposureSettings GetExposureSettings() const override;

  void SetFrameCallback(std::function<void(const CameraFrameData&)> callback) override;
  void ClearFrameCallback() override;

  CameraType GetCameraType() const override { return CameraType::IDS; }

  std::string GetLastError() const override { return m_lastError; }
  void ClearLastError() override { m_lastError.clear(); }

  // IDS-specific configuration (minimal)
  bool SetConfiguration(const std::string& key, const std::string& value) override;
  std::string GetConfiguration(const std::string& key) const override;

  // Access to camera handle
  HCAM GetCameraHandle() const { return m_hCam; }

private:
  std::string m_cameraId;
  int m_deviceId;
  HCAM m_hCam;

  // Camera state
  std::atomic<bool> m_isConnected;
  std::atomic<bool> m_isGrabbing;
  std::atomic<bool> m_isDeviceRemoved;

  // Frame callback with thread safety
  std::function<void(const CameraFrameData&)> m_frameCallback;
  mutable std::mutex m_callbackMutex;
  std::string m_lastError;

  // Frame grabbing thread
  std::thread m_grabThread;
  std::atomic<bool> m_threadRunning;

  // Image buffer (single buffer for simplicity)
  char* m_pImageMemory;
  int m_memoryId;

  // Camera info cache
  mutable std::mutex m_propertiesMutex;
  std::string m_modelName;
  std::string m_serialNumber;

  // Internal helper methods
  void SetError(const std::string& error);
  bool InitializeImageMemory();
  void CleanupImageMemory();
  bool UpdateCameraProperties();

  // Frame grabbing thread methods
  void StartGrabThread();
  void StopGrabThread();
  void GrabThreadFunction();

  // Core exposure/gain methods
  bool SetExposureTime(double exposureTimeUs);
  bool SetGain(double gain);
  bool SetAutoExposure(bool enable);
  bool SetAutoGain(bool enable);

  double GetExposureTime() const;
  double GetGain() const;
  bool GetAutoExposure() const;
  bool GetAutoGain() const;

  // IDS SDK error handling
  std::string GetIDSErrorString(INT result) const;
  bool CheckIDSResult(INT result, const std::string& operation) const;
};
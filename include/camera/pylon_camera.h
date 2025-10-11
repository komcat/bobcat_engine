#pragma once

// Prevent winsock conflicts before any includes
#ifdef _WIN32
#define _WINSOCKAPI_   // Prevent winsock.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "pylon/PylonIncludes.h"
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <functional>

// Forward declarations
class PylonCamera;

// Configuration event handler for device removal handling
class PylonDeviceRemovalHandler : public Pylon::CConfigurationEventHandler
{
private:
  PylonCamera* m_pOwner;

public:
  PylonDeviceRemovalHandler(PylonCamera* pOwner);
  virtual ~PylonDeviceRemovalHandler();

  // This method is called from a different thread when camera device removal has been detected
  virtual void OnCameraDeviceRemoved(Pylon::CInstantCamera& camera) override;
};

// PylonCamera class that handles camera connection, image acquisition, and device removal
class PylonCamera {
public:
  // Callback type for device removal notification
  using DeviceRemovalCallback = std::function<void()>;

  // Callback type for new frame notification
  using NewFrameCallback = std::function<void(const Pylon::CGrabResultPtr&)>;

public:
  // Constructor
  PylonCamera();

  // Destructor - ensures proper cleanup
  ~PylonCamera();

  // Initialize the camera system - call once before using any other methods
  bool Initialize();

  // EXISTING: Connect to the first available camera
  bool Connect();

  // EXISTING: Connect to a specific camera by serial number
  bool ConnectToSerial(const std::string& serialNumber);

  // NEW: Connect to a specific camera by IP address (GigE cameras)
  bool ConnectToIP(const std::string& ipAddress);

  // NEW: Connect to camera by device index
  bool ConnectToIndex(int deviceIndex);

  // Disconnect from the camera
  void Disconnect();
  // NEW: Bool-returning version for interface compatibility
  bool DisconnectWithResult();
  bool StopGrabbingWithResult();

  // Start continuous image acquisition
  bool StartGrabbing();

  // Stop image acquisition
  void StopGrabbing();

  // Get camera information
  std::string GetDeviceInfo() const;

  // NEW: Get detailed camera network info (for GigE cameras)
  std::string GetNetworkInfo() const;

  // Check if camera is connected
  bool IsConnected() const;

  // Check if camera is grabbing
  bool IsGrabbing() const;

  // Check if the camera device has been removed
  bool IsCameraDeviceRemoved() const;

  // Handle device removal - called by the event handler
  void HandleDeviceRemoval();

  // Try to reconnect to a removed camera
  bool TryReconnect();

  // Set a callback for device removal notification
  void SetDeviceRemovalCallback(DeviceRemovalCallback callback);

  // Set a callback for new frames
  void SetNewFrameCallback(NewFrameCallback callback);

  // Get access to the internal camera object (for advanced operations)
  Pylon::CInstantCamera& GetInternalCamera() { return m_camera; }
  const Pylon::CInstantCamera& GetInternalCamera() const { return m_camera; }

  // EXISTING: Direct exposure control methods
  // Set exposure time in microseconds
  bool SetExposureTime(double microseconds);

  // Set gain value (0-10 scale, mapped to camera's raw range)
  bool SetGain(double gain);

  // Enable/disable auto exposure
  bool SetExposureAuto(bool enabled);

  // Enable/disable auto gain
  bool SetGainAuto(bool enabled);

  // Get current exposure time in microseconds
  double GetExposureTime() const;

  // Get current gain value (0-10 scale)
  double GetGain() const;

  // Check if auto exposure is enabled
  bool IsExposureAuto() const;

  // Check if auto gain is enabled
  bool IsGainAuto() const;

  // Apply multiple settings at once (more efficient)
  struct ExposureSettings {
    double exposure_time = 10000.0;  // microseconds
    double gain = 1.0;               // 0-10 scale
    bool exposure_auto = false;
    bool gain_auto = false;
  };
  bool ApplyExposureSettings(const ExposureSettings& settings);

  // Get current settings as a structure
  ExposureSettings GetCurrentExposureSettings() const;

  // Debug method to print current camera settings
  void DebugCameraSettings();
  // Get the latest frame data from continuous grabbing (thread-safe)
  bool GetLatestFrameData(uint8_t*& imageData, uint32_t& width, uint32_t& height, bool& newFrame);

private:
  // NEW: Enhanced connection methods
  bool ConnectToDevice(const Pylon::CDeviceInfo& deviceInfo);
  Pylon::CDeviceInfo CreateIPDeviceInfo(const std::string& ipAddress);
  bool FindDeviceByIP(const std::string& ipAddress, Pylon::CDeviceInfo& deviceInfo);
  bool FindDeviceBySerial(const std::string& serialNumber, Pylon::CDeviceInfo& deviceInfo);
  bool FindDeviceByIndex(int deviceIndex, Pylon::CDeviceInfo& deviceInfo);

  // NEW: Configuration methods
  void ConfigureGigECamera();
  void ConfigureCamera();
  std::string GetCurrentCameraIP() const;

  // Grab thread function for continuous image acquisition
  void GrabThreadFunction();

  // Helper methods for exposure control
  bool SetExposureMode();  // Set to "Timed" mode
  bool ValidateConnection() const;  // Check if camera is connected and open
  bool ValidateConnectionForRead() const;  // Check connection for read operations (handles const issues)
  void DebugAllCameras();
  bool ConnectToIPWithFallback(const std::string& ipAddress, const std::string& fallbackSerial = "");

private:
  // Pylon camera object
  Pylon::CInstantCamera m_camera;

  // State flags
  bool m_initialized;
  bool m_connected;
  bool m_deviceRemoved;
  std::atomic<bool> m_reconnecting;

  // EXISTING: Device info for reconnection
  Pylon::String_t m_lastDeviceSerialNumber;
  Pylon::String_t m_lastDeviceClass;

  // NEW: Additional device info for enhanced reconnection
  std::string m_lastIPAddress;      // Store IP address for reconnection
  int m_lastDeviceIndex;           // Store device index for reconnection

  // Grabbing thread
  std::thread m_grabThread;
  std::atomic<bool> m_threadRunning;
  std::mutex m_cameraMutex;

  // Frame acquisition
  Pylon::CGrabResultPtr m_ptrGrabResult;
  std::atomic<bool> m_newFrameReady;

  // Device removal handler
  PylonDeviceRemovalHandler* m_pDeviceRemovalHandler;

  // Callbacks
  DeviceRemovalCallback m_deviceRemovalCallback;
  NewFrameCallback m_newFrameCallback;

  // Frame rate control
  int m_targetFPS;

  // Error message storage
  mutable std::string m_lastErrorMessage;

  // Friend the handler so it can access private members
  friend class PylonDeviceRemovalHandler;


  // Frame data for GetLatestFrameData method
  mutable std::mutex m_frameDataMutex;
  Pylon::CImageFormatConverter m_formatConverter;
  Pylon::CPylonImage m_latestPylonImage;
  Pylon::CPylonImage m_latestConvertedImage;
  std::atomic<bool> m_hasLatestFrame;
};
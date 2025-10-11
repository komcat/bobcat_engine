#pragma once

#include "../external/uEye/include/ueye.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

class IDSCameraTest {
public:
  // Callback type for status updates
  using StatusCallback = std::function<void(const std::string&)>;

  IDSCameraTest();
  ~IDSCameraTest();

  // Basic connection methods
  bool ConnectById(int cameraId = 0);
  bool ConnectBySerial(const std::string& serialNumber);
  void Disconnect();

  // Status methods
  bool IsConnected() const { return m_isConnected; }
  std::string GetLastError() const { return m_lastError; }
  std::string GetCameraInfo() const;

  // Camera ID management
  int GetConnectedCameraId() const { return m_cameraId; }
  static std::vector<int> GetAvailableCameraIds();
  static std::vector<std::string> GetAvailableCameraSerials();

  // NEW: Image capture methods
  bool CaptureImage();                           // Capture single image to memory
  bool CaptureImageToDisk(const std::string& filename = "");  // Capture and save to disk

  // NEW: Live video/grabbing methods
  bool StartGrabbing();                          // Start continuous grabbing
  bool StopGrabbing();                           // Stop continuous grabbing
  bool IsGrabbing() const { return m_isGrabbing.load(); }

  // NEW: Thread-safe image data access
  const char* GetImageData() const;
  int GetImageWidth() const { return m_imageWidth; }
  int GetImageHeight() const { return m_imageHeight; }
  int GetImageBitsPerPixel() const { return m_imageBitsPerPixel; }
  bool HasNewFrame() const { return m_hasNewFrame.load(); }
  void MarkFrameProcessed() { m_hasNewFrame.store(false); }

  // Set callback for status updates (optional)
  void SetStatusCallback(StatusCallback callback) { m_statusCallback = callback; }

private:
  HIDS m_cameraHandle;
  std::atomic<bool> m_isConnected;
  std::atomic<bool> m_isGrabbing;
  int m_cameraId;
  std::string m_lastError;
  StatusCallback m_statusCallback;

  // Thread-safe image memory management
  mutable std::mutex m_imageMutex;
  char* m_pImageMemory;
  int m_imageMemoryId;
  int m_imageWidth;
  int m_imageHeight;
  int m_imageBitsPerPixel;
  std::atomic<bool> m_hasNewFrame;

  // Grabbing thread management
  std::thread m_grabThread;
  std::atomic<bool> m_threadRunning;
  int m_targetFPS;

  // Helper methods
  void LogStatus(const std::string& message);
  void SetError(const std::string& error);
  bool SetupImageMemory();
  void FreeImageMemory();
  std::string GenerateTimestampFilename(const std::string& extension = ".bmp") const;
  bool SaveImageAlternativeMethod(const std::string& filename);

  // Thread function for continuous grabbing
  void GrabThreadFunction();
};
// CameraFrameData.h
#pragma once

#include <vector>
#include <string>
#include <cstdint>

// Frame data structure for broadcasting
struct CameraFrameData {
  std::string cameraId;
  uint64_t timestamp = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t channels = 3;  // RGB
  std::vector<uint8_t> imageData;

  // Metadata
  double exposureTime = 0.0;
  double gain = 0.0;
  uint64_t frameNumber = 0;

  // Helper methods
  size_t GetDataSize() const { return width * height * channels; }
  bool IsValid() const { return width > 0 && height > 0 && !imageData.empty(); }

  // Memory-efficient constructor
  CameraFrameData() = default;
  CameraFrameData(const std::string& id, uint32_t w, uint32_t h)
    : cameraId(id), width(w), height(h) {
    imageData.reserve(w * h * 3);
  }
};

// Abstract subscriber interface
class CameraFrameSubscriber {
public:
  virtual ~CameraFrameSubscriber() = default;

  // Main callback for new frames
  virtual void OnNewFrame(const CameraFrameData& frameData) = 0;

  // Status change callback (optional)
  virtual void OnCameraStatusChanged(const std::string& cameraId, bool connected, bool grabbing) {}

  // Subscriber identification
  virtual std::string GetSubscriberId() const = 0;
  virtual bool WantsFramesFromCamera(const std::string& cameraId) const = 0;

  // Rate limiting preference (0 = no limit, otherwise min ms between frames)
  virtual int GetMinFrameIntervalMs() const { return 33; } // ~30fps default
};
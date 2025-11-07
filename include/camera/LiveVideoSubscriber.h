// LiveVideoSubscriber.h
// =====================

#pragma once

#include <CameraManager.h>
#include <CameraFrameData.h>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>

/**
 * @brief Subscriber that receives camera frames for UI live video display
 *
 * This class subscribes to the CameraManager's broadcasting system and
 * receives frame copies for display in ImGui interfaces. It handles
 * thread-safe frame storage and camera status tracking.
 */
class LiveVideoSubscriber : public CameraFrameSubscriber {
public:
  /**
   * @brief Constructor
   * @param cameraId The ID of the camera to subscribe to
   * @param componentName Optional unique name for this subscriber (e.g., "CameraView", "AuxWindow")
   */
  explicit LiveVideoSubscriber(const std::string& cameraId, const std::string& componentName = "");

  /**
   * @brief Destructor
   */
  ~LiveVideoSubscriber();

  // Disable copy/move to prevent accidental sharing
  LiveVideoSubscriber(const LiveVideoSubscriber&) = delete;
  LiveVideoSubscriber& operator=(const LiveVideoSubscriber&) = delete;
  LiveVideoSubscriber(LiveVideoSubscriber&&) = delete;
  LiveVideoSubscriber& operator=(LiveVideoSubscriber&&) = delete;

  // CameraFrameSubscriber interface implementation
  void OnNewFrame(const CameraFrameData& frameData) override;
  void OnCameraStatusChanged(const std::string& cameraId, bool connected, bool grabbing) override;
  std::string GetSubscriberId() const override;
  bool WantsFramesFromCamera(const std::string& cameraId) const override;
  int GetMinFrameIntervalMs() const override { return 0; } // 30fps for UI

  // Camera selection and management
  /**
   * @brief Change which camera this subscriber watches
   * @param cameraId New camera ID to subscribe to
   */
  void SetTargetCamera(const std::string& cameraId);

  /**
   * @brief Get the currently targeted camera ID
   * @return Camera ID string
   */
  std::string GetTargetCamera() const;

  // Frame access (thread-safe)
  /**
   * @brief Check if a new frame is available
   * @return True if new frame is ready for consumption
   */
  bool HasNewFrame() const { return m_hasNewFrame.load(); }

  /**
   * @brief Get the latest received frame (thread-safe copy)
   * @return Copy of the latest frame data
   */
  CameraFrameData GetLatestFrame() const;

  /**
   * @brief Mark the current frame as consumed (resets HasNewFrame flag)
   */
  void MarkFrameConsumed() { m_hasNewFrame.store(false); }

  // Status access (thread-safe)
  /**
   * @brief Check if the target camera is connected
   * @return True if camera is connected
   */
  bool IsCameraConnected() const { return m_cameraConnected.load(); }

  /**
   * @brief Check if the target camera is currently grabbing
   * @return True if camera is grabbing frames
   */
  bool IsCameraGrabbing() const { return m_cameraGrabbing.load(); }

  // Statistics and debugging
  /**
   * @brief Get total number of frames received since creation
   * @return Frame count
   */
  uint64_t GetTotalFramesReceived() const { return m_totalFramesReceived.load(); }

  /**
   * @brief Get the timestamp of the last received frame
   * @return Timestamp in camera units
   */
  uint64_t GetLastFrameTimestamp() const;

  // NEW: Initialize status from camera manager (call after subscription)
/**
 * @brief Initialize camera status from camera manager when camera is already running
 * @param cameraManager Pointer to camera manager to query current status
 */
  void InitializeStatusFromManager(CameraManager* cameraManager);

private:
  // Identification
  std::string m_subscriberId;
  std::string m_targetCameraId;
  std::string m_componentName;

  // Frame data (protected by mutex)
  mutable std::mutex m_frameMutex;
  CameraFrameData m_latestFrame;
  std::atomic<bool> m_hasNewFrame{ false };

  // Status tracking (atomic for thread safety)
  std::atomic<bool> m_cameraConnected{ false };
  std::atomic<bool> m_cameraGrabbing{ false };

  // Statistics
  std::atomic<uint64_t> m_totalFramesReceived{ 0 };
  std::atomic<uint64_t> m_lastFrameTimestamp{ 0 };

  // Helper methods
  void ResetState();
  void UpdateSubscriberId();
};
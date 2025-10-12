// LiveVideoSubscriber.cpp
// =======================

#include "LiveVideoSubscriber.h"
#include <iostream>
#include <sstream>

LiveVideoSubscriber::LiveVideoSubscriber(const std::string& cameraId)
  : m_targetCameraId(cameraId) {

  UpdateSubscriberId();
  ResetState();

  std::cout << "[INFO] LiveVideoSubscriber created for camera: " << cameraId
    << " (ID: " << m_subscriberId << ")" << std::endl;
}

LiveVideoSubscriber::~LiveVideoSubscriber() {
  std::cout << "[INFO] LiveVideoSubscriber destroyed for camera: " << m_targetCameraId
    << " (Total frames received: " << m_totalFramesReceived.load() << ")" << std::endl;
}



// Add this enhanced debugging to LiveVideoSubscriber.cpp
void LiveVideoSubscriber::OnNewFrame(const CameraFrameData& frameData) {
  if (frameData.cameraId != m_targetCameraId) {
    return; // Not interested in this camera
  }

  // Get current frame count before increment
  uint64_t frameNumber = m_totalFramesReceived.load() + 1;

  // Thread-safe frame storage
  {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_latestFrame = frameData; // Deep copy of frame data
  }

  // Update atomic flags and statistics
  m_hasNewFrame.store(true);
  m_totalFramesReceived.fetch_add(1);
  m_lastFrameTimestamp.store(frameData.timestamp);

  // Enhanced debugging for first few frames and then periodically
  //if (frameNumber <= 10 || frameNumber % 50 == 0) {
  //  std::cout << "[FRAME DEBUG] " << m_subscriberId << " received frame #" << frameNumber
  //    << " from " << frameData.cameraId
  //    << " (" << frameData.width << "x" << frameData.height << ")"
  //    << " timestamp: " << frameData.timestamp << std::endl;
  //}

  // Special logging for the critical first few frames
  if (frameNumber == 1) {
    std::cout << "[CRITICAL] " << m_subscriberId << " received FIRST frame successfully!" << std::endl;
  }
  else if (frameNumber == 2) {
    std::cout << "[CRITICAL] " << m_subscriberId << " received SECOND frame - broadcast is working!" << std::endl;
  }
}


void LiveVideoSubscriber::OnCameraStatusChanged(const std::string& cameraId, bool connected, bool grabbing) {
  if (cameraId != m_targetCameraId) {
    return; // Not our camera
  }

  // Update status atomically
  bool wasConnected = m_cameraConnected.exchange(connected);
  bool wasGrabbing = m_cameraGrabbing.exchange(grabbing);

  // Log status changes
  if (wasConnected != connected || wasGrabbing != grabbing) {
    std::cout << "[INFO] Camera " << cameraId << " status changed: "
      << "connected=" << (connected ? "Yes" : "No")
      << ", grabbing=" << (grabbing ? "Yes" : "No") << std::endl;
  }

  // Reset frame flag if camera stopped grabbing
  if (wasGrabbing && !grabbing) {
    m_hasNewFrame.store(false);
  }
}

std::string LiveVideoSubscriber::GetSubscriberId() const {
  return m_subscriberId;
}

bool LiveVideoSubscriber::WantsFramesFromCamera(const std::string& cameraId) const {
  return cameraId == m_targetCameraId;
}

void LiveVideoSubscriber::SetTargetCamera(const std::string& cameraId) {
  if (m_targetCameraId == cameraId) {
    return; // No change needed
  }

  std::cout << "[INFO] LiveVideoSubscriber switching from '" << m_targetCameraId
    << "' to '" << cameraId << "'" << std::endl;

  // Update target camera
  m_targetCameraId = cameraId;
  UpdateSubscriberId();

  // Reset state for new camera
  ResetState();
}

std::string LiveVideoSubscriber::GetTargetCamera() const {
  return m_targetCameraId;
}

CameraFrameData LiveVideoSubscriber::GetLatestFrame() const {
  std::lock_guard<std::mutex> lock(m_frameMutex);
  return m_latestFrame; // Return copy
}

uint64_t LiveVideoSubscriber::GetLastFrameTimestamp() const {
  return m_lastFrameTimestamp.load();
}

void LiveVideoSubscriber::ResetState() {
  // Clear frame data
  {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_latestFrame = CameraFrameData(); // Reset to empty frame
  }

  // Reset atomic flags
  m_hasNewFrame.store(false);
  m_cameraConnected.store(false);
  m_cameraGrabbing.store(false);

  // Reset statistics
  m_totalFramesReceived.store(0);
  m_lastFrameTimestamp.store(0);
}

void LiveVideoSubscriber::UpdateSubscriberId() {
  // Use a consistent ID format without pointer address for uniqueness
  m_subscriberId = "UI_LiveVideo_" + m_targetCameraId;
}


void LiveVideoSubscriber::InitializeStatusFromManager(CameraManager* cameraManager) {
  if (!cameraManager) {
    std::cout << "[WARNING] LiveVideoSubscriber: Cannot initialize status - no camera manager" << std::endl;
    return;
  }

  // Query current camera status
  auto status = cameraManager->GetCameraStatus(m_targetCameraId);

  // Update our internal status to match current reality
  m_cameraConnected.store(status.connected);
  m_cameraGrabbing.store(status.grabbing);

  std::cout << "[INFO] LiveVideoSubscriber: Initialized status for " << m_targetCameraId
    << " - connected=" << (status.connected ? "Yes" : "No")
    << ", grabbing=" << (status.grabbing ? "Yes" : "No") << std::endl;
}
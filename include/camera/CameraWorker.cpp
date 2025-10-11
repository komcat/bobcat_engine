// CameraWorker.cpp
#include "CameraWorker.h"
#include "CameraManager.h"
#include "CameraFrameData.h"
#include "ICameraHardware.h"
#include <QTimer>
#include <chrono>
#include <iostream>

CameraWorker::CameraWorker(CameraManager* cameraManager, const std::string& cameraId)
  : QObject(nullptr)
  , m_cameraManager(cameraManager)
  , m_cameraId(cameraId)
  , m_running(false)
  , m_captureTimer(nullptr)
  , m_fpsTimer(nullptr)
  , m_targetFPS(30)
  , m_enableProcessing(false)
  , m_frameCount(0)
  , m_avgProcessingTime(0.0)
  , m_skippedFrames(0)
{
  std::cout << "[CameraWorker] Created for camera: " << cameraId << std::endl;
}

CameraWorker::~CameraWorker()
{
  stopCapture();
  std::cout << "[CameraWorker] Destroyed for camera: " << m_cameraId << std::endl;
}

void CameraWorker::startCapture()
{
  if (m_running.exchange(true)) {
    std::cout << "[CameraWorker] Already running" << std::endl;
    return; // Already running
  }

  std::cout << "[CameraWorker] Starting capture on worker thread..." << std::endl;

  // Reset counters
  m_frameCount.store(0);
  m_skippedFrames = 0;
  m_lastFrameTime = std::chrono::steady_clock::now();

  // Create capture timer (runs on worker thread)
  m_captureTimer = new QTimer(this);
  m_captureTimer->setTimerType(Qt::PreciseTimer);
  connect(m_captureTimer, &QTimer::timeout, this, &CameraWorker::captureLoop);

  int intervalMs = 1000 / m_targetFPS;
  m_captureTimer->start(intervalMs);

  // Create FPS monitoring timer (1 second updates)
  m_fpsTimer = new QTimer(this);
  connect(m_fpsTimer, &QTimer::timeout, this, &CameraWorker::updateFPS);
  m_fpsTimer->start(1000);

  emit statusMessage(QString("Capture started at %1 FPS").arg(m_targetFPS));
  std::cout << "[CameraWorker] Capture started at " << m_targetFPS << " FPS" << std::endl;
}

void CameraWorker::stopCapture()
{
  if (!m_running.exchange(false)) {
    return; // Already stopped
  }

  std::cout << "[CameraWorker] Stopping capture..." << std::endl;

  // Stop timers
  if (m_captureTimer) {
    m_captureTimer->stop();
    m_captureTimer->deleteLater();
    m_captureTimer = nullptr;
  }

  if (m_fpsTimer) {
    m_fpsTimer->stop();
    m_fpsTimer->deleteLater();
    m_fpsTimer = nullptr;
  }

  emit statusMessage("Capture stopped");
  std::cout << "[CameraWorker] Capture stopped. Total frames: " << m_frameCount.load()
    << ", Skipped: " << m_skippedFrames << std::endl;
}

void CameraWorker::captureLoop()
{
  if (!m_running.load()) {
    return;
  }

  auto frameStart = std::chrono::steady_clock::now();

  try {
    // Get camera hardware (non-blocking on UI)
    ICameraHardware* camera = m_cameraManager->GetCameraHardware(m_cameraId);
    if (!camera) {
      emit error("Camera hardware not found");
      stopCapture();
      return;
    }

    // Check if camera is still connected
    if (!camera->IsConnected()) {
      emit error("Camera disconnected");
      stopCapture();
      return;
    }

    // Capture frame (this is the potentially blocking operation)
    // ✅ Safe here - we're on worker thread!
    CameraFrameData frameData;
    if (!camera->CaptureFrame(frameData)) {
      m_skippedFrames++;
      // Don't emit error for occasional frame skip
      if (m_skippedFrames > 10) {
        emit statusMessage(QString("Warning: %1 frames skipped").arg(m_skippedFrames));
        m_skippedFrames = 0; // Reset counter
      }
      return;
    }

    // Reset skip counter on successful capture
    if (m_skippedFrames > 0) {
      m_skippedFrames = 0;
    }

    // Validate frame data
    if (frameData.imageData.empty() || frameData.width == 0 || frameData.height == 0) {
      return;
    }

    // Process frame (convert + optional processing)
    // ✅ Heavy operations safe on worker thread
    QImage image = processFrame(frameData);

    if (image.isNull()) {
      emit error("Failed to process frame");
      return;
    }

    // Emit to UI thread (QImage is implicitly shared, efficient)
    emit frameReady(image);

    // Update frame counter
    m_frameCount.fetch_add(1);

    // Calculate processing time
    auto frameEnd = std::chrono::steady_clock::now();
    double processingMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();

    // Update average processing time (simple moving average)
    double currentAvg = m_avgProcessingTime.load();
    double newAvg = (currentAvg * 0.9) + (processingMs * 0.1);
    m_avgProcessingTime.store(newAvg);

    // Store for FPS calculation
    m_lastFrameTime = frameEnd;

  }
  catch (const std::exception& e) {
    emit error(QString("Exception in capture loop: %1").arg(e.what()));
    std::cerr << "[CameraWorker] Exception: " << e.what() << std::endl;
  }
}

QImage CameraWorker::processFrame(const CameraFrameData& frameData)
{
  // Convert to QImage format
  QImage::Format format = (frameData.channels == 3)
    ? QImage::Format_RGB888
    : QImage::Format_Grayscale8;

  // Create QImage from frame data
  QImage image(frameData.imageData.data(),
    frameData.width,
    frameData.height,
    frameData.width * frameData.channels,
    format);

  // Make a deep copy for thread safety
  // (QImage uses implicit sharing, but we want independence from frameData)
  QImage result = image.copy();

  // ============================================================================
  // REAL-TIME PROCESSING SECTION
  // Add your image processing operations here!
  // ============================================================================

  if (m_enableProcessing.load()) {
    // Example: You can add processing here
    // result = applyFilter(result);
    // result = detectFeatures(result);
    // result = drawOverlay(result);

    // For now, just pass through
  }

  return result;
}

void CameraWorker::updateFPS()
{
  int frames = m_frameCount.exchange(0);
  double fps = static_cast<double>(frames);

  emit fpsUpdated(fps);
  emit processingTimeUpdated(m_avgProcessingTime.load());

  // Debug output every 5 seconds
  static int counter = 0;
  if (++counter >= 5) {
    std::cout << "[CameraWorker] FPS: " << fps
      << ", Processing: " << m_avgProcessingTime.load() << " ms" << std::endl;
    counter = 0;
  }
}

void CameraWorker::setExposure(double microseconds)
{
  if (!m_cameraManager) return;

  ICameraHardware* camera = m_cameraManager->GetCameraHardware(m_cameraId);
  if (!camera) return;

  ICameraHardware::ExposureSettings settings = camera->GetExposureSettings();
  settings.exposure_time = microseconds;

  if (camera->SetExposureSettings(settings)) {
    emit statusMessage(QString("Exposure set to %1 μs").arg(microseconds));
  }
  else {
    emit error("Failed to set exposure");
  }
}

void CameraWorker::setGain(double gain)
{
  if (!m_cameraManager) return;

  ICameraHardware* camera = m_cameraManager->GetCameraHardware(m_cameraId);
  if (!camera) return;

  ICameraHardware::ExposureSettings settings = camera->GetExposureSettings();
  settings.gain = gain;

  if (camera->SetExposureSettings(settings)) {
    emit statusMessage(QString("Gain set to %1").arg(gain));
  }
  else {
    emit error("Failed to set gain");
  }
}
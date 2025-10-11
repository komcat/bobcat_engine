// CameraWorker.cpp - With Zoom Feature
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
  , m_zoomPercent(100) // Default: no zoom
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
    return;
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
    return;
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
    ICameraHardware* camera = m_cameraManager->GetCameraHardware(m_cameraId);
    if (!camera) {
      emit error("Camera hardware not found");
      stopCapture();
      return;
    }

    if (!camera->IsConnected()) {
      emit error("Camera disconnected");
      stopCapture();
      return;
    }

    // Capture frame
    CameraFrameData frameData;
    if (!camera->CaptureFrame(frameData)) {
      m_skippedFrames++;
      if (m_skippedFrames > 10) {
        emit statusMessage(QString("Warning: %1 frames skipped").arg(m_skippedFrames));
        m_skippedFrames = 0;
      }
      return;
    }

    if (m_skippedFrames > 0) {
      m_skippedFrames = 0;
    }

    if (frameData.imageData.empty() || frameData.width == 0 || frameData.height == 0) {
      return;
    }

    // Process frame (includes zoom)
    QImage image = processFrame(frameData);

    if (image.isNull()) {
      emit error("Failed to process frame");
      return;
    }

    // Emit to UI thread
    emit frameReady(image);

    // Update frame counter
    m_frameCount.fetch_add(1);

    // Calculate processing time
    auto frameEnd = std::chrono::steady_clock::now();
    double processingMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();

    // Update average processing time
    double currentAvg = m_avgProcessingTime.load();
    double newAvg = (currentAvg * 0.9) + (processingMs * 0.1);
    m_avgProcessingTime.store(newAvg);

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
  QImage result = image.copy();

  // Apply zoom if not 100%
  int zoomLevel = m_zoomPercent.load();
  if (zoomLevel < 100) {
    result = applyZoom(result);
  }

  // Additional processing
  if (m_enableProcessing.load()) {
    // Add your custom processing here
    // result = yourProcessingFunction(result);
  }

  return result;
}

// ============================================================================
// ZOOM IMPLEMENTATION
// ============================================================================

QImage CameraWorker::applyZoom(const QImage& originalImage)
{
  int zoomLevel = m_zoomPercent.load();

  if (zoomLevel >= 100) {
    return originalImage; // No zoom needed
  }

  // Calculate ROI rectangle (centered)
  QRect roi = calculateZoomROI(originalImage.width(), originalImage.height(), zoomLevel);

  // Validate ROI
  if (roi.width() <= 0 || roi.height() <= 0) {
    std::cerr << "[CameraWorker] Invalid ROI dimensions" << std::endl;
    return originalImage;
  }

  // Extract ROI from original image
  QImage zoomed = originalImage.copy(roi);

  return zoomed; // This smaller region will be scaled up by the display
}

QRect CameraWorker::calculateZoomROI(int imageWidth, int imageHeight, int zoomPercent) const
{
  // Clamp zoom percent to valid range
  if (zoomPercent < 1) zoomPercent = 1;
  if (zoomPercent > 100) zoomPercent = 100;

  // Calculate ROI dimensions (maintaining aspect ratio)
  double zoomFactor = zoomPercent / 100.0;
  int roiWidth = static_cast<int>(imageWidth * zoomFactor);
  int roiHeight = static_cast<int>(imageHeight * zoomFactor);

  // Ensure dimensions are at least 1 pixel
  if (roiWidth < 1) roiWidth = 1;
  if (roiHeight < 1) roiHeight = 1;

  // Calculate centered position
  int roiX = (imageWidth - roiWidth) / 2;
  int roiY = (imageHeight - roiHeight) / 2;

  // Debug output
  static int debugCounter = 0;
  if (++debugCounter >= 30) { // Print every second at 30 FPS
    std::cout << "[Zoom] " << zoomPercent << "% → ROI: "
      << roiX << "," << roiY << " "
      << roiWidth << "x" << roiHeight
      << " from " << imageWidth << "x" << imageHeight << std::endl;
    debugCounter = 0;
  }

  return QRect(roiX, roiY, roiWidth, roiHeight);
}

void CameraWorker::setZoomPercent(int percent)
{
  // Clamp to valid range
  if (percent < 1) percent = 1;
  if (percent > 100) percent = 100;

  int oldZoom = m_zoomPercent.exchange(percent);

  if (oldZoom != percent) {
    emit zoomChanged(percent);
    emit statusMessage(QString("Zoom: %1%").arg(percent));

    std::cout << "[CameraWorker] Zoom changed: " << percent << "%" << std::endl;
  }
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
      << ", Processing: " << m_avgProcessingTime.load() << " ms"
      << ", Zoom: " << m_zoomPercent.load() << "%" << std::endl;
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
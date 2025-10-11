// CameraWorker.h
#pragma once

#include <QObject>
#include <QImage>
#include <QTimer>
#include <memory>
#include <atomic>
#include <string>

// Forward declarations
class CameraManager;
struct CameraFrameData;
class ICameraHardware;

/**
 * @brief Worker class that runs on separate thread for camera operations
 *
 * Handles:
 * - Frame capture (non-blocking UI)
 * - Image conversion
 * - Real-time processing
 * - Performance monitoring
 */
class CameraWorker : public QObject
{
  Q_OBJECT

public:
  explicit CameraWorker(CameraManager* cameraManager, const std::string& cameraId);
  ~CameraWorker();

  // Configuration
  void setTargetFPS(int fps) { m_targetFPS = fps; }
  void setEnableProcessing(bool enable) { m_enableProcessing = enable; }

signals:
  // Emitted when new frame is ready for display
  void frameReady(QImage image);

  // Performance monitoring
  void fpsUpdated(double fps);
  void processingTimeUpdated(double ms);

  // Error handling
  void error(QString errorMsg);

  // Status updates
  void statusMessage(QString msg);

public slots:
  // Thread control
  void startCapture();
  void stopCapture();

  // Settings that can be called from any thread
  void setExposure(double microseconds);
  void setGain(double gain);

private slots:
  // Main capture loop - runs on worker thread
  void captureLoop();

  // Performance monitoring
  void updateFPS();

private:
  // Frame processing (extend this for real-time operations)
  QImage processFrame(const CameraFrameData& frameData);

  // Helper methods
  void updatePerformanceStats();

  // Camera components
  CameraManager* m_cameraManager;
  std::string m_cameraId;

  // Thread control
  std::atomic<bool> m_running;
  QTimer* m_captureTimer;
  QTimer* m_fpsTimer;

  // Settings
  int m_targetFPS;
  std::atomic<bool> m_enableProcessing;

  // Performance monitoring
  std::atomic<int> m_frameCount;
  std::atomic<double> m_avgProcessingTime;
  std::chrono::steady_clock::time_point m_lastFrameTime;

  // Frame skip detection
  int m_skippedFrames;
};
// QtCameraBeamStreamController.h
// Self-contained "Beam Streaming" group: an enable checkbox plus a
// camera-selection combo. Owns the worker thread that drains an RGB
// frame queue, converts each frame to mono8 grayscale, and ships it
// over a TCP `BeamServerClient` to a remote beam-detection service.
//
// Two viewport push paths exist:
//   1. Asynchronous: the worker thread requests `m_viewport->update()`
//      after each STREAM_FRAME response.
//   2. Per-frame: `pushOverlayToViewport()` is called by the window's
//      `updateViewport()` so the overlay stays in sync with the camera
//      frame cadence.
//
// Extracted from QtCameraWindow so the window class can stay focused
// on camera display + side-panel orchestration. Behaviour is preserved
// exactly:
//   * QSettings keys (`BeamServer/Host`, `StreamPort`, `UmPerPxX`,
//     `UmPerPxY`, `Rotate180`, `IntervalMs`) are unchanged.
//   * Fallback file `config/beam_stream.json` overrides QSettings if
//     present.
//   * Frame queue depth capped at 2 (drops on overflow).
//   * mono8 conversion is plain (R+G+B)/3 average.
#pragma once

#include <QGroupBox>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

class QCheckBox;
class QComboBox;
class QtCameraViewport;
class CameraManager;
class BeamServerClient;

class QtCameraBeamStreamController : public QGroupBox
{
  Q_OBJECT

public:
  explicit QtCameraBeamStreamController(QtCameraViewport* viewport,
                                        QWidget* parent = nullptr);
  ~QtCameraBeamStreamController() override;

  // Frame ingestion (from QtCameraWindow::OnNewFrame on the camera
  // thread). Only the camera id matching the beam-stream selection is
  // forwarded; others are dropped.
  void enqueueFrame(const std::vector<uint8_t>& rgbData,
                    uint32_t width, uint32_t height,
                    const std::string& cameraId);

  // The page calls this from OnNewFrame for the *primary* camera so
  // STREAM_START can report a valid frame size even before the beam
  // queue has accumulated a frame.
  void setCurrentPrimaryFrameSize(uint32_t width, uint32_t height);

  // Push the latest beam-detection result to the viewport overlay.
  // Called every frame by the window's updateViewport().
  void pushOverlayToViewport();

  // Repopulate the camera-selection combo. The window calls this
  // during initialize() and refreshCameraList(). The current selection
  // is preserved if still present in the new list.
  void refreshCameraList(const std::vector<std::string>& cameraIds);

  // Public API (mirrors legacy QtCameraWindow surface).
  bool isStreamActive() const;
  nlohmann::json getLatestResult() const;
  uint64_t getResultSequence() const;
  double getFps() const;

  // Synchronous shutdown — joins the worker thread + tears down the
  // TCP connection. Call from owner's destructor before its members
  // go away.
  void shutdown();

private slots:
  void onEnableToggled(bool checked);
  void onCameraChanged(const QString& cameraId);

private:
  void setupUI();
  void loadConfig();
  void startStreaming();
  void stopStreaming();
  void threadLoop();

  QtCameraViewport* m_viewport;

  // UI controls
  QCheckBox* m_enableCheck = nullptr;
  QComboBox* m_cameraCombo = nullptr;

  // Stream config
  struct StreamConfig {
    std::string host       = "127.0.0.1";
    int    port            = 5567;
    double umPerPxX        = 0.25;
    double umPerPxY        = 0.25;
    bool   rotate180       = false;
    int    intervalMs      = 100;
  };
  StreamConfig m_config;

  // Worker thread state
  std::unique_ptr<BeamServerClient> m_streamClient;
  std::atomic<bool>                m_streamEnabled{false};
  std::atomic<bool>                m_threadRunning{false};
  std::thread                      m_thread;
  std::queue<std::vector<uint8_t>> m_frameQueue;
  uint32_t                         m_queuedWidth  = 0;
  uint32_t                         m_queuedHeight = 0;
  std::mutex                       m_frameQueueMutex;
  std::condition_variable          m_frameQueueCV;
  std::string                      m_streamCameraId;

  // Latest known primary-camera frame size (for STREAM_START).
  std::atomic<uint32_t> m_currentPrimaryWidth{0};
  std::atomic<uint32_t> m_currentPrimaryHeight{0};

  // Result state
  nlohmann::json        m_lastResult;
  mutable std::mutex    m_resultMutex;
  std::atomic<uint64_t> m_resultSequence{0};
  std::atomic<double>   m_fps{0.0};
};

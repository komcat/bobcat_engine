// GdiBeamStreamController.h
// Self-contained beam-stream controller for GdiCameraWindow.
// Owns:
//   * Win32 "Beam Stream" popup menu (enable / camera list)
//   * `BeamServerClient` connection to the remote beam server
//   * Worker thread that drains an RGB frame queue, converts to mono8
//     grayscale, ships frames over the connected client, and updates
//     the supplied `BeamStreamOverlay` with each response.
//   * Stream config loaded from QSettings (`BeamServer/...`) with
//     fallback to `config/beam_stream.json`.
//
// Extracted from GdiCameraWindow so the window class can stay focused
// on Win32 message routing + GDI rendering. Behaviour is preserved
// exactly:
//   * Same menu IDs (IDM_BEAM_STREAM_*).
//   * Same QSettings keys + JSON fallback path.
//   * Same protocol: STREAM_START, STREAM_FRAME, STREAM_STOP.
//   * Frame queue depth capped at 2 (drops on overflow).
#pragma once

#include "BeamStreamOverlay.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

#include <nlohmann/json.hpp>

class BeamServerClient;

class GdiBeamStreamController
{
public:
  // The overlay is owned by the window; the controller writes results
  // into it. Window handle is set later (post-Create) for the
  // PostMessage(WM_BEAM_STREAM_RESULT_READY) hook.
  explicit GdiBeamStreamController(BeamStreamOverlay* overlay);
  ~GdiBeamStreamController();

  // Build the "Beam Stream" popup, append it to `menuBar`. Returns the
  // popup HMENU so the page can compare it inside WM_INITMENUPOPUP.
  HMENU BuildMenu(HMENU menuBar);
  HMENU GetMenu() const { return m_menu; }

  // Hook for WM_INITMENUPOPUP. The page passes the current camera list
  // (the controller doesn't own a CameraManager).
  void OnMenuOpening(const std::vector<std::string>& cameraIds);

  // Hook for WM_COMMAND: returns true if commandId belongs to this
  // controller's menu and was handled. The page provides the current
  // camera list because IDM_BEAM_STREAM_ENABLE auto-selects the first
  // available camera if none is selected yet.
  bool HandleMenuCommand(WORD commandId,
                         const std::vector<std::string>& cameraIds);

  // Window handle for PostMessage(WM_BEAM_STREAM_RESULT_READY). Set by
  // the page after Create() succeeds; the controller does not own the
  // HWND.
  void SetWindowHandle(HWND hwnd) { m_hwnd = hwnd; }

  // Frame ingestion (from GdiCameraWindow::OnNewFrame on the camera
  // thread). Frames are filtered by camera id — only the camera
  // selected for streaming is forwarded.
  void EnqueueFrame(const std::vector<uint8_t>& rgbData,
                    uint32_t width, uint32_t height,
                    const std::string& cameraId);

  // The page calls this from OnNewFrame for the *primary* camera so
  // STREAM_START can report a valid frame size before the beam queue
  // has accumulated a frame.
  void SetCurrentPrimaryFrameSize(uint32_t width, uint32_t height);

  // True if `cameraId` is the camera currently selected for streaming.
  // Used by GdiCameraWindow::WantsFramesFromCamera() to opt-in.
  bool IsStreamCamera(const std::string& cameraId) const;

  // Public API (mirrors legacy GdiCameraWindow surface).
  bool IsActive() const { return m_streamEnabled.load() && m_threadRunning.load(); }
  nlohmann::json GetLatestResult() const { return m_overlay ? m_overlay->GetResult() : nlohmann::json(); }
  uint64_t GetResultSequence() const { return m_overlay ? m_overlay->GetResultSequence() : 0; }
  double GetFps() const { return m_overlay ? m_overlay->GetFps() : 0.0; }

  // Pixel-to-mm calibration (loaded from QSettings / beam_stream.json).
  // Used by BeamStreamOverlay::Draw on the rendering thread; safe to
  // read from any thread because StreamConfig is only mutated from the
  // start/stop flow on the GUI thread.
  double GetUmPerPxX() const { return m_config.umPerPxX; }
  double GetUmPerPxY() const { return m_config.umPerPxY; }

  // Synchronous shutdown — stops the stream + joins the worker thread.
  void Shutdown();

private:
  void UpdateMenu(const std::vector<std::string>& cameraIds);
  void LoadConfig();
  void StartStreaming();
  void StopStreaming();
  void ThreadLoop();

  BeamStreamOverlay* m_overlay;
  HWND m_hwnd = nullptr;
  HMENU m_menu = nullptr;

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
  std::atomic<bool>                 m_streamEnabled{false};
  std::atomic<bool>                 m_threadRunning{false};
  std::thread                       m_thread;
  std::queue<std::vector<uint8_t>>  m_frameQueue;
  uint32_t                          m_queuedWidth  = 0;
  uint32_t                          m_queuedHeight = 0;
  std::mutex                        m_frameQueueMutex;
  std::condition_variable           m_frameQueueCV;

  // Selected camera id (matched against frameData.cameraId in
  // EnqueueFrame).
  std::string        m_streamCameraId;
  mutable std::mutex m_streamCameraMutex;

  // Latest known primary-camera frame size (for STREAM_START).
  std::atomic<uint32_t> m_currentPrimaryWidth{0};
  std::atomic<uint32_t> m_currentPrimaryHeight{0};
};

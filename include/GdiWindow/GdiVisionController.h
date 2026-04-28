// GdiVisionController.h
// Self-contained continuous-vision controller for GdiCameraWindow.
// Owns:
//   * Win32 "Vision" popup menu (enable / refresh / profile list)
//   * Worker thread that consumes RGB frames from a 2-deep queue,
//     runs the active profile through VisionManager, and pushes the
//     resulting VisionResult into the supplied VisionResultOverlay.
//   * Profile discovery (config/vision and config/<MACHINE>/vision)
//   * Display-rotation-aware frame queueing (rotates to match what
//     the user sees, so vision processes the displayed image).
//
// Extracted from GdiCameraWindow so the window class can stay focused
// on Win32 message routing + GDI rendering. Behaviour is preserved
// exactly:
//   * Same menu IDs (IDM_VISION_*) so external GUI scripts keep
//     working.
//   * Same profile-discovery search paths (`config/vision/`, then
//     per-machine `config/<MACHINE>/vision/`).
//   * Same rotation handling: queues -90deg, +90deg, or unrotated RGB
//     based on the rotation passed at enqueue time.
//   * Posts WM_VISION_RESULT_READY to the owning window after each
//     completed profile execution.
//
// Conditionally compiled: only declared/defined when HAS_OPENCV is set.
#pragma once

#ifdef HAS_OPENCV

#include "VisionResultOverlay.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

class GdiVisionController
{
public:
  // The overlay is owned by the window; the controller writes results
  // into it. Window handle is set later (post-Create) for the
  // PostMessage(WM_VISION_RESULT_READY) hook.
  explicit GdiVisionController(VisionResultOverlay* overlay);
  ~GdiVisionController();

  // Build the "Vision" popup, append it to `menuBar`, and run the
  // initial profile discovery. Returns the popup HMENU so the page
  // can compare it inside WM_INITMENUPOPUP.
  HMENU BuildMenu(HMENU menuBar);
  HMENU GetMenu() const { return m_menu; }

  // Hook for WM_INITMENUPOPUP: refresh profiles + update the menu.
  void OnMenuOpening();

  // Hook for WM_COMMAND: returns true if commandId belongs to this
  // controller's menu and was handled.
  bool HandleMenuCommand(WORD commandId);

  // Window handle for PostMessage(WM_VISION_RESULT_READY). Set by the
  // page after Create() succeeds; the controller does not own the HWND.
  void SetWindowHandle(HWND hwnd) { m_hwnd = hwnd; }

  // Frame ingestion (from GdiCameraWindow::OnNewFrame on the camera
  // thread). The queue is single-producer/single-consumer with depth 2.
  // `displayRotation` is one of {0, -90, +90}. Non-zero rotations are
  // applied here before queueing so the vision thread sees what the
  // user sees.
  void EnqueueFrame(const std::vector<uint8_t>& rgbData,
                    uint32_t width, uint32_t height,
                    int displayRotation);

  // Public API (mirrors legacy GdiCameraWindow surface).
  void SetEnabled(bool enabled);
  bool IsActive() const { return m_visionEnabled.load() && m_threadRunning.load(); }
  VisionResult GetLatestResult() const { return m_overlay ? m_overlay->GetResult() : VisionResult(); }
  uint64_t GetResultSequence() const { return m_overlay ? m_overlay->GetResultSequence() : 0; }
  void SelectProfile(const std::string& profilePath);

  // Synchronous shutdown — joins the worker thread. Call from owner's
  // destructor before its members go away.
  void Shutdown();

private:
  void DiscoverProfiles();
  void UpdateMenu();
  void StartProcessing();
  void StopProcessing();
  void ThreadLoop();

  VisionResultOverlay* m_overlay;
  HWND m_hwnd = nullptr;
  HMENU m_menu = nullptr;

  // Worker thread state
  std::atomic<bool>                m_visionEnabled{false};
  std::atomic<bool>                m_threadRunning{false};
  std::thread                      m_thread;
  std::queue<std::vector<uint8_t>> m_frameQueue;
  uint32_t                         m_queuedWidth  = 0;
  uint32_t                         m_queuedHeight = 0;
  std::mutex                       m_frameQueueMutex;
  std::condition_variable          m_frameQueueCV;

  // Profile state
  std::string        m_activeProfile;
  mutable std::mutex m_profileMutex;

  // Profile discovery
  struct ProfileInfo { std::string fullPath; std::string displayName; };
  std::vector<ProfileInfo> m_availableProfiles;
};

#endif  // HAS_OPENCV

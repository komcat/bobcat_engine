# GdiCameraWindow Layout

`include/GdiWindow/`

The native Win32+GDI camera monitor (sibling of `QtCameraWindow`, instantiated x2 — toggled with the **GDI** button in the top bar, plus Ctrl+R / Ctrl+T which prefer the Qt window first then fall back to GDI). Uses raw Win32 menus instead of side-panel widgets, so the helper shape differs from `QtCameraWindow`.

| File | Owns |
|------|------|
| `GdiCameraWindow.{h,cpp}` | Top-level window: `WindowProc` / `HandleMessage`, menu bar construction, GDI rendering pipeline (`OnPaint`, `RenderFrame`, `PrepareBackBuffer`, double-buffering), camera-frame ingestion (`OnNewFrame` / `OnCameraStatusChanged`), display rotation, click-to-move, crosshair drawing, calibration loading, FPS overlay, dual-camera mode. The orchestrator — keep it thin. |
| `ChartOverlay` | Live chart overlay drawn on top of the camera frame. DataStore subscription + per-channel ring buffers + scale. Owned by the window, fed via `OnDataStoreValueChanged`. |
| `VisionResultOverlay` (HAS_OPENCV) | Renders the latest `VisionResult` (boxes, text, profile name) on top of the camera frame. Owned by the window, written by `GdiVisionController`. |
| `BeamStreamOverlay` | Renders beam-detection results received from the beam server. Owned by the window, written by `GdiBeamStreamController`. |
| `GdiVisionController` (HAS_OPENCV) | Vision worker thread + profile discovery + "Vision" Win32 popup menu. Frame ingestion via `EnqueueFrame()` (rotation-aware so the vision thread sees what the user sees). Posts `WM_VISION_RESULT_READY` after each profile execution. Settings: same as `QtCameraVisionController` (HAS_OPENCV section in profile JSON, no per-instance QSettings). |
| `GdiBeamStreamController` | Beam-stream worker thread + `BeamServerClient` lifecycle + "Beam Stream" Win32 popup menu. Settings: `BeamServer/Host`, `StreamPort`, `UmPerPxX`, `UmPerPxY`, `Rotate180`, `StreamIntervalMs` (QSettings); fallback `config/beam_stream.json`. Frames are filtered by camera id inside `EnqueueFrame()`. |
| `GdiCameraWindowManager` | Qt-side lifecycle owner of the `GdiCameraWindow`. The two camera-monitor instances are addressed as `gdiCameraWindowManager` and `gdiCameraWindowManager2` in `ApplicationContext`. |

When extending the GDI window, prefer adding/expanding a helper over enlarging `GdiCameraWindow.cpp`. The window should not own vision threads, beam-stream sockets, or beam-server config — those have homes already.

**Menu integration pattern:** each controller exposes `BuildMenu(HMENU menuBar)` (creates and attaches its popup), `GetMenu()` (for `WM_INITMENUPOPUP` routing), `OnMenuOpening(...)` (refresh items on open), and `HandleMenuCommand(WORD)` (returns true if the ID was handled). The window's `OnMenuCommand` dispatches by attempting each controller in turn, falling through to its own (Tools/Camera/Show) handlers if none claims it.

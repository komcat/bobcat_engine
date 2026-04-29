# QtCameraWindow Layout

`include/QtCameraWindow/`

The Qt-based camera monitor (`QtCameraWindow`, instantiated x2 — toggled with Ctrl+R / Ctrl+T) is composed from focused helpers — touch the helper for the area you're working in, **not** the orchestrator.

| File | Owns |
|------|------|
| `QtCameraWindow.{h,cpp}` | Top-level window, side-panel layout assembly, frame ingestion (`OnNewFrame` / `OnCameraStatusChanged` — `CameraFrameSubscriber` impl), `updateViewport()` per-frame push to the viewport, click-to-move, calibration loading, auto-focus, top-level QSettings (geometry / camera selection / tools). The orchestrator — keep it thin. |
| `QtCameraViewport` | The QPainter-based image surface. Renders the live frame plus all overlays (chart / vision / beam / device-tracking / crosshair / EE). Owned and updated by the window. |
| `QtCameraQuickMovePanel` | Left column 6×4 grid of "quick move" buttons that issue relative gantry moves via `UnifiedMotionManager`. Per-button label + ΔX/ΔY/ΔZ configurable via right-click; persisted to QSettings (`QtCameraWindow[/<instanceId>]/QuickMove/...`). |
| `QtCameraTrackingPanel` | Session-only device tracking: register P0 from gantry-main XY, register hex-left/hex-right, transform local-Δ to global via `transformation_matrix.json`, polling QTimer at 10 Hz, alert thresholds with hysteresis, button colour change on alert. |
| `QtCameraChartOverlay` | "Chart Overlay" tab: subscribes to `DataStore`, accumulates 200-point ring buffers per channel, throttled (~20 Hz) viewport push + per-frame push from `updateViewport()`. Settings: `ChartOverlay[<instanceId>]/...`. |
| `QtCameraVisionController` (HAS_OPENCV) | "Vision" tab: profile combo + enable checkbox. Owns the worker thread that drains a 2-deep RGB queue (fed by `OnNewFrame`), runs the active profile through `VisionManager`, hands `VisionResult` to the viewport. Also exposes `showExternalOverlay(...)` for callers like `VisionCommandHandler::execute_profile`. Settings: `Vision[<instanceId>]/...`. |
| `QtCameraBeamStreamController` | "Beam" tab: enable + camera-select. Owns the worker thread that ships mono8 frames over TCP via `BeamServerClient`. Settings: `BeamServer/...` (global) + fallback `config/beam_stream.json`. |

When extending the camera window, prefer adding/expanding a helper over enlarging `QtCameraWindow.cpp`. The window should not own quick-move data, tracking state, chart channel buffers, vision threads, or beam-stream sockets — those have homes already.

**Construction order constraint:** the viewport must be created *before* `buildSidePanel()` and `buildQuickMovePanel()` because the chart / tracking / vision / beam helpers all observe it at construction. Layout-add order is independent of construction order.

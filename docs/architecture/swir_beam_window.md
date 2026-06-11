# SWIR Beam Window Layout

`include/SwirBeamWindow/`

Real-time beam profiling/tracking shown in a Win32+GDI+ window (not Qt). The window is composed from several focused helpers — touch the helper for the area you're working in, **not** the orchestrator.

> See also `docs/SWIR_BEAM_DETECTION.md` for the detection algorithm itself (d4σ math, ROI extraction, EE computation).

## File layout

| File | Owns |
|------|------|
| `SwirBeamWindow.{h,cpp}` | Win32 message pump, frame ingestion (`CameraFrameSubscriber`), detection thread + B0/B1 tracking, panel/button rendering, click-to-move, PSU/TEC actions, magnification dialog. The orchestrator — should stay focused. |
| `SwirBeamDetector.{h,cpp}` | Three detection algorithms (d4σ / blob / Halcon) — see table below. |
| `PoyntBeamDetector.{h,cpp}` | `poynt` mode — faithful C++ port of poynt `server_v2.py` live tracking. Reuses `find_roi`/`d4sigma_fast`/`ComputeEncircledEnergy` from `SwirBeamDetector`. |
| `SwirPeakFinder.{h,cpp}` | 1-D peak detection used by the d4sigma path. |
| `SwirBeamOverlay.{h,cpp}` | All GDI+ overlay drawing: beam ROIs, centroids, EE ellipse, CE/EE charts, bottom-strip beam crops (heatmap-coloured). |
| `SwirBeamConfigStore.{h,cpp}` | JSON I/O for `config/beam_detection.json` — the d4σ + blob + Halcon configs and tracking flags. Pure load/save. |
| `SwirWorldMarkerStore.{h,cpp}` | World-locked V/H markers (Place / ClearAll / Snapshot / SaveToFile / LoadFromFile). The window keeps the dialog/render and forwards state mutation here. |
| `SwirImageSaver.{h,cpp}` | The five save APIs: `SaveFrameAsPng`, `SaveFrameWithOverlayAsPng`, `SaveBeamCropAsPng`, `SaveCollageAsPng`, `SaveImagePackage`. Pulls fresh snapshots via `Deps` callbacks, no window state held. |
| `SwirBeamFocusController.{h,cpp}` | BF (beam focus) hill-climb worker future + cancel flag. |
| `SwirAutoSatController.{h,cpp}` | AUTO SAT one-shot algorithm: phase-1 exposure/current bracket tuning + phase-2 ±50 µm gantry-Z focus sweep with mid-sweep retune. Worker future, abort flag, all `AS_*` constants live here. |
| `SwirBeamWindowManager.{h,cpp}` | Qt-side lifecycle owner. Manages the window's `std::shared_ptr` and exposes the public API (`saveImagePackage`, `enableBeamDetection`, `enableAutoSatPrevention`, `getBeamSnapshots`) wired to the `SystemCommandHandler` callbacks in `Application.cpp`. |

When extending the SWIR window, prefer adding a new helper class over enlarging `SwirBeamWindow.cpp`.

## Detection modes

Four detection modes coexist and are selected at runtime via `config/beam_detection.json`. Dispatch priority (`SwirBeamWindow.cpp`): **Halcon > Poynt > Blob > d4sigma**.

| Mode | Function | Notes |
|------|----------|-------|
| `halcon` (plan of record) | `RunHalconBeamDetection` | HDevelop port: local-mean BG (51×51) + threshold + connected components + per-region first-moment D4σ. Requires `HAS_HALCON`. Toggle: `useHalconDetection: true`. |
| `poynt` | `RunPoyntDetection` (`PoyntBeamDetector.cpp`) | Faithful port of poynt `server_v2.py` live tracking: **4-corner** BG + X/Y projection peak finding + per-ROI D4σ, **no rejection filters**. Tracks dim/odd beams better than legacy d4sigma. Toggle: `usePoyntDetection: true`. Config under nested `"poynt"` key. |
| `blob` | `RunBlobDetection` | OpenCV threshold + contour. Toggle: `useBlobDetection: true`. |
| `d4sigma` (legacy) | `RunBeamDetection` | OpenCV X/Y projection peak finding + **2-corner** BG + aggressive rejection filters. Default if no flag is set. Has known robustness issues vs Halcon/Poynt. |

For new tuning/fixes prefer the Halcon path. Don't propose tuning d4sigma thresholds (`minPeakValue`, `aspectRatioMax`, etc.) unless the miss log shows `mode=d4sigma`.

## Frame pipeline (single-slot, latest-wins)

- `CameraManager` rate-limits broadcasts to subscribers (`GetMinFrameIntervalMs()` returns 33 ms — ~30 fps cap).
- `SwirBeamWindow::OnNewFrame` writes two single-slot buffers under separate mutexes:
  - `m_detectionBuffer` (gray) — overwritten if detection thread hasn't consumed previous frame; tracked by `m_detectionInputDrops`.
  - `m_frameBuffer` (BGR) — write skipped entirely (`m_hasNewFrame` flag) when UI repaint hasn't completed.
- Detection runs on its own `std::thread` at `THREAD_PRIORITY_BELOW_NORMAL`, sleeping on `m_detectionCV` between frames.
- No queue. Frame drops at the detection input are silent except for the `[BeamDetect] detection-input drops total=...` console line.

## Beam ID stability

After detection, the caller in `SwirBeamWindow.cpp` (the detection thread lambda) re-orders the result vector via nearest-neighbour association to `m_prevBeams[2]` so that slot 0 / slot 1 keep following the same physical beam across frames. CE/EE history, broker publishing, and overlay rendering all depend on this slot identity. Single-beam frames are slot-aware: the lone candidate is matched to the closer prior slot rather than always overwriting slot 0.

## GA quadrant labels

`gaQuadrantLabels` in `beam_detection.json` maps each visual quadrant to a GA channel string. With the current Y-flip convention used by the labeling code, **set top-quadrants (`q1`, `q2`) to the GA channel that is physically at the bottom of the image** and vice versa — verify against the live overlay before committing changes.

// SwirBeamWindow.h
#pragma once

#include <winsock2.h>
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>
#include <future>
#include <condition_variable>
#include <chrono>
#include "camera/CameraFrameData.h"
#include "SwirBeamDetector.h"
#include "PoyntBeamDetector.h"
#include "BeamOptimizer/BeamHillClimb.h"
#include "BeamOptimizer/BeamScoreProvider.h"
#include "sequence/SystemCommandHandler.h"  // for SwirBeamSnapshot

enum class SwirMarkerType { VLine, HLine };

struct SwirWorldMarker {
    int id = 0;
    SwirMarkerType type = SwirMarkerType::VLine;
    double worldX = 0.0;    // Gantry X (mm) at placement time
    double worldY = 0.0;    // Gantry Y (mm) at placement time
    std::string label;       // Auto-label: "V1", "H2", etc.
};

class BeamResultBroker;
class CameraConfigManager;
class CameraManager;
class PowerSupplyManager;
class PneumaticManager;
class TECManager;
class UnifiedMotionManager;

/**
 * @brief Clickable button region for GDI-painted UI
 */
struct SwirButton {
    RECT rect;
    std::wstring label;
    bool hovered = false;
    bool pressed = false;
    bool toggled = false;  // For toggle buttons (ON/OFF state)
    bool isToggle = false; // Whether this is a toggle button
    int tag = 0;           // Identifier for button groups (e.g. PSU selector)
    std::function<void()> onClick;
};

/**
 * @brief Per-PSU runtime state (polled readings + settings)
 */
struct PSUDeviceState {
    std::string name;
    int channelCount = 1;
    int selectedChannel = 1;   // 1-based; which channel the controls act on
    double voltage = 0.0;
    double current = 0.0;
    bool outputOn = false;
    double setCurrent = 0.0;
};

/**
 * @brief Pure Win32 GDI window for SWIR beam processing
 *
 * Layout:
 * - Left panel (25%): PSU controls + SWIR exposure settings
 * - Right panel (75%): Real-time camera image viewport
 *
 * Subscribes to a SWIR camera for live frame display.
 * Controls power supply output and camera exposure via button clicks.
 */
class SwirBeamWindow : public CameraFrameSubscriber {
public:
    SwirBeamWindow();
    ~SwirBeamWindow();

    // Window lifecycle
    bool Create(const std::string& title = "SWIR Beam Window",
                int x = CW_USEDEFAULT, int y = CW_USEDEFAULT,
                int width = 1200, int height = 700);
    void Destroy();

    // Window control
    void Show();
    void Hide();
    bool IsVisible() const;
    void SetTitle(const std::string& title);

    // Emergency stop: cancels any running optimization (beam focus, etc.)
    void EmergencyStop();

    // Manager injection
    void SetCameraManager(CameraManager* cameraManager);
    void SetPowerSupplyManager(PowerSupplyManager* psuManager);
    void SetMotionManager(UnifiedMotionManager* motionManager);
    void SetCameraConfigManager(CameraConfigManager* configMgr);
    void SetPneumaticManager(PneumaticManager* pneumaticManager);
    void SetTECManager(TECManager* tecManager) { m_tecManager = tecManager; }
    void SetBeamResultBroker(BeamResultBroker* broker) { m_beamResultBroker = broker; }

    // Configuration
    void SetTargetCamera(const std::string& cameraId);
    std::string GetTargetCamera() const;

    // Subscriber identity
    void SetSubscriberId(const std::string& id) { m_subscriberId = id; }

    // CameraFrameSubscriber interface
    void OnNewFrame(const CameraFrameData& frameData) override;
    void OnCameraStatusChanged(const std::string& cameraId, bool connected, bool grabbing) override;
    std::string GetSubscriberId() const override { return m_subscriberId; }
    bool WantsFramesFromCamera(const std::string& cameraId) const override;
    int GetMinFrameIntervalMs() const override { return 33; } // ~30fps

    // Get native window handle
    HWND GetHwnd() const { return m_hwnd; }

    // Drain pending Win32 messages for THIS window only. Filtering by
    // m_hwnd prevents stealing/re-dispatching Qt window messages — see
    // the matching comment in GdiCameraWindow.h.
    bool ProcessMessages();

    // PSU polling (called from manager timer)
    void PollPSUReadings();

private:
    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Rendering
    void OnPaint();
    void RenderFrame(HDC hdc, const RECT& imageRect);
    void RenderControlPanel(HDC hdc, const RECT& panelRect);
    void PrepareBackBuffer();

    // Button management
    void RebuildButtons(const RECT& panelRect);
    void DrawButton(Gdiplus::Graphics& gfx, const SwirButton& btn);
    SwirButton* HitTestButton(int x, int y);

    // PSU actions
    void OnSelectPSU(int index);
    void OnSelectPSUChannel(int channel);  // 1-based; multi-channel devices only
    void OnPSUToggle();
    void OnCurrentIncrease();
    void OnCurrentDecrease();
    void OnCurrentIncreaseSmall();
    void OnCurrentDecreaseSmall();

    // Top-bar gantry quick-move: move gantry-main to a named position.
    void OnMoveToNamedPosition(const std::string& positionName);

    // Top-bar right-click: teach the named position from the CURRENT gantry-main
    // location. Shows a stored-vs-current comparison dialog and saves on confirm.
    void OnSaveCurrentToNamedPosition(const std::string& positionName);

    // Exposure actions
    void OnAutoExposureToggle();
    void OnSetExposure(double microseconds);
    void OnExposureIncrease();
    void OnExposureDecrease();
    void OnApplyManualExposure();

    // Click-to-move and overlays
    void HandleClickToMove(int mouseX, int mouseY);
    void LoadCalibrationFromConfig();
    void DrawOverlays(HDC hdc);
    void DrawGroupBox(Gdiplus::Graphics& gfx, const RECT& rect, const wchar_t* title);

    // World-locked markers
    void OnPlaceMarker();
    void OnClearAllMarkers();
    void DrawWorldMarkers(Gdiplus::Graphics& gfx);
    void SaveMarkersToFile();
    void LoadMarkersFromFile();
    double GetDisplayPxPerMm() const;

    // Magnification + camera orientation (the SET MAG dialog edits both)
    void OnSetMagnification();
    void SaveMagnification();
    void LoadMagnification();
    void SaveCameraOrientation();
    void LoadCameraOrientation();

    // Map a frame-pixel offset (+X right, +Y down) to a gantry XY move (mm),
    // applying the configured camera orientation (0 or 90 deg) and the current
    // magnification. Shared by click-to-move and center-on-beams.
    void PixelOffsetToGantryMm(double pxOffX, double pxOffY,
                               double effPxToMmX, double effPxToMmY,
                               double& outDxMm, double& outDyMm) const;

    // SWIR Head pneumatic
    void OnSwirHeadToggle();

    // Auto-tune & focus algorithm: button click handler + manual-override
    // helper. The actual algorithm lives in SwirAutoSatController.
    void OnAutoSatToggle();
    void DisableAutoSat();   // called by manual exposure/current changes
    void WireAutoSatDeps();  // populate controller deps from current state

    // BEAM FIT: gantry-main Z waist sweep, independent of AUTO SAT. Button
    // sits next to the bottom beam-crop strip. Algorithm lives in
    // SwirBeamFitController.
    void OnBeamFitToggle();
    void WireBeamFitDeps();

    // CENTER XY: move gantry-main so the midpoint of the detected beam(s) sits
    // at the frame center. Shows a confirm dialog with the relative ΔX/ΔY
    // first. Reuses the click-to-move calibration chain. Strip button.
    void OnCenterBeamsXY();

    // Save current frame to disk (button handlers — private; UI-only)
    void OnSaveImage();
    void OnSaveImagePackage();

public:
    // Save APIs callable from C++/manager; safe to call from any thread.
    // All methods snapshot the live frame + beam state under the
    // appropriate mutexes before encoding, so they don't block detection.
    bool SaveFrameAsPng(const std::wstring& fullPath);
    bool SaveFrameWithOverlayAsPng(const std::wstring& fullPath);
    bool SaveBeamCropAsPng(int slotIndex, const std::wstring& fullPath);
    bool SaveCollageAsPng(const std::wstring& fullPath);

    // One-call: writes <prefix>_raw.png, <prefix>_overlay.png, <prefix>_B0.png,
    // <prefix>_B1.png, <prefix>_collage.png into dirPath. Returns true if all
    // requested files saved; populates outFiles with absolute paths written.
    bool SaveImagePackage(const std::wstring& dirPath,
                          const std::wstring& namePrefix,
                          std::vector<std::wstring>* outFiles = nullptr);

    // Programmatic toggles (for scripts/recipes — same effect as clicking the
    // B DETECT / AUTO SAT buttons). No-op if already in the requested state.
    void EnableBeamDetection(bool enable);
    void EnableAutoSatPrevention(bool enable);

    // Returns a snapshot of the current beam display state (size 0..2).
    // Snapshots `m_beamDisplayResults` under `m_beamMutex`. Each entry is a
    // POD copy — no live pointers — safe to use from any thread / from
    // command handlers. Caller-supplied micronsPerPixel is filled in too;
    // pass `useHalcon` for the right config source.
    std::vector<SwirBeamSnapshot> GetBeamSnapshots() const;

private:
    // Beam detection
    void OnBeamDetectToggle();
    void StartDetectionThread();
    void StopDetectionThread();

    // Beam config
    void LoadBeamConfig();
    void SaveBeamConfig();
    void OnEditBeamConfig();          // B CONFIG: per-beam label selector dialog
    void EditBeamConfigJson();        // open beam_detection.local.json in notepad
    void LoadBeamSlotLabels();        // restore m_beamSlotLabels from QSettings
    void SaveBeamSlotLabels();        // persist m_beamSlotLabels to QSettings

    // (legacy CheckAndAdjustSaturation removed; superseded by AUTO SAT)

    // Beam optimization
    void OnBeamOptimize();
    void OnBeamFocus();

    // TEC actions
    void OnTECPreset(double temperature);
    void PollTECReadings();

    // Minimal Z jog for gantry-main (fixed 10 um step).
    static constexpr double SWIR_Z_JOG_STEP_MM = 0.010;  // 10 um
    void JogGantryZ(double dzMm);

    // PSU device discovery
    void DiscoverPSUDevices();

    // Window registration
    static bool RegisterWindowClass();
    static bool s_classRegistered;
    static const wchar_t* s_className;

    // GDI+ initialization
    static ULONG_PTR s_gdiplusToken;
    static bool s_gdiplusInitialized;
    static bool InitializeGdiPlus();

    // Subscriber identity
    std::string m_subscriberId = "SwirBeamWindow";

    // Window handle
    HWND m_hwnd = nullptr;

    // Frame data (thread-safe)
    mutable std::mutex m_frameMutex;
    std::vector<uint8_t> m_frameBuffer;  // BGR format for GDI
    uint32_t m_frameWidth = 0;
    uint32_t m_frameHeight = 0;
    std::atomic<bool> m_hasNewFrame{false};

    // FPS tracking
    int m_frameCount = 0;
    double m_displayFps = 0.0;
    std::chrono::steady_clock::time_point m_fpsTimestamp = std::chrono::steady_clock::now();

    // Camera selection
    std::string m_targetCameraId = "swir_camera";
    mutable std::mutex m_cameraMutex;

    // Manager pointers
    CameraConfigManager* m_cameraConfigManager = nullptr;
    CameraManager* m_cameraManager = nullptr;
    PowerSupplyManager* m_psuManager = nullptr;
    PneumaticManager* m_pneumaticManager = nullptr;
    TECManager* m_tecManager = nullptr;
    UnifiedMotionManager* m_motionManager = nullptr;
    BeamResultBroker* m_beamResultBroker = nullptr;

    // TEC state
    double m_tecTemperature = 0.0;      // last read temperature (°C)
    bool m_tecConnected = false;
    bool m_tecOutputOn = false;
    std::string m_tecDeviceName = "tec-main";

    // Click-to-move and crosshair
    bool m_clickToMoveEnabled = false;
    bool m_crosshairEnabled = true;       // Center crosshair on image
    bool m_showDistanceEnabled = true;    // Real-world distance at mouse
    double m_pixelToMmFactorX = 0.025;    // SWIR default
    double m_pixelToMmFactorY = 0.025;
    int m_calibrationRefWidth = 0;
    int m_calibrationRefHeight = 0;
    bool m_mouseOverImage = false;

    // Render destination rect (saved from RenderFrame for click-to-move)
    int m_lastRenderX = 0;
    int m_lastRenderY = 0;
    int m_lastRenderW = 0;
    int m_lastRenderH = 0;

    // Bottom beam-crop strip rect (saved from OnPaint). Used so the per-frame
    // invalidate also refreshes the crops at the live feed rate; empty when the
    // strip is not drawn.
    RECT m_lastStripRect = {0, 0, 0, 0};

    // PSU multi-device support
    mutable std::mutex m_psuMutex;
    std::vector<PSUDeviceState> m_psuDevices;
    int m_selectedPSU = 0;       // Index into m_psuDevices
    bool m_psuDiscovered = false;
    std::future<void> m_psuConnectFuture;  // background PSU auto-connect
    std::atomic<bool> m_psuPollInFlight{false};  // prevents overlapping polls
    std::future<void> m_psuPollFuture;
    std::future<void> m_tecPresetFuture;

    // Exposure state
    bool m_autoExposure = false;
    double m_currentExposure = 5000.0;  // microseconds

    // Double buffering
    HDC m_memDC = nullptr;
    HBITMAP m_memBitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;

    // BITMAPINFO for DIB rendering
    std::vector<uint8_t> m_bitmapInfoBuffer;
    BITMAPINFO* m_bitmapInfo = nullptr;

    // Control panel buttons
    std::vector<SwirButton> m_buttons;
    int m_panelWidth = 0;  // Cached panel width for layout

    // Button tag constants for identification
    static constexpr int TAG_PSU_SELECT_BASE = 100;  // PSU selector buttons: 100, 101, ...
    static constexpr int TAG_PSU_CHANNEL_BASE = 150; // PSU channel buttons: 150, 151, ... (multi-channel devices)
    static constexpr int TAG_PSU_TOGGLE = 200;
    static constexpr int TAG_AUTO_EXP = 300;
    static constexpr int TAG_CLICK_TO_MOVE = 400;
    static constexpr int TAG_PLACE_MARKER = 500;
    static constexpr int TAG_CLEAR_MARKERS = 600;
    static constexpr int TAG_APPLY_EXP = 450;
    static constexpr int TAG_SET_MAG = 700;
    static constexpr int TAG_SWIR_HEAD = 800;
    static constexpr int TAG_BEAM_DETECT = 900;
    static constexpr int TAG_BEAM_CONFIG = 910;
    static constexpr int TAG_BEAM_OPTIMIZE = 950;
    static constexpr int TAG_BEAM_FOCUS = 960;
    static constexpr int TAG_SAVE_IMAGE = 970;
    static constexpr int TAG_SAVE_PACKAGE = 980;
    static constexpr int TAG_AUTO_SAT = 990;
    static constexpr int TAG_TEC_25C = 1000;
    static constexpr int TAG_TEC_30C = 1010;
    static constexpr int TAG_Z_JOG_MINUS = 1020;
    static constexpr int TAG_Z_JOG_PLUS = 1030;
    static constexpr int TAG_BEAM_FIT = 1040;     // strip button (next to crops)
    static constexpr int TAG_BEAM_CENTER = 1050;  // strip button (center XY on beams)
    static constexpr int TAG_TOPBAR_BASE = 1100;  // top-bar gantry quick-move buttons: 1100, 1101, ...

    // Mouse tracking
    POINT m_mousePosition = {0, 0};
    bool m_mouseTracking = false;

    // Current step for PSU
    static constexpr double CURRENT_STEP_MA = 50.0;      // 50mA step
    static constexpr double CURRENT_STEP_SMALL_MA = 10.0; // 10mA step
    static constexpr double EXPOSURE_STEP_US = 50.0;      // 50us step

    // Magnification (optical objective)
    double m_magnification = 100.0;  // hardcoded for this branch (100x objective)

    // Camera mount orientation relative to the gantry XY axes. 90 = the M4AA
    // setup (gantry X+ = image up, Y+ = image right); 0 = normal (axes aligned).
    // Selectable via the SET MAG dialog, persisted in QSettings. Default 90.
    int m_cameraRotationDeg = 90;

    // World-locked markers
    // World markers are owned by SwirWorldMarkerStore (forward decl above).
    // Pointer rather than value to avoid pulling its header into this one.
    std::unique_ptr<class SwirWorldMarkerStore> m_markerStore;
    // Image-save APIs live in SwirImageSaver. Window keeps thin forwards.
    std::unique_ptr<class SwirImageSaver> m_imageSaver;

    // Beam detection
    bool m_beamDetectionEnabled = false;
    std::thread m_detectionThread;
    std::mutex m_detectionMutex;
    std::condition_variable m_detectionCV;
    std::vector<uint8_t> m_detectionBuffer;
    std::vector<uint8_t> m_detectionWorkBuffer;  // detection-thread-only; swapped with m_detectionBuffer to avoid per-frame alloc
    uint32_t m_detectionWidth = 0;
    uint32_t m_detectionHeight = 0;
    std::atomic<bool> m_detectionReady{false};
    std::atomic<bool> m_detectionShutdown{false};
    // Async stop worker. The detection loop can spend ~1-2 s inside a single
    // Halcon iteration with no interruption point, so joining on the UI
    // thread (B DETECT toggle click) freezes the window. The disable click
    // hands the join off to this future; StopDetectionThread() (destructor
    // path) drains it before doing its own sync join.
    std::future<void> m_detectionStopFuture;

    mutable std::mutex m_beamMutex;
    std::vector<BeamDetectionResult> m_beamResults;
    BeamDetectionConfig m_beamConfig;
    BlobDetectionConfig m_blobConfig;
    HalconBeamDetectionConfig m_halconConfig;
    PoyntDetectionConfig m_poyntConfig;
    bool m_useBlobDetection = false;     // false=d4sigma, true=blob
    bool m_useHalconDetection = false;   // takes precedence over blob/d4sigma when true
    bool m_usePoyntDetection = false;    // poynt-faithful mode; precedence below Halcon, above blob/d4sigma

    // User-selected per-slot display label shown in the overlay title for
    // B0 / B1 (one of RX / TX / GA1 / GA2). Chosen via B CONFIG, persisted in
    // QSettings. Defaults mirror the legacy hardcoded slot labels.
    std::string m_beamSlotLabels[2] = {"RX", "TX"};

    // Beam focus optimization. Worker future + cancel flag live in
    // SwirBeamFocusController; the score provider and config remain here
    // because the score provider binds to m_beamMutex/m_beamResults.
    std::future<void> m_configReloadFuture;
    BeamScoreProvider m_beamScoreProvider;
    HillClimbConfig m_beamFocusConfig;  // configurable step sizes, settle time, etc.
    std::unique_ptr<class SwirBeamFocusController> m_beamFocus;

    // CE/EE history for real-time charts (one ring buffer per beam, max 2)
    static constexpr int CE_HISTORY_SIZE = 200;
    std::array<std::vector<double>, 2> m_ceHistory;   // [0]=B0, [1]=B1
    std::array<std::vector<double>, 2> m_eeHistory;   // EE% history [0]=B0, [1]=B1
    std::array<int, 2> m_ceHistoryIndex = {0, 0};     // next write position (shared CE/EE)
    std::array<int, 2> m_ceHistoryCount = {0, 0};     // filled count (shared CE/EE)
    std::array<int, 2> m_ceRejectCount = {0, 0};      // consecutive outlier rejection count

    // Beam ID tracking: associate each frame's candidates to the previous
    // frame's slots by nearest-neighbor distance, so labels stay stable when
    // beams cross or one disappears momentarily.
    struct PrevBeamSlot {
        bool   valid = false;
        double cx = 0.0;
        double cy = 0.0;
        int    staleFrames = 0;
    };
    std::array<PrevBeamSlot, 2> m_prevBeams{};
    bool   m_trackingEnabled = true;
    double m_trackingMaxJumpPx = 50.0;
    int    m_trackingStaleMax = 30;

    // Diagnostic counters for detection-miss logging (throttled console output)
    int m_detectMissCount = 0;     // consecutive frames where actual < expected
    int64_t m_detectionInputDrops = 0;  // frames overwritten before detection consumed prev

    // Detection-time histogram (last N samples; periodic mean+p95 log)
    static constexpr int DETECT_TIMING_SIZE = 100;
    std::array<double, DETECT_TIMING_SIZE> m_detectTimings{};
    int m_detectTimingIdx = 0;
    int m_detectTimingCount = 0;
    int m_detectStatsCount = 0;

    // Display copy of beam results. Identical to m_beamResults when detection
    // succeeds; preserved across frames where detection misses so the overlay
    // text stays static instead of flickering.
    std::vector<BeamDetectionResult> m_beamDisplayResults;

    // Auto-tune & focus algorithm. Worker thread + state live in
    // SwirAutoSatController (forward-declared via unique_ptr to keep
    // its header out of this one). Default OFF; clicking flips to
    // "STOP". Any manual exposure/current change calls DisableAutoSat
    // → controller.Stop() so the user always wins.
    std::unique_ptr<class SwirAutoSatController> m_autoSat;

    // BEAM FIT one-shot Z-waist sweep worker. Standalone — shares no state
    // with AUTO SAT. Default OFF; clicking flips the strip button to "STOP".
    std::unique_ptr<class SwirBeamFitController> m_beamFit;

    // (legacy SAT_* constants removed; AUTO SAT now uses local constants
    //  in SwirBeamWindow.cpp's anonymous namespace.)

    // Paint throttle: track last InvalidateRect time to avoid flooding message queue
    std::chrono::steady_clock::time_point m_lastInvalidateTime{};

    // Cached control panel bitmap (only redrawn on state change)
    HDC m_panelCacheDC = nullptr;
    HBITMAP m_panelCacheBitmap = nullptr;
    HBITMAP m_panelCacheOldBitmap = nullptr;
    int m_panelCacheWidth = 0;
    int m_panelCacheHeight = 0;
    std::atomic<bool> m_panelDirty{true};

    void InvalidatePanel() { m_panelDirty.store(true); }
    void CleanupPanelCache();
};

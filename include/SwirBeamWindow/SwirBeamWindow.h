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
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>
#include <future>
#include <condition_variable>
#include <chrono>
#include "camera/CameraFrameData.h"
#include "SwirBeamDetector.h"
#include "BeamOptimizer/BeamHillClimb.h"
#include "BeamOptimizer/BeamScoreProvider.h"

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

    // Process Windows messages
    static bool ProcessMessages();

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
    void OnPSUToggle();
    void OnCurrentIncrease();
    void OnCurrentDecrease();
    void OnCurrentIncreaseSmall();
    void OnCurrentDecreaseSmall();

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

    // Magnification
    void OnSetMagnification();
    void SaveMagnification();
    void LoadMagnification();

    // SWIR Head pneumatic
    void OnSwirHeadToggle();

    // Beam detection
    void OnBeamDetectToggle();
    void StartDetectionThread();
    void StopDetectionThread();

    // Beam config
    void LoadBeamConfig();
    void SaveBeamConfig();
    void OnEditBeamConfig();

    // Beam optimization
    void OnBeamOptimize();
    void OnBeamFocus();

    // TEC actions
    void OnTECPreset(double temperature);
    void PollTECReadings();

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

    // PSU multi-device support
    mutable std::mutex m_psuMutex;
    std::vector<PSUDeviceState> m_psuDevices;
    int m_selectedPSU = 0;       // Index into m_psuDevices
    bool m_psuDiscovered = false;
    std::future<void> m_psuConnectFuture;  // background PSU auto-connect
    std::atomic<bool> m_psuPollInFlight{false};  // prevents overlapping polls

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
    static constexpr int TAG_TEC_25C = 1000;
    static constexpr int TAG_TEC_30C = 1010;

    // Mouse tracking
    POINT m_mousePosition = {0, 0};
    bool m_mouseTracking = false;

    // Current step for PSU
    static constexpr double CURRENT_STEP_MA = 50.0;      // 50mA step
    static constexpr double CURRENT_STEP_SMALL_MA = 10.0; // 10mA step
    static constexpr double EXPOSURE_STEP_US = 50.0;      // 50us step

    // Magnification (optical objective)
    double m_magnification = 20.0;

    // World-locked markers
    std::vector<SwirWorldMarker> m_worldMarkers;
    std::mutex m_markerMutex;
    int m_nextMarkerId = 1;
    std::string m_markerFilePath;

    // Beam detection
    bool m_beamDetectionEnabled = false;
    std::thread m_detectionThread;
    std::mutex m_detectionMutex;
    std::condition_variable m_detectionCV;
    std::vector<uint8_t> m_detectionBuffer;
    uint32_t m_detectionWidth = 0;
    uint32_t m_detectionHeight = 0;
    std::atomic<bool> m_detectionReady{false};
    std::atomic<bool> m_detectionShutdown{false};

    std::mutex m_beamMutex;
    std::vector<BeamDetectionResult> m_beamResults;
    BeamDetectionConfig m_beamConfig;

    // Beam focus optimization (reusable components)
    std::atomic<bool> m_beamFocusRunning{false};
    std::future<void> m_beamFocusFuture;
    BeamScoreProvider m_beamScoreProvider;
    HillClimbConfig m_beamFocusConfig;  // configurable step sizes, settle time, etc.

    // CE/EE history for real-time charts (one ring buffer per beam, max 2)
    static constexpr int CE_HISTORY_SIZE = 200;
    std::array<std::vector<double>, 2> m_ceHistory;   // [0]=B0, [1]=B1
    std::array<std::vector<double>, 2> m_eeHistory;   // EE% history [0]=B0, [1]=B1
    std::array<int, 2> m_ceHistoryIndex = {0, 0};     // next write position (shared CE/EE)
    std::array<int, 2> m_ceHistoryCount = {0, 0};     // filled count (shared CE/EE)

    // Frame averaging for stable detection
    static constexpr int FRAME_AVG_COUNT = 5;
    std::vector<std::vector<uint8_t>> m_avgFrameRing;  // ring buffer of grayscale frames
    int m_avgFrameIndex = 0;                            // next write position
    int m_avgFramesFilled = 0;                          // frames accumulated so far

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

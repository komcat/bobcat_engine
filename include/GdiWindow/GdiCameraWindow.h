// GdiCameraWindow.h
#pragma once

// winsock2.h must be included before Windows.h to avoid conflicts
#include <winsock2.h>
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <memory>
#include <functional>
#include <thread>
#include <queue>
#include <condition_variable>
#include "camera/CameraFrameData.h"
#include "nlohmann/json.hpp"

#ifdef HAS_OPENCV
#include "vision/VisionResult.h"
#endif

class DataStore;
class UnifiedMotionManager;
class CameraManager;
class BeamServerClient;

// Menu IDs
#define IDM_SHOW_MENU_BASE      6000
#define IDM_SHOW_CHART_ENABLED  6001
#define IDM_CHANNEL_BASE        6100  // Channel menu items start here

// Vision Menu IDs
#define IDM_VISION_MENU_BASE        7000
#define IDM_VISION_ENABLE           7001
#define IDM_VISION_REFRESH          7002
#define IDM_VISION_PROFILE_BASE     7100  // Max 100 profiles

// Tools Menu IDs
#define IDM_TOOLS_MENU_BASE         8000
#define IDM_TOOLS_CROSSHAIR         8001
#define IDM_TOOLS_CLICK_TO_MOVE     8002
#define IDM_TOOLS_SHOW_DISTANCE     8003
#define IDM_TOOLS_MAGNIFICATION     8004

// Overlay Size Menu IDs
#define IDM_OVERLAY_SIZE_INCREASE   8010
#define IDM_OVERLAY_SIZE_DECREASE   8011

// Camera Menu IDs
#define IDM_CAMERA_MENU_BASE        9000
#define IDM_CAMERA_DUAL_MODE        9001
#define IDM_CAMERA_PRIMARY_BASE     9100  // Primary camera selection (max 50)
#define IDM_CAMERA_SECONDARY_BASE   9200  // Secondary camera selection (max 50)

// Beam Stream Menu IDs
#define IDM_BEAM_STREAM_MENU_BASE   10000
#define IDM_BEAM_STREAM_ENABLE      10001
#define IDM_BEAM_STREAM_CAMERA_BASE 10100  // Max 50 cameras

// Custom Windows messages
#define WM_VISION_RESULT_READY      (WM_USER + 101)
#define WM_BEAM_STREAM_RESULT_READY (WM_USER + 102)

// Calibration config file (shared with CameraClickToMoveWidget)
#define CALIBRATION_CONFIG_FILE     "config/SYSTEM/val_pixelmm.json"

/**
 * @brief Chart overlay for displaying live data on camera view
 */
class ChartOverlay {
public:
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void AddDataPoint(const std::string& channelId, double value);
    void Clear();

    void SetChannelEnabled(const std::string& channelId, bool enabled);
    bool IsChannelEnabled(const std::string& channelId) const;
    std::vector<std::string> GetEnabledChannels() const;

    void Draw(HDC hDC, const RECT& windowRect);

    // Scale factor for overlay size (1.0 = default, 0.5 = half, 2.0 = double)
    void SetScale(float scale);
    float GetScale() const { return m_scale; }
    void IncreaseScale();
    void DecreaseScale();

private:
    bool m_enabled = false;
    float m_scale = 1.0f;  // Scale factor for overlay size
    mutable std::mutex m_dataMutex;

    struct ChannelData {
        std::deque<double> values;
        double runningMin = 1e30;
        double runningMax = -1e30;
    };
    std::map<std::string, ChannelData> m_channels;
    std::set<std::string> m_enabledChannels;

    static constexpr size_t MAX_POINTS = 200;
    static constexpr int BASE_CHART_WIDTH = 320;
    static constexpr int BASE_CHART_HEIGHT = 180;
    static constexpr int CHART_MARGIN = 10;
    static constexpr float MIN_SCALE = 0.5f;
    static constexpr float MAX_SCALE = 3.0f;
    static constexpr float SCALE_STEP = 0.25f;

    // Computed dimensions based on scale
    int GetChartWidth() const { return static_cast<int>(BASE_CHART_WIDTH * m_scale); }
    int GetChartHeight() const { return static_cast<int>(BASE_CHART_HEIGHT * m_scale); }

    static Gdiplus::Color GetChannelColor(int index);
    static std::wstring FormatValue(double value);
};

#ifdef HAS_OPENCV
/**
 * @brief Vision result overlay for displaying detection results on camera view
 */
class VisionResultOverlay {
public:
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void SetResult(const VisionResult& result, const std::string& profileName);
    void Clear();

    void Draw(HDC hDC, const RECT& windowRect, int frameWidth, int frameHeight,
              int destX, int destY, int destWidth, int destHeight);

    // Thread-safe copy of the latest result (for SequenceContext injection)
    VisionResult GetResult() const {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        return m_lastResult;
    }
    uint64_t GetResultSequence() const { return m_resultSequence.load(); }

private:
    bool m_enabled = false;
    mutable std::mutex m_resultMutex;
    VisionResult m_lastResult;
    std::string m_profileName;
    std::atomic<uint64_t> m_resultSequence{0};

    static constexpr int OVERLAY_WIDTH = 280;
    static constexpr int OVERLAY_MARGIN = 10;

    void DrawInfoPanel(Gdiplus::Graphics& gfx, const Gdiplus::RectF& panelRect);
    void DrawGeometricOverlays(HDC hDC, int frameWidth, int frameHeight,
                                int destX, int destY, int destWidth, int destHeight);

    // Convert image pixel coordinates to screen coordinates
    static Gdiplus::PointF ImageToScreen(double imgX, double imgY,
                                          int frameWidth, int frameHeight,
                                          int destX, int destY,
                                          int destWidth, int destHeight);
};
#endif

/**
 * @brief Beam stream overlay for displaying live beam detection results on camera view
 */
class BeamStreamOverlay {
public:
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void SetResult(const nlohmann::json& result);
    void Clear();

    void Draw(HDC hDC, const RECT& windowRect, int frameWidth, int frameHeight,
              int destX, int destY, int destWidth, int destHeight,
              double umPerPxX, double umPerPxY);

    double GetFps() const { return m_fps; }
    void SetFps(double fps) { m_fps = fps; }

    // Thread-safe accessors for SequenceContext injection
    nlohmann::json GetResult() const {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        return m_lastResult;
    }
    uint64_t GetResultSequence() const { return m_resultSequence.load(); }

private:
    bool m_enabled = false;
    mutable std::mutex m_resultMutex;
    nlohmann::json m_lastResult;
    double m_fps = 0.0;
    std::atomic<uint64_t> m_resultSequence{0};

    static constexpr int OVERLAY_WIDTH = 280;
    static constexpr int OVERLAY_MARGIN = 10;

    // Convert image pixel coordinates to screen coordinates
    static Gdiplus::PointF ImageToScreen(double imgX, double imgY,
                                          int frameWidth, int frameHeight,
                                          int destX, int destY,
                                          int destWidth, int destHeight);
};

/**
 * @brief Pure Win32 GDI window for camera live streaming with data chart overlay
 *
 * This is a native Windows window that displays camera frames using GDI.
 * It subscribes to the camera broadcasting system and renders frames
 * using SetDIBitsToDevice for optimal performance.
 *
 * Features:
 * - Menu bar with "Show" menu for selecting data channels
 * - Live chart overlay for selected data channels
 * - Subscribes to DataStore for real-time updates
 */
class GdiCameraWindow : public CameraFrameSubscriber {
public:
    GdiCameraWindow();
    ~GdiCameraWindow();

    // Window lifecycle
    bool Create(const std::string& title = "Camera Monitor",
                int x = CW_USEDEFAULT, int y = CW_USEDEFAULT,
                int width = 800, int height = 600);
    void Destroy();

    // Window control
    void Show();
    void Hide();
    bool IsVisible() const;
    void SetTitle(const std::string& title);

    // Camera configuration
    void SetTargetCamera(const std::string& cameraId);
    std::string GetTargetCamera() const;

    // Camera manager integration (for camera switching menu)
    void SetCameraManager(CameraManager* cameraManager);
    void RefreshCameraMenu();

    // Dual camera mode
    void SetDualCameraMode(bool enabled);
    bool IsDualCameraMode() const { return m_dualCameraMode; }
    void SetSecondaryCameraId(const std::string& cameraId);
    std::string GetSecondaryCameraId() const;

    // DataStore integration
    void SetDataStore(DataStore* dataStore);
    void RefreshChannelMenu();

    // Motion manager for click-to-move
    void SetMotionManager(UnifiedMotionManager* motionManager);

    // Click-to-move and crosshair controls
    void SetClickToMoveEnabled(bool enabled);
    void SetCrosshairEnabled(bool enabled);
    bool IsClickToMoveEnabled() const { return m_clickToMoveEnabled; }
    bool IsCrosshairEnabled() const { return m_crosshairEnabled; }

    // Distance display and magnification
    void SetShowDistanceEnabled(bool enabled);
    bool IsShowDistanceEnabled() const { return m_showDistanceEnabled; }
    void SetMagnification(double mag);
    double GetMagnification() const { return m_magnification; }

    // Subscriber identity (must be unique per instance for CameraManager)
    void SetSubscriberId(const std::string& id) { m_subscriberId = id; }

    // CameraFrameSubscriber interface
    void OnNewFrame(const CameraFrameData& frameData) override;
    void OnCameraStatusChanged(const std::string& cameraId, bool connected, bool grabbing) override;
    std::string GetSubscriberId() const override { return m_subscriberId; }
    bool WantsFramesFromCamera(const std::string& cameraId) const override;
    int GetMinFrameIntervalMs() const override { return 33; } // ~30fps

    // Get native window handle
    HWND GetHwnd() const { return m_hwnd; }

    // Process Windows messages (call from message loop)
    static bool ProcessMessages();

    // Chart overlay access
    ChartOverlay& GetChartOverlay() { return m_chartOverlay; }

    // Overlay scale control
    void SetOverlayScale(float scale) { m_chartOverlay.SetScale(scale); }
    float GetOverlayScale() const { return m_chartOverlay.GetScale(); }

#ifdef HAS_OPENCV
    // Continuous vision result access (for SequenceContext injection)
    bool IsVisionProcessingActive() const { return m_visionEnabled.load() && m_visionThreadRunning.load(); }
    VisionResult GetLatestVisionResult() const { return m_visionOverlay.GetResult(); }
    uint64_t GetVisionResultSequence() const { return m_visionOverlay.GetResultSequence(); }

    // Programmatic continuous vision control (for recipe commands)
    void SetVisionEnabled(bool enabled);
    void SelectVisionProfile(const std::string& profilePath);
#endif

    // Beam streaming
    void SetBeamServerClient(BeamServerClient* client);

    // Beam stream result access (for SequenceContext injection)
    bool IsBeamStreamActive() const { return m_beamStreamEnabled.load() && m_beamStreamThreadRunning.load(); }
    nlohmann::json GetLatestBeamStreamResult() const { return m_beamStreamOverlay.GetResult(); }
    uint64_t GetBeamStreamResultSequence() const { return m_beamStreamOverlay.GetResultSequence(); }
    double GetBeamStreamFps() const { return m_beamStreamOverlay.GetFps(); }

    // Callback for overlay scale changes (for settings save)
    void SetOverlayScaleChangedCallback(std::function<void(float)> callback) {
        m_onOverlayScaleChanged = callback;
    }

private:
    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Menu
    void CreateMenuBar();
    void UpdateShowMenu();
    void UpdateToolsMenu();
    void UpdateCameraMenu();
#ifdef HAS_OPENCV
    void UpdateVisionMenu();
#endif
    void OnMenuCommand(WORD commandId);

    // Click-to-move
    void HandleClickToMove(int mouseX, int mouseY);
    void LoadCalibrationFactors();
    void DrawCrosshairs(HDC hDC, const RECT& windowRect);

    // Beam streaming
    void UpdateBeamStreamMenu();
    void LoadBeamStreamConfig();
    void StartBeamStreaming();
    void StopBeamStreaming();
    void BeamStreamThreadLoop();

    // Vision processing
#ifdef HAS_OPENCV
    void DiscoverVisionProfiles();
    void StartVisionProcessing();
    void StopVisionProcessing();
    void VisionThreadLoop();
#endif

    // Rendering
    void OnPaint();
    void RenderFrame(HDC hdc);
    void PrepareBackBuffer();

    // DataStore callback
    void OnDataStoreValueChanged(const std::string& channelId, float value);

    // Window registration
    static bool RegisterWindowClass();
    static bool s_classRegistered;
    static const wchar_t* s_className;

    // GDI+ initialization
    static ULONG_PTR s_gdiplusToken;
    static bool s_gdiplusInitialized;
    static bool InitializeGdiPlus();
    static void ShutdownGdiPlus();

    // Subscriber identity
    std::string m_subscriberId = "GdiCameraWindow";

    // Window handle
    HWND m_hwnd = nullptr;
    HMENU m_menuBar = nullptr;
    HMENU m_showMenu = nullptr;
    HMENU m_visionMenu = nullptr;
    HMENU m_toolsMenu = nullptr;
    HMENU m_cameraMenu = nullptr;

    // Frame data (thread-safe)
    mutable std::mutex m_frameMutex;
    std::vector<uint8_t> m_frameBuffer;  // BGR format for GDI
    uint32_t m_frameWidth = 0;
    uint32_t m_frameHeight = 0;
    std::atomic<bool> m_hasNewFrame{false};

    // Camera selection
    std::string m_targetCameraId;  // Empty = accept all cameras (primary camera for dual mode)
    mutable std::mutex m_cameraMutex;

    // Camera manager for menu population
    CameraManager* m_cameraManager = nullptr;
    std::vector<std::string> m_availableCameraIds;
    mutable std::mutex m_cameraListMutex;

    // Dual camera mode
    bool m_dualCameraMode = false;
    std::string m_secondaryCameraId;  // Second camera for split view

    // Secondary frame data (for dual camera mode)
    mutable std::mutex m_secondaryFrameMutex;
    std::vector<uint8_t> m_secondaryFrameBuffer;  // BGR format for GDI
    uint32_t m_secondaryFrameWidth = 0;
    uint32_t m_secondaryFrameHeight = 0;
    std::atomic<bool> m_hasSecondaryNewFrame{false};

    // Status
    std::atomic<bool> m_cameraConnected{false};
    std::atomic<bool> m_cameraGrabbing{false};

    // Double buffering
    HDC m_memDC = nullptr;
    HBITMAP m_memBitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;

    // BITMAPINFO for DIB rendering
    std::vector<uint8_t> m_bitmapInfoBuffer;
    BITMAPINFO* m_bitmapInfo = nullptr;

    // Chart overlay
    ChartOverlay m_chartOverlay;
    std::function<void(float)> m_onOverlayScaleChanged;  // Callback for scale changes

    // DataStore
    DataStore* m_dataStore = nullptr;
    std::vector<std::string> m_availableChannels;
    mutable std::mutex m_channelMutex;

    // Click-to-move and crosshair
    UnifiedMotionManager* m_motionManager = nullptr;
    bool m_clickToMoveEnabled = false;
    bool m_crosshairEnabled = false;
    bool m_showDistanceEnabled = false;
    double m_magnification = 1.0;  // Optics magnification for distance display
    POINT m_mousePosition = {0, 0};
    bool m_mouseOverImage = false;
    double m_pixelToMmFactorX = 0.00248;
    double m_pixelToMmFactorY = 0.00248;
    int m_calibrationRefWidth = 0;   // Reference resolution at which pixel_to_mm was calibrated (0 = no scaling)
    int m_calibrationRefHeight = 0;

    // Frame destination info for overlay drawing (crosshair, vision)
    // Primary camera (left in dual mode, full in single mode)
    int m_lastDestX = 0;
    int m_lastDestY = 0;
    int m_lastDestWidth = 0;
    int m_lastDestHeight = 0;

    // Secondary camera destination info (right side in dual mode)
    int m_lastSecondaryDestX = 0;
    int m_lastSecondaryDestY = 0;
    int m_lastSecondaryDestWidth = 0;
    int m_lastSecondaryDestHeight = 0;

    // Beam streaming
    BeamServerClient* m_beamServerClient = nullptr;           // Injected command client (port 5555)
    std::unique_ptr<BeamServerClient> m_beamStreamClient;     // Owned stream client (port 5567)
    BeamStreamOverlay m_beamStreamOverlay;
    HMENU m_beamStreamMenu = nullptr;

    std::atomic<bool> m_beamStreamEnabled{false};
    std::atomic<bool> m_beamStreamThreadRunning{false};
    std::thread m_beamStreamThread;

    std::queue<std::vector<uint8_t>> m_beamStreamQueue;  // RGB frame data
    uint32_t m_beamStreamQueuedWidth = 0;
    uint32_t m_beamStreamQueuedHeight = 0;
    std::mutex m_beamStreamQueueMutex;
    std::condition_variable m_beamStreamQueueCV;

    std::string m_beamStreamCameraId;
    mutable std::mutex m_beamStreamCameraMutex;

    struct BeamStreamConfig {
        std::string host = "127.0.0.1";
        int port = 5567;
        double umPerPxX = 0.25;
        double umPerPxY = 0.25;
        bool rotate180 = false;
        int intervalMs = 100;
    };
    BeamStreamConfig m_beamStreamConfig;

    // Vision overlay and processing
#ifdef HAS_OPENCV
    VisionResultOverlay m_visionOverlay;
    std::atomic<bool> m_visionEnabled{false};
    std::string m_activeVisionProfile;
    mutable std::mutex m_visionProfileMutex;

    // Available vision profiles (path -> display name)
    struct VisionProfileInfo {
        std::string fullPath;
        std::string displayName;
    };
    std::vector<VisionProfileInfo> m_availableProfiles;

    // Vision processing thread
    std::thread m_visionThread;
    std::atomic<bool> m_visionThreadRunning{false};
    std::queue<std::vector<uint8_t>> m_frameQueue;
    uint32_t m_queuedFrameWidth = 0;
    uint32_t m_queuedFrameHeight = 0;
    std::mutex m_frameQueueMutex;
    std::condition_variable m_frameQueueCV;
#endif
};

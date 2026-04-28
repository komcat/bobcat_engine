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
#include <chrono>
#include "camera/CameraFrameData.h"
#include "nlohmann/json.hpp"

class DataStore;
class UnifiedMotionManager;
class CameraManager;
class CameraConfigManager;
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
#define IDM_TOOLS_DISPLAY_ROTATION  8005
#define IDM_TOOLS_DISPLAY_ROTATION_CW 8006

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
#define WM_CHART_REPAINT_REQUEST    (WM_USER + 103)

// Calibration config file (shared with CameraClickToMoveWidget)
#define CALIBRATION_CONFIG_FILE     "config/SYSTEM/val_pixelmm.json"

// Overlay classes (extracted to separate files)
#include "ChartOverlay.h"
#ifdef HAS_OPENCV
#include "VisionResultOverlay.h"
#include "GdiVisionController.h"
#endif
#include "BeamStreamOverlay.h"
#include "GdiBeamStreamController.h"

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

    // Camera config manager for per-camera calibration
    void SetCameraConfigManager(CameraConfigManager* configMgr);

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
    int GetMinFrameIntervalMs() const override { return 33; } // ~30fps display cap

    // FPS statistics
    double GetDisplayFps() const { return m_displayFps.load(); }
    double GetAcquisitionFps() const { return m_acquisitionFps.load(); }

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
    // Continuous vision result access (for SequenceContext injection).
    // All forward to GdiVisionController.
    bool IsVisionProcessingActive() const;
    VisionResult GetLatestVisionResult() const;
    uint64_t GetVisionResultSequence() const;

    // Programmatic continuous vision control (for recipe commands)
    void SetVisionEnabled(bool enabled);
    void SelectVisionProfile(const std::string& profilePath);
#endif

    // Beam streaming
    void SetBeamServerClient(BeamServerClient* client);

    // Beam stream result access (for SequenceContext injection)
    bool IsBeamStreamActive() const;
    nlohmann::json GetLatestBeamStreamResult() const;
    uint64_t GetBeamStreamResultSequence() const;
    double GetBeamStreamFps() const;

    // Display rotation (0 = none, -90 = CCW, +90 = CW)
    void SetDisplayRotation(int degrees);
    int GetDisplayRotation() const { return m_displayRotation; }
    bool IsDisplayRotation() const { return m_displayRotation != 0; }
    void SetDisplayRotationChangedCallback(std::function<void(int)> callback) {
        m_onDisplayRotationChanged = callback;
    }

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
    void OnMenuCommand(WORD commandId);

    // Click-to-move
    void HandleClickToMove(int mouseX, int mouseY);
    void LoadCalibrationFactors();
    void DrawCrosshairs(HDC hDC, const RECT& windowRect);


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
    HMENU m_toolsMenu = nullptr;
    HMENU m_cameraMenu = nullptr;
    // m_visionMenu HMENU is owned by m_visionController (HAS_OPENCV).

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

    // FPS tracking
    std::atomic<double> m_displayFps{0.0};
    std::atomic<double> m_acquisitionFps{0.0};
    int m_acquisitionFrameCount = 0;
    int m_displayFrameCount = 0;
    std::chrono::steady_clock::time_point m_lastAcquisitionFpsTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point m_lastDisplayFpsTime = std::chrono::steady_clock::now();
    void DrawFpsOverlay(HDC hDC, const RECT& windowRect);

    // Chart overlay
    ChartOverlay m_chartOverlay;
    std::function<void(float)> m_onOverlayScaleChanged;  // Callback for scale changes

    // Display rotation (0 = none, -90 = CCW, +90 = CW)
    int m_displayRotation = 0;
    uint32_t m_originalFrameWidth = 0;   // Pre-rotation width (for beam overlay transform)
    uint32_t m_originalFrameHeight = 0;  // Pre-rotation height
    std::function<void(int)> m_onDisplayRotationChanged;

    // Chart update throttling (prevents UI freezing from high-frequency data updates)
    std::chrono::steady_clock::time_point m_lastChartRepaint;
    static constexpr int CHART_REPAINT_INTERVAL_MS = 50;  // Max ~20fps for chart updates

    // DataStore
    DataStore* m_dataStore = nullptr;
    std::vector<std::string> m_availableChannels;
    mutable std::mutex m_channelMutex;

    // Click-to-move and crosshair
    CameraConfigManager* m_cameraConfigManager = nullptr;
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

    // Beam streaming — extracted to GdiBeamStreamController.
    BeamServerClient* m_beamServerClient = nullptr;  // Injected command client (port 5555); separate from streaming client owned by m_beamController.
    BeamStreamOverlay m_beamStreamOverlay;
    std::unique_ptr<GdiBeamStreamController> m_beamController;

    // Vision overlay (owned by GdiVisionController under HAS_OPENCV;
    // unused otherwise). The overlay must outlive the controller.
#ifdef HAS_OPENCV
    VisionResultOverlay m_visionOverlay;
    std::unique_ptr<GdiVisionController> m_visionController;
#endif
};

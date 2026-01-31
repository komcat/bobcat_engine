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

#ifdef HAS_OPENCV
#include "vision/VisionResult.h"
#endif

class DataStore;
class UnifiedMotionManager;

// Menu IDs
#define IDM_SHOW_MENU_BASE      6000
#define IDM_SHOW_CHART_ENABLED  6001
#define IDM_CHANNEL_BASE        6100  // Channel menu items start here

// Vision Menu IDs
#define IDM_VISION_MENU_BASE        7000
#define IDM_VISION_ENABLE           7001
#define IDM_VISION_PROFILE_BASE     7100  // Max 100 profiles

// Tools Menu IDs
#define IDM_TOOLS_MENU_BASE         8000
#define IDM_TOOLS_CROSSHAIR         8001
#define IDM_TOOLS_CLICK_TO_MOVE     8002

// Custom Windows messages
#define WM_VISION_RESULT_READY      (WM_USER + 101)

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

private:
    bool m_enabled = false;
    mutable std::mutex m_dataMutex;

    struct ChannelData {
        std::deque<double> values;
        double runningMin = 1e30;
        double runningMax = -1e30;
    };
    std::map<std::string, ChannelData> m_channels;
    std::set<std::string> m_enabledChannels;

    static constexpr size_t MAX_POINTS = 200;
    static constexpr int CHART_WIDTH = 320;
    static constexpr int CHART_HEIGHT = 180;  // Increased for larger value display
    static constexpr int CHART_MARGIN = 10;

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

private:
    bool m_enabled = false;
    mutable std::mutex m_resultMutex;
    VisionResult m_lastResult;
    std::string m_profileName;

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

    // CameraFrameSubscriber interface
    void OnNewFrame(const CameraFrameData& frameData) override;
    void OnCameraStatusChanged(const std::string& cameraId, bool connected, bool grabbing) override;
    std::string GetSubscriberId() const override { return "GdiCameraWindow"; }
    bool WantsFramesFromCamera(const std::string& cameraId) const override;
    int GetMinFrameIntervalMs() const override { return 33; } // ~30fps

    // Get native window handle
    HWND GetHwnd() const { return m_hwnd; }

    // Process Windows messages (call from message loop)
    static bool ProcessMessages();

    // Chart overlay access
    ChartOverlay& GetChartOverlay() { return m_chartOverlay; }

private:
    // Window procedure
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Menu
    void CreateMenuBar();
    void UpdateShowMenu();
    void UpdateToolsMenu();
#ifdef HAS_OPENCV
    void UpdateVisionMenu();
#endif
    void OnMenuCommand(WORD commandId);

    // Click-to-move
    void HandleClickToMove(int mouseX, int mouseY);
    void LoadCalibrationFactors();
    void DrawCrosshairs(HDC hDC, const RECT& windowRect);

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

    // Window handle
    HWND m_hwnd = nullptr;
    HMENU m_menuBar = nullptr;
    HMENU m_showMenu = nullptr;
    HMENU m_visionMenu = nullptr;
    HMENU m_toolsMenu = nullptr;

    // Frame data (thread-safe)
    mutable std::mutex m_frameMutex;
    std::vector<uint8_t> m_frameBuffer;  // BGR format for GDI
    uint32_t m_frameWidth = 0;
    uint32_t m_frameHeight = 0;
    std::atomic<bool> m_hasNewFrame{false};

    // Camera selection
    std::string m_targetCameraId;  // Empty = accept all cameras
    mutable std::mutex m_cameraMutex;

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

    // DataStore
    DataStore* m_dataStore = nullptr;
    std::vector<std::string> m_availableChannels;
    mutable std::mutex m_channelMutex;

    // Click-to-move and crosshair
    UnifiedMotionManager* m_motionManager = nullptr;
    bool m_clickToMoveEnabled = false;
    bool m_crosshairEnabled = false;
    POINT m_mousePosition = {0, 0};
    bool m_mouseOverImage = false;
    double m_pixelToMmFactorX = 0.00248;
    double m_pixelToMmFactorY = 0.00248;

    // Frame destination info for overlay drawing (crosshair, vision)
    int m_lastDestX = 0;
    int m_lastDestY = 0;
    int m_lastDestWidth = 0;
    int m_lastDestHeight = 0;

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

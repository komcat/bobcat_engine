// GdiCameraWindow.cpp
#include "GdiCameraWindow.h"
#include "customserver/BeamServerClient.h"
#include "data/DataStore.h"
#include "motions/unified_motion_manager.h"
#include "camera/CameraManager.h"
#include "camera/CameraConfigManager.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>

#include <QSettings>

#ifdef HAS_OPENCV
#include "vision/VisionManager.h"
#include "vision/VisionROI.h"
#include <opencv2/imgproc.hpp>
#include <QString>
#endif

// Static members
bool GdiCameraWindow::s_classRegistered = false;
const wchar_t* GdiCameraWindow::s_className = L"GdiCameraWindowClass";
ULONG_PTR GdiCameraWindow::s_gdiplusToken = 0;
bool GdiCameraWindow::s_gdiplusInitialized = false;

// ============================================================================
// GdiCameraWindow Implementation
// ============================================================================

GdiCameraWindow::GdiCameraWindow() {
    m_bitmapInfoBuffer.resize(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD));
    m_bitmapInfo = reinterpret_cast<BITMAPINFO*>(m_bitmapInfoBuffer.data());
    InitializeGdiPlus();
    LoadCalibrationFactors();
#ifdef HAS_OPENCV
    m_visionController = std::make_unique<GdiVisionController>(&m_visionOverlay);
#endif
    m_beamController = std::make_unique<GdiBeamStreamController>(&m_beamStreamOverlay);
}

GdiCameraWindow::~GdiCameraWindow() {
    if (m_beamController) m_beamController->Shutdown();
#ifdef HAS_OPENCV
    if (m_visionController) m_visionController->Shutdown();
#endif
    Destroy();
}

bool GdiCameraWindow::InitializeGdiPlus() {
    if (s_gdiplusInitialized) return true;

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&s_gdiplusToken, &gdiplusStartupInput, nullptr) == Gdiplus::Ok) {
        s_gdiplusInitialized = true;
        return true;
    }
    return false;
}

void GdiCameraWindow::ShutdownGdiPlus() {
    if (s_gdiplusInitialized) {
        Gdiplus::GdiplusShutdown(s_gdiplusToken);
        s_gdiplusInitialized = false;
    }
}

bool GdiCameraWindow::RegisterWindowClass() {
    if (s_classRegistered) return true;

    std::cout << "[GdiCameraWindow] Registering window class..." << std::endl;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = s_className;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    if (RegisterClassExW(&wc)) {
        s_classRegistered = true;
        std::cout << "[GdiCameraWindow] Window class registered successfully" << std::endl;
        return true;
    }
    DWORD error = GetLastError();
    std::cout << "[GdiCameraWindow] ERROR: Failed to register window class, error code: " << error << std::endl;
    return false;
}

bool GdiCameraWindow::Create(const std::string& title, int x, int y, int width, int height) {
    if (m_hwnd) return true;

    std::cout << "[GdiCameraWindow] Creating window: " << title << " at (" << x << ", " << y << ") size " << width << "x" << height << std::endl;

    if (!RegisterWindowClass()) {
        std::cout << "[GdiCameraWindow] ERROR: Window class registration failed" << std::endl;
        return false;
    }

    int titleLen = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
    std::vector<wchar_t> wideTitle(titleLen);
    MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wideTitle.data(), titleLen);

    int actualX = (x == CW_USEDEFAULT) ? 100 : x;
    int actualY = (y == CW_USEDEFAULT) ? 100 : y;

    m_hwnd = CreateWindowExW(
        WS_EX_OVERLAPPEDWINDOW,
        s_className,
        wideTitle.data(),
        WS_OVERLAPPEDWINDOW,
        actualX, actualY, width, height,
        nullptr,
        nullptr,
        GetModuleHandle(nullptr),
        this
    );

    if (!m_hwnd) {
        DWORD error = GetLastError();
        std::cout << "[GdiCameraWindow] ERROR: CreateWindowExW failed, error code: " << error << std::endl;
        return false;
    }

    // Create menu bar
    CreateMenuBar();

    std::cout << "[GdiCameraWindow] Window created successfully, HWND: " << m_hwnd << std::endl;
    return true;
}

void GdiCameraWindow::CreateMenuBar() {
    m_menuBar = CreateMenu();
    m_showMenu = CreatePopupMenu();
    m_toolsMenu = CreatePopupMenu();
    m_cameraMenu = CreatePopupMenu();

    if (m_menuBar && m_showMenu) {
        // Camera menu (first, most important)
        if (m_cameraMenu) {
            AppendMenuW(m_cameraMenu, MF_STRING, IDM_CAMERA_DUAL_MODE, L"Dual Camera Mode (Split View)");
            AppendMenuW(m_cameraMenu, MF_SEPARATOR, 0, nullptr);
            // Camera list will be populated dynamically
            AppendMenuW(m_menuBar, MF_POPUP, (UINT_PTR)m_cameraMenu, L"Camera");
        }

        // Add "Enable Chart" item at top
        AppendMenuW(m_showMenu, MF_STRING, IDM_SHOW_CHART_ENABLED, L"Enable Chart Overlay");
        AppendMenuW(m_showMenu, MF_SEPARATOR, 0, nullptr);

        // "Data Channels" will be populated dynamically
        AppendMenuW(m_menuBar, MF_POPUP, (UINT_PTR)m_showMenu, L"Show");

#ifdef HAS_OPENCV
        // Vision menu — delegated to GdiVisionController.
        if (m_visionController) {
            m_visionController->SetWindowHandle(m_hwnd);
            m_visionController->BuildMenu(m_menuBar);
        }
#endif

        // Tools menu
        if (m_toolsMenu) {
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_CROSSHAIR, L"Show Crosshair");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_CLICK_TO_MOVE, L"Enable Click-to-Move");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_SHOW_DISTANCE, L"Show Real World Distance");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_MAGNIFICATION, L"Magnification...");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_DISPLAY_ROTATION, L"Display Rotation (-90\u00B0)");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_TOOLS_DISPLAY_ROTATION_CW, L"Display Rotation (+90\u00B0)");
            AppendMenuW(m_toolsMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_OVERLAY_SIZE_INCREASE, L"Increase Overlay Size (+)");
            AppendMenuW(m_toolsMenu, MF_STRING, IDM_OVERLAY_SIZE_DECREASE, L"Decrease Overlay Size (-)");
            AppendMenuW(m_menuBar, MF_POPUP, (UINT_PTR)m_toolsMenu, L"Tools");
        }

        // Beam Stream menu — delegated to GdiBeamStreamController.
        if (m_beamController) {
            m_beamController->SetWindowHandle(m_hwnd);
            m_beamController->BuildMenu(m_menuBar);
        }

        SetMenu(m_hwnd, m_menuBar);
    }
}

void GdiCameraWindow::UpdateShowMenu() {
    if (!m_showMenu) return;

    // Remove old channel items (keep first 2: "Enable Chart" and separator)
    int count = GetMenuItemCount(m_showMenu);
    while (count > 2) {
        DeleteMenu(m_showMenu, count - 1, MF_BYPOSITION);
        count--;
    }

    // Get available channels
    std::vector<std::string> channels;
    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        channels = m_availableChannels;
    }

    if (channels.empty()) {
        AppendMenuW(m_showMenu, MF_STRING | MF_GRAYED, 0, L"(No channels available)");
    } else {
        for (size_t i = 0; i < channels.size() && i < 50; ++i) {
            std::wstring wChannel(channels[i].begin(), channels[i].end());
            UINT flags = MF_STRING;
            if (m_chartOverlay.IsChannelEnabled(channels[i])) {
                flags |= MF_CHECKED;
            }
            AppendMenuW(m_showMenu, flags, IDM_CHANNEL_BASE + static_cast<UINT>(i), wChannel.c_str());
        }
    }

    // Update chart enabled state
    CheckMenuItem(m_showMenu, IDM_SHOW_CHART_ENABLED,
        m_chartOverlay.IsEnabled() ? MF_CHECKED : MF_UNCHECKED);

    DrawMenuBar(m_hwnd);
}

void GdiCameraWindow::UpdateToolsMenu() {
    if (!m_toolsMenu) return;

    CheckMenuItem(m_toolsMenu, IDM_TOOLS_CROSSHAIR,
                  m_crosshairEnabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_toolsMenu, IDM_TOOLS_CLICK_TO_MOVE,
                  m_clickToMoveEnabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_toolsMenu, IDM_TOOLS_SHOW_DISTANCE,
                  m_showDistanceEnabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_toolsMenu, IDM_TOOLS_DISPLAY_ROTATION,
                  (m_displayRotation == -90) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_toolsMenu, IDM_TOOLS_DISPLAY_ROTATION_CW,
                  (m_displayRotation == 90) ? MF_CHECKED : MF_UNCHECKED);

    // Update magnification menu text to show current value
    std::wstring magText = L"Magnification... (" + std::to_wstring(static_cast<int>(m_magnification)) + L"x)";
    ModifyMenuW(m_toolsMenu, IDM_TOOLS_MAGNIFICATION, MF_BYCOMMAND | MF_STRING,
                IDM_TOOLS_MAGNIFICATION, magText.c_str());

    DrawMenuBar(m_hwnd);
}

void GdiCameraWindow::UpdateCameraMenu() {
    if (!m_cameraMenu) return;

    // Remove old camera items (keep first 2: "Dual Camera Mode" and separator)
    int count = GetMenuItemCount(m_cameraMenu);
    while (count > 2) {
        DeleteMenu(m_cameraMenu, count - 1, MF_BYPOSITION);
        count--;
    }

    // Update dual mode checkbox
    CheckMenuItem(m_cameraMenu, IDM_CAMERA_DUAL_MODE,
                  m_dualCameraMode ? MF_CHECKED : MF_UNCHECKED);

    // Get available cameras
    std::vector<std::string> cameras;
    {
        std::lock_guard<std::mutex> lock(m_cameraListMutex);
        cameras = m_availableCameraIds;
    }

    // Get current selections
    std::string primaryCamera;
    std::string secondaryCamera;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        primaryCamera = m_targetCameraId;
        secondaryCamera = m_secondaryCameraId;
    }

    if (cameras.empty()) {
        AppendMenuW(m_cameraMenu, MF_STRING | MF_GRAYED, 0, L"(No cameras available)");
    } else {
        // Primary camera submenu
        HMENU primarySubmenu = CreatePopupMenu();
        if (primarySubmenu) {
            for (size_t i = 0; i < cameras.size() && i < 50; ++i) {
                std::wstring wCameraId(cameras[i].begin(), cameras[i].end());
                UINT flags = MF_STRING;
                if (cameras[i] == primaryCamera || (primaryCamera.empty() && i == 0)) {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(primarySubmenu, flags,
                            IDM_CAMERA_PRIMARY_BASE + static_cast<UINT>(i),
                            wCameraId.c_str());
            }
            AppendMenuW(m_cameraMenu, MF_POPUP, (UINT_PTR)primarySubmenu,
                        m_dualCameraMode ? L"Left Camera (Primary)" : L"Select Camera");
        }

        // Secondary camera submenu (only in dual mode)
        if (m_dualCameraMode) {
            HMENU secondarySubmenu = CreatePopupMenu();
            if (secondarySubmenu) {
                // Add "None" option
                UINT noneFlags = MF_STRING;
                if (secondaryCamera.empty()) {
                    noneFlags |= MF_CHECKED;
                }
                AppendMenuW(secondarySubmenu, noneFlags, IDM_CAMERA_SECONDARY_BASE, L"(None)");
                AppendMenuW(secondarySubmenu, MF_SEPARATOR, 0, nullptr);

                for (size_t i = 0; i < cameras.size() && i < 50; ++i) {
                    std::wstring wCameraId(cameras[i].begin(), cameras[i].end());
                    UINT flags = MF_STRING;
                    if (cameras[i] == secondaryCamera) {
                        flags |= MF_CHECKED;
                    }
                    // Grey out if same as primary
                    if (cameras[i] == primaryCamera) {
                        flags |= MF_GRAYED;
                    }
                    AppendMenuW(secondarySubmenu, flags,
                                IDM_CAMERA_SECONDARY_BASE + 1 + static_cast<UINT>(i),
                                wCameraId.c_str());
                }
                AppendMenuW(m_cameraMenu, MF_POPUP, (UINT_PTR)secondarySubmenu, L"Right Camera (Secondary)");
            }
        }
    }

    DrawMenuBar(m_hwnd);
}

void GdiCameraWindow::OnMenuCommand(WORD commandId) {
    if (commandId == IDM_SHOW_CHART_ENABLED) {
        m_chartOverlay.SetEnabled(!m_chartOverlay.IsEnabled());
        UpdateShowMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId >= IDM_CHANNEL_BASE && commandId < IDM_CHANNEL_BASE + 100) {
        size_t index = commandId - IDM_CHANNEL_BASE;
        std::vector<std::string> channels;
        {
            std::lock_guard<std::mutex> lock(m_channelMutex);
            channels = m_availableChannels;
        }

        if (index < channels.size()) {
            const std::string& channelId = channels[index];
            bool isEnabled = m_chartOverlay.IsChannelEnabled(channelId);
            m_chartOverlay.SetChannelEnabled(channelId, !isEnabled);
            UpdateShowMenu();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
#ifdef HAS_OPENCV
    else if (m_visionController && m_visionController->HandleMenuCommand(commandId)) {
        // Handled by GdiVisionController.
    }
#endif
    // Tools menu commands
    else if (commandId == IDM_TOOLS_CROSSHAIR) {
        m_crosshairEnabled = !m_crosshairEnabled;
        std::cout << "[GdiCameraWindow] Crosshair " << (m_crosshairEnabled ? "enabled" : "disabled") << std::endl;
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId == IDM_TOOLS_CLICK_TO_MOVE) {
        m_clickToMoveEnabled = !m_clickToMoveEnabled;
        std::cout << "[GdiCameraWindow] Click-to-move " << (m_clickToMoveEnabled ? "enabled" : "disabled") << std::endl;
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId == IDM_TOOLS_SHOW_DISTANCE) {
        m_showDistanceEnabled = !m_showDistanceEnabled;
        std::cout << "[GdiCameraWindow] Show distance " << (m_showDistanceEnabled ? "enabled" : "disabled") << std::endl;
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId == IDM_TOOLS_MAGNIFICATION) {
        // Show magnification selection dialog
        struct MagOption { const wchar_t* label; double value; };
        static const MagOption options[] = {
            {L"1x",   1.0},
            {L"10x",  10.0},
            {L"20x",  20.0},
            {L"50x",  50.0},
            {L"100x", 100.0},
            {L"200x", 200.0}
        };
        static const int numOptions = sizeof(options) / sizeof(options[0]);

        // Build a popup menu as a simple magnification picker
        HMENU magPopup = CreatePopupMenu();
        if (magPopup) {
            for (int i = 0; i < numOptions; ++i) {
                UINT flags = MF_STRING;
                // Use a tolerance check for floating point comparison
                if (std::abs(m_magnification - options[i].value) < 0.01) {
                    flags |= MF_CHECKED;
                }
                AppendMenuW(magPopup, flags, 50000 + i, options[i].label);
            }

            // Show popup at cursor position
            POINT pt;
            GetCursorPos(&pt);
            int selected = TrackPopupMenu(magPopup, TPM_RETURNCMD | TPM_NONOTIFY,
                                          pt.x, pt.y, 0, m_hwnd, nullptr);
            DestroyMenu(magPopup);

            if (selected >= 50000 && selected < 50000 + numOptions) {
                m_magnification = options[selected - 50000].value;
                std::cout << "[GdiCameraWindow] Magnification set to " << m_magnification << "x" << std::endl;
                UpdateToolsMenu();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
    }
    else if (commandId == IDM_TOOLS_DISPLAY_ROTATION) {
        m_displayRotation = (m_displayRotation == -90) ? 0 : -90;
        std::cout << "[GdiCameraWindow] Display rotation: " << m_displayRotation << " degrees" << std::endl;
        if (m_onDisplayRotationChanged) {
            m_onDisplayRotationChanged(m_displayRotation);
        }
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    else if (commandId == IDM_TOOLS_DISPLAY_ROTATION_CW) {
        m_displayRotation = (m_displayRotation == 90) ? 0 : 90;
        std::cout << "[GdiCameraWindow] Display rotation: " << m_displayRotation << " degrees" << std::endl;
        if (m_onDisplayRotationChanged) {
            m_onDisplayRotationChanged(m_displayRotation);
        }
        UpdateToolsMenu();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    // Overlay size commands
    else if (commandId == IDM_OVERLAY_SIZE_INCREASE) {
        m_chartOverlay.IncreaseScale();
        std::cout << "[GdiCameraWindow] Overlay scale increased to " << m_chartOverlay.GetScale() << std::endl;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        // Emit signal for settings save (manager will handle this)
        if (m_onOverlayScaleChanged) {
            m_onOverlayScaleChanged(m_chartOverlay.GetScale());
        }
    }
    else if (commandId == IDM_OVERLAY_SIZE_DECREASE) {
        m_chartOverlay.DecreaseScale();
        std::cout << "[GdiCameraWindow] Overlay scale decreased to " << m_chartOverlay.GetScale() << std::endl;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        // Emit signal for settings save (manager will handle this)
        if (m_onOverlayScaleChanged) {
            m_onOverlayScaleChanged(m_chartOverlay.GetScale());
        }
    }
    // Camera menu commands
    else if (commandId == IDM_CAMERA_DUAL_MODE) {
        SetDualCameraMode(!m_dualCameraMode);
        UpdateCameraMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else if (commandId >= IDM_CAMERA_PRIMARY_BASE && commandId < IDM_CAMERA_PRIMARY_BASE + 50) {
        // Primary camera selection
        size_t index = commandId - IDM_CAMERA_PRIMARY_BASE;
        std::vector<std::string> cameras;
        {
            std::lock_guard<std::mutex> lock(m_cameraListMutex);
            cameras = m_availableCameraIds;
        }
        if (index < cameras.size()) {
            std::string newCameraId = cameras[index];
            {
                std::lock_guard<std::mutex> lock(m_cameraMutex);
                m_targetCameraId = newCameraId;
                // If secondary is same as new primary, clear secondary
                if (m_secondaryCameraId == newCameraId) {
                    m_secondaryCameraId.clear();
                }
            }
            std::cout << "[GdiCameraWindow] Primary camera set to: " << newCameraId << std::endl;
            // Clear primary frame buffer to show "Waiting for camera..."
            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_frameWidth = 0;
                m_frameHeight = 0;
            }
            UpdateCameraMenu();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
    else if (commandId >= IDM_CAMERA_SECONDARY_BASE && commandId < IDM_CAMERA_SECONDARY_BASE + 51) {
        // Secondary camera selection
        if (commandId == IDM_CAMERA_SECONDARY_BASE) {
            // "None" selected
            {
                std::lock_guard<std::mutex> lock(m_cameraMutex);
                m_secondaryCameraId.clear();
            }
            std::cout << "[GdiCameraWindow] Secondary camera cleared" << std::endl;
        } else {
            size_t index = commandId - IDM_CAMERA_SECONDARY_BASE - 1;
            std::vector<std::string> cameras;
            {
                std::lock_guard<std::mutex> lock(m_cameraListMutex);
                cameras = m_availableCameraIds;
            }
            if (index < cameras.size()) {
                std::string newCameraId = cameras[index];
                {
                    std::lock_guard<std::mutex> lock(m_cameraMutex);
                    // Don't set if same as primary
                    if (newCameraId != m_targetCameraId) {
                        m_secondaryCameraId = newCameraId;
                    }
                }
                std::cout << "[GdiCameraWindow] Secondary camera set to: " << newCameraId << std::endl;
            }
        }
        // Clear secondary frame buffer
        {
            std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);
            m_secondaryFrameWidth = 0;
            m_secondaryFrameHeight = 0;
        }
        UpdateCameraMenu();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    // Beam Stream menu commands — delegated to GdiBeamStreamController.
    else if (m_beamController) {
        std::vector<std::string> cameras;
        {
            std::lock_guard<std::mutex> lock(m_cameraListMutex);
            cameras = m_availableCameraIds;
        }
        m_beamController->HandleMenuCommand(commandId, cameras);
    }
}

void GdiCameraWindow::Destroy() {
    if (m_beamController) m_beamController->Shutdown();
#ifdef HAS_OPENCV
    if (m_visionController) m_visionController->Shutdown();
#endif

    if (m_memDC) {
        if (m_oldBitmap) {
            SelectObject(m_memDC, m_oldBitmap);
            m_oldBitmap = nullptr;
        }
        if (m_memBitmap) {
            DeleteObject(m_memBitmap);
            m_memBitmap = nullptr;
        }
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void GdiCameraWindow::Show() {
    if (m_hwnd) {
        std::cout << "[GdiCameraWindow] Showing window..." << std::endl;
        ShowWindow(m_hwnd, SW_SHOWNORMAL);
        UpdateWindow(m_hwnd);
        SetForegroundWindow(m_hwnd);
        std::cout << "[GdiCameraWindow] Window shown and brought to foreground" << std::endl;
    } else {
        std::cout << "[GdiCameraWindow] ERROR: Cannot show - HWND is null" << std::endl;
    }
}

void GdiCameraWindow::Hide() {
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

bool GdiCameraWindow::IsVisible() const {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void GdiCameraWindow::SetTitle(const std::string& title) {
    if (m_hwnd) {
        int titleLen = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> wideTitle(titleLen);
        MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wideTitle.data(), titleLen);
        SetWindowTextW(m_hwnd, wideTitle.data());
    }
}

void GdiCameraWindow::SetTargetCamera(const std::string& cameraId) {
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        m_targetCameraId = cameraId;
    }
    // Reload calibration for the new camera
    LoadCalibrationFactors();
}

std::string GdiCameraWindow::GetTargetCamera() const {
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    return m_targetCameraId;
}

void GdiCameraWindow::SetDisplayRotation(int degrees) {
    m_displayRotation = degrees;
    std::cout << "[GdiCameraWindow] Display rotation: " << degrees << " degrees" << std::endl;
    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void GdiCameraWindow::SetDataStore(DataStore* dataStore) {
    m_dataStore = dataStore;
    if (m_dataStore) {
        // Subscribe to all data changes
        m_dataStore->SubscribeToAll([this](const std::string& channelId, float value) {
            OnDataStoreValueChanged(channelId, value);
        });
        RefreshChannelMenu();
    }
}

void GdiCameraWindow::RefreshChannelMenu() {
    if (!m_dataStore) return;

    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        m_availableChannels = m_dataStore->GetAvailableChannels();
    }

    // Post message to update menu on main thread
    if (m_hwnd) {
        PostMessage(m_hwnd, WM_USER + 100, 0, 0);  // Custom message to refresh menu
    }
}

void GdiCameraWindow::OnDataStoreValueChanged(const std::string& channelId, float value) {
    // Add data point if channel is enabled
    if (m_chartOverlay.IsChannelEnabled(channelId)) {
        m_chartOverlay.AddDataPoint(channelId, static_cast<double>(value));

        // Throttled repaint to prevent UI freezing from high-frequency updates
        // Instead of calling InvalidateRect for every data point, we rate-limit repaints
        if (m_hwnd && m_chartOverlay.IsEnabled()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastChartRepaint).count();

            // Only request repaint if enough time has passed since last one
            if (elapsed >= CHART_REPAINT_INTERVAL_MS) {
                m_lastChartRepaint = now;
                // Use PostMessage for thread-safe UI update (safer than InvalidateRect from callback)
                PostMessage(m_hwnd, WM_CHART_REPAINT_REQUEST, 0, 0);  // Throttled chart repaint
            }
        }
    }

    // Check if this is a new channel
    bool isNew = false;
    {
        std::lock_guard<std::mutex> lock(m_channelMutex);
        auto it = std::find(m_availableChannels.begin(), m_availableChannels.end(), channelId);
        if (it == m_availableChannels.end()) {
            m_availableChannels.push_back(channelId);
            isNew = true;
        }
    }

    // Refresh menu if new channel discovered
    if (isNew && m_hwnd) {
        PostMessage(m_hwnd, WM_USER + 100, 0, 0);
    }
}

bool GdiCameraWindow::WantsFramesFromCamera(const std::string& cameraId) const {
    // Also accept frames from beam stream camera
    if (m_beamController && m_beamController->IsActive() &&
        m_beamController->IsStreamCamera(cameraId)) {
        return true;
    }

    std::lock_guard<std::mutex> lock(m_cameraMutex);
    // In dual mode, accept frames from both primary and secondary cameras
    if (m_dualCameraMode) {
        if (m_targetCameraId.empty()) return true;
        if (m_targetCameraId == cameraId) return true;
        if (!m_secondaryCameraId.empty() && m_secondaryCameraId == cameraId) return true;
        return false;
    }
    // Single mode: accept all if no target, or only target camera
    if (m_targetCameraId.empty()) return true;
    return m_targetCameraId == cameraId;
}

void GdiCameraWindow::OnNewFrame(const CameraFrameData& frameData) {
    if (!frameData.IsValid()) return;

    // Determine if this is primary or secondary camera
    bool isPrimaryCamera = false;
    bool isSecondaryCamera = false;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        if (m_dualCameraMode) {
            // In dual mode, check if this matches primary or secondary
            if (m_targetCameraId.empty() || frameData.cameraId == m_targetCameraId) {
                isPrimaryCamera = true;
            } else if (!m_secondaryCameraId.empty() && frameData.cameraId == m_secondaryCameraId) {
                isSecondaryCamera = true;
            }
        } else {
            // Single mode: all frames go to primary
            if (m_targetCameraId.empty() || frameData.cameraId == m_targetCameraId) {
                isPrimaryCamera = true;
            }
        }
    }

    static std::mutex loggedCamerasMutex;
    static std::set<std::string> loggedCameras;
    {
        std::lock_guard<std::mutex> logLock(loggedCamerasMutex);
        if (loggedCameras.find(frameData.cameraId) == loggedCameras.end()) {
            std::cout << "[GdiCameraWindow] First frame received from camera: " << frameData.cameraId
                      << " (" << frameData.width << "x" << frameData.height << ")"
                      << (isPrimaryCamera ? " [Primary]" : "")
                      << (isSecondaryCamera ? " [Secondary]" : "") << std::endl;
            loggedCameras.insert(frameData.cameraId);
        }
    }

    // Store frame in appropriate buffer
    if (isPrimaryCamera) {
        std::lock_guard<std::mutex> lock(m_frameMutex);

        size_t requiredSize = frameData.width * frameData.height * 3;
        if (m_frameBuffer.size() < requiredSize) {
            m_frameBuffer.resize(requiredSize);
        }

        m_frameWidth = frameData.width;
        m_frameHeight = frameData.height;

        const uint8_t* src = frameData.imageData.data();
        uint8_t* dst = m_frameBuffer.data();

        // RGB->BGR conversion + vertical flip for bottom-up BMP
        for (uint32_t y = 0; y < frameData.height; ++y) {
            uint32_t srcRow = y;
            uint32_t dstRow = frameData.height - 1 - y;

            const uint8_t* srcLine = src + srcRow * frameData.width * 3;
            uint8_t* dstLine = dst + dstRow * frameData.width * 3;

            for (uint32_t x = 0; x < frameData.width; ++x) {
                dstLine[x * 3 + 0] = srcLine[x * 3 + 2];  // B
                dstLine[x * 3 + 1] = srcLine[x * 3 + 1];  // G
                dstLine[x * 3 + 2] = srcLine[x * 3 + 0];  // R
            }
        }

        // Apply rotation if enabled
        if (m_displayRotation != 0) {
            m_originalFrameWidth = m_frameWidth;
            m_originalFrameHeight = m_frameHeight;

            uint32_t srcW = m_frameWidth;
            uint32_t srcH = m_frameHeight;
            // Rotated dimensions: W becomes H, H becomes W
            uint32_t rotW = srcH;
            uint32_t rotH = srcW;

            std::vector<uint8_t> rotated(rotW * rotH * 3);
            for (uint32_t dy = 0; dy < rotH; ++dy) {
                for (uint32_t dx = 0; dx < rotW; ++dx) {
                    uint32_t sx, sy;
                    if (m_displayRotation == -90) {
                        // -90° CCW: dest(dx, dy) = src(srcW - 1 - dy, dx)
                        sx = srcW - 1 - dy;
                        sy = dx;
                    }
                    else {
                        // +90° CW: dest(dx, dy) = src(dy, srcH - 1 - dx)
                        sx = dy;
                        sy = srcH - 1 - dx;
                    }
                    size_t srcIdx = (sy * srcW + sx) * 3;
                    size_t dstIdx = (dy * rotW + dx) * 3;
                    rotated[dstIdx + 0] = m_frameBuffer[srcIdx + 0];
                    rotated[dstIdx + 1] = m_frameBuffer[srcIdx + 1];
                    rotated[dstIdx + 2] = m_frameBuffer[srcIdx + 2];
                }
            }

            m_frameBuffer = std::move(rotated);
            m_frameWidth = rotW;
            m_frameHeight = rotH;
        }

        m_hasNewFrame = true;

        // Track acquisition FPS
        ++m_acquisitionFrameCount;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastAcquisitionFpsTime).count();
        if (elapsed >= 1000) {
            m_acquisitionFps.store(m_acquisitionFrameCount * 1000.0 / elapsed);
            m_acquisitionFrameCount = 0;
            m_lastAcquisitionFpsTime = now;
        }
    }
    else if (isSecondaryCamera) {
        std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);

        size_t requiredSize = frameData.width * frameData.height * 3;
        if (m_secondaryFrameBuffer.size() < requiredSize) {
            m_secondaryFrameBuffer.resize(requiredSize);
        }

        m_secondaryFrameWidth = frameData.width;
        m_secondaryFrameHeight = frameData.height;

        const uint8_t* src = frameData.imageData.data();
        uint8_t* dst = m_secondaryFrameBuffer.data();

        // RGB->BGR conversion + vertical flip for bottom-up BMP
        for (uint32_t y = 0; y < frameData.height; ++y) {
            uint32_t srcRow = y;
            uint32_t dstRow = frameData.height - 1 - y;

            const uint8_t* srcLine = src + srcRow * frameData.width * 3;
            uint8_t* dstLine = dst + dstRow * frameData.width * 3;

            for (uint32_t x = 0; x < frameData.width; ++x) {
                dstLine[x * 3 + 0] = srcLine[x * 3 + 2];  // B
                dstLine[x * 3 + 1] = srcLine[x * 3 + 1];  // G
                dstLine[x * 3 + 2] = srcLine[x * 3 + 0];  // R
            }
        }

        m_hasSecondaryNewFrame = true;
    } else {
        return; // Frame not for us
    }

#ifdef HAS_OPENCV
    // Queue frame for vision processing if enabled (only for primary
    // camera). Rotation is applied inside the controller so the vision
    // thread sees what the user sees.
    if (isPrimaryCamera && m_visionController) {
        m_visionController->EnqueueFrame(frameData.imageData,
                                         frameData.width, frameData.height,
                                         m_displayRotation);
    }
#endif

    // Queue frame for beam streaming (controller filters by camera id).
    if (m_beamController) {
        if (isPrimaryCamera) {
            m_beamController->SetCurrentPrimaryFrameSize(frameData.width, frameData.height);
        }
        m_beamController->EnqueueFrame(frameData.imageData,
                                       frameData.width, frameData.height,
                                       frameData.cameraId);
    }

    if (m_hwnd && IsWindowVisible(m_hwnd)) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void GdiCameraWindow::OnCameraStatusChanged(const std::string& cameraId, bool connected, bool grabbing) {
    bool isPrimaryCamera = false;
    bool isSecondaryCamera = false;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        if (m_dualCameraMode) {
            if (m_targetCameraId.empty() || m_targetCameraId == cameraId) {
                isPrimaryCamera = true;
            } else if (!m_secondaryCameraId.empty() && m_secondaryCameraId == cameraId) {
                isSecondaryCamera = true;
            }
        } else {
            if (m_targetCameraId.empty() || m_targetCameraId == cameraId) {
                isPrimaryCamera = true;
            }
        }
    }

    if (!isPrimaryCamera && !isSecondaryCamera) {
        return;
    }

    // Only update main status for primary camera
    if (isPrimaryCamera) {
        m_cameraConnected = connected;
        m_cameraGrabbing = grabbing;
    }

    if (m_hwnd) {
        std::string title = "Camera Monitor (GDI)";
        if (m_dualCameraMode) {
            title += " [Dual View]";
        }
        if (!m_cameraConnected) {
            title += " [Disconnected]";
        } else if (!m_cameraGrabbing) {
            title += " [Stopped]";
        } else {
            title += " [Live]";
        }
        SetTitle(title);
    }
}

bool GdiCameraWindow::ProcessMessages() {
    if (!m_hwnd) return true;
    // CRITICAL: filter by m_hwnd. With nullptr filter this would drain
    // every window's messages on this thread (including all Qt windows),
    // dispatch them via DispatchMessage, and starve Qt's own event loop.
    MSG msg;
    while (PeekMessage(&msg, m_hwnd, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

LRESULT CALLBACK GdiCameraWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    GdiCameraWindow* pThis = nullptr;

    if (uMsg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<GdiCameraWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<GdiCameraWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis) {
        return pThis->HandleMessage(hwnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT GdiCameraWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_COMMAND:
        OnMenuCommand(LOWORD(wParam));
        return 0;

    case WM_INITMENUPOPUP:
        if ((HMENU)wParam == m_showMenu) {
            UpdateShowMenu();
        }
#ifdef HAS_OPENCV
        else if (m_visionController && (HMENU)wParam == m_visionController->GetMenu()) {
            m_visionController->OnMenuOpening();
        }
#endif
        else if ((HMENU)wParam == m_toolsMenu) {
            UpdateToolsMenu();
        }
        else if ((HMENU)wParam == m_cameraMenu) {
            RefreshCameraMenu();
            UpdateCameraMenu();
        }
        else if (m_beamController && (HMENU)wParam == m_beamController->GetMenu()) {
            RefreshCameraMenu();
            std::vector<std::string> cameras;
            {
                std::lock_guard<std::mutex> lock(m_cameraListMutex);
                cameras = m_availableCameraIds;
            }
            m_beamController->OnMenuOpening(cameras);
        }
        return 0;

    case WM_USER + 100:  // Custom message to refresh menu
        UpdateShowMenu();
        return 0;

    case WM_CHART_REPAINT_REQUEST:  // Throttled chart repaint request
        if (IsWindowVisible(m_hwnd)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_VISION_RESULT_READY:  // Vision result ready from background thread
        if (IsWindowVisible(m_hwnd)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_BEAM_STREAM_RESULT_READY:  // Beam stream result ready
        if (IsWindowVisible(m_hwnd)) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (m_clickToMoveEnabled || m_crosshairEnabled) {
            m_mousePosition.x = LOWORD(lParam);
            m_mousePosition.y = HIWORD(lParam);

            // Request WM_MOUSELEAVE notification
            if (!m_mouseOverImage) {
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = m_hwnd;
                TrackMouseEvent(&tme);
            }

            m_mouseOverImage = true;
            SetCursor(LoadCursor(nullptr, m_clickToMoveEnabled ? IDC_CROSS : IDC_ARROW));
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (m_clickToMoveEnabled) {
            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);
            HandleClickToMove(mouseX, mouseY);
        }
        return 0;

    case WM_MOUSELEAVE:
        m_mouseOverImage = false;
        if (m_clickToMoveEnabled) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_CLOSE:
        Hide();
        return 0;

    case WM_DESTROY:
        if (m_memDC) {
            if (m_oldBitmap) {
                SelectObject(m_memDC, m_oldBitmap);
                m_oldBitmap = nullptr;
            }
            if (m_memBitmap) {
                DeleteObject(m_memBitmap);
                m_memBitmap = nullptr;
            }
            DeleteDC(m_memDC);
            m_memDC = nullptr;
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            Hide();
            return 0;
        }
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void GdiCameraWindow::PrepareBackBuffer() {
    RECT rect;
    GetClientRect(m_hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (width <= 0 || height <= 0) return;

    if (width != m_bufferWidth || height != m_bufferHeight || !m_memDC) {
        HDC hdc = GetDC(m_hwnd);

        if (m_memDC) {
            if (m_oldBitmap) {
                SelectObject(m_memDC, m_oldBitmap);
                m_oldBitmap = nullptr;
            }
            if (m_memBitmap) {
                DeleteObject(m_memBitmap);
                m_memBitmap = nullptr;
            }
            DeleteDC(m_memDC);
        }

        m_memDC = CreateCompatibleDC(hdc);
        m_memBitmap = CreateCompatibleBitmap(hdc, width, height);
        m_oldBitmap = (HBITMAP)SelectObject(m_memDC, m_memBitmap);

        m_bufferWidth = width;
        m_bufferHeight = height;

        ReleaseDC(m_hwnd, hdc);
    }
}

void GdiCameraWindow::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    PrepareBackBuffer();

    if (m_memDC) {
        RECT rect = {0, 0, m_bufferWidth, m_bufferHeight};
        HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
        FillRect(m_memDC, &rect, blackBrush);

        RenderFrame(m_memDC);

        // Draw crosshairs (center and tracking)
        DrawCrosshairs(m_memDC, rect);

#ifdef HAS_OPENCV
        // Draw vision overlay (on LEFT side of camera)
        m_visionOverlay.Draw(m_memDC, rect, m_frameWidth, m_frameHeight,
                             m_lastDestX, m_lastDestY, m_lastDestWidth, m_lastDestHeight);
#endif

        // Draw beam stream overlay (top-right)
        const double umPerPxX = m_beamController ? m_beamController->GetUmPerPxX() : 0.25;
        const double umPerPxY = m_beamController ? m_beamController->GetUmPerPxY() : 0.25;
        m_beamStreamOverlay.Draw(m_memDC, rect, m_frameWidth, m_frameHeight,
                                  m_lastDestX, m_lastDestY, m_lastDestWidth, m_lastDestHeight,
                                  umPerPxX, umPerPxY,
                                  m_displayRotation,
                                  static_cast<int>(m_originalFrameWidth),
                                  static_cast<int>(m_originalFrameHeight));

        // Draw chart overlay (on RIGHT side of camera)
        m_chartOverlay.Draw(m_memDC, rect);

        // Draw FPS overlay (bottom-left)
        DrawFpsOverlay(m_memDC, rect);

        // Track display FPS
        ++m_displayFrameCount;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDisplayFpsTime).count();
        if (elapsed >= 1000) {
            m_displayFps.store(m_displayFrameCount * 1000.0 / elapsed);
            m_displayFrameCount = 0;
            m_lastDisplayFpsTime = now;
        }

        BitBlt(hdc, 0, 0, m_bufferWidth, m_bufferHeight, m_memDC, 0, 0, SRCCOPY);
    }

    EndPaint(m_hwnd, &ps);
}

void GdiCameraWindow::DrawFpsOverlay(HDC hDC, const RECT& windowRect) {
    Gdiplus::Graphics gfx(hDC);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::Font font(L"Consolas", 9.0f, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(220, 255, 255, 255));
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(160, 0, 0, 0));

    double dispFps = m_displayFps.load();
    double acqFps = m_acquisitionFps.load();

    wchar_t line1[64];
    wchar_t line2[64];
    swprintf_s(line1, L"Display:     %.1f fps", dispFps);
    swprintf_s(line2, L"Acquisition: %.1f fps", acqFps);

    float x = 5.0f;
    float y = static_cast<float>(windowRect.bottom - windowRect.top) - 40.0f;

    // Background
    Gdiplus::RectF bgRect(x, y, 190.0f, 36.0f);
    gfx.FillRectangle(&bgBrush, bgRect);

    // Text
    gfx.DrawString(line1, -1, &font, Gdiplus::PointF(x + 4, y + 2), &textBrush);
    gfx.DrawString(line2, -1, &font, Gdiplus::PointF(x + 4, y + 18), &textBrush);
}

void GdiCameraWindow::RenderFrame(HDC hdc) {
    if (m_dualCameraMode) {
        // Dual camera mode: split view 50/50
        int halfWidth = m_bufferWidth / 2;

        // Render primary (left side)
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);

            if (m_frameWidth == 0 || m_frameHeight == 0 || m_frameBuffer.empty()) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(128, 128, 128));

                RECT rect = {0, 0, halfWidth, m_bufferHeight};
                DrawTextW(hdc, L"Left: Waiting for camera...", -1, &rect,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                m_lastDestX = 0;
                m_lastDestY = 0;
                m_lastDestWidth = 0;
                m_lastDestHeight = 0;
            } else {
                BITMAPINFOHEADER& bih = m_bitmapInfo->bmiHeader;
                bih.biSize = sizeof(BITMAPINFOHEADER);
                bih.biWidth = m_frameWidth;
                bih.biHeight = m_frameHeight;
                bih.biPlanes = 1;
                bih.biBitCount = 24;
                bih.biCompression = BI_RGB;
                bih.biSizeImage = 0;
                bih.biXPelsPerMeter = 0;
                bih.biYPelsPerMeter = 0;
                bih.biClrUsed = 0;
                bih.biClrImportant = 0;

                float frameAspect = static_cast<float>(m_frameWidth) / m_frameHeight;
                float windowAspect = static_cast<float>(halfWidth) / m_bufferHeight;

                int destWidth, destHeight, destX, destY;

                if (frameAspect > windowAspect) {
                    destWidth = halfWidth;
                    destHeight = static_cast<int>(halfWidth / frameAspect);
                    destX = 0;
                    destY = (m_bufferHeight - destHeight) / 2;
                } else {
                    destHeight = m_bufferHeight;
                    destWidth = static_cast<int>(m_bufferHeight * frameAspect);
                    destX = (halfWidth - destWidth) / 2;
                    destY = 0;
                }

                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchDIBits(
                    hdc,
                    destX, destY, destWidth, destHeight,
                    0, 0, m_frameWidth, m_frameHeight,
                    m_frameBuffer.data(),
                    m_bitmapInfo,
                    DIB_RGB_COLORS,
                    SRCCOPY
                );

                m_lastDestX = destX;
                m_lastDestY = destY;
                m_lastDestWidth = destWidth;
                m_lastDestHeight = destHeight;
            }
            m_hasNewFrame = false;
        }

        // Draw divider line
        Gdiplus::Graphics gfx(hdc);
        Gdiplus::Pen dividerPen(Gdiplus::Color(255, 80, 80, 80), 2.0f);
        gfx.DrawLine(&dividerPen, halfWidth, 0, halfWidth, m_bufferHeight);

        // Render secondary (right side)
        {
            std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);

            if (m_secondaryFrameWidth == 0 || m_secondaryFrameHeight == 0 || m_secondaryFrameBuffer.empty()) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(128, 128, 128));

                RECT rect = {halfWidth, 0, m_bufferWidth, m_bufferHeight};

                // Check if secondary camera is selected
                std::string secondaryCam;
                {
                    std::lock_guard<std::mutex> camLock(m_cameraMutex);
                    secondaryCam = m_secondaryCameraId;
                }
                if (secondaryCam.empty()) {
                    DrawTextW(hdc, L"Right: Select secondary camera", -1, &rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else {
                    DrawTextW(hdc, L"Right: Waiting for camera...", -1, &rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                m_lastSecondaryDestX = 0;
                m_lastSecondaryDestY = 0;
                m_lastSecondaryDestWidth = 0;
                m_lastSecondaryDestHeight = 0;
            } else {
                // Use a separate BITMAPINFO for secondary to avoid conflicts
                BITMAPINFO bitmapInfo2 = {};
                BITMAPINFOHEADER& bih2 = bitmapInfo2.bmiHeader;
                bih2.biSize = sizeof(BITMAPINFOHEADER);
                bih2.biWidth = m_secondaryFrameWidth;
                bih2.biHeight = m_secondaryFrameHeight;
                bih2.biPlanes = 1;
                bih2.biBitCount = 24;
                bih2.biCompression = BI_RGB;

                float frameAspect = static_cast<float>(m_secondaryFrameWidth) / m_secondaryFrameHeight;
                float windowAspect = static_cast<float>(halfWidth) / m_bufferHeight;

                int destWidth, destHeight, destX, destY;

                if (frameAspect > windowAspect) {
                    destWidth = halfWidth;
                    destHeight = static_cast<int>(halfWidth / frameAspect);
                    destX = halfWidth;  // Start at right half
                    destY = (m_bufferHeight - destHeight) / 2;
                } else {
                    destHeight = m_bufferHeight;
                    destWidth = static_cast<int>(m_bufferHeight * frameAspect);
                    destX = halfWidth + (halfWidth - destWidth) / 2;
                    destY = 0;
                }

                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchDIBits(
                    hdc,
                    destX, destY, destWidth, destHeight,
                    0, 0, m_secondaryFrameWidth, m_secondaryFrameHeight,
                    m_secondaryFrameBuffer.data(),
                    &bitmapInfo2,
                    DIB_RGB_COLORS,
                    SRCCOPY
                );

                m_lastSecondaryDestX = destX;
                m_lastSecondaryDestY = destY;
                m_lastSecondaryDestWidth = destWidth;
                m_lastSecondaryDestHeight = destHeight;
            }
            m_hasSecondaryNewFrame = false;
        }

        // Draw camera labels
        {
            Gdiplus::Graphics gfx2(hdc);
            gfx2.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
            Gdiplus::Font labelFont(L"Segoe UI", 10.0f, Gdiplus::FontStyleBold);
            Gdiplus::SolidBrush labelBrush(Gdiplus::Color(220, 255, 255, 255));
            Gdiplus::SolidBrush bgBrush(Gdiplus::Color(160, 0, 0, 0));

            // Get camera IDs for labels
            std::string primaryId, secondaryId;
            {
                std::lock_guard<std::mutex> camLock(m_cameraMutex);
                primaryId = m_targetCameraId.empty() ? "Primary" : m_targetCameraId;
                secondaryId = m_secondaryCameraId.empty() ? "(None)" : m_secondaryCameraId;
            }

            // Left label
            std::wstring leftLabel = L"L: " + std::wstring(primaryId.begin(), primaryId.end());
            Gdiplus::RectF leftLabelRect(5, 5, 200, 20);
            gfx2.FillRectangle(&bgBrush, leftLabelRect);
            gfx2.DrawString(leftLabel.c_str(), -1, &labelFont, Gdiplus::PointF(8, 5), &labelBrush);

            // Right label
            std::wstring rightLabel = L"R: " + std::wstring(secondaryId.begin(), secondaryId.end());
            Gdiplus::RectF rightLabelRect(static_cast<float>(halfWidth + 5), 5, 200, 20);
            gfx2.FillRectangle(&bgBrush, rightLabelRect);
            gfx2.DrawString(rightLabel.c_str(), -1, &labelFont, Gdiplus::PointF(static_cast<float>(halfWidth + 8), 5), &labelBrush);
        }
    } else {
        // Single camera mode (original behavior)
        std::lock_guard<std::mutex> lock(m_frameMutex);

        if (m_frameWidth == 0 || m_frameHeight == 0 || m_frameBuffer.empty()) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(128, 128, 128));

            RECT rect = {0, 0, m_bufferWidth, m_bufferHeight};
            DrawTextW(hdc, L"Waiting for camera...", -1, &rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }

        BITMAPINFOHEADER& bih = m_bitmapInfo->bmiHeader;
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = m_frameWidth;
        bih.biHeight = m_frameHeight;
        bih.biPlanes = 1;
        bih.biBitCount = 24;
        bih.biCompression = BI_RGB;
        bih.biSizeImage = 0;
        bih.biXPelsPerMeter = 0;
        bih.biYPelsPerMeter = 0;
        bih.biClrUsed = 0;
        bih.biClrImportant = 0;

        float frameAspect = static_cast<float>(m_frameWidth) / m_frameHeight;
        float windowAspect = static_cast<float>(m_bufferWidth) / m_bufferHeight;

        int destWidth, destHeight, destX, destY;

        if (frameAspect > windowAspect) {
            destWidth = m_bufferWidth;
            destHeight = static_cast<int>(m_bufferWidth / frameAspect);
            destX = 0;
            destY = (m_bufferHeight - destHeight) / 2;
        } else {
            destHeight = m_bufferHeight;
            destWidth = static_cast<int>(m_bufferHeight * frameAspect);
            destX = (m_bufferWidth - destWidth) / 2;
            destY = 0;
        }

        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(
            hdc,
            destX, destY, destWidth, destHeight,
            0, 0, m_frameWidth, m_frameHeight,
            m_frameBuffer.data(),
            m_bitmapInfo,
            DIB_RGB_COLORS,
            SRCCOPY
        );

        // Store destination info for overlays (vision overlay and crosshair)
        m_lastDestX = destX;
        m_lastDestY = destY;
        m_lastDestWidth = destWidth;
        m_lastDestHeight = destHeight;

        m_hasNewFrame = false;
    }
}

// ============================================================================
// Click-to-Move and Crosshair Implementation
// ============================================================================

void GdiCameraWindow::SetCameraConfigManager(CameraConfigManager* configMgr) {
    m_cameraConfigManager = configMgr;
    // Reload calibration with per-camera support now available
    LoadCalibrationFactors();
}

void GdiCameraWindow::LoadCalibrationFactors() {
    // Try per-camera calibration from CameraConfigManager first
    if (m_cameraConfigManager && !m_targetCameraId.empty()) {
        CameraConfigData::PixelCalibration cal;
        if (m_cameraConfigManager->GetCalibration(m_targetCameraId, cal)) {
            m_pixelToMmFactorX = cal.pixelToMmX;
            m_pixelToMmFactorY = cal.pixelToMmY;
            m_calibrationRefWidth = cal.referenceWidth;
            m_calibrationRefHeight = cal.referenceHeight;
            std::cout << "[GdiCameraWindow] Per-camera calibration for '" << m_targetCameraId
                      << "': PixelToMm X=" << m_pixelToMmFactorX
                      << ", Y=" << m_pixelToMmFactorY;
            if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0) {
                std::cout << ", RefRes=" << m_calibrationRefWidth << "x" << m_calibrationRefHeight;
            }
            std::cout << std::endl;
            return;
        }
        std::cout << "[GdiCameraWindow] No per-camera calibration for '" << m_targetCameraId
                  << "', falling back to val_pixelmm.json" << std::endl;
    }

    // Fallback: load from global JSON config file (shared with CameraClickToMoveWidget)
    std::vector<std::string> configPaths = {
        CALIBRATION_CONFIG_FILE,
        std::string("./") + CALIBRATION_CONFIG_FILE,
        std::string("../") + CALIBRATION_CONFIG_FILE
    };

    for (const auto& configPath : configPaths) {
        try {
            std::ifstream file(configPath);
            if (file.is_open()) {
                nlohmann::json j;
                file >> j;

                if (j.contains("calibration")) {
                    auto& cal = j["calibration"];
                    m_pixelToMmFactorX = cal.value("pixel_to_mm_x", 0.00248);
                    m_pixelToMmFactorY = cal.value("pixel_to_mm_y", 0.00248);
                    m_calibrationRefWidth = cal.value("reference_width", 0);
                    m_calibrationRefHeight = cal.value("reference_height", 0);
                    std::cout << "[GdiCameraWindow] Loaded calibration from fallback: PixelToMm X=" << m_pixelToMmFactorX
                              << ", Y=" << m_pixelToMmFactorY;
                    if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0) {
                        std::cout << ", RefRes=" << m_calibrationRefWidth << "x" << m_calibrationRefHeight;
                    }
                    std::cout << std::endl;
                    return;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[GdiCameraWindow] Error loading calibration from " << configPath
                      << ": " << e.what() << std::endl;
        }
    }

    std::cout << "[GdiCameraWindow] Using default calibration factors: X=" << m_pixelToMmFactorX
              << ", Y=" << m_pixelToMmFactorY << std::endl;
}

void GdiCameraWindow::SetMotionManager(UnifiedMotionManager* motionManager) {
    m_motionManager = motionManager;
    std::cout << "[GdiCameraWindow] Motion manager set" << std::endl;
}

void GdiCameraWindow::SetCameraManager(CameraManager* cameraManager) {
    m_cameraManager = cameraManager;
    if (m_cameraManager) {
        RefreshCameraMenu();
        std::cout << "[GdiCameraWindow] Camera manager set" << std::endl;
    }
}

void GdiCameraWindow::RefreshCameraMenu() {
    if (!m_cameraManager) return;

    std::vector<std::string> cameras = m_cameraManager->GetCameraIds();
    {
        std::lock_guard<std::mutex> lock(m_cameraListMutex);
        m_availableCameraIds = cameras;
    }

    std::cout << "[GdiCameraWindow] Refreshed camera list: " << cameras.size() << " cameras available" << std::endl;
}

void GdiCameraWindow::SetDualCameraMode(bool enabled) {
    m_dualCameraMode = enabled;
    std::cout << "[GdiCameraWindow] Dual camera mode " << (enabled ? "enabled" : "disabled") << std::endl;

    if (!enabled) {
        // Clear secondary camera data
        {
            std::lock_guard<std::mutex> lock(m_cameraMutex);
            m_secondaryCameraId.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_secondaryFrameMutex);
            m_secondaryFrameWidth = 0;
            m_secondaryFrameHeight = 0;
        }
    }

    // Update window title
    if (m_hwnd) {
        std::string title = "Camera Monitor (GDI)";
        if (enabled) {
            title += " [Dual View]";
        }
        if (m_cameraConnected && m_cameraGrabbing) {
            title += " [Live]";
        }
        SetTitle(title);
    }

    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void GdiCameraWindow::SetSecondaryCameraId(const std::string& cameraId) {
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    m_secondaryCameraId = cameraId;
    std::cout << "[GdiCameraWindow] Secondary camera set to: " << (cameraId.empty() ? "(none)" : cameraId) << std::endl;
}

std::string GdiCameraWindow::GetSecondaryCameraId() const {
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    return m_secondaryCameraId;
}

void GdiCameraWindow::SetClickToMoveEnabled(bool enabled) {
    m_clickToMoveEnabled = enabled;
    UpdateToolsMenu();
    std::cout << "[GdiCameraWindow] Click-to-move " << (enabled ? "enabled" : "disabled") << std::endl;
}

void GdiCameraWindow::SetCrosshairEnabled(bool enabled) {
    m_crosshairEnabled = enabled;
    UpdateToolsMenu();
    std::cout << "[GdiCameraWindow] Crosshair " << (enabled ? "enabled" : "disabled") << std::endl;
}

void GdiCameraWindow::SetShowDistanceEnabled(bool enabled) {
    m_showDistanceEnabled = enabled;
    UpdateToolsMenu();
    std::cout << "[GdiCameraWindow] Show distance " << (enabled ? "enabled" : "disabled") << std::endl;
}

void GdiCameraWindow::SetMagnification(double mag) {
    if (mag > 0.0) {
        m_magnification = mag;
        UpdateToolsMenu();
        std::cout << "[GdiCameraWindow] Magnification set to " << m_magnification << "x" << std::endl;
    }
}

void GdiCameraWindow::DrawCrosshairs(HDC hDC, const RECT& /*windowRect*/) {
    if (!m_crosshairEnabled && !m_clickToMoveEnabled && !m_showDistanceEnabled) return;
    if (m_lastDestWidth <= 0 || m_lastDestHeight <= 0) return;

    Gdiplus::Graphics gfx(hDC);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // Calculate center of the rendered image area
    int centerX = m_lastDestX + m_lastDestWidth / 2;
    int centerY = m_lastDestY + m_lastDestHeight / 2;

    // Draw center crosshair (green, full width/height) if crosshair enabled
    if (m_crosshairEnabled) {
        Gdiplus::Pen centerPen(Gdiplus::Color(255, 0, 255, 0), 1.0f);  // Green

        // Horizontal line across entire image area
        gfx.DrawLine(&centerPen,
                     m_lastDestX, centerY,
                     m_lastDestX + m_lastDestWidth, centerY);

        // Vertical line across entire image area
        gfx.DrawLine(&centerPen,
                     centerX, m_lastDestY,
                     centerX, m_lastDestY + m_lastDestHeight);
    }

    // Draw tracking crosshair (cyan) if click-to-move enabled and mouse over image
    bool mouseInImage = m_mouseOverImage &&
        m_mousePosition.x >= m_lastDestX &&
        m_mousePosition.x < m_lastDestX + m_lastDestWidth &&
        m_mousePosition.y >= m_lastDestY &&
        m_mousePosition.y < m_lastDestY + m_lastDestHeight;

    if (m_clickToMoveEnabled && mouseInImage) {
        Gdiplus::Pen trackPen(Gdiplus::Color(255, 0, 255, 255), 1.0f);  // Cyan

        int mouseX = static_cast<int>(m_mousePosition.x);
        int mouseY = static_cast<int>(m_mousePosition.y);

        // Full crosshair lines across entire image area
        gfx.DrawLine(&trackPen,
                     m_lastDestX, mouseY,
                     m_lastDestX + m_lastDestWidth, mouseY);
        gfx.DrawLine(&trackPen,
                     mouseX, m_lastDestY,
                     mouseX, m_lastDestY + m_lastDestHeight);

        // Small cross at mouse position
        int crossSize = 15;
        gfx.DrawLine(&trackPen,
                     mouseX - crossSize, mouseY,
                     mouseX + crossSize, mouseY);
        gfx.DrawLine(&trackPen,
                     mouseX, mouseY - crossSize,
                     mouseX, mouseY + crossSize);
    }

    // Draw real-world distance text near mouse cursor
    if (m_showDistanceEnabled && mouseInImage && m_frameWidth > 0 && m_frameHeight > 0) {
        int mouseX = static_cast<int>(m_mousePosition.x);
        int mouseY = static_cast<int>(m_mousePosition.y);

        // Calculate pixel offset from image center to mouse position
        int pixelOffsetX = mouseX - centerX;
        int pixelOffsetY = mouseY - centerY;

        // Scale to original image pixels
        double scaleFactorX = static_cast<double>(m_frameWidth) / m_lastDestWidth;
        double scaleFactorY = static_cast<double>(m_frameHeight) / m_lastDestHeight;
        double scaledOffsetX = pixelOffsetX * scaleFactorX;
        double scaledOffsetY = pixelOffsetY * scaleFactorY;

        // Apply effective pixel-to-mm factor (with calibration ref scaling)
        double effectivePixelToMmX = m_pixelToMmFactorX;
        double effectivePixelToMmY = m_pixelToMmFactorY;
        if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0) {
            effectivePixelToMmX = m_pixelToMmFactorX * static_cast<double>(m_calibrationRefWidth) / m_frameWidth;
            effectivePixelToMmY = m_pixelToMmFactorY * static_cast<double>(m_calibrationRefHeight) / m_frameHeight;
        }

        // Divide by magnification to get physical distance at the sample
        double distX_mm = (scaledOffsetX * effectivePixelToMmX) / m_magnification;
        double distY_mm = (scaledOffsetY * effectivePixelToMmY) / m_magnification;

        // Format text
        wchar_t dxBuf[64];
        wchar_t dyBuf[64];
        swprintf_s(dxBuf, L"dX: %+.3f mm", distX_mm);
        swprintf_s(dyBuf, L"dY: %+.3f mm", distY_mm);

        // Position text offset from mouse cursor (bottom-right, or adjust if near edge)
        int textOffsetX = 18;
        int textOffsetY = 18;
        float textX = static_cast<float>(mouseX + textOffsetX);
        float textY = static_cast<float>(mouseY + textOffsetY);

        // Flip to left side if too close to right edge
        if (mouseX + textOffsetX + 140 > m_lastDestX + m_lastDestWidth) {
            textX = static_cast<float>(mouseX - textOffsetX - 130);
        }
        // Flip above if too close to bottom
        if (mouseY + textOffsetY + 36 > m_lastDestY + m_lastDestHeight) {
            textY = static_cast<float>(mouseY - textOffsetY - 36);
        }

        Gdiplus::Font font(L"Consolas", 10.0f, Gdiplus::FontStyleBold);

        // Draw dark background rectangle for readability
        Gdiplus::RectF bgRect(textX - 3.0f, textY - 2.0f, 136.0f, 36.0f);
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(180, 0, 0, 0));
        gfx.FillRectangle(&bgBrush, bgRect);

        // Draw outline border
        Gdiplus::Pen borderPen(Gdiplus::Color(200, 255, 255, 255), 1.0f);
        gfx.DrawRectangle(&borderPen, bgRect);

        // Draw text in white
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::PointF dxPos(textX, textY);
        Gdiplus::PointF dyPos(textX, textY + 16.0f);
        gfx.DrawString(dxBuf, -1, &font, dxPos, &textBrush);
        gfx.DrawString(dyBuf, -1, &font, dyPos, &textBrush);
    }
}

void GdiCameraWindow::HandleClickToMove(int mouseX, int mouseY) {
    if (!m_motionManager) {
        std::cerr << "[GdiCameraWindow] Cannot move - no motion manager!" << std::endl;
        return;
    }

    if (m_lastDestWidth <= 0 || m_lastDestHeight <= 0) {
        std::cerr << "[GdiCameraWindow] Cannot move - no valid image displayed!" << std::endl;
        return;
    }

    // Check if click is within image area
    if (mouseX < m_lastDestX || mouseX >= m_lastDestX + m_lastDestWidth ||
        mouseY < m_lastDestY || mouseY >= m_lastDestY + m_lastDestHeight) {
        std::cout << "[GdiCameraWindow] Click outside image area" << std::endl;
        return;
    }

    // Calculate pixel offset from center of displayed image
    int centerX = m_lastDestX + m_lastDestWidth / 2;
    int centerY = m_lastDestY + m_lastDestHeight / 2;

    int pixelOffsetX = mouseX - centerX;
    int pixelOffsetY = mouseY - centerY;

    // Scale to original image size (account for scaling when displaying)
    double scaleFactorX = static_cast<double>(m_frameWidth) / m_lastDestWidth;
    double scaleFactorY = static_cast<double>(m_frameHeight) / m_lastDestHeight;

    double scaledOffsetX = pixelOffsetX * scaleFactorX;
    double scaledOffsetY = pixelOffsetY * scaleFactorY;

    // Convert to mm (auto-scale if actual resolution differs from calibration reference)
    double effectivePixelToMmX = m_pixelToMmFactorX;
    double effectivePixelToMmY = m_pixelToMmFactorY;
    if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0 &&
        m_frameWidth > 0 && m_frameHeight > 0) {
        effectivePixelToMmX = m_pixelToMmFactorX * static_cast<double>(m_calibrationRefWidth) / m_frameWidth;
        effectivePixelToMmY = m_pixelToMmFactorY * static_cast<double>(m_calibrationRefHeight) / m_frameHeight;
    }
    double offsetX_mm = (scaledOffsetX * effectivePixelToMmX) / m_magnification;
    double offsetY_mm = (scaledOffsetY * effectivePixelToMmY) / m_magnification;

    std::cout << "[GdiCameraWindow] Click detected: Pixel offset (" << pixelOffsetX << ", " << pixelOffsetY << ")"
              << " scaled (" << scaledOffsetX << ", " << scaledOffsetY << ")"
              << " = (" << offsetX_mm << " mm, " << offsetY_mm << " mm)" << std::endl;

    // Get current position
    PositionStruct currentPos = m_motionManager->GetCurrentPosition("gantry-main");

    // Calculate new absolute position
    double newX = currentPos.x + offsetX_mm;
    double newY = currentPos.y + offsetY_mm;
    double newZ = currentPos.z;  // Keep Z unchanged

    std::cout << "[GdiCameraWindow] Current position: X=" << currentPos.x
              << ", Y=" << currentPos.y << ", Z=" << currentPos.z << std::endl;
    std::cout << "[GdiCameraWindow] Target position: X=" << newX
              << ", Y=" << newY << ", Z=" << newZ << std::endl;

    // Move to absolute position (coordinated XY move, Z stays same, slow speed for precision)
    bool success = m_motionManager->MoveToAbs("gantry-main", newX, newY, newZ, 10.0, false);

    if (success) {
        std::cout << "[GdiCameraWindow] Gantry move command sent successfully" << std::endl;
    } else {
        std::cerr << "[GdiCameraWindow] Failed to send gantry move command!" << std::endl;
    }
}

// ============================================================================
// Vision Processing — public API forwarding to GdiVisionController.
// ============================================================================

#ifdef HAS_OPENCV

bool GdiCameraWindow::IsVisionProcessingActive() const {
    return m_visionController && m_visionController->IsActive();
}

VisionResult GdiCameraWindow::GetLatestVisionResult() const {
    return m_visionController ? m_visionController->GetLatestResult() : VisionResult();
}

uint64_t GdiCameraWindow::GetVisionResultSequence() const {
    return m_visionController ? m_visionController->GetResultSequence() : 0;
}

void GdiCameraWindow::SetVisionEnabled(bool enabled) {
    if (m_visionController) m_visionController->SetEnabled(enabled);
}

void GdiCameraWindow::SelectVisionProfile(const std::string& profilePath) {
    if (m_visionController) m_visionController->SelectProfile(profilePath);
}

#endif // HAS_OPENCV

// ============================================================================
// Beam Stream — public API forwarding to GdiBeamStreamController.
// ============================================================================

void GdiCameraWindow::SetBeamServerClient(BeamServerClient* client) {
    m_beamServerClient = client;
}

bool GdiCameraWindow::IsBeamStreamActive() const {
    return m_beamController && m_beamController->IsActive();
}

nlohmann::json GdiCameraWindow::GetLatestBeamStreamResult() const {
    return m_beamController ? m_beamController->GetLatestResult() : nlohmann::json();
}

uint64_t GdiCameraWindow::GetBeamStreamResultSequence() const {
    return m_beamController ? m_beamController->GetResultSequence() : 0;
}

double GdiCameraWindow::GetBeamStreamFps() const {
    return m_beamController ? m_beamController->GetFps() : 0.0;
}

// SwirBeamWindow.cpp
#include "SwirBeamWindow.h"
#include "BeamOptimizer/BeamResultBroker.h"
#include "SwirBeamOverlay.h"
#include "camera/CameraConfigManager.h"
#include "camera/CameraManager.h"
#include "motions/unified_motion_manager.h"
#include "PSU/PowerSupplyManager.h"
#include "pneumatics/PneumaticManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <QSettings>

// Static members
bool SwirBeamWindow::s_classRegistered = false;
const wchar_t* SwirBeamWindow::s_className = L"SwirBeamWindowClass";
ULONG_PTR SwirBeamWindow::s_gdiplusToken = 0;
bool SwirBeamWindow::s_gdiplusInitialized = false;

// Panel width ratio
static constexpr double PANEL_RATIO = 0.25;

// Color constants
static const Gdiplus::Color CLR_PANEL_BG(255, 30, 30, 30);
static const Gdiplus::Color CLR_SECTION_HEADER(255, 255, 102, 0);
static const Gdiplus::Color CLR_TEXT_DIM(255, 140, 140, 140);
static const Gdiplus::Color CLR_VALUE(255, 100, 255, 100);
static const Gdiplus::Color CLR_BTN_NORMAL(255, 60, 60, 60);
static const Gdiplus::Color CLR_BTN_HOVER(255, 80, 80, 80);
static const Gdiplus::Color CLR_BTN_PRESSED(255, 40, 40, 40);
static const Gdiplus::Color CLR_BTN_ON(255, 39, 174, 96);
static const Gdiplus::Color CLR_BTN_OFF(255, 180, 50, 50);
static const Gdiplus::Color CLR_BTN_ACTIVE(255, 52, 152, 219);
static const Gdiplus::Color CLR_DIVIDER(255, 80, 80, 80);
static const Gdiplus::Color CLR_PSU_SELECTED(255, 52, 152, 219);
static const Gdiplus::Color CLR_PSU_UNSELECTED(255, 50, 50, 50);

SwirBeamWindow::SwirBeamWindow()
{
    m_bitmapInfoBuffer.resize(sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD), 0);
    m_bitmapInfo = reinterpret_cast<BITMAPINFO*>(m_bitmapInfoBuffer.data());
    InitializeGdiPlus();
}

SwirBeamWindow::~SwirBeamWindow()
{
    Destroy();
}

bool SwirBeamWindow::InitializeGdiPlus()
{
    if (s_gdiplusInitialized) return true;
    Gdiplus::GdiplusStartupInput startupInput;
    if (Gdiplus::GdiplusStartup(&s_gdiplusToken, &startupInput, nullptr) == Gdiplus::Ok) {
        s_gdiplusInitialized = true;
        return true;
    }
    return false;
}

bool SwirBeamWindow::RegisterWindowClass()
{
    if (s_classRegistered) return true;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = s_className;
    if (RegisterClassExW(&wc)) {
        s_classRegistered = true;
        return true;
    }
    return false;
}

bool SwirBeamWindow::Create(const std::string& title, int x, int y, int width, int height)
{
    if (!RegisterWindowClass()) {
        std::cerr << "[SwirBeamWindow] Failed to register window class" << std::endl;
        return false;
    }
    std::wstring wideTitle(title.begin(), title.end());
    m_hwnd = CreateWindowExW(
        WS_EX_OVERLAPPEDWINDOW, s_className, wideTitle.c_str(),
        WS_OVERLAPPEDWINDOW, x, y, width, height,
        nullptr, nullptr, GetModuleHandle(nullptr), this);
    if (!m_hwnd) {
        std::cerr << "[SwirBeamWindow] Failed to create window" << std::endl;
        return false;
    }
    m_markerFilePath = "swir_markers.session.json";
    LoadMarkersFromFile();
    LoadMagnification();
    std::cout << "[SwirBeamWindow] Window created" << std::endl;
    return true;
}

void SwirBeamWindow::Destroy()
{
    StopDetectionThread();
    if (m_hwnd) {
        SaveMarkersToFile();
        if (m_memDC) {
            if (m_oldBitmap) SelectObject(m_memDC, m_oldBitmap);
            if (m_memBitmap) DeleteObject(m_memBitmap);
            DeleteDC(m_memDC);
            m_memDC = nullptr;
            m_memBitmap = nullptr;
            m_oldBitmap = nullptr;
        }
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

void SwirBeamWindow::Show()
{
    if (m_hwnd) { ShowWindow(m_hwnd, SW_SHOW); UpdateWindow(m_hwnd); }
}

void SwirBeamWindow::Hide()
{
    if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
}

bool SwirBeamWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void SwirBeamWindow::SetTitle(const std::string& title)
{
    if (m_hwnd) {
        std::wstring wt(title.begin(), title.end());
        SetWindowTextW(m_hwnd, wt.c_str());
    }
}

void SwirBeamWindow::SetCameraManager(CameraManager* cameraManager) { m_cameraManager = cameraManager; }
void SwirBeamWindow::SetPowerSupplyManager(PowerSupplyManager* psuManager) { m_psuManager = psuManager; }

void SwirBeamWindow::SetPneumaticManager(PneumaticManager* pneumaticManager) {
    m_pneumaticManager = pneumaticManager;
    std::cout << "[SwirBeamWindow] Pneumatic manager set" << std::endl;
}

void SwirBeamWindow::SetMotionManager(UnifiedMotionManager* motionManager) {
    m_motionManager = motionManager;
    std::cout << "[SwirBeamWindow] Motion manager set" << std::endl;
}

void SwirBeamWindow::SetCameraConfigManager(CameraConfigManager* configMgr) {
    m_cameraConfigManager = configMgr;
    LoadCalibrationFromConfig();
}

void SwirBeamWindow::LoadCalibrationFromConfig() {
    // Try per-camera calibration from CameraConfigManager
    if (m_cameraConfigManager && !m_targetCameraId.empty()) {
        CameraConfigData::PixelCalibration cal;
        if (m_cameraConfigManager->GetCalibration(m_targetCameraId, cal)) {
            m_pixelToMmFactorX = cal.pixelToMmX;
            m_pixelToMmFactorY = cal.pixelToMmY;
            m_calibrationRefWidth = cal.referenceWidth;
            m_calibrationRefHeight = cal.referenceHeight;
            std::cout << "[SwirBeamWindow] Per-camera calibration for '" << m_targetCameraId
                      << "': " << m_pixelToMmFactorX << " mm/px (X), "
                      << m_pixelToMmFactorY << " mm/px (Y)" << std::endl;
            return;
        }
    }
    std::cout << "[SwirBeamWindow] Using default SWIR calibration: "
              << m_pixelToMmFactorX << " mm/px" << std::endl;
}

void SwirBeamWindow::HandleClickToMove(int mouseX, int mouseY) {
    if (!m_motionManager || m_lastRenderW <= 0 || m_lastRenderH <= 0) return;

    // Calculate pixel offset from center of rendered image
    int renderCenterX = m_lastRenderX + m_lastRenderW / 2;
    int renderCenterY = m_lastRenderY + m_lastRenderH / 2;

    double pixelOffsetX = static_cast<double>(mouseX - renderCenterX);
    double pixelOffsetY = static_cast<double>(mouseY - renderCenterY);

    // Scale from display coordinates to image coordinates
    double scaleX = static_cast<double>(m_frameWidth) / m_lastRenderW;
    double scaleY = static_cast<double>(m_frameHeight) / m_lastRenderH;
    pixelOffsetX *= scaleX;
    pixelOffsetY *= scaleY;

    // Apply calibration (auto-scale if resolution differs from reference)
    double effectivePixelToMmX = m_pixelToMmFactorX;
    double effectivePixelToMmY = m_pixelToMmFactorY;
    if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0 &&
        m_frameWidth > 0 && m_frameHeight > 0) {
        effectivePixelToMmX = m_pixelToMmFactorX * static_cast<double>(m_calibrationRefWidth) / m_frameWidth;
        effectivePixelToMmY = m_pixelToMmFactorY * static_cast<double>(m_calibrationRefHeight) / m_frameHeight;
    }

    // Image-space mm (what the sensor sees), then divide by magnification
    // to get real-world gantry distance
    double offsetX_mm = (pixelOffsetX * effectivePixelToMmX) / m_magnification;
    double offsetY_mm = (pixelOffsetY * effectivePixelToMmY) / m_magnification;

    std::cout << "[SwirBeamWindow] Click-to-move: pixel offset (" << pixelOffsetX << ", " << pixelOffsetY
              << ") image-mm=(" << pixelOffsetX * effectivePixelToMmX << ", " << pixelOffsetY * effectivePixelToMmY
              << ") / " << m_magnification << "x = real (" << offsetX_mm << " mm, " << offsetY_mm << " mm)" << std::endl;

    // Get current position and move
    PositionStruct currentPos = m_motionManager->GetCurrentPosition("gantry-main");
    double newX = currentPos.x + offsetX_mm;
    double newY = currentPos.y + offsetY_mm;

    std::cout << "[SwirBeamWindow] Moving from (" << currentPos.x << ", " << currentPos.y
              << ") to (" << newX << ", " << newY << ")" << std::endl;

    bool success = m_motionManager->MoveToAbs("gantry-main", newX, newY, currentPos.z, 10.0, false);
    if (!success) {
        std::cerr << "[SwirBeamWindow] Click-to-move failed!" << std::endl;
    }
}

void SwirBeamWindow::SetTargetCamera(const std::string& cameraId)
{
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        m_targetCameraId = cameraId;
    }
    LoadCalibrationFromConfig();
}

std::string SwirBeamWindow::GetTargetCamera() const
{
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    return m_targetCameraId;
}

// ============================================================================
// PSU Device Discovery
// ============================================================================

void SwirBeamWindow::DiscoverPSUDevices()
{
    if (!m_psuManager || m_psuDiscovered) return;

    auto deviceNames = m_psuManager->getDeviceNames();
    if (deviceNames.empty()) return;  // Retry next poll

    std::lock_guard<std::mutex> lock(m_psuMutex);
    m_psuDevices.clear();
    for (const auto& name : deviceNames) {
        PSUDeviceState state;
        state.name = name;
        state.channelCount = m_psuManager->getDeviceChannelCount(name);
        m_psuDevices.push_back(state);
    }
    m_selectedPSU = 0;
    m_psuDiscovered = true;
    m_panelWidth = 0;  // Force button rebuild to show PSU selector
    std::cout << "[SwirBeamWindow] Discovered " << m_psuDevices.size() << " PSU device(s)" << std::endl;

    // Auto-connect any disconnected PSUs
    for (const auto& dev : m_psuDevices) {
        auto psu = m_psuManager->getPowerSupply(dev.name);
        if (!psu || psu->isConnected()) continue;
        std::cout << "[SwirBeamWindow] Auto-connecting " << dev.name << "..." << std::endl;
        if (psu->connect()) {
            std::cout << "[SwirBeamWindow] " << dev.name << " connected" << std::endl;
        } else {
            std::cerr << "[SwirBeamWindow] " << dev.name << " auto-connect failed" << std::endl;
        }
    }
}

// ============================================================================
// CameraFrameSubscriber Interface
// ============================================================================

bool SwirBeamWindow::WantsFramesFromCamera(const std::string& cameraId) const
{
    std::lock_guard<std::mutex> lock(m_cameraMutex);
    if (m_targetCameraId.empty()) return true;
    return cameraId == m_targetCameraId;
}

void SwirBeamWindow::OnNewFrame(const CameraFrameData& frameData)
{
    if (!m_hwnd || !IsVisible()) return;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        if (!m_targetCameraId.empty() && frameData.cameraId != m_targetCameraId) return;
    }
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        const uint32_t w = frameData.width;
        const uint32_t h = frameData.height;
        const size_t bgrSize = static_cast<size_t>(w) * h * 3;
        if (m_frameBuffer.size() != bgrSize) m_frameBuffer.resize(bgrSize);
        m_frameWidth = w;
        m_frameHeight = h;
        const uint8_t* src = frameData.imageData.data();
        const int channels = frameData.channels;
        for (uint32_t row = 0; row < h; ++row) {
            const uint32_t dstRow = h - 1 - row;
            for (uint32_t col = 0; col < w; ++col) {
                const size_t srcIdx = (static_cast<size_t>(row) * w + col) * channels;
                const size_t dstIdx = (static_cast<size_t>(dstRow) * w + col) * 3;
                if (channels >= 3) {
                    m_frameBuffer[dstIdx + 0] = src[srcIdx + 2];
                    m_frameBuffer[dstIdx + 1] = src[srcIdx + 1];
                    m_frameBuffer[dstIdx + 2] = src[srcIdx + 0];
                } else {
                    m_frameBuffer[dstIdx + 0] = src[srcIdx];
                    m_frameBuffer[dstIdx + 1] = src[srcIdx];
                    m_frameBuffer[dstIdx + 2] = src[srcIdx];
                }
            }
        }
        m_hasNewFrame.store(true);

        // FPS tracking
        ++m_frameCount;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - m_fpsTimestamp).count();
        if (elapsed >= 1.0) {
            m_displayFps = m_frameCount / elapsed;
            m_frameCount = 0;
            m_fpsTimestamp = now;
        }

        // Feed grayscale to beam detection thread
        if (m_beamDetectionEnabled) {
            std::lock_guard<std::mutex> detLock(m_detectionMutex);
            const size_t graySize = static_cast<size_t>(w) * h;
            if (m_detectionBuffer.size() != graySize) m_detectionBuffer.resize(graySize);
            m_detectionWidth = w;
            m_detectionHeight = h;
            if (channels == 1) {
                std::memcpy(m_detectionBuffer.data(), src, graySize);
            } else {
                // Luminance from RGB: use green channel as fast approximation
                for (uint32_t i = 0; i < w * h; ++i) {
                    m_detectionBuffer[i] = src[i * channels + 1];
                }
            }
            m_detectionReady.store(true);
        }
    }
    if (m_beamDetectionEnabled) {
        m_detectionCV.notify_one();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SwirBeamWindow::OnCameraStatusChanged(const std::string& /*cameraId*/, bool /*connected*/, bool /*grabbing*/)
{
}

// ============================================================================
// PSU Polling
// ============================================================================

void SwirBeamWindow::PollPSUReadings()
{
    if (!m_psuManager) return;

    // Discover devices on first poll
    DiscoverPSUDevices();

    std::lock_guard<std::mutex> lock(m_psuMutex);
    for (auto& dev : m_psuDevices) {
        auto psu = m_psuManager->getPowerSupply(dev.name);
        if (!psu || !psu->isConnected()) continue;
        dev.voltage = psu->readVoltage(1);
        dev.current = psu->readCurrent(1);
        dev.outputOn = psu->readOutputState(1);
    }

    if (m_hwnd && IsVisible()) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// ============================================================================
// PSU Actions
// ============================================================================

void SwirBeamWindow::OnSelectPSU(int index)
{
    std::lock_guard<std::mutex> lock(m_psuMutex);
    if (index >= 0 && index < static_cast<int>(m_psuDevices.size())) {
        m_selectedPSU = index;
        m_panelWidth = 0;  // Force button rebuild
        std::cout << "[SwirBeamWindow] Selected PSU: " << m_psuDevices[index].name << std::endl;
    }
}

void SwirBeamWindow::OnPSUToggle()
{
    if (!m_psuManager) return;
    std::string devName;
    bool currentState;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        if (m_selectedPSU >= static_cast<int>(m_psuDevices.size())) return;
        devName = m_psuDevices[m_selectedPSU].name;
        currentState = m_psuDevices[m_selectedPSU].outputOn;
    }
    auto psu = m_psuManager->getPowerSupply(devName);
    if (!psu) return;

    // If not connected, connect and configure defaults before turning on
    if (!psu->isConnected()) {
        if (!currentState) {
            std::cout << "[SwirBeamWindow] " << devName << " not connected, connecting..." << std::endl;
            if (!psu->connect()) {
                std::cerr << "[SwirBeamWindow] " << devName << " connection failed" << std::endl;
                return;
            }
            std::cout << "[SwirBeamWindow] " << devName << " connected, setting 2.0V / 300mA" << std::endl;
            psu->setVoltage(2.0, 1);
            psu->setCurrent(0.3, 1);
            {
                std::lock_guard<std::mutex> lock(m_psuMutex);
                if (m_selectedPSU < static_cast<int>(m_psuDevices.size())) {
                    m_psuDevices[m_selectedPSU].setCurrent = 0.3;
                }
            }
            psu->setOutputOn(1);
            std::cout << "[SwirBeamWindow] " << devName << " output ON" << std::endl;
        }
        return;
    }

    if (currentState) {
        psu->setOutputOff(1);
        std::cout << "[SwirBeamWindow] " << devName << " output OFF" << std::endl;
    } else {
        psu->setOutputOn(1);
        std::cout << "[SwirBeamWindow] " << devName << " output ON" << std::endl;
    }
}

void SwirBeamWindow::OnCurrentIncrease()
{
    if (!m_psuManager) return;
    std::string devName;
    double newCurrent;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        if (m_selectedPSU >= static_cast<int>(m_psuDevices.size())) return;
        auto& dev = m_psuDevices[m_selectedPSU];
        dev.setCurrent += CURRENT_STEP_MA / 1000.0;
        devName = dev.name;
        newCurrent = dev.setCurrent;
    }
    auto psu = m_psuManager->getPowerSupply(devName);
    if (!psu || !psu->isConnected()) return;
    psu->setCurrent(newCurrent, 1);
    std::cout << "[SwirBeamWindow] " << devName << " current set to " << (newCurrent * 1000.0) << " mA" << std::endl;
}

void SwirBeamWindow::OnCurrentDecrease()
{
    if (!m_psuManager) return;
    std::string devName;
    double newCurrent;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        if (m_selectedPSU >= static_cast<int>(m_psuDevices.size())) return;
        auto& dev = m_psuDevices[m_selectedPSU];
        dev.setCurrent -= CURRENT_STEP_MA / 1000.0;
        if (dev.setCurrent < 0.0) dev.setCurrent = 0.0;
        devName = dev.name;
        newCurrent = dev.setCurrent;
    }
    auto psu = m_psuManager->getPowerSupply(devName);
    if (!psu || !psu->isConnected()) return;
    psu->setCurrent(newCurrent, 1);
    std::cout << "[SwirBeamWindow] " << devName << " current set to " << (newCurrent * 1000.0) << " mA" << std::endl;
}

// ============================================================================
// Exposure Actions
// ============================================================================

void SwirBeamWindow::OnAutoExposureToggle()
{
    if (!m_cameraManager) return;
    std::string camId = GetTargetCamera();
    auto settings = m_cameraManager->GetExposureSettings(camId);
    m_autoExposure = !m_autoExposure;
    settings.auto_exposure = m_autoExposure;
    m_cameraManager->ApplyExposureSettings(camId, settings);
    std::cout << "[SwirBeamWindow] Auto exposure " << (m_autoExposure ? "ON" : "OFF") << std::endl;
}

void SwirBeamWindow::OnSetExposure(double microseconds)
{
    if (!m_cameraManager) return;
    std::string camId = GetTargetCamera();
    auto settings = m_cameraManager->GetExposureSettings(camId);
    settings.exposure_time = microseconds;
    settings.auto_exposure = false;
    settings.auto_gain = false;
    m_autoExposure = false;
    m_currentExposure = microseconds;
    m_cameraManager->ApplyExposureSettings(camId, settings);

    std::cout << "[SwirBeamWindow] Exposure set to " << microseconds << " us (auto gain OFF)" << std::endl;
}

// Dialog proc for exposure input
static INT_PTR CALLBACK ExpDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        auto* valPtr = reinterpret_cast<double*>(lParam);
        SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(valPtr));
        wchar_t buf[32];
        swprintf_s(buf, L"%.0f", *valPtr);
        SetDlgItemTextW(hDlg, 101, buf);
        SendDlgItemMessageW(hDlg, 101, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(hDlg, 101));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[32] = {};
            GetDlgItemTextW(hDlg, 101, buf, 32);
            double val = _wtof(buf);
            if (val >= 1.0 && val <= 1000000.0) {
                auto* valPtr = reinterpret_cast<double*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
                *valPtr = val;
                EndDialog(hDlg, IDOK);
            } else {
                MessageBoxW(hDlg, L"Enter a value between 1 and 1000000.", L"Invalid", MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL); return TRUE;
    }
    return FALSE;
}

void SwirBeamWindow::OnApplyManualExposure()
{
    // Build dialog template in memory
    alignas(4) BYTE dlgBuf[1024] = {};
    DLGTEMPLATE* dlg = reinterpret_cast<DLGTEMPLATE*>(dlgBuf);
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
    dlg->cdit = 4;
    dlg->cx = 170;
    dlg->cy = 70;

    WORD* ptr = reinterpret_cast<WORD*>(dlg + 1);
    *ptr++ = 0;  // no menu
    *ptr++ = 0;  // default class
    const wchar_t* title = L"Set Exposure (us)";
    for (const wchar_t* c = title; *c; ++c) *ptr++ = *c;
    *ptr++ = 0;
    *ptr++ = 9;  // font size
    const wchar_t* fontName = L"Segoe UI";
    for (const wchar_t* c = fontName; *c; ++c) *ptr++ = *c;
    *ptr++ = 0;

    auto addCtrl = [&](DWORD style, short x, short y, short cx, short cy, WORD id, const wchar_t* cls, const wchar_t* text) {
        ptr = reinterpret_cast<WORD*>((reinterpret_cast<uintptr_t>(ptr) + 3) & ~3);
        DLGITEMTEMPLATE* item = reinterpret_cast<DLGITEMTEMPLATE*>(ptr);
        item->style = WS_CHILD | WS_VISIBLE | style;
        item->x = x; item->y = y; item->cx = cx; item->cy = cy;
        item->id = id;
        ptr = reinterpret_cast<WORD*>(item + 1);
        for (const wchar_t* c = cls; *c; ++c) *ptr++ = *c;
        *ptr++ = 0;
        for (const wchar_t* c = text; *c; ++c) *ptr++ = *c;
        *ptr++ = 0;
        *ptr++ = 0;
    };

    addCtrl(SS_LEFT, 8, 8, 154, 10, 0xFFFF, L"Static", L"Exposure time in microseconds:");
    addCtrl(ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 8, 22, 154, 14, 101, L"Edit", L"");
    addCtrl(BS_DEFPUSHBUTTON | WS_TABSTOP, 56, 46, 50, 16, IDOK, L"Button", L"OK");
    addCtrl(WS_TABSTOP, 110, 46, 50, 16, IDCANCEL, L"Button", L"Cancel");

    double newVal = m_currentExposure;
    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandle(nullptr), dlg, m_hwnd, ExpDlgProc, reinterpret_cast<LPARAM>(&newVal));

    if (result == IDOK) {
        OnSetExposure(newVal);
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

// ============================================================================
// Button Management
// ============================================================================

void SwirBeamWindow::RebuildButtons(const RECT& panelRect)
{
    const int pw = panelRect.right - panelRect.left;
    if (pw == m_panelWidth && !m_buttons.empty()) return;
    m_buttons.clear();
    m_panelWidth = pw;

    const int margin = 10;
    const int groupPad = 6;       // padding inside group box before/after content
    const int groupInner = 8;     // inner horizontal padding from group edge to button
    const int titleH = 22;        // group box title height
    const int btnHeight = 28;
    const int btnSpacing = 4;
    const int sectionGap = 10;    // gap between group boxes
    const int labelHeight = 17;   // text label row height

    const int btnLeft = panelRect.left + margin + groupInner;
    const int btnWidth = pw - 2 * (margin + groupInner);
    const int halfBtnWidth = (btnWidth - btnSpacing) / 2;

    int y = panelRect.top + margin;

    // === POWER SUPPLY Group ===
    // Group box starts here, title takes titleH, then groupPad before first button
    y += titleH + groupPad;

    // PSU device selector buttons
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        int numPSU = static_cast<int>(m_psuDevices.size());
        if (numPSU > 0) {
            int psuBtnWidth = (btnWidth - (numPSU - 1) * btnSpacing) / numPSU;
            for (int i = 0; i < numPSU; ++i) {
                SwirButton psuBtn;
                int bx = btnLeft + i * (psuBtnWidth + btnSpacing);
                psuBtn.rect = {bx, y, bx + psuBtnWidth, y + btnHeight};
                std::wstring name(m_psuDevices[i].name.begin(), m_psuDevices[i].name.end());
                psuBtn.label = name;
                psuBtn.tag = TAG_PSU_SELECT_BASE + i;
                psuBtn.isToggle = true;
                psuBtn.toggled = (i == m_selectedPSU);
                int idx = i;
                psuBtn.onClick = [this, idx]() { OnSelectPSU(idx); };
                m_buttons.push_back(psuBtn);
            }
            y += btnHeight + btnSpacing;
        }
    }

    // OUTPUT ON/OFF
    SwirButton psuToggle;
    psuToggle.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    psuToggle.label = L"OUTPUT ON/OFF";
    psuToggle.isToggle = true;
    psuToggle.tag = TAG_PSU_TOGGLE;
    psuToggle.onClick = [this]() { OnPSUToggle(); };
    m_buttons.push_back(psuToggle);
    y += btnHeight + btnSpacing;

    // +50mA / -50mA
    SwirButton btnInc;
    btnInc.rect = {btnLeft, y, btnLeft + halfBtnWidth, y + btnHeight};
    btnInc.label = L"+50mA";
    btnInc.onClick = [this]() { OnCurrentIncrease(); };
    m_buttons.push_back(btnInc);
    SwirButton btnDec;
    btnDec.rect = {btnLeft + halfBtnWidth + btnSpacing, y, btnLeft + btnWidth, y + btnHeight};
    btnDec.label = L"-50mA";
    btnDec.onClick = [this]() { OnCurrentDecrease(); };
    m_buttons.push_back(btnDec);
    y += btnHeight + btnSpacing;

    // 3 text label rows (Set, Voltage, Current) + bottom padding
    y += labelHeight * 3 + groupPad;

    // Remember PSU group bottom for RenderControlPanel
    // (stored as m_psuGroupBottom via simple calculation)

    y += sectionGap;

    // === SWIR EXPOSURE Group ===
    y += titleH + groupPad;

    // AUTO EXP
    SwirButton autoExpBtn;
    autoExpBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    autoExpBtn.label = L"AUTO EXP";
    autoExpBtn.isToggle = true;
    autoExpBtn.tag = TAG_AUTO_EXP;
    autoExpBtn.onClick = [this]() { OnAutoExposureToggle(); };
    m_buttons.push_back(autoExpBtn);
    y += btnHeight + btnSpacing;

    // Exposure presets (2x2)
    struct ExpPreset { const wchar_t* label; double value; };
    ExpPreset presets[] = {
        {L"50 us", 50.0}, {L"300 us", 300.0},
        {L"5000 us", 5000.0}, {L"20000 us", 20000.0}
    };
    for (int i = 0; i < 4; i += 2) {
        SwirButton b1;
        b1.rect = {btnLeft, y, btnLeft + halfBtnWidth, y + btnHeight};
        b1.label = presets[i].label;
        double v1 = presets[i].value;
        b1.onClick = [this, v1]() { OnSetExposure(v1); };
        m_buttons.push_back(b1);
        SwirButton b2;
        b2.rect = {btnLeft + halfBtnWidth + btnSpacing, y, btnLeft + btnWidth, y + btnHeight};
        b2.label = presets[i + 1].label;
        double v2 = presets[i + 1].value;
        b2.onClick = [this, v2]() { OnSetExposure(v2); };
        m_buttons.push_back(b2);
        y += btnHeight + btnSpacing;
    }

    // SET EXPOSURE button (opens dialog)
    SwirButton setExpBtn;
    setExpBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    setExpBtn.label = L"SET EXPOSURE";
    setExpBtn.tag = TAG_APPLY_EXP;
    setExpBtn.onClick = [this]() { OnApplyManualExposure(); };
    m_buttons.push_back(setExpBtn);
    y += btnHeight + btnSpacing;

    // "Current: xxxx us" label + bottom padding
    y += labelHeight + groupPad;

    y += sectionGap;

    // === TOOLS Group ===
    y += titleH + groupPad;

    // SWIR Head Up/Down toggle
    SwirButton headBtn;
    headBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    headBtn.label = L"SWIR HEAD DOWN";
    headBtn.isToggle = true;
    headBtn.tag = TAG_SWIR_HEAD;
    headBtn.onClick = [this]() { OnSwirHeadToggle(); };
    m_buttons.push_back(headBtn);
    y += btnHeight + btnSpacing;

    // Click-to-Move toggle
    SwirButton ctmBtn;
    ctmBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    ctmBtn.label = L"CLICK TO MOVE";
    ctmBtn.isToggle = true;
    ctmBtn.tag = TAG_CLICK_TO_MOVE;
    ctmBtn.toggled = m_clickToMoveEnabled;
    ctmBtn.onClick = [this]() {
        m_clickToMoveEnabled = !m_clickToMoveEnabled;
        std::cout << "[SwirBeamWindow] Click-to-move " << (m_clickToMoveEnabled ? "enabled" : "disabled") << std::endl;
    };
    m_buttons.push_back(ctmBtn);
    y += btnHeight + btnSpacing;

    // Place Marker
    SwirButton placeBtn;
    placeBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    placeBtn.label = L"PLACE MARKER";
    placeBtn.tag = TAG_PLACE_MARKER;
    placeBtn.onClick = [this]() { OnPlaceMarker(); };
    m_buttons.push_back(placeBtn);
    y += btnHeight + btnSpacing;

    // Clear All Markers
    SwirButton clearBtn;
    clearBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    clearBtn.label = L"CLEAR ALL";
    clearBtn.tag = TAG_CLEAR_MARKERS;
    clearBtn.onClick = [this]() { OnClearAllMarkers(); };
    m_buttons.push_back(clearBtn);
    y += btnHeight + btnSpacing;

    // SET MAGNIFICATION button
    SwirButton magBtn;
    magBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    magBtn.label = L"SET MAGNIFICATION";
    magBtn.tag = TAG_SET_MAG;
    magBtn.onClick = [this]() { OnSetMagnification(); };
    m_buttons.push_back(magBtn);
    y += btnHeight + btnSpacing;

    // BEAM DETECT toggle
    SwirButton beamBtn;
    beamBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    beamBtn.label = L"BEAM DETECT";
    beamBtn.isToggle = true;
    beamBtn.toggled = m_beamDetectionEnabled;
    beamBtn.tag = TAG_BEAM_DETECT;
    beamBtn.onClick = [this]() { OnBeamDetectToggle(); };
    m_buttons.push_back(beamBtn);
    y += btnHeight + btnSpacing;

    // Beam Optimize (placeholder — logs hardware status)
    SwirButton optimizeBtn;
    optimizeBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
    optimizeBtn.label = L"BEAM OPTIMIZE";
    optimizeBtn.isToggle = false;
    optimizeBtn.tag = TAG_BEAM_OPTIMIZE;
    optimizeBtn.onClick = [this]() { OnBeamOptimize(); };
    m_buttons.push_back(optimizeBtn);
    y += btnHeight + btnSpacing;

    y += groupPad;
}

void SwirBeamWindow::DrawButton(Gdiplus::Graphics& gfx, const SwirButton& btn)
{
    Gdiplus::Color bgColor;

    // PSU selector buttons
    if (btn.tag >= TAG_PSU_SELECT_BASE && btn.tag < TAG_PSU_SELECT_BASE + 50) {
        if (btn.toggled) {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? Gdiplus::Color(255, 70, 170, 235) : CLR_PSU_SELECTED);
        } else {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? CLR_BTN_HOVER : CLR_PSU_UNSELECTED);
        }
    } else if (btn.tag == TAG_PSU_TOGGLE) {
        if (btn.toggled) {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? Gdiplus::Color(255, 35, 195, 110) : CLR_BTN_ON);
        } else {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? Gdiplus::Color(255, 200, 70, 70) : CLR_BTN_OFF);
        }
    } else if (btn.tag == TAG_AUTO_EXP || btn.tag == TAG_CLICK_TO_MOVE) {
        if (btn.toggled) {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? Gdiplus::Color(255, 35, 195, 110) : CLR_BTN_ON);
        } else {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? Gdiplus::Color(255, 200, 70, 70) : CLR_BTN_OFF);
        }
    } else {
        // Exposure preset buttons - highlight active one
        bool isActive = false;
        if (!m_autoExposure) {
            if (btn.label == L"50 us" && std::abs(m_currentExposure - 50.0) < 0.1) isActive = true;
            if (btn.label == L"300 us" && std::abs(m_currentExposure - 300.0) < 0.1) isActive = true;
            if (btn.label == L"5000 us" && std::abs(m_currentExposure - 5000.0) < 0.1) isActive = true;
            if (btn.label == L"20000 us" && std::abs(m_currentExposure - 20000.0) < 0.1) isActive = true;
        }
        if (isActive) {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? Gdiplus::Color(255, 70, 170, 235) : CLR_BTN_ACTIVE);
        } else {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? CLR_BTN_HOVER : CLR_BTN_NORMAL);
        }
    }

    float bx = static_cast<float>(btn.rect.left);
    float by = static_cast<float>(btn.rect.top);
    float bw = static_cast<float>(btn.rect.right - btn.rect.left);
    float bh = static_cast<float>(btn.rect.bottom - btn.rect.top);

    // Rounded rectangle
    Gdiplus::SolidBrush brush(bgColor);
    Gdiplus::GraphicsPath path;
    float r = 4.0f;
    path.AddArc(bx, by, r * 2, r * 2, 180, 90);
    path.AddArc(bx + bw - r * 2, by, r * 2, r * 2, 270, 90);
    path.AddArc(bx + bw - r * 2, by + bh - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(bx, by + bh - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    gfx.FillPath(&brush, &path);

    Gdiplus::Pen pen(Gdiplus::Color(255, 90, 90, 90), 1.0f);
    gfx.DrawPath(&pen, &path);

    // Label text
    Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::Font font(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF layoutRect(bx, by, bw, bh);
    gfx.DrawString(btn.label.c_str(), -1, &font, layoutRect, &format, &textBrush);
}

SwirButton* SwirBeamWindow::HitTestButton(int x, int y)
{
    POINT pt = {x, y};
    for (auto& btn : m_buttons) {
        if (PtInRect(&btn.rect, pt)) return &btn;
    }
    return nullptr;
}

// ============================================================================
// Rendering
// ============================================================================

void SwirBeamWindow::PrepareBackBuffer()
{
    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);
    int w = clientRect.right - clientRect.left;
    int h = clientRect.bottom - clientRect.top;
    if (w <= 0 || h <= 0) return;
    if (w != m_bufferWidth || h != m_bufferHeight) {
        HDC hdc = GetDC(m_hwnd);
        if (m_memDC) {
            SelectObject(m_memDC, m_oldBitmap);
            DeleteObject(m_memBitmap);
            DeleteDC(m_memDC);
        }
        m_memDC = CreateCompatibleDC(hdc);
        m_memBitmap = CreateCompatibleBitmap(hdc, w, h);
        m_oldBitmap = static_cast<HBITMAP>(SelectObject(m_memDC, m_memBitmap));
        m_bufferWidth = w;
        m_bufferHeight = h;
        ReleaseDC(m_hwnd, hdc);
    }
}

void SwirBeamWindow::RenderFrame(HDC hdc, const RECT& imageRect)
{
    std::lock_guard<std::mutex> lock(m_frameMutex);
    int destX = imageRect.left;
    int destY = imageRect.top;
    int destW = imageRect.right - imageRect.left;
    int destH = imageRect.bottom - imageRect.top;

    if (m_frameWidth == 0 || m_frameHeight == 0 || m_frameBuffer.empty()) {
        Gdiplus::Graphics gfx(hdc);
        gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        Gdiplus::SolidBrush textBrush(CLR_TEXT_DIM);
        Gdiplus::Font font(L"Segoe UI", 10.0f);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF layoutRect(static_cast<float>(destX), static_cast<float>(destY),
                                   static_cast<float>(destW), static_cast<float>(destH));
        gfx.DrawString(L"Waiting for camera...", -1, &font, layoutRect, &format, &textBrush);
        return;
    }

    BITMAPINFOHEADER& bih = m_bitmapInfo->bmiHeader;
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = static_cast<LONG>(m_frameWidth);
    bih.biHeight = static_cast<LONG>(m_frameHeight);
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;

    double srcAspect = static_cast<double>(m_frameWidth) / m_frameHeight;
    double dstAspect = static_cast<double>(destW) / destH;
    int renderX = destX, renderY = destY, renderW = destW, renderH = destH;
    if (srcAspect > dstAspect) {
        renderW = destW;
        renderH = static_cast<int>(destW / srcAspect);
        renderY = destY + (destH - renderH) / 2;
    } else {
        renderH = destH;
        renderW = static_cast<int>(destH * srcAspect);
        renderX = destX + (destW - renderW) / 2;
    }

    // Save render destination for click-to-move calculations
    m_lastRenderX = renderX;
    m_lastRenderY = renderY;
    m_lastRenderW = renderW;
    m_lastRenderH = renderH;

    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchDIBits(hdc, renderX, renderY, renderW, renderH,
                  0, 0, static_cast<int>(m_frameWidth), static_cast<int>(m_frameHeight),
                  m_frameBuffer.data(), m_bitmapInfo, DIB_RGB_COLORS, SRCCOPY);
}

void SwirBeamWindow::DrawGroupBox(Gdiplus::Graphics& gfx, const RECT& rect, const wchar_t* title)
{
    float bx = static_cast<float>(rect.left);
    float by = static_cast<float>(rect.top);
    float bw = static_cast<float>(rect.right - rect.left);
    float bh = static_cast<float>(rect.bottom - rect.top);

    // Dark filled background with rounded corners
    Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, 38, 38, 38));
    Gdiplus::GraphicsPath path;
    float r = 6.0f;
    path.AddArc(bx, by, r * 2, r * 2, 180, 90);
    path.AddArc(bx + bw - r * 2, by, r * 2, r * 2, 270, 90);
    path.AddArc(bx + bw - r * 2, by + bh - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(bx, by + bh - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();
    gfx.FillPath(&fillBrush, &path);

    // Border
    Gdiplus::Pen borderPen(Gdiplus::Color(255, 65, 65, 65), 1.0f);
    gfx.DrawPath(&borderPen, &path);

    // Title label (top-left, inside box)
    Gdiplus::Font titleFont(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush titleBrush(CLR_SECTION_HEADER);
    Gdiplus::StringFormat leftFormat;
    leftFormat.SetAlignment(Gdiplus::StringAlignmentNear);
    Gdiplus::RectF titleRect(bx + 8.0f, by + 4.0f, bw - 16.0f, 16.0f);
    gfx.DrawString(title, -1, &titleFont, titleRect, &leftFormat, &titleBrush);
}

void SwirBeamWindow::RenderControlPanel(HDC hdc, const RECT& panelRect)
{
    Gdiplus::Graphics gfx(hdc);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);

    const int pw = panelRect.right - panelRect.left;
    const int margin = 10;
    const int groupPad = 6;
    const int groupInner = 8;
    const int titleH = 22;
    const int btnHeight = 28;
    const int btnSpacing = 4;
    const int sectionGap = 10;
    const int labelHeight = 17;

    // Panel background
    Gdiplus::SolidBrush bgBrush(CLR_PANEL_BG);
    gfx.FillRectangle(&bgBrush, panelRect.left, panelRect.top,
                       pw, panelRect.bottom - panelRect.top);

    // Divider line on right edge
    Gdiplus::Pen dividerPen(CLR_DIVIDER, 2.0f);
    gfx.DrawLine(&dividerPen,
                  static_cast<float>(panelRect.right - 1), static_cast<float>(panelRect.top),
                  static_cast<float>(panelRect.right - 1), static_cast<float>(panelRect.bottom));

    // Fonts
    Gdiplus::Font labelFont(L"Segoe UI", 8.0f, Gdiplus::FontStyleRegular);
    Gdiplus::Font valueFont(L"Consolas", 9.0f, Gdiplus::FontStyleBold);
    Gdiplus::Font smallFont(L"Segoe UI", 7.5f);
    Gdiplus::SolidBrush labelBrush(CLR_TEXT_DIM);
    Gdiplus::SolidBrush valueBrush(CLR_VALUE);
    Gdiplus::SolidBrush dimBrush(CLR_TEXT_DIM);
    Gdiplus::StringFormat leftFormat;
    leftFormat.SetAlignment(Gdiplus::StringAlignmentNear);

    float innerX = static_cast<float>(panelRect.left + margin + groupInner + 2);
    float innerW = static_cast<float>(pw - 2 * (margin + groupInner + 2));

    // === Mirror RebuildButtons Y-cursor exactly ===
    int y = panelRect.top + margin;

    // --- PSU Group Box ---
    int psuGroupTop = y;
    y += titleH + groupPad;  // title + pad

    int numPSU;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        numPSU = static_cast<int>(m_psuDevices.size());
    }
    if (numPSU > 0) y += btnHeight + btnSpacing;
    y += btnHeight + btnSpacing;  // ON/OFF
    y += btnHeight + btnSpacing;  // +50/-50

    // Readings Y starts here
    float readY = static_cast<float>(y);

    y += labelHeight * 3 + groupPad;  // 3 text rows + bottom pad
    int psuGroupBottom = y;

    RECT psuGroupRect = { panelRect.left + margin, psuGroupTop,
                          panelRect.right - margin, psuGroupBottom };
    DrawGroupBox(gfx, psuGroupRect, L"POWER SUPPLY");

    // --- Get PSU data & update toggles ---
    double voltage = 0.0, current = 0.0;
    bool outputOn = false;
    double setCurrent = 0.0;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        if (m_selectedPSU < static_cast<int>(m_psuDevices.size())) {
            const auto& dev = m_psuDevices[m_selectedPSU];
            voltage = dev.voltage;
            current = dev.current;
            outputOn = dev.outputOn;
            setCurrent = dev.setCurrent;
        }
        for (auto& btn : m_buttons) {
            if (btn.tag >= TAG_PSU_SELECT_BASE && btn.tag < TAG_PSU_SELECT_BASE + 50)
                btn.toggled = ((btn.tag - TAG_PSU_SELECT_BASE) == m_selectedPSU);
        }
    }
    // Update SWIR head state
    bool swirHeadDown = false;
    if (m_pneumaticManager && m_pneumaticManager->HasSlide("SWIR_Head")) {
        PneumaticPosition headPos = m_pneumaticManager->GetPosition("SWIR_Head");
        swirHeadDown = (headPos == PneumaticPosition::EXTENDED);
    }

    for (auto& btn : m_buttons) {
        if (btn.tag == TAG_PSU_TOGGLE) btn.toggled = outputOn;
        if (btn.tag == TAG_AUTO_EXP) btn.toggled = m_autoExposure;
        if (btn.tag == TAG_CLICK_TO_MOVE) btn.toggled = m_clickToMoveEnabled;
        if (btn.tag == TAG_SWIR_HEAD) {
            btn.toggled = swirHeadDown;
            btn.label = swirHeadDown ? L"SWIR HEAD UP" : L"SWIR HEAD DOWN";
        }
        if (btn.tag == TAG_BEAM_DETECT) btn.toggled = m_beamDetectionEnabled;
    }

    // Draw PSU readings
    std::wostringstream setStr;
    setStr << L"Set: " << std::fixed << std::setprecision(1) << (setCurrent * 1000.0) << L" mA";
    gfx.DrawString(setStr.str().c_str(), -1, &smallFont,
                   Gdiplus::RectF(innerX, readY, innerW, static_cast<float>(labelHeight)),
                   &leftFormat, &dimBrush);
    readY += static_cast<float>(labelHeight);

    std::wostringstream voltStr;
    voltStr << std::fixed << std::setprecision(3) << voltage << L" V";
    gfx.DrawString(L"Voltage:", -1, &labelFont,
                   Gdiplus::RectF(innerX, readY, 58.0f, static_cast<float>(labelHeight)),
                   &leftFormat, &labelBrush);
    gfx.DrawString(voltStr.str().c_str(), -1, &valueFont,
                   Gdiplus::RectF(innerX + 58.0f, readY, innerW - 58.0f, static_cast<float>(labelHeight)),
                   &leftFormat, &valueBrush);
    readY += static_cast<float>(labelHeight);

    std::wostringstream currStr;
    currStr << std::fixed << std::setprecision(1) << (current * 1000.0) << L" mA";
    gfx.DrawString(L"Current:", -1, &labelFont,
                   Gdiplus::RectF(innerX, readY, 58.0f, static_cast<float>(labelHeight)),
                   &leftFormat, &labelBrush);
    gfx.DrawString(currStr.str().c_str(), -1, &valueFont,
                   Gdiplus::RectF(innerX + 58.0f, readY, innerW - 58.0f, static_cast<float>(labelHeight)),
                   &leftFormat, &valueBrush);

    // --- Exposure Group Box ---
    y += sectionGap;
    int expGroupTop = y;
    y += titleH + groupPad;
    y += btnHeight + btnSpacing;        // AUTO EXP
    y += (btnHeight + btnSpacing) * 2;  // 2x2 presets
    y += btnHeight + btnSpacing;        // manual input + SET
    float expInfoY = static_cast<float>(y);
    y += labelHeight + groupPad;
    int expGroupBottom = y;

    RECT expGroupRect = { panelRect.left + margin, expGroupTop,
                          panelRect.right - margin, expGroupBottom };
    DrawGroupBox(gfx, expGroupRect, L"SWIR EXPOSURE");

    // Exposure info text
    if (!m_autoExposure) {
        std::wostringstream expStr;
        expStr << L"Current: " << std::fixed << std::setprecision(0) << m_currentExposure << L" us";
        gfx.DrawString(expStr.str().c_str(), -1, &smallFont,
                       Gdiplus::RectF(innerX, expInfoY, innerW, static_cast<float>(labelHeight)),
                       &leftFormat, &dimBrush);
    } else {
        gfx.DrawString(L"Auto Exposure Active", -1, &smallFont,
                       Gdiplus::RectF(innerX, expInfoY, innerW, static_cast<float>(labelHeight)),
                       &leftFormat, &dimBrush);
    }

    // --- Tools Group Box ---
    y += sectionGap;
    int toolsGroupTop = y;
    y += titleH + groupPad;
    y += btnHeight + btnSpacing;  // SWIR HEAD
    y += btnHeight + btnSpacing;  // CLICK TO MOVE
    y += btnHeight + btnSpacing;  // PLACE MARKER
    y += btnHeight + btnSpacing;  // CLEAR ALL
    y += btnHeight + btnSpacing;  // SET MAGNIFICATION
    y += btnHeight + btnSpacing;  // BEAM DETECT
    y += btnHeight + btnSpacing;  // BEAM OPTIMIZE
    y += groupPad;

    RECT toolsGroupRect = { panelRect.left + margin, toolsGroupTop,
                            panelRect.right - margin, y };
    DrawGroupBox(gfx, toolsGroupRect, L"TOOLS");

    // Draw all buttons
    for (const auto& btn : m_buttons) {
        DrawButton(gfx, btn);
    }
}

void SwirBeamWindow::DrawOverlays(HDC hdc)
{
    if (m_lastRenderW <= 0 || m_lastRenderH <= 0) return;

    Gdiplus::Graphics gfx(hdc);
    gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    // Draw world-locked markers (before crosshair so crosshair draws on top)
    DrawWorldMarkers(gfx);

    int centerX = m_lastRenderX + m_lastRenderW / 2;
    int centerY = m_lastRenderY + m_lastRenderH / 2;

    // Green center crosshair (full width/height lines)
    if (m_crosshairEnabled) {
        Gdiplus::Pen centerPen(Gdiplus::Color(200, 0, 255, 0), 1.0f);
        gfx.DrawLine(&centerPen,
                     m_lastRenderX, centerY,
                     m_lastRenderX + m_lastRenderW, centerY);
        gfx.DrawLine(&centerPen,
                     centerX, m_lastRenderY,
                     centerX, m_lastRenderY + m_lastRenderH);
    }

    // Check if mouse is over the image area
    bool mouseInImage = m_mouseOverImage &&
        m_mousePosition.x >= m_lastRenderX &&
        m_mousePosition.x < m_lastRenderX + m_lastRenderW &&
        m_mousePosition.y >= m_lastRenderY &&
        m_mousePosition.y < m_lastRenderY + m_lastRenderH;

    // Cyan tracking crosshair at mouse position
    if (m_clickToMoveEnabled && mouseInImage) {
        Gdiplus::Pen trackPen(Gdiplus::Color(200, 0, 255, 255), 1.0f);
        int mx = m_mousePosition.x;
        int my = m_mousePosition.y;

        gfx.DrawLine(&trackPen,
                     m_lastRenderX, my,
                     m_lastRenderX + m_lastRenderW, my);
        gfx.DrawLine(&trackPen,
                     mx, m_lastRenderY,
                     mx, m_lastRenderY + m_lastRenderH);

        // Small cross at cursor
        int cs = 12;
        Gdiplus::Pen crossPen(Gdiplus::Color(255, 0, 255, 255), 2.0f);
        gfx.DrawLine(&crossPen, mx - cs, my, mx + cs, my);
        gfx.DrawLine(&crossPen, mx, my - cs, mx, my + cs);
    }

    // Real-world distance display near mouse
    if (m_showDistanceEnabled && mouseInImage && m_frameWidth > 0 && m_frameHeight > 0) {
        int mx = m_mousePosition.x;
        int my = m_mousePosition.y;

        int pixelOffsetX = mx - centerX;
        int pixelOffsetY = my - centerY;

        double scaleX = static_cast<double>(m_frameWidth) / m_lastRenderW;
        double scaleY = static_cast<double>(m_frameHeight) / m_lastRenderH;
        double scaledX = pixelOffsetX * scaleX;
        double scaledY = pixelOffsetY * scaleY;

        double effX = m_pixelToMmFactorX;
        double effY = m_pixelToMmFactorY;
        if (m_calibrationRefWidth > 0 && m_calibrationRefHeight > 0) {
            effX = m_pixelToMmFactorX * static_cast<double>(m_calibrationRefWidth) / m_frameWidth;
            effY = m_pixelToMmFactorY * static_cast<double>(m_calibrationRefHeight) / m_frameHeight;
        }

        // Real-world distance: sensor mm / magnification, then to um (*1000)
        double realUmX = (scaledX * effX / m_magnification) * 1000.0;
        double realUmY = (scaledY * effY / m_magnification) * 1000.0;

        wchar_t dxBuf[64], dyBuf[64];
        swprintf_s(dxBuf, L"dX: %+.1f um", realUmX);
        swprintf_s(dyBuf, L"dY: %+.1f um", realUmY);

        // Position tooltip near cursor
        int textOffX = 18, textOffY = 18;
        float textX = static_cast<float>(mx + textOffX);
        float textY = static_cast<float>(my + textOffY);

        float tooltipW = 160.0f;
        float tooltipH = 38.0f;
        if (mx + textOffX + static_cast<int>(tooltipW) > m_lastRenderX + m_lastRenderW)
            textX = static_cast<float>(mx - textOffX - tooltipW + 10);
        if (my + textOffY + static_cast<int>(tooltipH) > m_lastRenderY + m_lastRenderH)
            textY = static_cast<float>(my - textOffY - tooltipH);

        // Background
        Gdiplus::RectF bgRect(textX - 4.0f, textY - 2.0f, tooltipW, tooltipH);
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(190, 0, 0, 0));
        gfx.FillRectangle(&bgBrush, bgRect);
        Gdiplus::Pen borderPen(Gdiplus::Color(200, 255, 255, 255), 1.0f);
        gfx.DrawRectangle(&borderPen, bgRect);

        // Text
        Gdiplus::Font font(L"Consolas", 9.5f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
        gfx.DrawString(dxBuf, -1, &font, Gdiplus::PointF(textX, textY), &textBrush);
        gfx.DrawString(dyBuf, -1, &font, Gdiplus::PointF(textX, textY + 17.0f), &textBrush);
    }

    // Beam detection overlays
    if (m_beamDetectionEnabled) {
        std::lock_guard<std::mutex> lock(m_beamMutex);
        if (!m_beamResults.empty()) {
            DrawBeamOverlays(gfx, m_beamResults,
                             m_lastRenderX, m_lastRenderY, m_lastRenderW, m_lastRenderH,
                             m_frameWidth, m_frameHeight);
        }
    }

    // Info overlay - lower left corner of viewport (cal, mag, FPS stacked)
    {
        Gdiplus::Font infoFont(L"Consolas", 9.0f, Gdiplus::FontStyleBold);
        float infoX = static_cast<float>(m_lastRenderX + 6);
        float lineH = 18.0f;

        // Build cal string
        double effPxToMm = m_pixelToMmFactorX;
        if (m_calibrationRefWidth > 0 && m_frameWidth > 0) {
            effPxToMm = m_pixelToMmFactorX * static_cast<double>(m_calibrationRefWidth) / m_frameWidth;
        }
        wchar_t calBuf[64];
        swprintf_s(calBuf, L"Cal: %.4f mm/px", effPxToMm);

        wchar_t magBuf[32];
        swprintf_s(magBuf, L"Mag: %.0fx", m_magnification);

        wchar_t fpsBuf[32];
        swprintf_s(fpsBuf, L"%.1f FPS", m_displayFps);

        // 3 lines stacked from bottom
        float baseY = static_cast<float>(m_lastRenderY + m_lastRenderH) - 3.0f * lineH - 4.0f;
        float bgW = 160.0f;

        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(160, 0, 0, 0));
        gfx.FillRectangle(&bgBrush, infoX - 3.0f, baseY - 1.0f, bgW, 3.0f * lineH + 4.0f);

        Gdiplus::SolidBrush dimBrush(Gdiplus::Color(255, 140, 140, 140));
        Gdiplus::SolidBrush greenBrush(Gdiplus::Color(255, 180, 255, 180));

        gfx.DrawString(calBuf, -1, &infoFont, Gdiplus::PointF(infoX, baseY), &dimBrush);
        gfx.DrawString(magBuf, -1, &infoFont, Gdiplus::PointF(infoX, baseY + lineH), &dimBrush);
        gfx.DrawString(fpsBuf, -1, &infoFont, Gdiplus::PointF(infoX, baseY + 2.0f * lineH), &greenBrush);
    }
}

// ============================================================================
// World-Locked Markers
// ============================================================================

double SwirBeamWindow::GetDisplayPxPerMm() const
{
    if (m_lastRenderW <= 0 || m_frameWidth == 0 || m_magnification <= 0.0) return 0.0;

    // effectiveMmPerPx is the sensor-plane mm per pixel (image-space)
    double effectiveMmPerPx = m_pixelToMmFactorX;
    if (m_calibrationRefWidth > 0 && m_frameWidth > 0) {
        effectiveMmPerPx = m_pixelToMmFactorX * static_cast<double>(m_calibrationRefWidth) / m_frameWidth;
    }
    if (effectiveMmPerPx <= 0.0) return 0.0;

    // Real-world mm per pixel = sensor mm/px / magnification
    double realWorldMmPerPx = effectiveMmPerPx / m_magnification;

    double displayScale = static_cast<double>(m_lastRenderW) / m_frameWidth;
    return displayScale / realWorldMmPerPx;
}

void SwirBeamWindow::DrawWorldMarkers(Gdiplus::Graphics& gfx)
{
    if (!m_motionManager || m_lastRenderW <= 0 || m_lastRenderH <= 0) return;

    double pxPerMm = GetDisplayPxPerMm();
    if (pxPerMm <= 0.0) return;

    PositionStruct gantryPos = m_motionManager->GetCurrentPosition("gantry-main");
    int renderCenterX = m_lastRenderX + m_lastRenderW / 2;
    int renderCenterY = m_lastRenderY + m_lastRenderH / 2;

    Gdiplus::Pen markerPen(Gdiplus::Color(200, 255, 140, 0), 1.5f);  // Orange
    Gdiplus::Font labelFont(L"Consolas", 8.0f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush labelBrush(Gdiplus::Color(230, 255, 140, 0));
    Gdiplus::SolidBrush labelBgBrush(Gdiplus::Color(160, 0, 0, 0));

    std::lock_guard<std::mutex> lock(m_markerMutex);
    for (const auto& marker : m_worldMarkers) {
        double screenX = renderCenterX + (marker.worldX - gantryPos.x) * pxPerMm;
        double screenY = renderCenterY + (marker.worldY - gantryPos.y) * pxPerMm;

        int sx = static_cast<int>(std::round(screenX));
        int sy = static_cast<int>(std::round(screenY));

        std::wstring wlabel(marker.label.begin(), marker.label.end());

        if (marker.type == SwirMarkerType::VLine) {
            // Clip: only draw if within horizontal render bounds
            if (sx < m_lastRenderX || sx > m_lastRenderX + m_lastRenderW) continue;
            gfx.DrawLine(&markerPen, sx, m_lastRenderY, sx, m_lastRenderY + m_lastRenderH);
            // Label at top
            Gdiplus::RectF bgRect(static_cast<float>(sx + 2), static_cast<float>(m_lastRenderY + 2), 28.0f, 14.0f);
            gfx.FillRectangle(&labelBgBrush, bgRect);
            gfx.DrawString(wlabel.c_str(), -1, &labelFont,
                           Gdiplus::PointF(static_cast<float>(sx + 3), static_cast<float>(m_lastRenderY + 1)),
                           &labelBrush);
        } else {
            // HLine: clip to vertical render bounds
            if (sy < m_lastRenderY || sy > m_lastRenderY + m_lastRenderH) continue;
            gfx.DrawLine(&markerPen, m_lastRenderX, sy, m_lastRenderX + m_lastRenderW, sy);
            // Label at left
            Gdiplus::RectF bgRect(static_cast<float>(m_lastRenderX + 2), static_cast<float>(sy + 2), 28.0f, 14.0f);
            gfx.FillRectangle(&labelBgBrush, bgRect);
            gfx.DrawString(wlabel.c_str(), -1, &labelFont,
                           Gdiplus::PointF(static_cast<float>(m_lastRenderX + 3), static_cast<float>(sy + 1)),
                           &labelBrush);
        }
    }
}

void SwirBeamWindow::OnPlaceMarker()
{
    if (!m_motionManager) {
        MessageBoxW(m_hwnd, L"Motion manager not available.", L"Place Marker", MB_OK | MB_ICONWARNING);
        return;
    }

    int result = MessageBoxW(m_hwnd,
        L"Place a world-locked marker at current gantry position.\n\n"
        L"YES = Vertical line\n"
        L"NO = Horizontal line\n"
        L"CANCEL = Abort",
        L"Place Marker", MB_YESNOCANCEL | MB_ICONQUESTION);

    if (result == IDCANCEL) return;

    SwirMarkerType type = (result == IDYES) ? SwirMarkerType::VLine : SwirMarkerType::HLine;
    PositionStruct pos = m_motionManager->GetCurrentPosition("gantry-main");

    SwirWorldMarker marker;
    {
        std::lock_guard<std::mutex> lock(m_markerMutex);
        marker.id = m_nextMarkerId++;
    }
    marker.type = type;
    marker.worldX = pos.x;
    marker.worldY = pos.y;
    marker.label = (type == SwirMarkerType::VLine ? "V" : "H") + std::to_string(marker.id);

    {
        std::lock_guard<std::mutex> lock(m_markerMutex);
        m_worldMarkers.push_back(marker);
    }

    std::cout << "[SwirBeamWindow] Placed marker " << marker.label
              << " at (" << marker.worldX << ", " << marker.worldY << ")" << std::endl;

    SaveMarkersToFile();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SwirBeamWindow::OnClearAllMarkers()
{
    int result = MessageBoxW(m_hwnd,
        L"Clear all markers?", L"Clear Markers", MB_YESNO | MB_ICONQUESTION);
    if (result != IDYES) return;

    {
        std::lock_guard<std::mutex> lock(m_markerMutex);
        m_worldMarkers.clear();
        m_nextMarkerId = 1;
    }

    std::cout << "[SwirBeamWindow] All markers cleared" << std::endl;
    SaveMarkersToFile();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SwirBeamWindow::SaveMarkersToFile()
{
    if (m_markerFilePath.empty()) return;

    nlohmann::json jArray = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(m_markerMutex);
        for (const auto& m : m_worldMarkers) {
            nlohmann::json j;
            j["id"] = m.id;
            j["type"] = (m.type == SwirMarkerType::VLine) ? "VLine" : "HLine";
            j["worldX"] = m.worldX;
            j["worldY"] = m.worldY;
            j["label"] = m.label;
            jArray.push_back(j);
        }
    }

    std::ofstream ofs(m_markerFilePath);
    if (ofs.is_open()) {
        ofs << jArray.dump(2);
    }
}

void SwirBeamWindow::LoadMarkersFromFile()
{
    if (m_markerFilePath.empty()) return;

    std::ifstream ifs(m_markerFilePath);
    if (!ifs.is_open()) return;

    nlohmann::json jArray;
    jArray = nlohmann::json::parse(ifs, nullptr, false);
    if (jArray.is_discarded() || !jArray.is_array()) return;

    std::lock_guard<std::mutex> lock(m_markerMutex);
    m_worldMarkers.clear();
    int maxId = 0;

    for (const auto& j : jArray) {
        SwirWorldMarker marker;
        marker.id = j.value("id", 0);
        std::string typeStr = j.value("type", "VLine");
        marker.type = (typeStr == "HLine") ? SwirMarkerType::HLine : SwirMarkerType::VLine;
        marker.worldX = j.value("worldX", 0.0);
        marker.worldY = j.value("worldY", 0.0);
        marker.label = j.value("label", "");
        m_worldMarkers.push_back(marker);
        if (marker.id > maxId) maxId = marker.id;
    }

    m_nextMarkerId = maxId + 1;
    std::cout << "[SwirBeamWindow] Loaded " << m_worldMarkers.size() << " markers from file" << std::endl;
}

// ============================================================================
// Magnification Setting
// ============================================================================

// Win32 dialog proc for magnification input
static INT_PTR CALLBACK MagDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        // lParam is pointer to the current magnification value
        auto* magPtr = reinterpret_cast<double*>(lParam);
        SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(magPtr));
        // Set current value in the edit control
        wchar_t buf[32];
        swprintf_s(buf, L"%.1f", *magPtr);
        SetDlgItemTextW(hDlg, 101, buf);
        // Select all text for easy overwrite
        SendDlgItemMessageW(hDlg, 101, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(hDlg, 101));
        return FALSE;  // We set focus manually
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[32] = {};
            GetDlgItemTextW(hDlg, 101, buf, 32);
            double val = _wtof(buf);
            if (val >= 1.0 && val <= 1000.0) {
                auto* magPtr = reinterpret_cast<double*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
                *magPtr = val;
                EndDialog(hDlg, IDOK);
            } else {
                MessageBoxW(hDlg, L"Enter a value between 1 and 1000.", L"Invalid", MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void SwirBeamWindow::OnSetMagnification()
{
    // Build a simple dialog template in memory
    // Layout: static label + edit control + OK/Cancel buttons
    alignas(4) BYTE dlgBuf[1024] = {};
    DLGTEMPLATE* dlg = reinterpret_cast<DLGTEMPLATE*>(dlgBuf);
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
    dlg->cdit = 4;  // 4 controls: label, edit, OK, Cancel
    dlg->cx = 160;
    dlg->cy = 70;

    // Dialog title follows DLGTEMPLATE + menu(0) + class(0) + title
    WORD* ptr = reinterpret_cast<WORD*>(dlg + 1);
    *ptr++ = 0;  // no menu
    *ptr++ = 0;  // default class

    // Title: "Set Magnification"
    const wchar_t* title = L"Set Magnification";
    for (const wchar_t* c = title; *c; ++c) *ptr++ = *c;
    *ptr++ = 0;

    // Font size + font name (DS_SETFONT)
    *ptr++ = 9;  // font size
    const wchar_t* fontName = L"Segoe UI";
    for (const wchar_t* c = fontName; *c; ++c) *ptr++ = *c;
    *ptr++ = 0;

    // Helper lambda to add a control item
    auto addControl = [&](DWORD style, short x, short y, short cx, short cy, WORD id, const wchar_t* className, const wchar_t* text) {
        // Align to DWORD boundary
        ptr = reinterpret_cast<WORD*>((reinterpret_cast<uintptr_t>(ptr) + 3) & ~3);
        DLGITEMTEMPLATE* item = reinterpret_cast<DLGITEMTEMPLATE*>(ptr);
        item->style = WS_CHILD | WS_VISIBLE | style;
        item->x = x; item->y = y; item->cx = cx; item->cy = cy;
        item->id = id;
        ptr = reinterpret_cast<WORD*>(item + 1);
        // Class
        for (const wchar_t* c = className; *c; ++c) *ptr++ = *c;
        *ptr++ = 0;
        // Text
        for (const wchar_t* c = text; *c; ++c) *ptr++ = *c;
        *ptr++ = 0;
        // Creation data
        *ptr++ = 0;
    };

    // Static label
    addControl(SS_LEFT, 8, 8, 144, 10, 0xFFFF, L"Static", L"Enter magnification (1-1000):");
    // Edit control
    addControl(ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 8, 22, 144, 14, 101, L"Edit", L"");
    // OK button
    addControl(BS_DEFPUSHBUTTON | WS_TABSTOP, 50, 46, 50, 16, IDOK, L"Button", L"OK");
    // Cancel button
    addControl(WS_TABSTOP, 104, 46, 50, 16, IDCANCEL, L"Button", L"Cancel");

    double newMag = m_magnification;
    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandle(nullptr), dlg, m_hwnd, MagDlgProc, reinterpret_cast<LPARAM>(&newMag));

    if (result == IDOK) {
        m_magnification = newMag;
        std::cout << "[SwirBeamWindow] Magnification set to " << m_magnification << "x" << std::endl;
        SaveMagnification();
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void SwirBeamWindow::SaveMagnification()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    settings.setValue("SwirBeamWindow/magnification", m_magnification);
}

void SwirBeamWindow::LoadMagnification()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    m_magnification = settings.value("SwirBeamWindow/magnification", 20.0).toDouble();
    if (m_magnification < 1.0) m_magnification = 1.0;
    std::cout << "[SwirBeamWindow] Loaded magnification: " << m_magnification << "x" << std::endl;
}

// ============================================================================
// SWIR Head Pneumatic
// ============================================================================

void SwirBeamWindow::OnSwirHeadToggle()
{
    if (!m_pneumaticManager) {
        MessageBoxW(m_hwnd, L"Pneumatic manager not available.", L"SWIR Head", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!m_pneumaticManager->HasSlide("SWIR_Head")) {
        MessageBoxW(m_hwnd, L"SWIR_Head slide not configured.", L"SWIR Head", MB_OK | MB_ICONWARNING);
        return;
    }

    PneumaticPosition pos = m_pneumaticManager->GetPosition("SWIR_Head");
    PneumaticError err;
    if (pos == PneumaticPosition::EXTENDED) {
        err = m_pneumaticManager->Retract("SWIR_Head");
        std::cout << "[SwirBeamWindow] SWIR Head retracting (UP)" << std::endl;
    } else {
        err = m_pneumaticManager->Extend("SWIR_Head");
        std::cout << "[SwirBeamWindow] SWIR Head extending (DOWN)" << std::endl;
    }

    if (err != PneumaticError::SUCCESS && err != PneumaticError::ALREADY_AT_POSITION) {
        std::cerr << "[SwirBeamWindow] SWIR Head toggle failed: " << GetPneumaticErrorString(err) << std::endl;
    }

    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ============================================================================
// Beam Detection
// ============================================================================

void SwirBeamWindow::OnBeamDetectToggle()
{
    m_beamDetectionEnabled = !m_beamDetectionEnabled;
    std::cout << "[SwirBeamWindow] Beam detection "
              << (m_beamDetectionEnabled ? "enabled" : "disabled") << std::endl;

    if (m_beamDetectionEnabled) {
        StartDetectionThread();
    } else {
        StopDetectionThread();
        std::lock_guard<std::mutex> lock(m_beamMutex);
        m_beamResults.clear();
    }
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SwirBeamWindow::StartDetectionThread()
{
    if (m_detectionThread.joinable()) return;  // already running

    m_detectionShutdown.store(false);
    m_detectionReady.store(false);

    m_detectionThread = std::thread([this]() {
#ifdef HAS_OPENCV
        // Set below-normal priority
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        static constexpr int MAX_ITERATIONS = 10000000;  // bounded loop
        for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
            // Wait for new frame data or shutdown
            {
                std::unique_lock<std::mutex> lock(m_detectionMutex);
                m_detectionCV.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return m_detectionReady.load() || m_detectionShutdown.load();
                });
            }

            if (m_detectionShutdown.load()) break;
            if (!m_detectionReady.load()) continue;

            // Copy buffer locally
            std::vector<uint8_t> localBuffer;
            uint32_t localW = 0, localH = 0;
            {
                std::lock_guard<std::mutex> lock(m_detectionMutex);
                localBuffer = m_detectionBuffer;
                localW = m_detectionWidth;
                localH = m_detectionHeight;
                m_detectionReady.store(false);
            }

            if (localBuffer.empty() || localW == 0 || localH == 0) continue;

            // Run detection
            auto results = RunBeamDetection(localBuffer.data(), localW, localH, m_beamConfig);

            // Publish results
            {
                std::lock_guard<std::mutex> lock(m_beamMutex);
                m_beamResults = std::move(results);
            }

            // Publish to broker (subscribers get a copy)
            if (m_beamResultBroker) {
                std::lock_guard<std::mutex> lock(m_beamMutex);
                m_beamResultBroker->Publish(m_beamResults);
            }

            // Request repaint
            if (m_hwnd) {
                PostMessage(m_hwnd, WM_USER + 1, 0, 0);
            }
        }
#endif // HAS_OPENCV
    });
}

void SwirBeamWindow::StopDetectionThread()
{
    m_detectionShutdown.store(true);
    m_detectionCV.notify_all();
    if (m_detectionThread.joinable()) {
        m_detectionThread.join();
    }
}

// ============================================================================
// Beam Optimization (placeholder)
// ============================================================================

void SwirBeamWindow::OnBeamOptimize()
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "[BeamOptimize] Hardware Status Report" << std::endl;
    std::cout << "========================================" << std::endl;

    // --- Hex-right position ---
    if (m_motionManager) {
        PositionStruct hexPos = m_motionManager->GetCurrentPosition("hex-right");
        std::cout << "[BeamOptimize] hex-right position:"
                  << " X=" << std::fixed << std::setprecision(4) << hexPos.x
                  << " Y=" << hexPos.y
                  << " Z=" << hexPos.z
                  << " U=" << hexPos.u
                  << " V=" << hexPos.v
                  << " W=" << hexPos.w << std::endl;

        // --- Gantry position ---
        PositionStruct gantryPos = m_motionManager->GetCurrentPosition("gantry-main");
        std::cout << "[BeamOptimize] gantry-main position:"
                  << " X=" << std::fixed << std::setprecision(4) << gantryPos.x
                  << " Y=" << gantryPos.y
                  << " Z=" << gantryPos.z << std::endl;
    } else {
        std::cout << "[BeamOptimize] Motion manager: NOT AVAILABLE" << std::endl;
    }

    // --- Camera status ---
    if (m_cameraManager) {
        auto status = m_cameraManager->GetCameraStatus(m_targetCameraId);
        auto exp = m_cameraManager->GetExposureSettings(m_targetCameraId);
        std::cout << "[BeamOptimize] Camera '" << m_targetCameraId << "':"
                  << " connected=" << (status.connected ? "YES" : "NO")
                  << " grabbing=" << (status.grabbing ? "YES" : "NO")
                  << " exposure=" << std::fixed << std::setprecision(1) << exp.exposure_time << " us"
                  << " gain=" << std::setprecision(2) << exp.gain << std::endl;
    } else {
        std::cout << "[BeamOptimize] Camera manager: NOT AVAILABLE" << std::endl;
    }

    // --- PSU status ---
    if (m_psuManager) {
        auto psuNames = m_psuManager->getDeviceNames();
        if (psuNames.empty()) {
            std::cout << "[BeamOptimize] PSU: No devices found" << std::endl;
        }
        for (const auto& name : psuNames) {
            auto psu = m_psuManager->getPowerSupply(name);
            if (!psu) continue;
            bool connected = psu->isConnected();
            std::cout << "[BeamOptimize] PSU '" << name << "':"
                      << " connected=" << (connected ? "YES" : "NO");
            if (connected) {
                double v = psu->readVoltage(1);
                double i = psu->readCurrent(1);
                bool on = psu->readOutputState(1);
                std::cout << " output=" << (on ? "ON" : "OFF")
                          << " V=" << std::fixed << std::setprecision(3) << v
                          << " I=" << std::setprecision(4) << (i * 1000.0) << " mA";
            }
            std::cout << std::endl;
        }
    } else {
        std::cout << "[BeamOptimize] PSU manager: NOT AVAILABLE" << std::endl;
    }

    // --- Pneumatic status ---
    if (m_pneumaticManager) {
        auto slideNames = m_pneumaticManager->GetAllSlideNames();
        if (slideNames.empty()) {
            std::cout << "[BeamOptimize] Pneumatics: No slides found" << std::endl;
        }
        for (const auto& name : slideNames) {
            PneumaticPosition pos = m_pneumaticManager->GetPosition(name);
            std::string posStr;
            switch (pos) {
                case PneumaticPosition::EXTENDED:       posStr = "EXTENDED"; break;
                case PneumaticPosition::RETRACTED:      posStr = "RETRACTED"; break;
                case PneumaticPosition::EXTENDING:      posStr = "EXTENDING"; break;
                case PneumaticPosition::RETRACTING:     posStr = "RETRACTING"; break;
                case PneumaticPosition::HARDWARE_ERROR: posStr = "HARDWARE_ERROR"; break;
                case PneumaticPosition::TIMEOUT:        posStr = "TIMEOUT"; break;
                default:                                posStr = "UNKNOWN"; break;
            }
            std::cout << "[BeamOptimize] Pneumatic '" << name << "': " << posStr << std::endl;
        }
    } else {
        std::cout << "[BeamOptimize] Pneumatic manager: NOT AVAILABLE" << std::endl;
    }

    // --- Beam detection results ---
    {
        std::lock_guard<std::mutex> lock(m_beamMutex);
        if (m_beamResults.empty()) {
            std::cout << "[BeamOptimize] Beam results: NONE (enable BEAM DETECT first)" << std::endl;
        } else {
            std::cout << "[BeamOptimize] Beam results: " << m_beamResults.size() << " beam(s)" << std::endl;
            for (const auto& beam : m_beamResults) {
                std::cout << "  " << beam.label
                          << " cx=" << std::fixed << std::setprecision(1) << beam.d4.cx
                          << " cy=" << beam.d4.cy
                          << " dx=" << beam.d4.dx
                          << " dy=" << beam.d4.dy
                          << " EE=" << std::fixed << std::setprecision(1) << (beam.encircledEnergy * 100.0) << "%"
                          << (beam.saturated ? " SATURATED" : "")
                          << (!beam.gaLabel.empty() ? " ga=" + beam.gaLabel : "")
                          << std::endl;
            }
        }
    }

    std::cout << "========================================\n" << std::endl;
}

void SwirBeamWindow::OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);
    PrepareBackBuffer();
    if (m_memDC) {
        RECT clientRect;
        GetClientRect(m_hwnd, &clientRect);
        int totalWidth = clientRect.right - clientRect.left;
        int totalHeight = clientRect.bottom - clientRect.top;

        HBRUSH blackBrush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        FillRect(m_memDC, &clientRect, blackBrush);

        int panelW = static_cast<int>(totalWidth * PANEL_RATIO);
        if (panelW < 180) panelW = 180;

        RECT panelRect = {0, 0, panelW, totalHeight};
        RECT imageRect = {panelW, 0, totalWidth, totalHeight};

        RebuildButtons(panelRect);
        RenderFrame(m_memDC, imageRect);
        DrawOverlays(m_memDC);
        RenderControlPanel(m_memDC, panelRect);

        BitBlt(hdc, 0, 0, totalWidth, totalHeight, m_memDC, 0, 0, SRCCOPY);
    }
    EndPaint(m_hwnd, &ps);
}

// ============================================================================
// Window Procedure
// ============================================================================

bool SwirBeamWindow::ProcessMessages()
{
    MSG msg;
    bool hasMessage = false;
    for (int i = 0; i < 100 && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE); ++i) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        hasMessage = true;
    }
    return hasMessage;
}

LRESULT CALLBACK SwirBeamWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    auto* window = reinterpret_cast<SwirBeamWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (window) return window->HandleMessage(hwnd, uMsg, wParam, lParam);
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT SwirBeamWindow::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_SIZE:
        m_panelWidth = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_USER + 1:
        // Beam detection thread requests repaint
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        int mx = LOWORD(lParam);
        int my = HIWORD(lParam);
        m_mousePosition = {mx, my};
        m_mouseOverImage = true;
        if (!m_mouseTracking) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            m_mouseTracking = true;
        }
        bool needRepaint = false;
        for (auto& btn : m_buttons) {
            bool wasHovered = btn.hovered;
            btn.hovered = (PtInRect(&btn.rect, m_mousePosition) != 0);
            if (btn.hovered != wasHovered) needRepaint = true;
        }
        // Repaint when mouse is over image area AND overlays are active
        if ((m_crosshairEnabled || m_showDistanceEnabled || m_clickToMoveEnabled) &&
            m_lastRenderW > 0 && mx >= m_lastRenderX && mx < m_lastRenderX + m_lastRenderW &&
            my >= m_lastRenderY && my < m_lastRenderY + m_lastRenderH) {
            needRepaint = true;
        }
        if (needRepaint) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        m_mouseTracking = false;
        m_mouseOverImage = false;
        for (auto& btn : m_buttons) btn.hovered = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        int mx = LOWORD(lParam);
        int my = HIWORD(lParam);
        auto* btn = HitTestButton(mx, my);
        if (btn) {
            btn->pressed = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            SetCapture(hwnd);
        } else if (m_clickToMoveEnabled && m_motionManager &&
                   m_lastRenderW > 0 && m_lastRenderH > 0) {
            // Check if click is within the camera image area
            if (mx >= m_lastRenderX && mx < m_lastRenderX + m_lastRenderW &&
                my >= m_lastRenderY && my < m_lastRenderY + m_lastRenderH) {
                HandleClickToMove(mx, my);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        ReleaseCapture();
        int mx = LOWORD(lParam);
        int my = HIWORD(lParam);
        for (auto& btn : m_buttons) {
            if (btn.pressed) {
                btn.pressed = false;
                POINT pt = {mx, my};
                if (PtInRect(&btn.rect, pt) && btn.onClick) btn.onClick();
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_CLOSE:
        Hide();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { Hide(); return 0; }
        break;
    case WM_DESTROY:
        if (m_memDC) {
            if (m_oldBitmap) SelectObject(m_memDC, m_oldBitmap);
            if (m_memBitmap) DeleteObject(m_memBitmap);
            DeleteDC(m_memDC);
            m_memDC = nullptr;
            m_memBitmap = nullptr;
            m_oldBitmap = nullptr;
        }
        m_hwnd = nullptr;
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

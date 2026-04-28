// SwirBeamWindow.cpp
#include "SwirBeamWindow.h"
#include "BeamOptimizer/BeamResultBroker.h"
#include "SwirBeamConfigStore.h"
#include "SwirBeamFocusController.h"
#include "SwirBeamOverlay.h"
#include "SwirImageSaver.h"
#include "SwirWorldMarkerStore.h"
#include "camera/CameraConfigManager.h"
#include "camera/CameraManager.h"
#include "motions/unified_motion_manager.h"
#include "PSU/PowerSupplyManager.h"
#include "TEC/TECManager.h"
#include "pneumatics/PneumaticManager.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <nlohmann/json.hpp>
#include <shellapi.h>
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
    LoadBeamConfig();

    // Initialize beam focus config defaults
    m_beamFocusConfig.stepSizes = {0.010, 0.005, 0.002};  // 10, 5, 2 µm
    m_beamFocusConfig.label = "BeamFocus";

    // Bind score provider to beam results
    m_beamScoreProvider.Bind(&m_beamMutex, &m_beamResults);

    // World-marker store: Place() pulls current gantry XY via this lambda.
    m_markerStore = std::make_unique<SwirWorldMarkerStore>(
        [this](double& x, double& y) -> bool {
            if (!m_motionManager) return false;
            PositionStruct pos = m_motionManager->GetCurrentPosition("gantry-main");
            x = pos.x;
            y = pos.y;
            return true;
        });

    // Image-save APIs: configured with snapshot callbacks so they don't
    // hold any window state directly.
    SwirImageSaver::Deps saveDeps;
    saveDeps.snapshotFrame =
        [this](std::vector<uint8_t>& outBuf, uint32_t& outW, uint32_t& outH) {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            if (m_frameWidth == 0 || m_frameHeight == 0 || m_frameBuffer.empty())
                return false;
            outBuf = m_frameBuffer;
            outW = m_frameWidth;
            outH = m_frameHeight;
            return true;
        };
    saveDeps.snapshotBeams = [this]() {
        std::lock_guard<std::mutex> lock(m_beamMutex);
        return m_beamDisplayResults;
    };
    saveDeps.getMicronsPerPixel = [this]() {
        return m_useHalconDetection ? m_halconConfig.micronsPerPixel
                                     : m_beamConfig.micronsPerPixel;
    };
    saveDeps.getUseBlobMode = [this]() { return m_useBlobDetection; };
    m_imageSaver = std::make_unique<SwirImageSaver>(std::move(saveDeps));

    // BF (beam focus) hill climb worker.
    m_beamFocus = std::make_unique<SwirBeamFocusController>();
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
    m_markerStore->SetFilePath("swir_markers.session.json");
    m_markerStore->LoadFromFile();
    LoadMagnification();
    std::cout << "[SwirBeamWindow] Window created" << std::endl;
    return true;
}

void SwirBeamWindow::CleanupPanelCache()
{
    if (m_panelCacheDC) {
        if (m_panelCacheOldBitmap) SelectObject(m_panelCacheDC, m_panelCacheOldBitmap);
        if (m_panelCacheBitmap) DeleteObject(m_panelCacheBitmap);
        DeleteDC(m_panelCacheDC);
        m_panelCacheDC = nullptr;
        m_panelCacheBitmap = nullptr;
        m_panelCacheOldBitmap = nullptr;
        m_panelCacheWidth = 0;
        m_panelCacheHeight = 0;
    }
}

void SwirBeamWindow::EmergencyStop()
{
    if (m_beamFocus && m_beamFocus->IsRunning()) {
        std::cout << "[SwirBeamWindow] E-STOP: cancelling beam focus optimization" << std::endl;
        m_beamFocus->RequestStop();
        InvalidatePanel();
    }
}

void SwirBeamWindow::Destroy()
{
    // Stop beam focus if running
    if (m_beamFocus) m_beamFocus->StopAndWait();
    if (m_configReloadFuture.valid()) {
        m_configReloadFuture.wait();
    }
    if (m_psuPollFuture.valid()) {
        m_psuPollFuture.wait();
    }
    if (m_tecPresetFuture.valid()) {
        m_tecPresetFuture.wait();
    }
    StopAutoSatAlgorithm();
    StopDetectionThread();
    if (m_hwnd) {
        SaveMarkersToFile();
        CleanupPanelCache();
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

    // Set SWIR camera gain=0, auto_gain=off on show
    if (m_cameraManager && !m_targetCameraId.empty()) {
        auto settings = m_cameraManager->GetExposureSettings(m_targetCameraId);
        settings.gain = 0.0;
        settings.auto_gain = false;
        m_cameraManager->ApplyExposureSettings(m_targetCameraId, settings);
    }
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

    // Auto-connect any disconnected PSUs (background thread to avoid blocking Qt event loop)
    std::vector<std::string> disconnectedNames;
    for (const auto& dev : m_psuDevices) {
        auto psu = m_psuManager->getPowerSupply(dev.name);
        if (psu && !psu->isConnected()) {
            disconnectedNames.push_back(dev.name);
        }
    }
    if (!disconnectedNames.empty()) {
        auto* psuMgr = m_psuManager;
        m_psuConnectFuture = std::async(std::launch::async, [psuMgr, disconnectedNames]() {
            for (const auto& name : disconnectedNames) {
                auto psu = psuMgr->getPowerSupply(name);
                if (!psu) continue;
                std::cout << "[SwirBeamWindow] Auto-connecting " << name << "..." << std::endl;
                if (psu->connect()) {
                    std::cout << "[SwirBeamWindow] " << name << " connected" << std::endl;
                } else {
                    std::cerr << "[SwirBeamWindow] " << name << " auto-connect failed" << std::endl;
                }
            }
        });
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
    // Don't gate on IsVisible() — saves and detection still need fresh
    // frames even when the window is hidden. The display path elsewhere
    // skips its expensive paint when the window isn't visible.
    if (!m_hwnd) return;
    {
        std::lock_guard<std::mutex> lock(m_cameraMutex);
        if (!m_targetCameraId.empty() && frameData.cameraId != m_targetCameraId) return;
    }

    const uint32_t w = frameData.width;
    const uint32_t h = frameData.height;
    const uint8_t* src = frameData.imageData.data();
    const int channels = frameData.channels;

    // Feed grayscale to beam detection thread (separate lock, independent of display)
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
        // Detection-input drop: writing while previous frame still pending
        // means the detection thread won't see the older frame at all.
        if (m_detectionReady.load()) {
            ++m_detectionInputDrops;
            if ((m_detectionInputDrops % 30) == 0) {
                std::cout << "[BeamDetect] detection-input drops total="
                          << m_detectionInputDrops
                          << " (detection thread can't keep up with frame rate)"
                          << std::endl;
            }
        }
        m_detectionReady.store(true);
    }

    // Skip display buffer conversion if previous frame not yet rendered.
    // This prevents the grab thread from stalling on expensive pixel work
    // when the UI can't keep up, while detection still gets every frame.
    if (!m_hasNewFrame.load()) {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        const size_t bgrSize = static_cast<size_t>(w) * h * 3;
        if (m_frameBuffer.size() != bgrSize) m_frameBuffer.resize(bgrSize);
        m_frameWidth = w;
        m_frameHeight = h;

        if (channels >= 3) {
            // Row-level copy with RGB->BGR swap and vertical flip
            const size_t rowBytes = static_cast<size_t>(w) * 3;
            for (uint32_t row = 0; row < h; ++row) {
                const uint32_t dstRow = h - 1 - row;
                const uint8_t* srcLine = src + static_cast<size_t>(row) * w * channels;
                uint8_t* dstLine = m_frameBuffer.data() + static_cast<size_t>(dstRow) * rowBytes;
                for (uint32_t col = 0; col < w; ++col) {
                    dstLine[col * 3 + 0] = srcLine[col * 3 + 2];
                    dstLine[col * 3 + 1] = srcLine[col * 3 + 1];
                    dstLine[col * 3 + 2] = srcLine[col * 3 + 0];
                }
            }
        } else {
            // Mono: row-level grayscale-to-BGR expansion with vertical flip
            for (uint32_t row = 0; row < h; ++row) {
                const uint32_t dstRow = h - 1 - row;
                const uint8_t* srcLine = src + static_cast<size_t>(row) * w;
                uint8_t* dstLine = m_frameBuffer.data() + static_cast<size_t>(dstRow) * w * 3;
                for (uint32_t col = 0; col < w; ++col) {
                    const uint8_t v = srcLine[col];
                    dstLine[col * 3 + 0] = v;
                    dstLine[col * 3 + 1] = v;
                    dstLine[col * 3 + 2] = v;
                }
            }
        }
        m_hasNewFrame.store(true);
    }

    // FPS tracking (lock-free, only written from grab thread)
    ++m_frameCount;
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_fpsTimestamp).count();
    if (elapsed >= 1.0) {
        m_displayFps = m_frameCount / elapsed;
        m_frameCount = 0;
        m_fpsTimestamp = now;
    }

    if (m_beamDetectionEnabled) {
        m_detectionCV.notify_one();
    }

    // Throttle repaint requests to ~30fps to avoid flooding the message queue
    auto timeSinceLastInvalidate = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastInvalidateTime).count();
    if (timeSinceLastInvalidate >= 30) {
        m_lastInvalidateTime = now;
        // Only invalidate image area — skip repainting the control panel
        if (m_lastRenderW > 0 && m_lastRenderH > 0) {
            RECT imageInvalidRect = { m_lastRenderX, 0,
                                      m_lastRenderX + m_lastRenderW + 1,
                                      m_lastRenderY + m_lastRenderH + 1 };
            InvalidateRect(m_hwnd, &imageInvalidRect, FALSE);
        } else {
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }
}

void SwirBeamWindow::OnCameraStatusChanged(const std::string& /*cameraId*/, bool /*connected*/, bool /*grabbing*/)
{
}

// ============================================================================
// PSU Polling
// ============================================================================

void SwirBeamWindow::PollPSUReadings()
{
    // Also poll TEC readings on the same timer
    PollTECReadings();

    if (!m_psuManager) return;

    // Discover devices on first poll
    DiscoverPSUDevices();

    // Skip if a previous poll is still running (VISA reads can be slow)
    if (m_psuPollInFlight.load()) return;

    // Collect device names to poll
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        for (const auto& dev : m_psuDevices) names.push_back(dev.name);
    }
    if (names.empty()) return;

    // Run blocking VISA reads on a background thread
    m_psuPollInFlight.store(true);
    auto* psuMgr = m_psuManager;
    auto* self = this;
    m_psuPollFuture = std::async(std::launch::async, [self, psuMgr, names]() {
        // Read from hardware (may block 50-100ms per call)
        struct Reading { std::string name; double voltage; double current; double setCurrentVal; bool outputOn; };
        std::vector<Reading> readings;
        for (const auto& name : names) {
            auto psu = psuMgr->getPowerSupply(name);
            if (!psu || !psu->isConnected()) continue;
            Reading r;
            r.name = name;
            r.voltage = psu->readVoltage(1);
            r.current = psu->readCurrent(1);
            r.setCurrentVal = psu->readSetCurrent(1);
            r.outputOn = psu->readOutputState(1);
            readings.push_back(r);
        }

        // Update state under lock
        {
            std::lock_guard<std::mutex> lock(self->m_psuMutex);
            for (const auto& r : readings) {
                for (auto& dev : self->m_psuDevices) {
                    if (dev.name == r.name) {
                        dev.voltage = r.voltage;
                        dev.current = r.current;
                        dev.outputOn = r.outputOn;
                        // Sync programmed current from hardware so +/- buttons work correctly
                        if (r.setCurrentVal >= 0.0) {
                            dev.setCurrent = r.setCurrentVal;
                        }
                        break;
                    }
                }
            }
        }

        self->m_psuPollInFlight.store(false);
        self->InvalidatePanel();

        if (self->m_hwnd && self->IsVisible()) {
            InvalidateRect(self->m_hwnd, nullptr, FALSE);
        }
    });
}

// ============================================================================
// TEC Actions
// ============================================================================

void SwirBeamWindow::OnTECPreset(double temperature)
{
    if (!m_tecManager) {
        std::cout << "[SwirBeamWindow] TEC manager not available" << std::endl;
        return;
    }

    // Auto-connect if not connected (background thread)
    auto* tecMgr = m_tecManager;
    std::string devName = m_tecDeviceName;

    if (m_tecPresetFuture.valid()) {
        m_tecPresetFuture.wait();
    }
    m_tecPresetFuture = std::async(std::launch::async, [tecMgr, devName, temperature, this]() {
        // Connect if needed
        auto status = tecMgr->GetDeviceStatus(devName);
        if (!status.Connected) {
            std::cout << "[SwirBeamWindow] Connecting TEC '" << devName << "'..." << std::endl;
            if (!tecMgr->ConnectDevice(devName)) {
                std::cerr << "[SwirBeamWindow] Failed to connect TEC '" << devName << "'" << std::endl;
                return;
            }
            std::cout << "[SwirBeamWindow] TEC '" << devName << "' connected" << std::endl;
        }

        // Set temperature
        auto result = tecMgr->SetTemperature(devName, temperature);
        if (result.Success) {
            std::cout << "[SwirBeamWindow] TEC set to " << temperature << " C" << std::endl;
        } else {
            std::cerr << "[SwirBeamWindow] TEC SetTemperature failed: " << result.Message << std::endl;
            return;
        }

        // Enable TEC output
        auto onResult = tecMgr->TEC_On(devName);
        if (onResult.Success) {
            std::cout << "[SwirBeamWindow] TEC output ON" << std::endl;
        } else {
            std::cerr << "[SwirBeamWindow] TEC_On failed: " << onResult.Message << std::endl;
        }

        InvalidatePanel();
    });
}

void SwirBeamWindow::PollTECReadings()
{
    if (!m_tecManager) return;

    auto status = m_tecManager->GetDeviceStatus(m_tecDeviceName);
    m_tecConnected = status.Connected;

    if (!m_tecConnected) return;

    auto tempResult = m_tecManager->ReadTemperature(m_tecDeviceName);
    if (tempResult.Success) {
        m_tecTemperature = tempResult.Value;
    }

    m_tecOutputOn = status.TECOn;
    InvalidatePanel();
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
        InvalidatePanel();
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
    InvalidatePanel();
}

void SwirBeamWindow::OnCurrentIncrease()
{
    DisableAutoSat();
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
    InvalidatePanel();
    std::cout << "[SwirBeamWindow] " << devName << " current set to " << (newCurrent * 1000.0) << " mA" << std::endl;
}

void SwirBeamWindow::OnCurrentDecrease()
{
    DisableAutoSat();
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
    InvalidatePanel();
    std::cout << "[SwirBeamWindow] " << devName << " current set to " << (newCurrent * 1000.0) << " mA" << std::endl;
}

void SwirBeamWindow::OnCurrentIncreaseSmall()
{
    DisableAutoSat();
    if (!m_psuManager) return;
    std::string devName;
    double newCurrent;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        if (m_selectedPSU >= static_cast<int>(m_psuDevices.size())) return;
        auto& dev = m_psuDevices[m_selectedPSU];
        dev.setCurrent += CURRENT_STEP_SMALL_MA / 1000.0;
        devName = dev.name;
        newCurrent = dev.setCurrent;
    }
    auto psu = m_psuManager->getPowerSupply(devName);
    if (!psu || !psu->isConnected()) return;
    psu->setCurrent(newCurrent, 1);
    InvalidatePanel();
    std::cout << "[SwirBeamWindow] " << devName << " current set to " << (newCurrent * 1000.0) << " mA" << std::endl;
}

void SwirBeamWindow::OnCurrentDecreaseSmall()
{
    DisableAutoSat();
    if (!m_psuManager) return;
    std::string devName;
    double newCurrent;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        if (m_selectedPSU >= static_cast<int>(m_psuDevices.size())) return;
        auto& dev = m_psuDevices[m_selectedPSU];
        dev.setCurrent -= CURRENT_STEP_SMALL_MA / 1000.0;
        if (dev.setCurrent < 0.0) dev.setCurrent = 0.0;
        devName = dev.name;
        newCurrent = dev.setCurrent;
    }
    auto psu = m_psuManager->getPowerSupply(devName);
    if (!psu || !psu->isConnected()) return;
    psu->setCurrent(newCurrent, 1);
    InvalidatePanel();
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
    InvalidatePanel();
    std::cout << "[SwirBeamWindow] Auto exposure " << (m_autoExposure ? "ON" : "OFF") << std::endl;
}

// ============================================================================
// AUTO SAT — one-shot auto-tune & focus algorithm
// ============================================================================
//
// Click "AUTO SAT" → runs on its own worker thread:
//   Phase 1: tune exposure (20us steps) then PSU current (5mA, both PSUs)
//            until max(B0_PK, B1_PK) is in [PK_TARGET_LOW, PK_TARGET_HIGH].
//   Phase 2: sweep gantry-main Z by ±50 um in 10 um steps; best focus is
//            the Z with maximum peak. If a peak saturates mid-sweep,
//            jump back to Phase 1 to re-tune, then resume. Converges
//            when best-Z is interior to the sweep (or after 3 iterations).
// While running the button label shows "STOP"; clicking it again aborts.
// Any manual exposure/current change calls StopAutoSatAlgorithm so the
// user always wins.
//
// Default state: OFF.

namespace {
constexpr int    AS_PK_TARGET_LOW    = 180;   // tune until pk >= this
constexpr int    AS_PK_TARGET_HIGH   = 250;   // and pk < this (header room before 255)
constexpr int    AS_PK_SAT           = 254;   // any pixel >= this counts as saturated
constexpr double AS_EXP_STEP_COARSE  = 20.0;
constexpr double AS_EXP_STEP_FINE    = 10.0;  // used after the first oscillation
constexpr int    AS_CUR_STEP_MA      = 5;
constexpr double AS_EXP_MIN_US       = 30.0;
constexpr double AS_EXP_MAX_US       = 1000.0;
constexpr int    AS_CUR_MIN_MA       = 100;
constexpr int    AS_CUR_MAX_MA       = 200;
constexpr int    AS_BEAM_POLL_MS     = 250;   // settle after exp/cur change
constexpr int    AS_TUNE_MAX_ITERS   = 80;
constexpr double AS_Z_STEP_MM        = 0.010; // 10 um
constexpr int    AS_Z_HALF_STEPS     = 5;     // ±50 um
constexpr int    AS_FOCUS_MAX_ITERS  = 3;
constexpr int    AS_MOTION_SETTLE_MS = 350;
constexpr double AS_MOTION_VEL_MMS   = 5.0;   // gentle move for focus search
}

void SwirBeamWindow::OnAutoSatToggle()
{
    if (m_autoSatRunning.load()) {
        // Click while running → abort.
        StopAutoSatAlgorithm();
        return;
    }
    EnableAutoSatPrevention(true);
}

void SwirBeamWindow::EnableAutoSatPrevention(bool enable)
{
    if (!enable) {
        StopAutoSatAlgorithm();
        return;
    }
    if (m_autoSatRunning.load()) return;  // already running

    m_autoSatAbort.store(false);
    m_autoSatRunning.store(true);
    InvalidatePanel();
    std::cout << "[AutoSat] starting algorithm" << std::endl;

    // std::async with std::launch::async returns a future that joins in its
    // destructor — assigning to a member future cancels (= joins) any prior
    // run, so a quick double-start is safe.
    m_autoSatFuture = std::async(std::launch::async, [this]() {
        try {
            RunAutoSatAlgorithm();
        } catch (const std::exception& e) {
            std::cerr << "[AutoSat] exception: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[AutoSat] unknown exception" << std::endl;
        }
        m_autoSatRunning.store(false);
        m_autoSatAbort.store(false);
        if (m_hwnd) {
            // Refresh the button label back to "AUTO SAT".
            InvalidatePanel();
            PostMessage(m_hwnd, WM_USER + 1, 0, 0);
        }
        std::cout << "[AutoSat] done" << std::endl;
    });
}

void SwirBeamWindow::StopAutoSatAlgorithm()
{
    if (!m_autoSatRunning.load()) return;
    m_autoSatAbort.store(true);
    std::cout << "[AutoSat] abort requested" << std::endl;
    if (m_autoSatFuture.valid()) {
        m_autoSatFuture.wait();
    }
}

void SwirBeamWindow::EnableBeamDetection(bool enable)
{
    if (m_beamDetectionEnabled == enable) return;
    OnBeamDetectToggle();
}

std::vector<SwirBeamSnapshot> SwirBeamWindow::GetBeamSnapshots() const
{
    std::vector<SwirBeamSnapshot> out;
    // Pick the right calibration source for the active detection mode.
    const double upp =
#ifdef HAS_HALCON
        m_useHalconDetection ? m_halconConfig.micronsPerPixel :
#endif
        m_useBlobDetection   ? m_beamConfig.micronsPerPixel
                             : m_beamConfig.micronsPerPixel;

    std::lock_guard<std::mutex> lock(m_beamMutex);
    out.reserve(m_beamDisplayResults.size());
    for (const auto& r : m_beamDisplayResults) {
        SwirBeamSnapshot s;
        s.label = r.label;
        s.gaLabel = r.gaLabel;
        s.cx_px = r.d4.cx;
        s.cy_px = r.d4.cy;
        s.dx_px = r.d4.dx;
        s.dy_px = r.d4.dy;
        s.peakIntensity = static_cast<int>(r.peakIntensity);
        s.encircledEnergy = r.encircledEnergy;
        s.saturated = r.saturated;
        s.saturatedPixelCount = r.saturatedPixelCount;
        s.totalEnergy = r.totalEnergy;
        s.circleEnergy = r.circleEnergy;
        s.micronsPerPixel = upp;
        out.push_back(std::move(s));
    }
    return out;
}

void SwirBeamWindow::DisableAutoSat()
{
    StopAutoSatAlgorithm();
}

// ----------------------------------------------------------------------------
// Helpers used by the auto-sat algorithm
// ----------------------------------------------------------------------------

bool SwirBeamWindow::AutoSatIncreaseCurrent(int stepMa)
{
    if (!m_psuManager) return false;
    std::vector<std::string> names;
    int curMa = 0;
    int newMa = 0;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        if (m_psuDevices.empty()) return false;
        curMa = static_cast<int>(std::round(m_psuDevices[0].setCurrent * 1000.0));
        newMa = curMa + stepMa;
        if (newMa > AS_CUR_MAX_MA) return false;
        const double newAmps = newMa / 1000.0;
        for (auto& d : m_psuDevices) {
            names.push_back(d.name);
            d.setCurrent = newAmps;
        }
    }
    const double newAmps = newMa / 1000.0;
    for (const auto& name : names) {
        auto psu = m_psuManager->getPowerSupply(name);
        if (psu && psu->isConnected()) psu->setCurrent(newAmps, 1);
    }
    std::cout << "[AutoSat] current " << curMa << " -> " << newMa << " mA" << std::endl;
    InvalidatePanel();
    return true;
}

bool SwirBeamWindow::AutoSatDecreaseCurrent(int stepMa)
{
    if (!m_psuManager) return false;
    std::vector<std::string> names;
    int curMa = 0;
    int newMa = 0;
    {
        std::lock_guard<std::mutex> lock(m_psuMutex);
        if (m_psuDevices.empty()) return false;
        curMa = static_cast<int>(std::round(m_psuDevices[0].setCurrent * 1000.0));
        newMa = curMa - stepMa;
        if (newMa < AS_CUR_MIN_MA) return false;
        const double newAmps = newMa / 1000.0;
        for (auto& d : m_psuDevices) {
            names.push_back(d.name);
            d.setCurrent = newAmps;
        }
    }
    const double newAmps = newMa / 1000.0;
    for (const auto& name : names) {
        auto psu = m_psuManager->getPowerSupply(name);
        if (psu && psu->isConnected()) psu->setCurrent(newAmps, 1);
    }
    std::cout << "[AutoSat] current " << curMa << " -> " << newMa << " mA" << std::endl;
    InvalidatePanel();
    return true;
}

// ----------------------------------------------------------------------------
// Algorithm
// ----------------------------------------------------------------------------

void SwirBeamWindow::RunAutoSatAlgorithm()
{
    // Make sure detection is running — algorithm is no-op without it.
    if (!m_beamDetectionEnabled) {
        EnableBeamDetection(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    // Phase 1: tune brightness.
    if (!AutoSatTuneExposureCurrent()) {
        std::cout << "[AutoSat] phase 1 (tune) failed/aborted — stopping" << std::endl;
        return;
    }
    if (m_autoSatAbort.load()) return;

    // Phase 2: focus search.
    AutoSatFocusSearch();
}

bool SwirBeamWindow::AutoSatTuneExposureCurrent()
{
    // Oscillation handling: when a single 20us step takes us past the band
    // (e.g. 30us PK=150 → 50us PK=255 → 30us → 50us …), we (a) shrink the
    // step to 10us; if the camera still can't land us in band, (b) nudge
    // the PSU current and reset the step back to 20us. Without this the
    // tuner can loop forever between two exposures that straddle the band.
    int prevDir = 0;       // -1 = darken, +1 = brighten, 0 = none yet
    int flipCount = 0;
    double expStep = AS_EXP_STEP_COARSE;

    for (int iter = 0; iter < AS_TUNE_MAX_ITERS; ++iter) {
        if (m_autoSatAbort.load()) return false;

        auto snaps = GetBeamSnapshots();
        if (snaps.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(AS_BEAM_POLL_MS));
            continue;
        }
        int pkMax = 0;
        for (const auto& s : snaps) {
            if (s.peakIntensity > pkMax) pkMax = s.peakIntensity;
        }

        // Decide which direction we want to push.
        int wantedDir;
        if (pkMax >= AS_PK_SAT)             wantedDir = -1;    // dim
        else if (pkMax < AS_PK_TARGET_LOW)  wantedDir = +1;    // brighten
        else {                                                 // in band
            std::cout << "[AutoSat] tune: PK=" << pkMax
                      << " in band, done" << std::endl;
            return true;
        }

        // If we just flipped direction we're bracketing across the band.
        // Refine the step; if already fine, shift the current rail instead.
        const bool flipped = (prevDir != 0 && wantedDir != prevDir);
        if (flipped) {
            ++flipCount;
            if (expStep > AS_EXP_STEP_FINE + 0.5) {
                expStep = AS_EXP_STEP_FINE;
                std::cout << "[AutoSat] tune: oscillation, narrowing step to "
                          << expStep << " us" << std::endl;
            } else {
                // Even 10us straddles the band → bias the current and reset.
                bool ok = (wantedDir == -1)
                            ? AutoSatDecreaseCurrent(AS_CUR_STEP_MA)
                            : AutoSatIncreaseCurrent(AS_CUR_STEP_MA);
                if (!ok) {
                    std::cout << "[AutoSat] tune: oscillation at fine step "
                              << "and current rail — giving up" << std::endl;
                    return false;
                }
                std::cout << "[AutoSat] tune: oscillation at fine step, "
                          << "current "
                          << (wantedDir == -1 ? "-" : "+") << AS_CUR_STEP_MA
                          << " mA, resetting step" << std::endl;
                expStep = AS_EXP_STEP_COARSE;
                prevDir = 0;
                flipCount = 0;
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(AS_BEAM_POLL_MS));
                continue;
            }
        }

        // Apply an exposure step in the wanted direction; if the rail is
        // already hit, fall back to current adjustment.
        const double signedStep = wantedDir * expStep;
        const double targetExp = m_currentExposure + signedStep;
        const bool railLow  = (wantedDir == -1) && (targetExp < AS_EXP_MIN_US);
        const bool railHigh = (wantedDir == +1) && (targetExp > AS_EXP_MAX_US);

        if (railLow) {
            if (!AutoSatDecreaseCurrent(AS_CUR_STEP_MA)) {
                std::cout << "[AutoSat] tune: cannot dim further (rails)"
                          << std::endl;
                return false;
            }
            std::cout << "[AutoSat] tune: PK=" << pkMax
                      << " exposure floored, current -" << AS_CUR_STEP_MA
                      << " mA" << std::endl;
        } else if (railHigh) {
            if (!AutoSatIncreaseCurrent(AS_CUR_STEP_MA)) {
                std::cout << "[AutoSat] tune: cannot brighten further (rails)"
                          << std::endl;
                return false;
            }
            std::cout << "[AutoSat] tune: PK=" << pkMax
                      << " exposure capped, current +" << AS_CUR_STEP_MA
                      << " mA" << std::endl;
        } else {
            std::cout << "[AutoSat] tune: PK=" << pkMax
                      << (wantedDir < 0 ? " (sat)" : "")
                      << " -> exposure " << (wantedDir < 0 ? "-" : "+")
                      << expStep << " us" << std::endl;
            OnSetExposure(targetExp);
        }

        prevDir = wantedDir;
        std::this_thread::sleep_for(std::chrono::milliseconds(AS_BEAM_POLL_MS));
    }
    std::cout << "[AutoSat] tune: max iters reached without converging"
              << std::endl;
    return false;
}

void SwirBeamWindow::AutoSatFocusSearch()
{
    if (!m_motionManager) {
        std::cout << "[AutoSat] focus: no motion manager — skipping focus search"
                  << std::endl;
        return;
    }

    PositionStruct startPos = m_motionManager->GetCurrentPosition("gantry-main");
    double centerZ = startPos.z;
    std::cout << "[AutoSat] focus: starting at Z=" << centerZ << " mm" << std::endl;

    double bestZ = centerZ;
    int bestPk = -1;

    for (int iter = 0; iter < AS_FOCUS_MAX_ITERS; ++iter) {
        if (m_autoSatAbort.load()) return;

        std::vector<std::pair<double, int>> trace;  // (z, pk)
        for (int k = -AS_Z_HALF_STEPS; k <= AS_Z_HALF_STEPS; ++k) {
            if (m_autoSatAbort.load()) return;
            const double z = centerZ + k * AS_Z_STEP_MM;
            m_motionManager->MoveToAbs("gantry-main",
                                       startPos.x, startPos.y, z,
                                       AS_MOTION_VEL_MMS, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(AS_MOTION_SETTLE_MS));

            auto snaps = GetBeamSnapshots();
            if (snaps.empty()) continue;
            int pk = 0;
            for (const auto& s : snaps) {
                if (s.peakIntensity > pk) pk = s.peakIntensity;
            }

            // Saturated mid-sweep → re-tune, then re-read at this z.
            if (pk >= AS_PK_SAT) {
                std::cout << "[AutoSat] focus: PK=" << pk
                          << " saturated at Z=" << z << " — re-tuning" << std::endl;
                if (!AutoSatTuneExposureCurrent()) return;
                if (m_autoSatAbort.load()) return;
                snaps = GetBeamSnapshots();
                if (snaps.empty()) continue;
                pk = 0;
                for (const auto& s : snaps) {
                    if (s.peakIntensity > pk) pk = s.peakIntensity;
                }
            }
            std::cout << "[AutoSat] focus iter " << iter
                      << "  Z=" << z << " mm  PK=" << pk << std::endl;
            trace.emplace_back(z, pk);
        }
        if (trace.empty()) {
            std::cout << "[AutoSat] focus: no readings — stopping" << std::endl;
            return;
        }

        auto best = std::max_element(trace.begin(), trace.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        bestZ = best->first;
        bestPk = best->second;

        const bool atEdge = (best == trace.begin() || best == trace.end() - 1);
        if (!atEdge) {
            std::cout << "[AutoSat] focus converged at Z=" << bestZ
                      << " mm (PK=" << bestPk << ")" << std::endl;
            break;
        }
        std::cout << "[AutoSat] focus iter " << iter
                  << ": best Z is at sweep edge (Z=" << bestZ
                  << "), shifting and resweeping" << std::endl;
        centerZ = bestZ;
    }

    // Move to the best Z.
    if (m_autoSatAbort.load()) return;
    m_motionManager->MoveToAbs("gantry-main",
                               startPos.x, startPos.y, bestZ,
                               AS_MOTION_VEL_MMS, true);
    std::cout << "[AutoSat] moved to best focus Z=" << bestZ
              << " mm (PK=" << bestPk << ")" << std::endl;
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

void SwirBeamWindow::OnExposureIncrease()
{
    DisableAutoSat();
    double newExposure = m_currentExposure + EXPOSURE_STEP_US;
    OnSetExposure(newExposure);
    InvalidatePanel();
}

void SwirBeamWindow::OnExposureDecrease()
{
    DisableAutoSat();
    double newExposure = m_currentExposure - EXPOSURE_STEP_US;
    if (newExposure < 1.0) newExposure = 1.0;
    OnSetExposure(newExposure);
    InvalidatePanel();
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
        DisableAutoSat();
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
    const int thirdBtnWidth = (btnWidth - 2 * btnSpacing) / 3;

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

    // +10mA / -10mA
    SwirButton btnIncSmall;
    btnIncSmall.rect = {btnLeft, y, btnLeft + halfBtnWidth, y + btnHeight};
    btnIncSmall.label = L"+10mA";
    btnIncSmall.onClick = [this]() { OnCurrentIncreaseSmall(); };
    m_buttons.push_back(btnIncSmall);
    SwirButton btnDecSmall;
    btnDecSmall.rect = {btnLeft + halfBtnWidth + btnSpacing, y, btnLeft + btnWidth, y + btnHeight};
    btnDecSmall.label = L"-10mA";
    btnDecSmall.onClick = [this]() { OnCurrentDecreaseSmall(); };
    m_buttons.push_back(btnDecSmall);
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

    // +50us / -50us
    SwirButton btnExpInc;
    btnExpInc.rect = {btnLeft, y, btnLeft + halfBtnWidth, y + btnHeight};
    btnExpInc.label = L"+50 us";
    btnExpInc.onClick = [this]() { OnExposureIncrease(); };
    m_buttons.push_back(btnExpInc);
    SwirButton btnExpDec;
    btnExpDec.rect = {btnLeft + halfBtnWidth + btnSpacing, y, btnLeft + btnWidth, y + btnHeight};
    btnExpDec.label = L"-50 us";
    btnExpDec.onClick = [this]() { OnExposureDecrease(); };
    m_buttons.push_back(btnExpDec);
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
        b1.onClick = [this, v1]() { DisableAutoSat(); OnSetExposure(v1); };
        m_buttons.push_back(b1);
        SwirButton b2;
        b2.rect = {btnLeft + halfBtnWidth + btnSpacing, y, btnLeft + btnWidth, y + btnHeight};
        b2.label = presets[i + 1].label;
        double v2 = presets[i + 1].value;
        b2.onClick = [this, v2]() { DisableAutoSat(); OnSetExposure(v2); };
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

    // === TOOLS Group (3-column layout) ===
    y += titleH + groupPad;

    // Column positions
    int col1 = btnLeft;
    int col2 = btnLeft + thirdBtnWidth + btnSpacing;
    int col3 = btnLeft + 2 * (thirdBtnWidth + btnSpacing);

    // Row 1: SWIR HEAD | CLICK TO MOVE | BF
    {
        SwirButton headBtn;
        headBtn.rect = {col1, y, col1 + thirdBtnWidth, y + btnHeight};
        headBtn.label = L"HEAD UP";
        headBtn.isToggle = true;
        headBtn.tag = TAG_SWIR_HEAD;
        headBtn.onClick = [this]() { OnSwirHeadToggle(); };
        m_buttons.push_back(headBtn);

        SwirButton ctmBtn;
        ctmBtn.rect = {col2, y, col2 + thirdBtnWidth, y + btnHeight};
        ctmBtn.label = L"CLICK MOVE";
        ctmBtn.isToggle = true;
        ctmBtn.tag = TAG_CLICK_TO_MOVE;
        ctmBtn.toggled = m_clickToMoveEnabled;
        ctmBtn.onClick = [this]() {
            m_clickToMoveEnabled = !m_clickToMoveEnabled;
            InvalidatePanel();
            std::cout << "[SwirBeamWindow] Click-to-move " << (m_clickToMoveEnabled ? "enabled" : "disabled") << std::endl;
        };
        m_buttons.push_back(ctmBtn);

        SwirButton bfBtn;
        bfBtn.rect = {col3, y, col3 + thirdBtnWidth, y + btnHeight};
        bfBtn.label = L"BF";
        bfBtn.isToggle = true;
        bfBtn.toggled = m_beamFocus && m_beamFocus->IsRunning();
        bfBtn.tag = TAG_BEAM_FOCUS;
        bfBtn.onClick = [this]() { OnBeamFocus(); };
        m_buttons.push_back(bfBtn);
    }
    y += btnHeight + btnSpacing;

    // Row 2: PLACE MARKER | CLEAR ALL | SET MAG
    {
        SwirButton placeBtn;
        placeBtn.rect = {col1, y, col1 + thirdBtnWidth, y + btnHeight};
        placeBtn.label = L"MARKER";
        placeBtn.tag = TAG_PLACE_MARKER;
        placeBtn.onClick = [this]() { OnPlaceMarker(); };
        m_buttons.push_back(placeBtn);

        SwirButton clearBtn;
        clearBtn.rect = {col2, y, col2 + thirdBtnWidth, y + btnHeight};
        clearBtn.label = L"CLEAR ALL";
        clearBtn.tag = TAG_CLEAR_MARKERS;
        clearBtn.onClick = [this]() { OnClearAllMarkers(); };
        m_buttons.push_back(clearBtn);

        SwirButton magBtn;
        magBtn.rect = {col3, y, col3 + thirdBtnWidth, y + btnHeight};
        magBtn.label = L"SET MAG";
        magBtn.tag = TAG_SET_MAG;
        magBtn.onClick = [this]() { OnSetMagnification(); };
        m_buttons.push_back(magBtn);
    }
    y += btnHeight + btnSpacing;

    // Row 3: BEAM DETECT | BEAM CONFIG | BEAM OPT
    {
        SwirButton beamBtn;
        beamBtn.rect = {col1, y, col1 + thirdBtnWidth, y + btnHeight};
        beamBtn.label = m_useBlobDetection ? L"B BLOB" : L"B DETECT";
        beamBtn.isToggle = true;
        beamBtn.toggled = m_beamDetectionEnabled;
        beamBtn.tag = TAG_BEAM_DETECT;
        beamBtn.onClick = [this]() { OnBeamDetectToggle(); };
        m_buttons.push_back(beamBtn);

        SwirButton configBtn;
        configBtn.rect = {col2, y, col2 + thirdBtnWidth, y + btnHeight};
        configBtn.label = L"B CONFIG";
        configBtn.tag = TAG_BEAM_CONFIG;
        configBtn.onClick = [this]() { OnEditBeamConfig(); };
        m_buttons.push_back(configBtn);

        SwirButton optimizeBtn;
        optimizeBtn.rect = {col3, y, col3 + thirdBtnWidth, y + btnHeight};
        optimizeBtn.label = L"B OPTIM";
        optimizeBtn.tag = TAG_BEAM_OPTIMIZE;
        optimizeBtn.onClick = [this]() { OnBeamOptimize(); };
        m_buttons.push_back(optimizeBtn);
    }
    y += btnHeight + btnSpacing;

    // Row 4: SAVE IMG (raw only) | SAVE PKG (raw + overlay + beams + collage)
    {
        SwirButton saveBtn;
        saveBtn.rect = {btnLeft, y, btnLeft + halfBtnWidth, y + btnHeight};
        saveBtn.label = L"SAVE IMG";
        saveBtn.tag = TAG_SAVE_IMAGE;
        saveBtn.onClick = [this]() { OnSaveImage(); };
        m_buttons.push_back(saveBtn);

        SwirButton savePkgBtn;
        savePkgBtn.rect = {btnLeft + halfBtnWidth + btnSpacing, y,
                           btnLeft + btnWidth, y + btnHeight};
        savePkgBtn.label = L"SAVE PKG";
        savePkgBtn.tag = TAG_SAVE_PACKAGE;
        savePkgBtn.onClick = [this]() { OnSaveImagePackage(); };
        m_buttons.push_back(savePkgBtn);
    }
    y += btnHeight + btnSpacing;

    // Row 5: AUTO SAT (full width toggle)
    {
        SwirButton autoSatBtn;
        autoSatBtn.rect = {btnLeft, y, btnLeft + btnWidth, y + btnHeight};
        autoSatBtn.label = m_autoSatRunning.load() ? L"STOP" : L"AUTO SAT";
        autoSatBtn.tag = TAG_AUTO_SAT;
        autoSatBtn.isToggle = true;
        autoSatBtn.toggled = m_autoSatRunning.load();
        autoSatBtn.onClick = [this]() { OnAutoSatToggle(); };
        m_buttons.push_back(autoSatBtn);
    }
    y += btnHeight + btnSpacing;

    y += groupPad;

    y += sectionGap;

    // === TEC Group (2-column: 25C | 30C, then temp reading label) ===
    y += titleH + groupPad;

    {
        SwirButton tec25Btn;
        tec25Btn.rect = {btnLeft, y, btnLeft + halfBtnWidth, y + btnHeight};
        tec25Btn.label = L"25\u00B0C";
        tec25Btn.tag = TAG_TEC_25C;
        tec25Btn.onClick = [this]() { OnTECPreset(25.0); };
        m_buttons.push_back(tec25Btn);

        SwirButton tec30Btn;
        tec30Btn.rect = {btnLeft + halfBtnWidth + btnSpacing, y, btnLeft + btnWidth, y + btnHeight};
        tec30Btn.label = L"30\u00B0C";
        tec30Btn.tag = TAG_TEC_30C;
        tec30Btn.onClick = [this]() { OnTECPreset(30.0); };
        m_buttons.push_back(tec30Btn);
    }
    y += btnHeight + btnSpacing;

    // Temperature reading label space
    y += labelHeight;

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
    } else if (btn.tag == TAG_BEAM_FOCUS) {
        // BF button: red when running (to indicate "click to stop"), normal otherwise
        if (btn.toggled) {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? Gdiplus::Color(255, 200, 70, 70) : Gdiplus::Color(255, 180, 40, 40));
        } else {
            bgColor = btn.pressed ? CLR_BTN_PRESSED : (btn.hovered ? CLR_BTN_HOVER : CLR_BTN_NORMAL);
        }
    } else if (btn.tag == TAG_AUTO_EXP || btn.tag == TAG_CLICK_TO_MOVE
               || btn.tag == TAG_AUTO_SAT) {
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
    static Gdiplus::Font font(L"Segoe UI", 8.5f, Gdiplus::FontStyleBold);
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
    m_hasNewFrame.store(false);  // Allow grab thread to write next frame
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

    // Fonts (static — created once, reused every frame)
    static Gdiplus::Font labelFont(L"Segoe UI", 8.0f, Gdiplus::FontStyleRegular);
    static Gdiplus::Font valueFont(L"Consolas", 9.0f, Gdiplus::FontStyleBold);
    static Gdiplus::Font smallFont(L"Segoe UI", 7.5f);
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
    y += btnHeight + btnSpacing;  // +10/-10

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
        if (btn.tag == TAG_AUTO_SAT) {
            btn.toggled = m_autoSatRunning.load();
            btn.label = btn.toggled ? L"STOP" : L"AUTO SAT";
        }
        if (btn.tag == TAG_SWIR_HEAD) {
            btn.toggled = swirHeadDown;
            btn.label = swirHeadDown ? L"HEAD UP" : L"HEAD DN";
        }
        if (btn.tag == TAG_BEAM_DETECT) {
            btn.toggled = m_beamDetectionEnabled;
            btn.label = m_useBlobDetection ? L"B BLOB" : L"B DETECT";
        }
        if (btn.tag == TAG_BEAM_FOCUS) {
            btn.toggled = m_beamFocus && m_beamFocus->IsRunning();
            btn.label = btn.toggled ? L"STOP" : L"BF";
        }
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
    y += btnHeight + btnSpacing;        // +50us/-50us
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

    // --- Tools Group Box (3x3 grid) ---
    y += sectionGap;
    int toolsGroupTop = y;
    y += titleH + groupPad;
    y += btnHeight + btnSpacing;  // Row 1: HEAD | CLICK MOVE | BF
    y += btnHeight + btnSpacing;  // Row 2: MARKER | CLEAR ALL | SET MAG
    y += btnHeight + btnSpacing;  // Row 3: B DETECT | B CONFIG | B OPTIM
    y += btnHeight + btnSpacing;  // Row 4: SAVE IMG | SAVE PKG
    y += btnHeight + btnSpacing;  // Row 5: AUTO SAT
    y += groupPad;

    RECT toolsGroupRect = { panelRect.left + margin, toolsGroupTop,
                            panelRect.right - margin, y };
    DrawGroupBox(gfx, toolsGroupRect, L"TOOLS");

    // --- TEC Group Box ---
    y += sectionGap;
    int tecGroupTop = y;
    y += titleH + groupPad;
    y += btnHeight + btnSpacing;  // Row: 25°C | 30°C
    float tecReadY = static_cast<float>(y);  // temperature reading position
    y += labelHeight;
    y += groupPad;

    RECT tecGroupRect = { panelRect.left + margin, tecGroupTop,
                          panelRect.right - margin, y };
    DrawGroupBox(gfx, tecGroupRect, L"TEC");

    // Draw TEC status: indicator dot + temperature
    {
        if (m_tecConnected) {
            // Status indicator: green circle = ON, red circle = OFF
            float dotX = innerX + 2.0f;
            float dotY = tecReadY + 2.0f;
            float dotSize = 10.0f;
            if (m_tecOutputOn) {
                Gdiplus::SolidBrush greenDot(Gdiplus::Color(255, 0, 220, 80));
                gfx.FillEllipse(&greenDot, dotX, dotY, dotSize, dotSize);
            } else {
                Gdiplus::SolidBrush redDot(Gdiplus::Color(255, 200, 60, 60));
                gfx.FillEllipse(&redDot, dotX, dotY, dotSize, dotSize);
            }

            // Temperature + ON/OFF text
            std::wostringstream tempStr;
            tempStr << (m_tecOutputOn ? L"ON  " : L"OFF  ")
                    << std::fixed << std::setprecision(1) << m_tecTemperature << L" \u00B0C";
            Gdiplus::SolidBrush tempBrush(m_tecOutputOn
                ? Gdiplus::Color(255, 0, 255, 100)
                : Gdiplus::Color(255, 200, 120, 60));
            gfx.DrawString(tempStr.str().c_str(), -1, &smallFont,
                           Gdiplus::RectF(innerX + dotSize + 6.0f, tecReadY, innerW - dotSize - 6.0f, static_cast<float>(labelHeight)),
                           &leftFormat, &tempBrush);
        } else {
            Gdiplus::SolidBrush dimBrushLocal(Gdiplus::Color(255, 128, 128, 128));
            gfx.DrawString(L"Not connected", -1, &smallFont,
                           Gdiplus::RectF(innerX, tecReadY, innerW, static_cast<float>(labelHeight)),
                           &leftFormat, &dimBrushLocal);
        }
    }

    // Draw all buttons
    for (const auto& btn : m_buttons) {
        DrawButton(gfx, btn);
    }
}

void SwirBeamWindow::DrawOverlays(HDC hdc)
{
    if (m_lastRenderW <= 0 || m_lastRenderH <= 0) return;

    Gdiplus::Graphics gfx(hdc);
    if (m_beamDetectionEnabled) {
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeHighSpeed);
        gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintSystemDefault);
    } else {
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    }

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
        static Gdiplus::Font font(L"Consolas", 9.5f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255, 255));
        gfx.DrawString(dxBuf, -1, &font, Gdiplus::PointF(textX, textY), &textBrush);
        gfx.DrawString(dyBuf, -1, &font, Gdiplus::PointF(textX, textY + 17.0f), &textBrush);
    }

    // Beam detection overlays
    if (m_beamDetectionEnabled) {
        std::lock_guard<std::mutex> lock(m_beamMutex);
        if (!m_beamDisplayResults.empty()) {
            DrawBeamOverlays(gfx, m_beamDisplayResults,
                             m_lastRenderX, m_lastRenderY, m_lastRenderW, m_lastRenderH,
                             m_frameWidth, m_frameHeight, m_useBlobDetection,
                             m_beamConfig.micronsPerPixel);
        }

        // CE/EE charts (drawn even when no current detection — shows previous data)
        if (m_ceHistoryCount[0] > 0 || m_ceHistoryCount[1] > 0) {
            DrawCECharts(gfx, m_ceHistory, m_eeHistory,
                         m_ceHistoryIndex, m_ceHistoryCount,
                         CE_HISTORY_SIZE,
                         m_lastRenderX, m_lastRenderY, m_lastRenderW, m_lastRenderH,
                         m_useBlobDetection);
        }
    }

    // Info overlay - lower left corner of viewport (cal, mag, FPS stacked)
    {
        static Gdiplus::Font infoFont(L"Consolas", 9.0f, Gdiplus::FontStyleBold);
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

    const auto markers = m_markerStore ? m_markerStore->Snapshot()
                                        : std::vector<SwirWorldMarker>{};
    for (const auto& marker : markers) {
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

    if (m_markerStore && m_markerStore->Place(type)) {
        m_markerStore->SaveToFile();
    }
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SwirBeamWindow::OnClearAllMarkers()
{
    int result = MessageBoxW(m_hwnd,
        L"Clear all markers?", L"Clear Markers", MB_YESNO | MB_ICONQUESTION);
    if (result != IDYES) return;

    if (m_markerStore) {
        m_markerStore->ClearAll();
        m_markerStore->SaveToFile();
    }
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SwirBeamWindow::SaveMarkersToFile()
{
    if (m_markerStore) m_markerStore->SaveToFile();
}

void SwirBeamWindow::LoadMarkersFromFile()
{
    if (m_markerStore) m_markerStore->LoadFromFile();
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

// ============================================================================
// Save Image
// ============================================================================

namespace {
struct SaveImageDlgState {
    bool useTimestamp = true;
    std::wstring customName;
};

constexpr WORD ID_RADIO_TIMESTAMP = 201;
constexpr WORD ID_RADIO_CUSTOM    = 202;
constexpr WORD ID_EDIT_NAME       = 203;
}

static INT_PTR CALLBACK SaveImageDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_INITDIALOG: {
        auto* state = reinterpret_cast<SaveImageDlgState*>(lParam);
        SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        CheckRadioButton(hDlg, ID_RADIO_TIMESTAMP, ID_RADIO_CUSTOM, ID_RADIO_TIMESTAMP);
        EnableWindow(GetDlgItem(hDlg, ID_EDIT_NAME), FALSE);
        SetFocus(GetDlgItem(hDlg, ID_RADIO_TIMESTAMP));
        return FALSE;
    }
    case WM_COMMAND: {
        const WORD id = LOWORD(wParam);
        if (id == ID_RADIO_TIMESTAMP || id == ID_RADIO_CUSTOM) {
            BOOL custom = (id == ID_RADIO_CUSTOM);
            EnableWindow(GetDlgItem(hDlg, ID_EDIT_NAME), custom);
            if (custom) {
                SetFocus(GetDlgItem(hDlg, ID_EDIT_NAME));
                SendDlgItemMessageW(hDlg, ID_EDIT_NAME, EM_SETSEL, 0, -1);
            }
            return TRUE;
        }
        if (id == IDOK) {
            auto* state = reinterpret_cast<SaveImageDlgState*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
            BOOL custom = (IsDlgButtonChecked(hDlg, ID_RADIO_CUSTOM) == BST_CHECKED);
            state->useTimestamp = !custom;
            if (custom) {
                wchar_t buf[256] = {};
                GetDlgItemTextW(hDlg, ID_EDIT_NAME, buf, 256);
                std::wstring name(buf);
                // Trim trailing/leading whitespace
                while (!name.empty() && iswspace(name.back())) name.pop_back();
                while (!name.empty() && iswspace(name.front())) name.erase(name.begin());
                if (name.empty()) {
                    MessageBoxW(hDlg, L"Please enter a file name.", L"Save Image",
                                MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hDlg, ID_EDIT_NAME));
                    return TRUE;
                }
                // Reject path separators and other illegal chars
                if (name.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
                    MessageBoxW(hDlg, L"Name contains illegal characters: \\ / : * ? \" < > |",
                                L"Save Image", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hDlg, ID_EDIT_NAME));
                    return TRUE;
                }
                state->customName = name;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        break;
    }
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void SwirBeamWindow::OnSaveImage()
{
    // Confirm a frame is available before showing the dialog
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (m_frameWidth == 0 || m_frameHeight == 0 || m_frameBuffer.empty()) {
            MessageBoxW(m_hwnd, L"No camera frame available to save.",
                        L"Save Image", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    // Build dialog template: title + 2 radio buttons + edit + OK/Cancel = 6 controls
    alignas(4) BYTE dlgBuf[1024] = {};
    DLGTEMPLATE* dlg = reinterpret_cast<DLGTEMPLATE*>(dlgBuf);
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
    dlg->cdit = 6;
    dlg->cx = 200;
    dlg->cy = 90;

    WORD* ptr = reinterpret_cast<WORD*>(dlg + 1);
    *ptr++ = 0;
    *ptr++ = 0;
    const wchar_t* title = L"Save SWIR Image";
    for (const wchar_t* c = title; *c; ++c) *ptr++ = *c;
    *ptr++ = 0;
    *ptr++ = 9;
    const wchar_t* fontName = L"Segoe UI";
    for (const wchar_t* c = fontName; *c; ++c) *ptr++ = *c;
    *ptr++ = 0;

    auto addCtrl = [&](DWORD style, short x, short y, short cx, short cy, WORD id,
                       const wchar_t* cls, const wchar_t* text) {
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

    addCtrl(SS_LEFT, 8, 6, 184, 10, 0xFFFF, L"Static", L"Choose how to name the saved file:");
    addCtrl(BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 8, 20, 184, 12,
            ID_RADIO_TIMESTAMP, L"Button", L"Use timestamp (e.g. swir_20260424_153012.png)");
    addCtrl(BS_AUTORADIOBUTTON | WS_TABSTOP, 8, 36, 184, 12,
            ID_RADIO_CUSTOM, L"Button", L"Custom name:");
    addCtrl(ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 16, 50, 176, 14,
            ID_EDIT_NAME, L"Edit", L"");
    addCtrl(BS_DEFPUSHBUTTON | WS_TABSTOP, 78, 70, 50, 16, IDOK, L"Button", L"Save");
    addCtrl(WS_TABSTOP, 138, 70, 54, 16, IDCANCEL, L"Button", L"Cancel");

    SaveImageDlgState state;
    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandle(nullptr), dlg, m_hwnd, SaveImageDlgProc,
        reinterpret_cast<LPARAM>(&state));

    if (result != IDOK) return;

    // Build output filename
    std::wstring fileName;
    if (state.useTimestamp) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"swir_%04u%02u%02u_%02u%02u%02u.png",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fileName = buf;
    } else {
        fileName = state.customName;
        // Append .png if no extension provided
        size_t dotPos = fileName.find_last_of(L'.');
        size_t sepPos = fileName.find_last_of(L"\\/");
        if (dotPos == std::wstring::npos || (sepPos != std::wstring::npos && dotPos < sepPos)) {
            fileName += L".png";
        }
    }

    // Output directory: logs/swir_captures relative to the executable working dir
    std::filesystem::path outDir = std::filesystem::path("logs") / "swir_captures";
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    std::filesystem::path outPath = outDir / fileName;

    if (!SaveFrameAsPng(outPath.wstring())) {
        std::wstring msg = L"Failed to save image to:\n" + outPath.wstring();
        MessageBoxW(m_hwnd, msg.c_str(), L"Save Image", MB_OK | MB_ICONERROR);
        return;
    }

    std::cout << "[SwirBeamWindow] Saved frame to " << outPath.string() << std::endl;
}

void SwirBeamWindow::OnSaveImagePackage()
{
    // Confirm a frame is available before showing the dialog.
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (m_frameWidth == 0 || m_frameHeight == 0 || m_frameBuffer.empty()) {
            MessageBoxW(m_hwnd, L"No camera frame available to save.",
                        L"Save Image Package", MB_OK | MB_ICONINFORMATION);
            return;
        }
    }

    // Reuse the SAVE IMG dialog: same timestamp/custom-name choice, but the
    // entered text is interpreted as a *prefix* (no extension) instead of a
    // full filename, and we save 5 PNGs into a single subfolder.
    alignas(4) BYTE dlgBuf[1024] = {};
    DLGTEMPLATE* dlg = reinterpret_cast<DLGTEMPLATE*>(dlgBuf);
    dlg->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT;
    dlg->cdit = 6;
    dlg->cx = 220;
    dlg->cy = 90;

    WORD* ptr = reinterpret_cast<WORD*>(dlg + 1);
    *ptr++ = 0;
    *ptr++ = 0;
    const wchar_t* title = L"Save Image Package";
    for (const wchar_t* c = title; *c; ++c) *ptr++ = *c;
    *ptr++ = 0;
    *ptr++ = 9;
    const wchar_t* fontName = L"Segoe UI";
    for (const wchar_t* c = fontName; *c; ++c) *ptr++ = *c;
    *ptr++ = 0;

    auto addCtrl = [&](DWORD style, short x, short y, short cx, short cy, WORD id,
                       const wchar_t* cls, const wchar_t* text) {
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

    addCtrl(SS_LEFT, 8, 6, 204, 10, 0xFFFF, L"Static",
            L"Saves raw + overlay + B0 + B1 + collage as 5 PNGs.");
    addCtrl(BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 8, 20, 204, 12,
            201, L"Button", L"Use timestamp prefix (e.g. swir_20260425_153012)");
    addCtrl(BS_AUTORADIOBUTTON | WS_TABSTOP, 8, 36, 204, 12,
            202, L"Button", L"Custom prefix:");
    addCtrl(ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 16, 50, 196, 14,
            203, L"Edit", L"");
    addCtrl(BS_DEFPUSHBUTTON | WS_TABSTOP, 88, 70, 60, 16, IDOK, L"Button", L"Save All");
    addCtrl(WS_TABSTOP, 152, 70, 60, 16, IDCANCEL, L"Button", L"Cancel");

    struct PkgState { bool useTimestamp = true; std::wstring customPrefix; };
    PkgState state;

    auto pkgProc = [](HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) -> INT_PTR {
        switch (msg) {
        case WM_INITDIALOG: {
            SetWindowLongPtr(hDlg, GWLP_USERDATA, static_cast<LONG_PTR>(lParam));
            CheckRadioButton(hDlg, 201, 202, 201);
            EnableWindow(GetDlgItem(hDlg, 203), FALSE);
            SetFocus(GetDlgItem(hDlg, 201));
            return FALSE;
        }
        case WM_COMMAND: {
            const WORD id = LOWORD(wParam);
            if (id == 201 || id == 202) {
                BOOL custom = (id == 202);
                EnableWindow(GetDlgItem(hDlg, 203), custom);
                if (custom) {
                    SetFocus(GetDlgItem(hDlg, 203));
                    SendDlgItemMessageW(hDlg, 203, EM_SETSEL, 0, -1);
                }
                return TRUE;
            }
            if (id == IDOK) {
                auto* st = reinterpret_cast<PkgState*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
                BOOL custom = (IsDlgButtonChecked(hDlg, 202) == BST_CHECKED);
                st->useTimestamp = !custom;
                if (custom) {
                    wchar_t buf[256] = {};
                    GetDlgItemTextW(hDlg, 203, buf, 256);
                    std::wstring n(buf);
                    while (!n.empty() && iswspace(n.back())) n.pop_back();
                    while (!n.empty() && iswspace(n.front())) n.erase(n.begin());
                    if (n.empty()) {
                        MessageBoxW(hDlg, L"Please enter a prefix.",
                                    L"Save Image Package", MB_OK | MB_ICONWARNING);
                        SetFocus(GetDlgItem(hDlg, 203));
                        return TRUE;
                    }
                    if (n.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
                        MessageBoxW(hDlg, L"Prefix contains illegal characters: \\ / : * ? \" < > |",
                                    L"Save Image Package", MB_OK | MB_ICONWARNING);
                        SetFocus(GetDlgItem(hDlg, 203));
                        return TRUE;
                    }
                    st->customPrefix = n;
                }
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
            break;
        }
        case WM_CLOSE: EndDialog(hDlg, IDCANCEL); return TRUE;
        }
        return FALSE;
    };

    INT_PTR result = DialogBoxIndirectParamW(
        GetModuleHandle(nullptr), dlg, m_hwnd, pkgProc,
        reinterpret_cast<LPARAM>(&state));
    if (result != IDOK) return;

    // Build prefix.
    std::wstring prefix;
    if (state.useTimestamp) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t buf[64];
        swprintf_s(buf, L"swir_%04u%02u%02u_%02u%02u%02u",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        prefix = buf;
    } else {
        prefix = state.customPrefix;
    }

    // Output directory: logs/swir_captures/<prefix>/
    std::filesystem::path outDir = std::filesystem::path("logs") / "swir_captures" / prefix;
    std::vector<std::wstring> savedFiles;
    bool ok = SaveImagePackage(outDir.wstring(), prefix, &savedFiles);

    if (savedFiles.empty()) {
        MessageBoxW(m_hwnd,
                    L"Failed to save image package (no files written).",
                    L"Save Image Package", MB_OK | MB_ICONERROR);
        return;
    }

    std::wstring msg = L"Saved " + std::to_wstring(savedFiles.size()) +
                       L" file(s) to:\n" + outDir.wstring();
    if (!ok) msg += L"\n\n(Some files failed — see console.)";
    MessageBoxW(m_hwnd, msg.c_str(), L"Save Image Package",
                ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONWARNING);

    std::cout << "[SwirBeamWindow] Image package saved to " << outDir.string()
              << " (" << savedFiles.size() << " files)" << std::endl;
}

bool SwirBeamWindow::SaveFrameAsPng(const std::wstring& fullPath)
{
    return m_imageSaver && m_imageSaver->SaveFrameAsPng(fullPath);
}

bool SwirBeamWindow::SaveFrameWithOverlayAsPng(const std::wstring& fullPath)
{
    return m_imageSaver && m_imageSaver->SaveFrameWithOverlayAsPng(fullPath);
}

bool SwirBeamWindow::SaveBeamCropAsPng(int slotIndex, const std::wstring& fullPath)
{
    return m_imageSaver && m_imageSaver->SaveBeamCropAsPng(slotIndex, fullPath);
}

bool SwirBeamWindow::SaveCollageAsPng(const std::wstring& fullPath)
{
    return m_imageSaver && m_imageSaver->SaveCollageAsPng(fullPath);
}

bool SwirBeamWindow::SaveImagePackage(const std::wstring& dirPath,
                                      const std::wstring& namePrefix,
                                      std::vector<std::wstring>* outFiles)
{
    return m_imageSaver
        && m_imageSaver->SaveImagePackage(dirPath, namePrefix, outFiles);
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

    InvalidatePanel();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

// ============================================================================
// Beam Detection
// ============================================================================

// ============================================================================
// Beam Detection Config (JSON persistence)
// ============================================================================

void SwirBeamWindow::LoadBeamConfig()
{
    SwirBeamConfigStore::Load(m_beamConfig, m_blobConfig, m_halconConfig,
                              m_useBlobDetection, m_useHalconDetection,
                              m_trackingEnabled, m_trackingMaxJumpPx,
                              m_trackingStaleMax);
}

void SwirBeamWindow::SaveBeamConfig()
{
    SwirBeamConfigStore::Save(m_beamConfig, m_blobConfig, m_halconConfig,
                              m_useBlobDetection, m_useHalconDetection,
                              m_trackingEnabled, m_trackingMaxJumpPx,
                              m_trackingStaleMax);
}

void SwirBeamWindow::OnEditBeamConfig()
{
    // Save current config first so the file is up to date
    SaveBeamConfig();

    // Get absolute path to local config file (user-editable)
    std::string path = "config\\beam_detection.local.json";
    char fullPath[MAX_PATH] = {};
    GetFullPathNameA(path.c_str(), MAX_PATH, fullPath, nullptr);

    // Launch notepad and get process handle
    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "open";
    sei.lpFile = "notepad.exe";
    sei.lpParameters = fullPath;
    sei.nShow = SW_SHOW;

    if (!ShellExecuteExA(&sei) || !sei.hProcess) {
        std::cerr << "[SwirBeamWindow] Failed to open notepad" << std::endl;
        return;
    }

    std::cout << "[SwirBeamWindow] Editing beam config: " << fullPath << std::endl;
    std::cout << "[SwirBeamWindow] Config will reload automatically when notepad closes" << std::endl;

    // Wait for notepad to close on a background thread, then reload config
    HANDLE hProc = sei.hProcess;
    if (m_configReloadFuture.valid()) {
        m_configReloadFuture.wait();
    }
    m_configReloadFuture = std::async(std::launch::async, [this, hProc]() {
        WaitForSingleObject(hProc, INFINITE);
        CloseHandle(hProc);
        std::cout << "[SwirBeamWindow] Notepad closed, reloading beam config..." << std::endl;
        LoadBeamConfig();
        std::cout << "[SwirBeamWindow] Beam config reloaded" << std::endl;
    });
}

void SwirBeamWindow::OnBeamDetectToggle()
{
    m_beamDetectionEnabled = !m_beamDetectionEnabled;
    std::cout << "[SwirBeamWindow] Beam detection "
              << (m_beamDetectionEnabled ? "enabled" : "disabled") << std::endl;

    if (m_beamDetectionEnabled) {
        LoadBeamConfig();  // reload config each time detection starts
        StartDetectionThread();
    } else {
        StopDetectionThread();
        std::lock_guard<std::mutex> lock(m_beamMutex);
        m_beamResults.clear();
    }
    InvalidatePanel();
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SwirBeamWindow::StartDetectionThread()
{
    if (m_detectionThread.joinable()) return;  // already running

    m_detectionShutdown.store(false);
    m_detectionReady.store(false);

    // Reset display smoothing
    m_beamDisplayResults.clear();

    // Reset CE/EE history ring buffers
    for (int i = 0; i < 2; ++i) {
        m_ceHistory[i].assign(CE_HISTORY_SIZE, 0.0);
        m_eeHistory[i].assign(CE_HISTORY_SIZE, 0.0);
        m_ceHistoryIndex[i] = 0;
        m_ceHistoryCount[i] = 0;
        m_ceRejectCount[i] = 0;
        m_prevBeams[i] = PrevBeamSlot{};
    }
    m_detectMissCount = 0;

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

            // Run detection on single frame (raw, no averaging).
            // Mode priority: Halcon > Blob > D4sigma (toggled via JSON config).
            std::vector<BeamDetectionResult> results;
            const auto detectStart = std::chrono::steady_clock::now();
#ifdef HAS_HALCON
            if (m_useHalconDetection) {
                results = RunHalconBeamDetection(localBuffer.data(), localW, localH, m_halconConfig);
            } else
#endif
            if (m_useBlobDetection) {
                results = RunBlobDetection(localBuffer.data(), localW, localH, m_blobConfig);
            } else {
                results = RunBeamDetection(localBuffer.data(), localW, localH, m_beamConfig);
            }
            const double detectMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - detectStart).count();

            // Histogram: keep last DETECT_TIMING_SIZE timings, log mean+p95 every 100.
            m_detectTimings[m_detectTimingIdx] = detectMs;
            m_detectTimingIdx = (m_detectTimingIdx + 1) % DETECT_TIMING_SIZE;
            if (m_detectTimingCount < DETECT_TIMING_SIZE) ++m_detectTimingCount;
            if (++m_detectStatsCount >= 100 && m_detectTimingCount > 0) {
                m_detectStatsCount = 0;
                std::vector<double> sorted(m_detectTimings.begin(),
                                           m_detectTimings.begin() + m_detectTimingCount);
                std::sort(sorted.begin(), sorted.end());
                double sum = 0.0;
                for (double v : sorted) sum += v;
                double mean = sum / sorted.size();
                size_t p95idx = static_cast<size_t>(sorted.size() * 0.95);
                if (p95idx >= sorted.size()) p95idx = sorted.size() - 1;
                std::cout << "[BeamDetect] detect-time mean=" << std::fixed
                          << std::setprecision(1) << mean
                          << "ms  p95=" << sorted[p95idx]
                          << "ms  max=" << sorted.back()
                          << "ms  (n=" << sorted.size() << ")"
                          << std::endl;
            }

            // Miss tracking (counter retained for stats; per-frame "miss x1"
            // and "recovered after N" lines are intentionally not logged
            // — they were too chatty in normal operation).
            {
                int expected = m_useHalconDetection ? m_halconConfig.maxBeams
                             : m_useBlobDetection   ? m_blobConfig.maxBeams
                                                    : m_beamConfig.maxBeams;
                int actual = static_cast<int>(results.size());
                if (actual < expected) ++m_detectMissCount;
                else if (m_detectMissCount > 0) m_detectMissCount = 0;
            }

            // Beam ID tracking: re-order `results` so slot 0 / slot 1 keep
            // following the same physical beam across frames. Detector returns
            // candidates cy-sorted; the caller decides which slot each one
            // occupies based on prior-frame centroids. Single-result frames
            // skip the reorder and stay in slot 0 — preserving the existing
            // index==slot invariant the CE/EE history loop depends on.
            auto sqDist = [](double ax, double ay, double bx, double by) {
                double dx = ax - bx, dy = ay - by;
                return dx * dx + dy * dy;
            };

            if (m_trackingEnabled && results.size() >= 2 &&
                m_prevBeams[0].valid && m_prevBeams[1].valid) {
                const double d00 = sqDist(results[0].d4.cx, results[0].d4.cy,
                                          m_prevBeams[0].cx, m_prevBeams[0].cy);
                const double d11 = sqDist(results[1].d4.cx, results[1].d4.cy,
                                          m_prevBeams[1].cx, m_prevBeams[1].cy);
                const double d01 = sqDist(results[0].d4.cx, results[0].d4.cy,
                                          m_prevBeams[1].cx, m_prevBeams[1].cy);
                const double d10 = sqDist(results[1].d4.cx, results[1].d4.cy,
                                          m_prevBeams[0].cx, m_prevBeams[0].cy);
                const bool useSwapped = (d01 + d10) < (d00 + d11);
                const double maxJump2 = m_trackingMaxJumpPx * m_trackingMaxJumpPx;
                const double chosenWorst = useSwapped
                    ? std::max(d01, d10) : std::max(d00, d11);

                if (chosenWorst > maxJump2) {
                    // Even the best pairing is implausibly far — prior is
                    // stale (e.g. a single-beam frame poisoned a slot).
                    // Don't trust the swap decision; keep detector cy-sort
                    // and reset prev so next frame re-seeds clean.
                    std::cout << "[BeamDetect] tracking jump > " << m_trackingMaxJumpPx
                              << "px (worst=" << std::sqrt(chosenWorst)
                              << "px) - prior reset, keeping cy-sort"
                              << std::endl;
                    m_prevBeams[0].valid = false;
                    m_prevBeams[1].valid = false;
                } else {
                    if (useSwapped) std::swap(results[0], results[1]);
                    results[0].label = "B0";
                    results[1].label = "B1";
                }
            }

            // Update prev-beam slots from the (possibly re-ordered) results.
            // For single-result frames, pick the slot whose prior centroid is
            // closer to the lone candidate so a missed-top frame doesn't
            // poison slot 0 with the bottom beam's position.
            std::array<bool, 2> updated = {false, false};
            if (results.size() >= 2) {
                for (int i = 0; i < 2; ++i) {
                    m_prevBeams[i].cx = results[i].d4.cx;
                    m_prevBeams[i].cy = results[i].d4.cy;
                    m_prevBeams[i].valid = true;
                    m_prevBeams[i].staleFrames = 0;
                    updated[i] = true;
                }
            } else if (results.size() == 1) {
                int targetSlot = 0;
                if (m_prevBeams[0].valid && m_prevBeams[1].valid) {
                    const double d0 = sqDist(results[0].d4.cx, results[0].d4.cy,
                                             m_prevBeams[0].cx, m_prevBeams[0].cy);
                    const double d1 = sqDist(results[0].d4.cx, results[0].d4.cy,
                                             m_prevBeams[1].cx, m_prevBeams[1].cy);
                    targetSlot = (d1 < d0) ? 1 : 0;
                } else if (m_prevBeams[1].valid && !m_prevBeams[0].valid) {
                    targetSlot = 1;
                }
                m_prevBeams[targetSlot].cx = results[0].d4.cx;
                m_prevBeams[targetSlot].cy = results[0].d4.cy;
                m_prevBeams[targetSlot].valid = true;
                m_prevBeams[targetSlot].staleFrames = 0;
                updated[targetSlot] = true;
            }
            for (int i = 0; i < 2; ++i) {
                if (!updated[i] && m_prevBeams[i].valid) {
                    if (++m_prevBeams[i].staleFrames > m_trackingStaleMax) {
                        m_prevBeams[i].valid = false;
                    }
                }
            }

            // Publish raw results to both stores. m_beamResults feeds the score
            // provider; m_beamDisplayResults feeds the overlay. On a detection
            // miss (empty results) we leave both untouched so the crosshair and
            // text keep showing the last good reading instead of flickering.
            {
                std::lock_guard<std::mutex> lock(m_beamMutex);
                if (!results.empty()) {
                    m_beamResults = results;
                    m_beamDisplayResults = results;
                }

                // Push peak intensity into history ring buffers.
                // No outlier rejection — every detected sample goes in as-is.
                for (size_t bi = 0; bi < m_beamResults.size() && bi < 2; ++bi) {
                    int idx = static_cast<int>(bi);
                    int wi = m_ceHistoryIndex[idx];
                    m_ceHistory[idx][wi] = static_cast<double>(m_beamResults[bi].peakIntensity);
                    m_eeHistory[idx][wi] = m_beamResults[bi].encircledEnergy * 100.0;
                    m_ceHistoryIndex[idx] = (wi + 1) % CE_HISTORY_SIZE;
                    if (m_ceHistoryCount[idx] < CE_HISTORY_SIZE) ++m_ceHistoryCount[idx];
                }
                // If no beam detected, history retains previous data (no update)
            }

            // Publish to broker (subscribers get a copy)
            if (m_beamResultBroker) {
                std::lock_guard<std::mutex> lock(m_beamMutex);
                m_beamResultBroker->Publish(m_beamResults);
            }

            // AUTO SAT now runs as a one-shot algorithm on its own
            // worker thread (see RunAutoSatAlgorithm); the detection
            // thread no longer drives any auto-tuning.

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

// (legacy CheckAndAdjustSaturation removed; replaced by RunAutoSatAlgorithm.)

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

// ============================================================================
// Beam Focus - hill climb Z-axis optimization
// ============================================================================

void SwirBeamWindow::OnBeamFocus()
{
    // Toggle off if already running.
    if (m_beamFocus && m_beamFocus->IsRunning()) {
        std::cout << "[BeamFocus] Stopping beam focus optimization" << std::endl;
        m_beamFocus->RequestStop();
        InvalidatePanel();
        return;
    }

    // Validate prerequisites.
    if (!m_motionManager) {
        std::cout << "[BeamFocus] Motion manager not available" << std::endl;
        return;
    }
    if (!m_beamDetectionEnabled) {
        std::cout << "[BeamFocus] Enable BEAM DETECT first" << std::endl;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_beamMutex);
        if (m_beamResults.empty()) {
            std::cout << "[BeamFocus] No beam detected" << std::endl;
            return;
        }
    }

    if (!m_beamFocus) return;
    m_beamFocus->Start(m_motionManager, &m_beamScoreProvider, m_beamFocusConfig,
                       [this]() { InvalidatePanel(); });
    InvalidatePanel();
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

        // Bottom strip carve-out: only when beam detection is on AND there's
        // enough vertical room to leave a usable image area above it.
        constexpr int STRIP_HEIGHT = 220;
        constexpr int MIN_IMAGE_HEIGHT = 250;
        const bool drawStrip = m_beamDetectionEnabled &&
                               (totalHeight - STRIP_HEIGHT) >= MIN_IMAGE_HEIGHT;
        const int imageBottom = drawStrip ? (totalHeight - STRIP_HEIGHT) : totalHeight;

        RECT panelRect = {0, 0, panelW, totalHeight};
        RECT imageRect = {panelW, 0, totalWidth, imageBottom};
        RECT stripRect = {panelW, imageBottom, totalWidth, totalHeight};

        RebuildButtons(panelRect);
        RenderFrame(m_memDC, imageRect);
        DrawOverlays(m_memDC);

        // Bottom-strip beam crop tiles
        if (drawStrip) {
            Gdiplus::Graphics gfx(m_memDC);
            gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::SolidBrush stripBg(Gdiplus::Color(255, 22, 22, 22));
            gfx.FillRectangle(&stripBg,
                              static_cast<INT>(stripRect.left),
                              static_cast<INT>(stripRect.top),
                              static_cast<INT>(stripRect.right - stripRect.left),
                              static_cast<INT>(stripRect.bottom - stripRect.top));

            std::vector<BeamDetectionResult> resultsCopy;
            {
                std::lock_guard<std::mutex> bLock(m_beamMutex);
                resultsCopy = m_beamDisplayResults;
            }
            std::lock_guard<std::mutex> fLock(m_frameMutex);
            if (!m_frameBuffer.empty()) {
                DrawBeamCropStrip(gfx, resultsCopy,
                                  m_frameBuffer.data(),
                                  m_frameWidth, m_frameHeight,
                                  stripRect);
            }
        }

        // Panel cache: only re-render control panel when state changes (dirty flag)
        {
            int pw = panelRect.right - panelRect.left;
            int ph = panelRect.bottom - panelRect.top;
            // Recreate cache bitmap if size changed
            if (pw != m_panelCacheWidth || ph != m_panelCacheHeight) {
                CleanupPanelCache();
                m_panelDirty.store(true);
            }
            if (m_panelDirty.load()) {
                // (Re)create cache DC/bitmap if needed
                if (!m_panelCacheDC) {
                    m_panelCacheDC = CreateCompatibleDC(m_memDC);
                    m_panelCacheBitmap = CreateCompatibleBitmap(m_memDC, pw, ph);
                    m_panelCacheOldBitmap = static_cast<HBITMAP>(SelectObject(m_panelCacheDC, m_panelCacheBitmap));
                    m_panelCacheWidth = pw;
                    m_panelCacheHeight = ph;
                }
                // Render panel into cache at origin (0,0)
                // Offset the panelRect so it draws at (0,0) in the cache DC
                RECT cachePanelRect = { 0, 0, pw, ph };
                RenderControlPanel(m_panelCacheDC, cachePanelRect);
                m_panelDirty.store(false);
            }
            // BitBlt cached panel into the back buffer
            BitBlt(m_memDC, panelRect.left, panelRect.top, pw, ph,
                   m_panelCacheDC, 0, 0, SRCCOPY);
        }

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
        InvalidatePanel();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_USER + 1:
        // Beam detection thread requests repaint (image area only)
        if (m_lastRenderW > 0 && m_lastRenderH > 0) {
            RECT imgRect = { m_lastRenderX, 0,
                             m_lastRenderX + m_lastRenderW + 1,
                             m_lastRenderY + m_lastRenderH + 1 };
            InvalidateRect(hwnd, &imgRect, FALSE);
        } else {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
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
            if (btn.hovered != wasHovered) { needRepaint = true; InvalidatePanel(); }
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
        InvalidatePanel();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        int mx = LOWORD(lParam);
        int my = HIWORD(lParam);
        auto* btn = HitTestButton(mx, my);
        if (btn) {
            btn->pressed = true;
            InvalidatePanel();
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
        InvalidatePanel();
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

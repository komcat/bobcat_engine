// GdiCameraWindowManager.cpp
#include "GdiCameraWindowManager.h"
#include "GdiCameraWindow.h"
#include "camera/CameraManager.h"
#ifdef HAS_OPENCV
#include "vision/VisionResult.h"
#endif
#include <Windows.h>
#include <QSettings>
#include <iostream>

// QSettings keys for window geometry and camera selection
static constexpr const char* SETTINGS_KEY_X = "GdiCameraWindow/x";
static constexpr const char* SETTINGS_KEY_Y = "GdiCameraWindow/y";
static constexpr const char* SETTINGS_KEY_WIDTH = "GdiCameraWindow/width";
static constexpr const char* SETTINGS_KEY_HEIGHT = "GdiCameraWindow/height";
static constexpr const char* SETTINGS_KEY_VISIBLE = "GdiCameraWindow/visible";
static constexpr const char* SETTINGS_KEY_TARGET_CAMERA = "GdiCameraWindow/targetCamera";

// Application Settings keys (shared with ApplicationSettingsPage)
static constexpr const char* SETTINGS_KEY_VIEW_MODE = "CameraMonitor/ViewMode";
static constexpr const char* SETTINGS_KEY_PRIMARY_CAMERA = "CameraMonitor/PrimaryCamera";
static constexpr const char* SETTINGS_KEY_SECONDARY_CAMERA = "CameraMonitor/SecondaryCamera";
static constexpr const char* SETTINGS_KEY_CHART_ENABLED = "ChartOverlay/Enabled";
static constexpr const char* SETTINGS_KEY_CHART_CHANNEL = "ChartOverlay/LastChannel";
static constexpr const char* SETTINGS_KEY_CHART_SCALE = "ChartOverlay/Scale";
static constexpr const char* SETTINGS_KEY_VISION_ENABLED = "Vision/Enabled";
static constexpr const char* SETTINGS_KEY_VISION_PROFILE = "Vision/LastProfile";
static constexpr const char* SETTINGS_KEY_CROSSHAIR = "Tools/CrosshairEnabled";
static constexpr const char* SETTINGS_KEY_CLICK_TO_MOVE = "Tools/ClickToMoveEnabled";

GdiCameraWindowManager::GdiCameraWindowManager(QObject* parent)
    : QObject(parent) {
}

GdiCameraWindowManager::~GdiCameraWindowManager() {
    shutdown();
}

bool GdiCameraWindowManager::initialize() {
    std::cout << "[GdiCameraWindowManager] Initializing..." << std::endl;

    if (m_initialized) {
        std::cout << "[GdiCameraWindowManager] Already initialized" << std::endl;
        return true;
    }

    if (!m_cameraManager) {
        std::cout << "[GdiCameraWindowManager] ERROR: CameraManager is null!" << std::endl;
        return false;
    }

    // Load saved window geometry from settings
    loadWindowGeometry();

    // Create the GDI window
    std::cout << "[GdiCameraWindowManager] Creating GdiCameraWindow..." << std::endl;
    m_window = std::make_shared<GdiCameraWindow>();

    if (!m_window->Create(m_windowTitle, m_initialX, m_initialY,
                          m_initialWidth, m_initialHeight)) {
        std::cout << "[GdiCameraWindowManager] ERROR: Failed to create window!" << std::endl;
        m_window.reset();
        return false;
    }

    // Set camera manager for menu population
    std::cout << "[GdiCameraWindowManager] Setting camera manager for menu..." << std::endl;
    m_window->SetCameraManager(m_cameraManager);

    // Auto-select first camera if none specified (prevents flickering with multiple cameras)
    if (m_targetCameraId.empty()) {
        auto cameraIds = m_cameraManager->GetCameraIds();
        if (!cameraIds.empty()) {
            m_targetCameraId = cameraIds.front();
            std::cout << "[GdiCameraWindowManager] Auto-selecting first camera: " << m_targetCameraId << std::endl;
        }
    } else {
        // Validate saved camera still exists
        auto cameraIds = m_cameraManager->GetCameraIds();
        bool found = false;
        for (const auto& id : cameraIds) {
            if (id == m_targetCameraId) {
                found = true;
                break;
            }
        }
        if (!found && !cameraIds.empty()) {
            std::cout << "[GdiCameraWindowManager] Saved camera '" << m_targetCameraId
                      << "' not found, selecting first available: " << cameraIds.front() << std::endl;
            m_targetCameraId = cameraIds.front();
        }
    }

    // Set target camera
    if (!m_targetCameraId.empty()) {
        std::cout << "[GdiCameraWindowManager] Setting target camera: " << m_targetCameraId << std::endl;
        m_window->SetTargetCamera(m_targetCameraId);
    }

    // Subscribe to camera frames
    std::cout << "[GdiCameraWindowManager] Subscribing to camera frames..." << std::endl;
    m_cameraManager->SubscribeToFrames(m_window);

    // Set DataStore for chart overlay
    if (m_dataStore) {
        std::cout << "[GdiCameraWindowManager] Setting DataStore for chart overlay..." << std::endl;
        m_window->SetDataStore(m_dataStore);
    }

    // Set motion manager for click-to-move
    if (m_motionManager) {
        std::cout << "[GdiCameraWindowManager] Setting motion manager for click-to-move..." << std::endl;
        m_window->SetMotionManager(m_motionManager);
    }

    // Set beam server client for beam streaming
    if (m_beamServerClient) {
        std::cout << "[GdiCameraWindowManager] Setting beam server client..." << std::endl;
        m_window->SetBeamServerClient(m_beamServerClient);
    }

    // Apply application settings (view mode, overlays, etc.)
    applyApplicationSettings();

    // Setup timer for Win32 message processing
    // This integrates the Win32 message loop with Qt's event loop
    m_messageTimer = new QTimer(this);
    connect(m_messageTimer, &QTimer::timeout, this, &GdiCameraWindowManager::processMessages);
    m_messageTimer->start(16);  // ~60fps message processing

    m_initialized = true;
    std::cout << "[GdiCameraWindowManager] Initialized successfully" << std::endl;
    return true;
}

void GdiCameraWindowManager::shutdown() {
    if (!m_initialized) return;

    // Save window geometry before destroying
    saveWindowGeometry();

    // Stop message timer
    if (m_messageTimer) {
        m_messageTimer->stop();
        delete m_messageTimer;
        m_messageTimer = nullptr;
    }

    // Unsubscribe from camera
    if (m_cameraManager && m_window) {
        m_cameraManager->UnsubscribeFromFrames(m_window->GetSubscriberId());
    }

    // Destroy window
    if (m_window) {
        m_window->Destroy();
        m_window.reset();
    }

    m_initialized = false;
}

void GdiCameraWindowManager::setCameraManager(CameraManager* manager) {
    m_cameraManager = manager;
}

void GdiCameraWindowManager::setDataStore(DataStore* dataStore) {
    m_dataStore = dataStore;
    if (m_window) {
        m_window->SetDataStore(dataStore);
    }
}

void GdiCameraWindowManager::setMotionManager(UnifiedMotionManager* motionManager) {
    m_motionManager = motionManager;
    if (m_window) {
        m_window->SetMotionManager(motionManager);
    }
}

void GdiCameraWindowManager::setBeamServerClient(BeamServerClient* client) {
    m_beamServerClient = client;
    if (m_window) {
        m_window->SetBeamServerClient(client);
    }
}

void GdiCameraWindowManager::setTargetCamera(const std::string& cameraId) {
    m_targetCameraId = cameraId;
    if (m_window) {
        m_window->SetTargetCamera(cameraId);
    }
}

std::string GdiCameraWindowManager::getTargetCamera() const {
    if (m_window) {
        return m_window->GetTargetCamera();
    }
    return m_targetCameraId;
}

void GdiCameraWindowManager::setDualCameraMode(bool enabled) {
    if (m_window) {
        m_window->SetDualCameraMode(enabled);
    }
}

bool GdiCameraWindowManager::isDualCameraMode() const {
    if (m_window) {
        return m_window->IsDualCameraMode();
    }
    return false;
}

void GdiCameraWindowManager::setSecondaryCameraId(const std::string& cameraId) {
    if (m_window) {
        m_window->SetSecondaryCameraId(cameraId);
    }
}

std::string GdiCameraWindowManager::getSecondaryCameraId() const {
    if (m_window) {
        return m_window->GetSecondaryCameraId();
    }
    return "";
}

#ifdef HAS_OPENCV
bool GdiCameraWindowManager::isVisionProcessingActive() const {
    if (m_window) {
        return m_window->IsVisionProcessingActive();
    }
    return false;
}

VisionResult GdiCameraWindowManager::getLatestVisionResult() const {
    if (m_window) {
        return m_window->GetLatestVisionResult();
    }
    return VisionResult{};
}

uint64_t GdiCameraWindowManager::getVisionResultSequence() const {
    if (m_window) {
        return m_window->GetVisionResultSequence();
    }
    return 0;
}

void GdiCameraWindowManager::setVisionEnabled(bool enabled) {
    if (m_window) {
        m_window->SetVisionEnabled(enabled);
    }
}

void GdiCameraWindowManager::selectVisionProfile(const std::string& profilePath) {
    if (m_window) {
        m_window->SelectVisionProfile(profilePath);
    }
}
#endif

void GdiCameraWindowManager::show() {
    std::cout << "[GdiCameraWindowManager] show() called" << std::endl;
    if (m_window) {
        m_window->Show();
        emit visibilityChanged(true);
    } else {
        std::cout << "[GdiCameraWindowManager] ERROR: Window is null, cannot show" << std::endl;
    }
}

void GdiCameraWindowManager::hide() {
    if (m_window) {
        m_window->Hide();
        emit visibilityChanged(false);
    }
}

bool GdiCameraWindowManager::isVisible() const {
    return m_window && m_window->IsVisible();
}

void GdiCameraWindowManager::toggleVisibility() {
    if (isVisible()) {
        hide();
    } else {
        show();
    }
}

void GdiCameraWindowManager::setWindowTitle(const std::string& title) {
    m_windowTitle = title;
    if (m_window) {
        m_window->SetTitle(title);
    }
}

void GdiCameraWindowManager::setWindowPosition(int x, int y) {
    m_initialX = x;
    m_initialY = y;
    if (m_window && m_window->GetHwnd()) {
        SetWindowPos(m_window->GetHwnd(), nullptr, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
    }
}

void GdiCameraWindowManager::setWindowSize(int width, int height) {
    m_initialWidth = width;
    m_initialHeight = height;
    if (m_window && m_window->GetHwnd()) {
        SetWindowPos(m_window->GetHwnd(), nullptr, 0, 0, width, height,
                     SWP_NOMOVE | SWP_NOZORDER);
    }
}

void GdiCameraWindowManager::processMessages() {
    // Process any pending Win32 messages for the GDI window
    GdiCameraWindow::ProcessMessages();
}

void GdiCameraWindowManager::applyApplicationSettings() {
    if (!m_window) return;

    QSettings settings("Fabrinet", "BobcatEngine");

    // View Mode
    QString viewMode = settings.value(SETTINGS_KEY_VIEW_MODE, "single").toString();
    bool dualMode = (viewMode == "dual");
    m_window->SetDualCameraMode(dualMode);
    std::cout << "[GdiCameraWindowManager] View mode: " << (dualMode ? "dual" : "single") << std::endl;

    // Primary Camera (override if set in app settings)
    QString primaryCamera = settings.value(SETTINGS_KEY_PRIMARY_CAMERA, "").toString();
    if (!primaryCamera.isEmpty() && primaryCamera != m_targetCameraId.c_str()) {
        m_targetCameraId = primaryCamera.toStdString();
        m_window->SetTargetCamera(m_targetCameraId);
        std::cout << "[GdiCameraWindowManager] Primary camera from settings: " << m_targetCameraId << std::endl;
    }

    // Secondary Camera (for dual mode)
    QString secondaryCamera = settings.value(SETTINGS_KEY_SECONDARY_CAMERA, "").toString();
    if (!secondaryCamera.isEmpty()) {
        m_window->SetSecondaryCameraId(secondaryCamera.toStdString());
        std::cout << "[GdiCameraWindowManager] Secondary camera: " << secondaryCamera.toStdString() << std::endl;
    }

    // Chart Overlay
    bool chartEnabled = settings.value(SETTINGS_KEY_CHART_ENABLED, "false").toString() == "true";
    m_window->GetChartOverlay().SetEnabled(chartEnabled);
    if (chartEnabled) {
        QString lastChannel = settings.value(SETTINGS_KEY_CHART_CHANNEL, "").toString();
        if (!lastChannel.isEmpty()) {
            m_window->GetChartOverlay().SetChannelEnabled(lastChannel.toStdString(), true);
            std::cout << "[GdiCameraWindowManager] Chart overlay enabled, channel: " << lastChannel.toStdString() << std::endl;
        }
    }

    // Chart Overlay Scale
    float overlayScale = settings.value(SETTINGS_KEY_CHART_SCALE, 1.0f).toFloat();
    m_window->SetOverlayScale(overlayScale);
    std::cout << "[GdiCameraWindowManager] Chart overlay scale: " << overlayScale << std::endl;

    // Setup callback to save overlay scale when changed via menu
    m_window->SetOverlayScaleChangedCallback([](float newScale) {
        QSettings settings("Fabrinet", "BobcatEngine");
        settings.setValue(SETTINGS_KEY_CHART_SCALE, newScale);
        settings.sync();
        std::cout << "[GdiCameraWindowManager] Saved overlay scale: " << newScale << std::endl;
    });

    // Tools
    bool crosshairEnabled = settings.value(SETTINGS_KEY_CROSSHAIR, "false").toString() == "true";
    m_window->SetCrosshairEnabled(crosshairEnabled);

    bool clickToMoveEnabled = settings.value(SETTINGS_KEY_CLICK_TO_MOVE, "false").toString() == "true";
    m_window->SetClickToMoveEnabled(clickToMoveEnabled);

    std::cout << "[GdiCameraWindowManager] Crosshair: " << (crosshairEnabled ? "on" : "off")
              << ", Click-to-move: " << (clickToMoveEnabled ? "on" : "off") << std::endl;
}

void GdiCameraWindowManager::saveWindowGeometry() {
    if (!m_window || !m_window->GetHwnd()) {
        return;
    }

    HWND hwnd = m_window->GetHwnd();
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        int x = rect.left;
        int y = rect.top;
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        bool visible = m_window->IsVisible();

        QSettings settings("Fabrinet", "BobcatEngine");
        settings.setValue(SETTINGS_KEY_X, x);
        settings.setValue(SETTINGS_KEY_Y, y);
        settings.setValue(SETTINGS_KEY_WIDTH, width);
        settings.setValue(SETTINGS_KEY_HEIGHT, height);
        settings.setValue(SETTINGS_KEY_VISIBLE, visible);

        // Save the current target camera selection
        std::string currentCamera = m_window->GetTargetCamera();
        settings.setValue(SETTINGS_KEY_TARGET_CAMERA, QString::fromStdString(currentCamera));

        // Save current application settings state
        // View mode
        settings.setValue(SETTINGS_KEY_VIEW_MODE, m_window->IsDualCameraMode() ? "dual" : "single");

        // Cameras
        settings.setValue(SETTINGS_KEY_PRIMARY_CAMERA, QString::fromStdString(m_window->GetTargetCamera()));
        settings.setValue(SETTINGS_KEY_SECONDARY_CAMERA, QString::fromStdString(m_window->GetSecondaryCameraId()));

        // Chart overlay
        settings.setValue(SETTINGS_KEY_CHART_ENABLED, m_window->GetChartOverlay().IsEnabled() ? "true" : "false");
        auto enabledChannels = m_window->GetChartOverlay().GetEnabledChannels();
        if (!enabledChannels.empty()) {
            settings.setValue(SETTINGS_KEY_CHART_CHANNEL, QString::fromStdString(enabledChannels.front()));
        }
        settings.setValue(SETTINGS_KEY_CHART_SCALE, m_window->GetOverlayScale());

        // Tools
        settings.setValue(SETTINGS_KEY_CROSSHAIR, m_window->IsCrosshairEnabled() ? "true" : "false");
        settings.setValue(SETTINGS_KEY_CLICK_TO_MOVE, m_window->IsClickToMoveEnabled() ? "true" : "false");

        settings.sync();

        std::cout << "[GdiCameraWindowManager] Saved window geometry: "
                  << x << "," << y << " " << width << "x" << height
                  << " visible=" << (visible ? "true" : "false")
                  << " camera=" << (currentCamera.empty() ? "(none)" : currentCamera) << std::endl;
    }
}

void GdiCameraWindowManager::loadWindowGeometry() {
    QSettings settings("Fabrinet", "BobcatEngine");

    // Check if we have saved settings
    if (!settings.contains(SETTINGS_KEY_X)) {
        std::cout << "[GdiCameraWindowManager] No saved geometry found, using defaults (will show on first run)" << std::endl;
        m_restoreVisible = true;  // Show on first run
        return;
    }

    m_initialX = settings.value(SETTINGS_KEY_X, 100).toInt();
    m_initialY = settings.value(SETTINGS_KEY_Y, 100).toInt();
    m_initialWidth = settings.value(SETTINGS_KEY_WIDTH, 800).toInt();
    m_initialHeight = settings.value(SETTINGS_KEY_HEIGHT, 600).toInt();
    m_restoreVisible = settings.value(SETTINGS_KEY_VISIBLE, true).toBool();

    // Load saved target camera selection
    if (settings.contains(SETTINGS_KEY_TARGET_CAMERA)) {
        m_targetCameraId = settings.value(SETTINGS_KEY_TARGET_CAMERA).toString().toStdString();
    }

    // Validate position is on screen (in case screen configuration changed)
    if (m_initialX < -100) m_initialX = 100;
    if (m_initialY < -100) m_initialY = 100;
    if (m_initialWidth < 200) m_initialWidth = 800;
    if (m_initialHeight < 150) m_initialHeight = 600;

    std::cout << "[GdiCameraWindowManager] Loaded window geometry: "
              << m_initialX << "," << m_initialY << " "
              << m_initialWidth << "x" << m_initialHeight
              << " visible=" << (m_restoreVisible ? "true" : "false")
              << " camera=" << (m_targetCameraId.empty() ? "(none)" : m_targetCameraId) << std::endl;
}

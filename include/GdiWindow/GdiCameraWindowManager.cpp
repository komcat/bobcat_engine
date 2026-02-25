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

GdiCameraWindowManager::GdiCameraWindowManager(const std::string& instanceId, QObject* parent)
    : QObject(parent)
    , m_instanceId(instanceId) {
    // Compute settings key prefixes based on instance ID
    // First instance (empty id): "GdiCameraWindow", "CameraMonitor", etc. (backwards compatible)
    // Second instance (id="2"):  "GdiCameraWindow2", "CameraMonitor2", etc.
    m_settingsPrefix = "GdiCameraWindow" + m_instanceId;
    m_monitorPrefix  = "CameraMonitor" + m_instanceId;
    m_chartPrefix    = "ChartOverlay" + m_instanceId;
    m_visionPrefix   = "Vision" + m_instanceId;
    m_toolsPrefix    = "Tools" + m_instanceId;
    m_subscriberId   = "GdiCameraWindow" + m_instanceId;
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
    std::cout << "[GdiCameraWindowManager] Creating GdiCameraWindow (instance: '"
              << (m_instanceId.empty() ? "default" : m_instanceId) << "')..." << std::endl;
    m_window = std::make_shared<GdiCameraWindow>();

    // Set unique subscriber ID before subscribing to cameras
    m_window->SetSubscriberId(m_subscriberId);

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

    // Hide the window immediately so it disappears before slow cleanup
    if (m_window) {
        m_window->Hide();
    }

    // Stop message timer
    if (m_messageTimer) {
        m_messageTimer->stop();
        delete m_messageTimer;
        m_messageTimer = nullptr;
    }

    // Unsubscribe from camera (stops frame delivery before destroying window)
    if (m_cameraManager && m_window) {
        m_cameraManager->UnsubscribeFromFrames(m_window->GetSubscriberId());
    }

    // Destroy window (stops threads, releases GDI resources, destroys HWND)
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

bool GdiCameraWindowManager::isBeamStreamActive() const {
    if (m_window) {
        return m_window->IsBeamStreamActive();
    }
    return false;
}

nlohmann::json GdiCameraWindowManager::getLatestBeamStreamResult() const {
    if (m_window) {
        return m_window->GetLatestBeamStreamResult();
    }
    return nlohmann::json();
}

uint64_t GdiCameraWindowManager::getBeamStreamResultSequence() const {
    if (m_window) {
        return m_window->GetBeamStreamResultSequence();
    }
    return 0;
}

double GdiCameraWindowManager::getBeamStreamFps() const {
    if (m_window) {
        return m_window->GetBeamStreamFps();
    }
    return 0.0;
}

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
    QString viewModeKey = QString::fromStdString(m_monitorPrefix + "/ViewMode");
    QString viewMode = settings.value(viewModeKey, "single").toString();
    bool dualMode = (viewMode == "dual");
    m_window->SetDualCameraMode(dualMode);
    std::cout << "[GdiCameraWindowManager] View mode: " << (dualMode ? "dual" : "single") << std::endl;

    // Primary Camera (override if set in app settings)
    QString primaryCameraKey = QString::fromStdString(m_monitorPrefix + "/PrimaryCamera");
    QString primaryCamera = settings.value(primaryCameraKey, "").toString();
    if (!primaryCamera.isEmpty() && primaryCamera != m_targetCameraId.c_str()) {
        m_targetCameraId = primaryCamera.toStdString();
        m_window->SetTargetCamera(m_targetCameraId);
        std::cout << "[GdiCameraWindowManager] Primary camera from settings: " << m_targetCameraId << std::endl;
    }

    // Secondary Camera (for dual mode)
    QString secondaryCameraKey = QString::fromStdString(m_monitorPrefix + "/SecondaryCamera");
    QString secondaryCamera = settings.value(secondaryCameraKey, "").toString();
    if (!secondaryCamera.isEmpty()) {
        m_window->SetSecondaryCameraId(secondaryCamera.toStdString());
        std::cout << "[GdiCameraWindowManager] Secondary camera: " << secondaryCamera.toStdString() << std::endl;
    }

    // Chart Overlay
    QString chartEnabledKey = QString::fromStdString(m_chartPrefix + "/Enabled");
    QString chartChannelKey = QString::fromStdString(m_chartPrefix + "/LastChannel");
    QString chartScaleKey = QString::fromStdString(m_chartPrefix + "/Scale");

    bool chartEnabled = settings.value(chartEnabledKey, "false").toString() == "true";
    m_window->GetChartOverlay().SetEnabled(chartEnabled);
    if (chartEnabled) {
        QString lastChannel = settings.value(chartChannelKey, "").toString();
        if (!lastChannel.isEmpty()) {
            m_window->GetChartOverlay().SetChannelEnabled(lastChannel.toStdString(), true);
            std::cout << "[GdiCameraWindowManager] Chart overlay enabled, channel: " << lastChannel.toStdString() << std::endl;
        }
    }

    // Chart Overlay Scale
    float overlayScale = settings.value(chartScaleKey, 1.0f).toFloat();
    m_window->SetOverlayScale(overlayScale);
    std::cout << "[GdiCameraWindowManager] Chart overlay scale: " << overlayScale << std::endl;

    // Setup callback to save overlay scale when changed via menu
    // Capture the key string by value for the lambda
    std::string scaleKeyCopy = m_chartPrefix + "/Scale";
    m_window->SetOverlayScaleChangedCallback([scaleKeyCopy](float newScale) {
        QSettings settings("Fabrinet", "BobcatEngine");
        settings.setValue(QString::fromStdString(scaleKeyCopy), newScale);
        settings.sync();
        std::cout << "[GdiCameraWindowManager] Saved overlay scale: " << newScale << std::endl;
    });

    // Tools
    QString crosshairKey = QString::fromStdString(m_toolsPrefix + "/CrosshairEnabled");
    QString clickToMoveKey = QString::fromStdString(m_toolsPrefix + "/ClickToMoveEnabled");

    bool crosshairEnabled = settings.value(crosshairKey, "false").toString() == "true";
    m_window->SetCrosshairEnabled(crosshairEnabled);

    bool clickToMoveEnabled = settings.value(clickToMoveKey, "false").toString() == "true";
    m_window->SetClickToMoveEnabled(clickToMoveEnabled);

    QString showDistanceKey = QString::fromStdString(m_toolsPrefix + "/ShowDistanceEnabled");
    bool showDistanceEnabled = settings.value(showDistanceKey, "false").toString() == "true";
    m_window->SetShowDistanceEnabled(showDistanceEnabled);

    QString magnificationKey = QString::fromStdString(m_toolsPrefix + "/Magnification");
    double magnification = settings.value(magnificationKey, 1.0).toDouble();
    if (magnification > 0.0) {
        m_window->SetMagnification(magnification);
    }

    std::cout << "[GdiCameraWindowManager] Crosshair: " << (crosshairEnabled ? "on" : "off")
              << ", Click-to-move: " << (clickToMoveEnabled ? "on" : "off")
              << ", Show-distance: " << (showDistanceEnabled ? "on" : "off")
              << ", Magnification: " << magnification << "x" << std::endl;
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
        settings.setValue(QString::fromStdString(m_settingsPrefix + "/x"), x);
        settings.setValue(QString::fromStdString(m_settingsPrefix + "/y"), y);
        settings.setValue(QString::fromStdString(m_settingsPrefix + "/width"), width);
        settings.setValue(QString::fromStdString(m_settingsPrefix + "/height"), height);
        settings.setValue(QString::fromStdString(m_settingsPrefix + "/visible"), visible);

        // Save the current target camera selection
        std::string currentCamera = m_window->GetTargetCamera();
        settings.setValue(QString::fromStdString(m_settingsPrefix + "/targetCamera"),
                          QString::fromStdString(currentCamera));

        // Save current application settings state
        // View mode
        settings.setValue(QString::fromStdString(m_monitorPrefix + "/ViewMode"),
                          m_window->IsDualCameraMode() ? "dual" : "single");

        // Cameras
        settings.setValue(QString::fromStdString(m_monitorPrefix + "/PrimaryCamera"),
                          QString::fromStdString(m_window->GetTargetCamera()));
        settings.setValue(QString::fromStdString(m_monitorPrefix + "/SecondaryCamera"),
                          QString::fromStdString(m_window->GetSecondaryCameraId()));

        // Chart overlay
        settings.setValue(QString::fromStdString(m_chartPrefix + "/Enabled"),
                          m_window->GetChartOverlay().IsEnabled() ? "true" : "false");
        auto enabledChannels = m_window->GetChartOverlay().GetEnabledChannels();
        if (!enabledChannels.empty()) {
            settings.setValue(QString::fromStdString(m_chartPrefix + "/LastChannel"),
                              QString::fromStdString(enabledChannels.front()));
        }
        settings.setValue(QString::fromStdString(m_chartPrefix + "/Scale"),
                          m_window->GetOverlayScale());

        // Tools
        settings.setValue(QString::fromStdString(m_toolsPrefix + "/CrosshairEnabled"),
                          m_window->IsCrosshairEnabled() ? "true" : "false");
        settings.setValue(QString::fromStdString(m_toolsPrefix + "/ClickToMoveEnabled"),
                          m_window->IsClickToMoveEnabled() ? "true" : "false");
        settings.setValue(QString::fromStdString(m_toolsPrefix + "/ShowDistanceEnabled"),
                          m_window->IsShowDistanceEnabled() ? "true" : "false");
        settings.setValue(QString::fromStdString(m_toolsPrefix + "/Magnification"),
                          m_window->GetMagnification());

        settings.sync();

        std::cout << "[GdiCameraWindowManager] Saved window geometry: "
                  << x << "," << y << " " << width << "x" << height
                  << " visible=" << (visible ? "true" : "false")
                  << " camera=" << (currentCamera.empty() ? "(none)" : currentCamera) << std::endl;
    }
}

void GdiCameraWindowManager::loadWindowGeometry() {
    QSettings settings("Fabrinet", "BobcatEngine");

    QString keyX = QString::fromStdString(m_settingsPrefix + "/x");

    // Check if we have saved settings
    if (!settings.contains(keyX)) {
        std::cout << "[GdiCameraWindowManager] No saved geometry found, using defaults (will show on first run)" << std::endl;
        m_restoreVisible = true;  // Show on first run
        return;
    }

    m_initialX = settings.value(keyX, 100).toInt();
    m_initialY = settings.value(QString::fromStdString(m_settingsPrefix + "/y"), 100).toInt();
    m_initialWidth = settings.value(QString::fromStdString(m_settingsPrefix + "/width"), 800).toInt();
    m_initialHeight = settings.value(QString::fromStdString(m_settingsPrefix + "/height"), 600).toInt();
    m_restoreVisible = settings.value(QString::fromStdString(m_settingsPrefix + "/visible"), true).toBool();

    // Load saved target camera selection
    QString targetCameraKey = QString::fromStdString(m_settingsPrefix + "/targetCamera");
    if (settings.contains(targetCameraKey)) {
        m_targetCameraId = settings.value(targetCameraKey).toString().toStdString();
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

// SwirBeamWindowManager.cpp
#include "SwirBeamWindowManager.h"
#include "SwirBeamWindow.h"  // Brings in Windows.h via SwirBeamWindow.h
#include "camera/CameraManager.h"
#include "PSU/PowerSupplyManager.h"
#include <QCoreApplication>
#include <QSettings>
#include <iostream>

SwirBeamWindowManager::SwirBeamWindowManager(QObject* parent)
    : QObject(parent)
{
    // Stop timers when app is about to quit (before exec() returns)
    connect(qApp, &QCoreApplication::aboutToQuit, this, &SwirBeamWindowManager::shutdown);
}

SwirBeamWindowManager::~SwirBeamWindowManager()
{
    shutdown();
}

bool SwirBeamWindowManager::initialize()
{
    if (m_initialized) return true;

    loadWindowGeometry();

    m_window = std::make_shared<SwirBeamWindow>();
    m_window->SetSubscriberId("SwirBeamWindow");

    if (m_cameraManager) m_window->SetCameraManager(m_cameraManager);
    if (m_psuManager) m_window->SetPowerSupplyManager(m_psuManager);
    if (m_motionManager) m_window->SetMotionManager(m_motionManager);
    if (m_pneumaticManager) m_window->SetPneumaticManager(m_pneumaticManager);
    if (m_tecManager) m_window->SetTECManager(m_tecManager);
    if (m_cameraConfigManager) m_window->SetCameraConfigManager(m_cameraConfigManager);

    m_window->SetTargetCamera(m_targetCameraId);

    if (!m_window->Create(m_windowTitle, m_initialX, m_initialY,
                           m_initialWidth, m_initialHeight)) {
        std::cerr << "[SwirBeamWindowManager] Failed to create window" << std::endl;
        m_window.reset();
        return false;
    }

    if (m_cameraManager) m_cameraManager->SubscribeToFrames(m_window);

    // Win32 message pump timer (~60fps)
    m_messageTimer = new QTimer(this);
    connect(m_messageTimer, &QTimer::timeout, this, &SwirBeamWindowManager::processMessages);
    m_messageTimer->start(33);

    // PSU polling timer (500ms)
    m_psuPollTimer = new QTimer(this);
    connect(m_psuPollTimer, &QTimer::timeout, this, &SwirBeamWindowManager::pollPSU);
    m_psuPollTimer->start(500);

    m_initialized = true;
    std::cout << "[SwirBeamWindowManager] Initialized" << std::endl;
    return true;
}

void SwirBeamWindowManager::shutdown()
{
    if (!m_initialized) return;
    saveWindowGeometry();

    if (m_psuPollTimer) { m_psuPollTimer->stop(); delete m_psuPollTimer; m_psuPollTimer = nullptr; }
    if (m_messageTimer) { m_messageTimer->stop(); delete m_messageTimer; m_messageTimer = nullptr; }

    if (m_cameraManager && m_window) {
        m_cameraManager->UnsubscribeFromFrames(m_window->GetSubscriberId());
    }
    if (m_window) { m_window->Hide(); m_window->Destroy(); m_window.reset(); }

    m_initialized = false;
    std::cout << "[SwirBeamWindowManager] Shutdown complete" << std::endl;
}

void SwirBeamWindowManager::setCameraManager(CameraManager* manager) { m_cameraManager = manager; }
void SwirBeamWindowManager::setPowerSupplyManager(PowerSupplyManager* psuManager) { m_psuManager = psuManager; }
void SwirBeamWindowManager::setMotionManager(UnifiedMotionManager* motionManager) { m_motionManager = motionManager; }
void SwirBeamWindowManager::setPneumaticManager(PneumaticManager* pneumaticManager) { m_pneumaticManager = pneumaticManager; }
void SwirBeamWindowManager::setTECManager(TECManager* tecManager) { m_tecManager = tecManager; }
void SwirBeamWindowManager::setCameraConfigManager(CameraConfigManager* configMgr) { m_cameraConfigManager = configMgr; }
void SwirBeamWindowManager::setBeamResultBroker(BeamResultBroker* broker) { if (m_window) m_window->SetBeamResultBroker(broker); }

void SwirBeamWindowManager::setTargetCamera(const std::string& cameraId)
{
    m_targetCameraId = cameraId;
    if (m_window) m_window->SetTargetCamera(cameraId);
}

void SwirBeamWindowManager::show()
{
    if (m_window) { m_window->Show(); emit visibilityChanged(true); }
}

void SwirBeamWindowManager::hide()
{
    if (m_window) { m_window->Hide(); emit visibilityChanged(false); }
}

bool SwirBeamWindowManager::isVisible() const
{
    return m_window && m_window->IsVisible();
}

void SwirBeamWindowManager::toggleVisibility()
{
    if (isVisible()) hide(); else show();
}

void SwirBeamWindowManager::setWindowTitle(const std::string& title)
{
    m_windowTitle = title;
    if (m_window) m_window->SetTitle(title);
}

void SwirBeamWindowManager::setWindowPosition(int x, int y) { m_initialX = x; m_initialY = y; }
void SwirBeamWindowManager::setWindowSize(int width, int height) { m_initialWidth = width; m_initialHeight = height; }

void SwirBeamWindowManager::emergencyStop()
{
    if (m_window) m_window->EmergencyStop();
}

void SwirBeamWindowManager::processMessages()
{
    if (m_window) SwirBeamWindow::ProcessMessages();
}

void SwirBeamWindowManager::pollPSU()
{
    if (m_window && m_window->IsVisible()) m_window->PollPSUReadings();
}

void SwirBeamWindowManager::saveWindowGeometry()
{
    if (!m_window || !m_window->GetHwnd()) return;
    HWND hwnd = m_window->GetHwnd();

    // Use GetWindowPlacement so a minimized/maximized window still saves its
    // restored (normal) position rather than the iconic sentinel (-32000,-32000).
    WINDOWPLACEMENT wp = {};
    wp.length = sizeof(WINDOWPLACEMENT);
    if (!GetWindowPlacement(hwnd, &wp)) return;
    const RECT& rect = wp.rcNormalPosition;

    const int x = static_cast<int>(rect.left);
    const int y = static_cast<int>(rect.top);
    const int width = static_cast<int>(rect.right - rect.left);
    const int height = static_cast<int>(rect.bottom - rect.top);

    // Sanity-check before persisting; refuse to save absurd geometry.
    if (width < 200 || height < 200) return;

    QSettings settings("Fabrinet", "BobcatEngine");
    settings.setValue("SwirBeamWindow/x", x);
    settings.setValue("SwirBeamWindow/y", y);
    settings.setValue("SwirBeamWindow/width", width);
    settings.setValue("SwirBeamWindow/height", height);
    settings.setValue("SwirBeamWindow/visible", m_window->IsVisible());
}

void SwirBeamWindowManager::loadWindowGeometry()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    if (settings.contains("SwirBeamWindow/x")) {
        const int x = settings.value("SwirBeamWindow/x", 100).toInt();
        const int y = settings.value("SwirBeamWindow/y", 100).toInt();
        const int width = settings.value("SwirBeamWindow/width", 1200).toInt();
        const int height = settings.value("SwirBeamWindow/height", 700).toInt();

        // Reject corrupt/offscreen/minuscule values that were previously
        // persisted from a minimized window (e.g. x=y=-32000, width=198, height=56).
        const bool sizeOk = (width >= 200 && height >= 200);
        const bool posOk = (x > -10000 && y > -10000);
        if (sizeOk && posOk) {
            m_initialX = x;
            m_initialY = y;
            m_initialWidth = width;
            m_initialHeight = height;
            m_restoreVisible = settings.value("SwirBeamWindow/visible", false).toBool();
        } else {
            std::cerr << "[SwirBeamWindowManager] Ignoring corrupt saved geometry "
                      << "(x=" << x << ", y=" << y
                      << ", w=" << width << ", h=" << height << "); using defaults"
                      << std::endl;
        }
    }
}

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
    RECT rect;
    GetWindowRect(hwnd, &rect);
    QSettings settings("Fabrinet", "BobcatEngine");
    settings.setValue("SwirBeamWindow/x", static_cast<int>(rect.left));
    settings.setValue("SwirBeamWindow/y", static_cast<int>(rect.top));
    settings.setValue("SwirBeamWindow/width", static_cast<int>(rect.right - rect.left));
    settings.setValue("SwirBeamWindow/height", static_cast<int>(rect.bottom - rect.top));
    settings.setValue("SwirBeamWindow/visible", m_window->IsVisible());
}

void SwirBeamWindowManager::loadWindowGeometry()
{
    QSettings settings("Fabrinet", "BobcatEngine");
    if (settings.contains("SwirBeamWindow/x")) {
        m_initialX = settings.value("SwirBeamWindow/x", 100).toInt();
        m_initialY = settings.value("SwirBeamWindow/y", 100).toInt();
        m_initialWidth = settings.value("SwirBeamWindow/width", 1200).toInt();
        m_initialHeight = settings.value("SwirBeamWindow/height", 700).toInt();
        m_restoreVisible = settings.value("SwirBeamWindow/visible", false).toBool();
    }
}

// AuxWindowManager.cpp
#include "AuxWindowManager.h"
#include "AuxViewWindow.h"
#include <iostream>

AuxWindowManager::AuxWindowManager(QObject* parent)
  : QObject(parent)
  , m_cameraManager(nullptr)
  , m_motionManager(nullptr)
  , m_dataStore(nullptr)
  , m_tcpManager(nullptr)
  , m_initialized(false)
{
  std::cout << "[AuxWindowManager] Created" << std::endl;
}

AuxWindowManager::~AuxWindowManager()
{
  shutdown();
  std::cout << "[AuxWindowManager] Destroyed" << std::endl;
}

void AuxWindowManager::initialize()
{
  if (m_initialized) {
    std::cout << "[AuxWindowManager] Already initialized" << std::endl;
    return;
  }

  if (!m_cameraManager) {
    std::cout << "[AuxWindowManager] ERROR: CameraManager not set!" << std::endl;
    return;
  }

  if (!m_motionManager) {
    std::cout << "[AuxWindowManager] ERROR: UnifiedMotionManager not set!" << std::endl;
    return;
  }

  if (!m_dataStore) {
    std::cout << "[AuxWindowManager] ERROR: DataStore not set!" << std::endl;
    return;
  }

  if (!m_tcpManager) {
    std::cout << "[AuxWindowManager] ERROR: TcpClientManager not set!" << std::endl;
    return;
  }

  std::cout << "[AuxWindowManager] Initializing auxiliary window with click-to-move..." << std::endl;

  // Create the window with all required managers
  m_window = std::make_unique<AuxViewWindow>(m_cameraManager, m_motionManager, m_dataStore, m_tcpManager);

  m_initialized = true;

  // Show window on startup
  show();

  std::cout << "[AuxWindowManager] Initialization complete" << std::endl;
}

void AuxWindowManager::shutdown()
{
  if (m_window) {
    std::cout << "[AuxWindowManager] Shutting down auxiliary window" << std::endl;
    m_window->close();
    m_window.reset();
  }
  m_initialized = false;
}

void AuxWindowManager::setCameraManager(CameraManager* manager)
{
  m_cameraManager = manager;
  std::cout << "[AuxWindowManager] CameraManager set" << std::endl;
}

void AuxWindowManager::setMotionManager(UnifiedMotionManager* manager)
{
  m_motionManager = manager;
  std::cout << "[AuxWindowManager] UnifiedMotionManager set" << std::endl;
}

void AuxWindowManager::setDataStore(DataStore* dataStore)
{
  m_dataStore = dataStore;
  std::cout << "[AuxWindowManager] DataStore set" << std::endl;
}

void AuxWindowManager::setTcpClientManager(TcpClientManager* tcpManager)
{
  m_tcpManager = tcpManager;
  std::cout << "[AuxWindowManager] TcpClientManager set" << std::endl;
}

void AuxWindowManager::show()
{
  if (m_window) {
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
    std::cout << "[AuxWindowManager] Window shown" << std::endl;
  }
}

void AuxWindowManager::hide()
{
  if (m_window) {
    m_window->hide();
    std::cout << "[AuxWindowManager] Window hidden" << std::endl;
  }
}

bool AuxWindowManager::isVisible() const
{
  return m_window && m_window->isVisible();
}
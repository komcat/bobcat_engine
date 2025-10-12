// Application.cpp
#include "Application.h"
#include "MainWindow.h"
#include "CameraManager.h"
#include <QApplication>
#include <iostream>

Application::Application(int argc, char* argv[])
  : QObject(nullptr)
  , m_initialized(false)
  , m_argc(argc)
  , m_argv(argv)
{
  std::cout << "[Application] Creating Bobcat Engine..." << std::endl;
}

Application::~Application()
{
  shutdown();
  std::cout << "[Application] Bobcat Engine destroyed" << std::endl;
}

int Application::run()
{
  if (!initialize()) {
    std::cerr << "[Application] Failed to initialize" << std::endl;
    return -1;
  }

  std::cout << "[Application] Starting " << getApplicationName() << "..." << std::endl;

  int result = m_qApplication->exec();

  std::cout << "[Application] Exited with code: " << result << std::endl;
  return result;
}

void Application::shutdown()
{
  if (!m_initialized) return;

  std::cout << "[Application] Shutting down..." << std::endl;

  // Close main window
  if (m_mainWindow) {
    m_mainWindow->close();
    m_mainWindow.reset();
  }

  // Shutdown managers
  shutdownManagers();

  m_initialized = false;
}

bool Application::initialize()
{
  if (m_initialized) return true;

  try {
    // Create Qt application
    m_qApplication = std::make_unique<QApplication>(m_argc, m_argv);
    m_qApplication->setApplicationName(QString::fromStdString(getApplicationName()));
    m_qApplication->setApplicationVersion(QString::fromStdString(getVersion()));

    std::cout << "[Application] " << getApplicationName()
      << " v" << getVersion() << std::endl;

    // Initialize managers BEFORE creating main window
    initializeManagers();

    // Create main window (receives context)
    createMainWindow();

    m_initialized = true;
    std::cout << "[Application] Initialization complete" << std::endl;

    return true;
  }
  catch (const std::exception& e) {
    std::cerr << "[Application] Init failed: " << e.what() << std::endl;
    return false;
  }
}

void Application::initializeManagers()
{
  std::cout << "[Application] Initializing managers..." << std::endl;

  try {
    // Create CameraManager
    m_cameraManager = std::make_unique<CameraManager>();
    m_context.cameraManager = m_cameraManager.get();
    std::cout << "[Application] ✓ CameraManager created" << std::endl;

    std::cout << "[Application] All managers initialized successfully" << std::endl;
  }
  catch (const std::exception& e) {
    std::cerr << "[Application] Manager initialization failed: " << e.what() << std::endl;
    throw;
  }
}

void Application::shutdownManagers()
{
  std::cout << "[Application] Shutting down managers..." << std::endl;

  // Clear context pointers
  m_context = ApplicationContext();

  // Destroy managers
  m_cameraManager.reset();

  std::cout << "[Application] All managers shut down" << std::endl;
}

void Application::createMainWindow()
{
  std::cout << "[Application] Creating main window..." << std::endl;

  m_mainWindow = std::make_unique<MainWindow>(&m_context);
  m_mainWindow->setWindowTitle(QString::fromStdString(
    getApplicationName() + " v" + getVersion()));
  m_mainWindow->show();

  std::cout << "[Application] Main window created" << std::endl;
}
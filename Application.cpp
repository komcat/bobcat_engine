// Application.cpp
#include "Application.h"
#include "MainWindow.h"
#include "CameraManager.h"
#include "LogManager.h"  // ✅ NEW
#include "TcpClientManager.h" // ← NEW
#include <QApplication>
#include <iostream>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

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

  if (m_mainWindow) {
    m_mainWindow->close();
    m_mainWindow.reset();
  }

  shutdownManagers();

  m_initialized = false;
}

bool Application::initialize()
{
  if (m_initialized) return true;

  try {
    m_qApplication = std::make_unique<QApplication>(m_argc, m_argv);
    m_qApplication->setApplicationName(QString::fromStdString(getApplicationName()));
    m_qApplication->setApplicationVersion(QString::fromStdString(getVersion()));

    std::cout << "[Application] " << getApplicationName()
      << " v" << getVersion() << std::endl;

    initializeManagers();
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
    initializeLogManager();
    initializeDataStore();
    initializeTcpClientManager();
    initializeCameraManager();
    initializeMotionManager();  // ← Add this line

    std::cout << "[Application] All managers initialized successfully" << std::endl;
  }
  catch (const std::exception& e) {
    std::cerr << "[Application] Manager initialization failed: " << e.what() << std::endl;
    if (m_logManager) {
      m_logManager->Critical(std::string("Manager init failed: ") + e.what(), "Application");
    }
    throw;
  }
}

void Application::initializeLogManager()
{
  m_logManager = std::make_unique<LogManager>();
  m_context.logManager = m_logManager.get();
  m_logManager->EnableFileOutput(true, "logs/bobcat.log");
  m_logManager->Info("Bobcat Engine starting...", "Application");
}

void Application::initializeDataStore()
{
  m_dataStore = std::make_unique<DataStore>();
  m_context.dataStore = m_dataStore.get();
  m_logManager->Info("DataStore initialized", "Application");
}

void Application::initializeTcpClientManager()
{
  // FIXED: Pass nullptr as parent (or this if Application inherits QObject)
  m_tcpClientManager = std::make_unique<TcpClientManager>(m_dataStore.get(), nullptr);
  m_context.tcpClientManager = m_tcpClientManager.get();

  // Load TCP config - try local first, then default
  bool tcpConfigLoaded = false;

  // Try local config first (not in git)
  if (m_tcpClientManager->LoadConfig("config/tcp_clients.local.json")) {
    m_logManager->Info("TCP client config loaded from local file", "Application");
    tcpConfigLoaded = true;
  }
  // Fall back to default config
  else if (m_tcpClientManager->LoadConfig("config/tcp_clients.json")) {
    m_logManager->Info("TCP client config loaded from default file", "Application");
    tcpConfigLoaded = true;
  }
  else {
    m_logManager->Warning("No TCP client config found - using empty config", "Application");
  }

  // Auto-connect if config was loaded
  if (tcpConfigLoaded) {
    m_tcpClientManager->ConnectAutoClients();
  }
}


void Application::initializeCameraManager()
{
  m_cameraManager = std::make_unique<CameraManager>();
  m_context.cameraManager = m_cameraManager.get();
  m_logManager->Info("CameraManager initialized", "Application");
}


void Application::shutdownManagers()
{
  std::cout << "[Application] Shutting down managers..." << std::endl;

  if (m_motionManager) {
    m_motionManager->DisconnectAll();  // Disconnect before shutdown
  }


  // ✅ CRITICAL FIX: Clear UI callback FIRST before any logging
  if (m_logManager) {
    m_logManager->ClearUICallback();  // ← ADD THIS LINE
    m_logManager->Info("Bobcat Engine shutting down...", "Application");
  }

  m_context = ApplicationContext();

  m_cameraManager.reset();
  m_tcpClientManager.reset();
  m_dataStore.reset();
  m_logManager.reset();

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

  if (m_logManager) {
    m_logManager->Info("Main window created", "Application");
  }
}

void Application::initializeMotionManager() {
  std::cout << "[Application] Initializing motion manager..." << std::endl;

  try {
    // 1. Create UnifiedMotionManager
    m_motionManager = std::make_unique<UnifiedMotionManager>();
    m_context.motionManager = m_motionManager.get();
    m_logManager->Info("UnifiedMotionManager created", "Application");

    // 2. Load JSON config - try local first, then default
    std::string configPath;
    std::ifstream file;

    // Try local config first (not in git)
    file.open("config/motor_devices.local.json");
    if (file.good()) {
      configPath = "config/motor_devices.local.json";
      m_logManager->Info("Loading motion config from LOCAL file", "Application");
    }
    else {
      // Fall back to default config
      file.close();
      file.open("config/motor_devices.json");
      if (file.good()) {
        configPath = "config/motor_devices.json";
        m_logManager->Info("Loading motion config from DEFAULT file", "Application");
      }
      else {
        m_logManager->Warning("No motion config found - motion control not configured", "Application");
        return;
      }
    }

    // Parse JSON
    nlohmann::json config = nlohmann::json::parse(file);
    file.close();

    if (config.contains("motion_devices")) {
      int deviceCount = 0;

      for (const auto& deviceJson : config["motion_devices"]) {
        // Parse JSON to MotionDevice struct
        MotionDevice device;
        device.Name = deviceJson.value("name", "");
        device.TypeController = deviceJson.value("controller_type", "");
        device.IpAddress = deviceJson.value("ip_address", "");
        device.Port = deviceJson.value("port", 0);
        device.InstalledAxes = deviceJson.value("axes", "");
        device.Enabled = deviceJson.value("enabled", false);

        // Add device to UnifiedMotionManager
        m_motionManager->AddDevice(device);
        deviceCount++;

        m_logManager->Info(
          "Added device: " + device.Name +
          " (Type: " + device.TypeController +
          ", Enabled: " + (device.Enabled ? "Yes" : "No") + ")",
          "MotionManager"
        );
      }

      m_logManager->Info(
        "Loaded " + std::to_string(deviceCount) + " motion devices from " + configPath,
        "Application"
      );

      // Auto-connect enabled devices
      int connectedCount = m_motionManager->ConnectEnabledDevices();
      m_logManager->Info(
        "Connected " + std::to_string(connectedCount) + " devices",
        "Application"
      );
    }

    m_logManager->Info("Motion manager initialized successfully", "Application");
  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("Motion manager initialization failed: ") + e.what(),
      "Application"
    );
    // Don't throw - allow app to continue without motion control
  }
}
// Application.cpp
#include "Application.h"
#include "MainWindow.h"
#include "CameraManager.h"
#include "LogManager.h"
#include "TcpClientManager.h"
#include "EziIO_JsonLoader.h"
#include "PneumaticJsonLoader.h"
#include "product_position_manager.h"
#include "CameraConfigManager.h"
#include "AuxWindowManager.h"
#include "MotionJsonLoader.h"
#include "TransformationMatrixLoader.h"
#include "SequenceBuilder.h"
#include "SequenceContext.h"
#include "TECManager.h"
#include "TECJsonLoader.h"
#include "PowerSupplyManager.h"
#include "PowerSupplyJsonLoader.h"
#include "SMUManager.h"  // ✅ ADD SMU Manager
#include "SMUJsonLoader.h"  // ✅ ADD SMU Config Loader
#include "Keithley6482Adapter.h"  // ✅ For polling test and channel info
#include "CoordinateSystemManager.h"
#include "lighting/LightingManager.h"
#include "lighting/LightingJsonLoader.h"
#include "lighting/LightingTypes.h"
#include "settings/ApplicationSettings.h"
#include "safety/SafetyEventLog.h"
#include "customserver/BeamServerClient.h"
#include "ProcessPluginRegistry.h"
#include "DarkTheme.h"  // ✅ NEW: Centralized dark theme
#include "ConnectionSplashScreen.h"  // ✅ NEW: Connection progress splash
#include "startup/ConnectionConfig.h"
#include "startup/ConnectionWorkerPool.h"
#include "startup/ConnectionResult.h"
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QSettings>
#include <QTimer>  // ✅ NEW: For deferred hardware connection
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>  // for std::setprecision
#include <cstdlib>  // for std::getenv
#include "nlohmann/json.hpp"

using json = nlohmann::json;

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef HAS_OPENCV
#include "OpenCVTest.h"
#endif

Application::Application(int argc, char* argv[])
  : QObject(nullptr)
  , m_initialized(false)
  , m_argc(argc)
  , m_argv(argv)
  , m_splashScreen(nullptr)  // ✅ NEW: Initialize splash screen pointer
{
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);  // ← ADD HERE (inside constructor)
#endif
  // Note: m_logManager not yet initialized, use std::cout here only
}

Application::~Application()
{
  // Clean up splash screen
  if (m_splashScreen) {
    delete m_splashScreen;
    m_splashScreen = nullptr;
  }

  shutdown();
  // Note: m_logManager may already be destroyed, use std::cout for final message
}

int Application::run()
{
  if (!initialize()) {
    if (m_logManager) m_logManager->Error("Failed to initialize", "Application");
    return -1;
  }

  m_logManager->Info("Starting " + getApplicationName() + "...", "Application");

  // Initialize plugin registry (triggers static registration in plugin files)
  auto& pluginRegistry = ProcessPluginRegistry::Instance();
  m_logManager->Info("Plugin registry initialized with " +
            std::to_string(pluginRegistry.GetAllProcessTypes().size()) + " plugins", "Application");

  m_logManager->Debug("About to call exec()...", "Application");

  if (!m_qApplication) {
    m_logManager->Error("m_qApplication is NULL!", "Application");
    return -1;
  }

  m_logManager->Debug("Calling exec()...", "Application");
  int result = m_qApplication->exec();
  m_logManager->Debug("exec() returned: " + std::to_string(result), "Application");

  if (m_logManager) m_logManager->Info("Exited with code: " + std::to_string(result), "Application");

  // Shutdown immediately after event loop exits, before run() returns.
  // This ensures GDI camera windows and other resources are cleaned up
  // right away instead of waiting for the Application destructor.
  shutdown();

  return result;
}

void Application::shutdown()
{
  if (!m_initialized) return;

  if (m_logManager) m_logManager->Info("Shutting down...", "Application");

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

    // Load and set custom font from assets folder
    QString fontPath = "assets/fonts/RobotoMono-Regular.ttf";
    int fontId = QFontDatabase::addApplicationFont(fontPath);

    if (fontId != -1) {
      QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
      if (!fontFamilies.isEmpty()) {
        QFont appFont(fontFamilies.at(0), 11);  // Use loaded Roboto font, size 9
        m_qApplication->setFont(appFont);
        // Note: m_logManager not yet initialized here
      }
      else {
        QFont fallbackFont("Segoe UI", 11);
        m_qApplication->setFont(fallbackFont);
      }
    }
    else {
      QFont fallbackFont("Segoe UI", 9);
      m_qApplication->setFont(fallbackFont);
    }

    // Set application icon from embedded resource
    m_qApplication->setWindowIcon(QIcon(":/icons/logo.png"));

    // ✅ Set Fusion style for better stylesheet support (prevents Windows native style interference)
    m_qApplication->setStyle("Fusion");

    // ✅ UPDATED: Apply centralized dark theme globally to all Qt widgets
    m_qApplication->setStyleSheet(DarkTheme::getStyleSheet());

    // Note: Logging version info happens after LogManager init in initializeManagers()

    // ✅ Check BOBCAT_SIMULATION environment variable
    char* simEnv = nullptr;
    size_t simEnvLen = 0;
    _dupenv_s(&simEnv, &simEnvLen, "BOBCAT_SIMULATION");
    if (simEnv != nullptr) {
      std::string simValue(simEnv);
      free(simEnv);
      m_context.simulationMode = (simValue == "1" || simValue == "true" || simValue == "TRUE");
    } else {
      // Default to false if environment variable not set
      m_context.simulationMode = false;
    }

    // ✅ NEW: Show splash screen BEFORE initialization (immediate user feedback)
    if (!m_context.simulationMode) {
      m_splashScreen = new ConnectionSplashScreen();
      // Splash auto-shows in constructor

      // Start loading phase (15 managers total)
      m_splashScreen->startPhase(
        ConnectionSplashScreen::Phase::Loading,
        "Initializing System",
        15  // Total manager count
      );
    }

    initializeManagers();
    createMainWindow();

    // ✅ Start hardware connection phase
    if (!m_context.simulationMode) {
      // Switch to connecting phase
      m_splashScreen->startPhase(
        ConnectionSplashScreen::Phase::Connecting,
        "Connecting Hardware",
        5  // TCP, Cameras, Motion, I/O, TEC
      );

      // Use QTimer to defer hardware connection until after event loop starts
      QTimer::singleShot(100, [this]() {
        connectHardwareAsync();
      });
      m_logManager->Info("Hardware connections scheduled for background (with splash screen)", "Application");
    } else {
      m_logManager->Info("Simulation mode: Hardware connections skipped", "Application");
    }

    m_initialized = true;
    m_logManager->Info("Initialization complete (UI shown, hardware connecting in background)", "Application");

    return true;
  }
  catch (const std::exception& e) {
    if (m_logManager) m_logManager->Critical(std::string("Init failed: ") + e.what(), "Application");
    return false;
  }
}

void Application::initializeManagers()
{
  // LogManager will be initialized first in this function
  // Check for simulation mode - logged after LogManager init

  // Helper lambda for splash updates (no-op if simulation mode)
  auto updateSplash = [this](const QString& name) {
    if (m_splashScreen) {
      m_splashScreen->startStep(name);
    }
  };

  auto completeSplash = [this](const QString& name, bool success = true) {
    if (m_splashScreen) {
      m_splashScreen->completeStep(name, success);
    }
  };

  try {
    updateSplash("LogManager");
    initializeLogManager();
    completeSplash("LogManager");

    // Load application settings early (before other managers)
    updateSplash("ApplicationSettings");
    initializeApplicationSettings();
    completeSplash("ApplicationSettings");

    // Now we can log properly
    m_logManager->Info("Initializing managers...", "Application");
    m_logManager->Info(getApplicationName() + " v" + getVersion(), "Application");

    // Log simulation mode status
    if (m_context.simulationMode) {
      m_logManager->Warning("SIMULATION MODE ENABLED - Hardware connections will be SKIPPED", "Application");
    } else {
      m_logManager->Info("Hardware mode - will connect to physical devices", "Application");
    }

    updateSplash("DataStore");
    initializeDataStore();
    completeSplash("DataStore");

    updateSplash("TcpClientManager");
    initializeTcpClientManager();
    completeSplash("TcpClientManager");

    updateSplash("CameraManager");
    initializeCameraManager();
    completeSplash("CameraManager");

    updateSplash("MotionManager");
    initializeMotionManager();
    completeSplash("MotionManager");

    updateSplash("IOManager");
    initializeIOManager();
    completeSplash("IOManager");

    updateSplash("PneumaticManager");
    initializePneumaticManager();
    completeSplash("PneumaticManager");

    updateSplash("TECManager");
    initializeTECManager();
    completeSplash("TECManager");

    updateSplash("PowerSupplyManager");
    initializePowerSupplySystem();
    completeSplash("PowerSupplyManager");

    updateSplash("SMUManager");
    initializeSMUSystem();
    completeSplash("SMUManager");

    updateSplash("AuxWindowManager");
    initializeAuxWindowManager();
    completeSplash("AuxWindowManager");

    updateSplash("GdiCameraWindow");
    initializeGdiCameraWindow();
    completeSplash("GdiCameraWindow");

    updateSplash("GdiCameraWindow2");
    initializeGdiCameraWindow2();
    completeSplash("GdiCameraWindow2");

#ifdef USE_QT_CAMERA_WINDOW
    updateSplash("QtCameraWindow");
    initializeQtCameraWindow();
    completeSplash("QtCameraWindow");

    updateSplash("QtCameraWindow2");
    initializeQtCameraWindow2();
    completeSplash("QtCameraWindow2");
#endif

    updateSplash("SwirBeamWindow");
    initializeSwirBeamWindow();
    completeSplash("SwirBeamWindow");

    updateSplash("BeamOptimizer");
    initializeBeamOptimizer();
    completeSplash("BeamOptimizer");

    updateSplash("ProductPositionManager");
    initializeProductPositionManager();
    completeSplash("ProductPositionManager");

    // DeviceLinkManager must be initialized BEFORE SequenceEngine
    // because SequenceEngine's MotionCommandHandler needs it
    updateSplash("DeviceLinkManager");
    initializeDeviceLinkManager();
    completeSplash("DeviceLinkManager");

    updateSplash("SequenceEngine");
    initializeSequenceEngine();
    completeSplash("SequenceEngine");

    updateSplash("ScanController");
    initializeScanController();
    completeSplash("ScanController");

    updateSplash("CoordinateSystemManager");
    initializeCoordinateSystemManager();
    completeSplash("CoordinateSystemManager");

    updateSplash("LightingManager");
    initializeLightingManager();
    completeSplash("LightingManager");

    // Initialize beam server client (connection is recipe-driven, not automatic)
    m_beamServerClient = std::make_unique<BeamServerClient>();
    m_context.beamServerClient = m_beamServerClient.get();
    m_logManager->Info("BeamServerClient created (connection deferred to recipe)", "Application");

    // Wire beam server client to GDI camera windows (created earlier, needs late injection)
    if (m_gdiCameraWindowManager) {
      m_gdiCameraWindowManager->setBeamServerClient(m_beamServerClient.get());
    }
    if (m_gdiCameraWindowManager2) {
      m_gdiCameraWindowManager2->setBeamServerClient(m_beamServerClient.get());
    }

    // Initialize safety event log before safety watch (so safety watch can use it)
    updateSplash("SafetyEventLog");
    m_safetyEventLog = std::make_unique<SafetyEventLog>();
    if (m_safetyEventLog->Initialize("logs")) {
      m_context.safetyEventLog = m_safetyEventLog.get();
      m_logManager->Info("SafetyEventLog initialized", "Application");
    } else {
      m_logManager->Warning("SafetyEventLog failed to initialize", "Application");
    }
    completeSplash("SafetyEventLog");

    updateSplash("SafetyWatch");
    initializeSafetyWatch();
    completeSplash("SafetyWatch");

    // Initialize InterlockManager
    m_interlockManager = std::make_unique<InterlockManager>();
    m_interlockManager->SetMotionManager(m_motionManager.get());
    m_interlockManager->LoadConfig("config/interlocks.json");
    m_motionManager->SetInterlockManager(m_interlockManager.get());
    m_context.interlockManager = m_interlockManager.get();
    m_logManager->Info("InterlockManager initialized", "Application");

    // Optional: run test commands to verify managers
    //testMotionCommands();
    //testIOCommands();  // Test IO with Vacuum_Base

		//testPneumaticCommands();  // ✅ TEST PNEUMATICS

		//testSequenceEngine();  // ✅ TEST SEQUENCE ENGINE
		//testPneumaticSequence();	// ✅ TEST PNEUMATIC SEQUENCE

     // Optional: run test commands
    // testScanController();  // ✅ UNCOMMENT TO TEST

    // ✅ OpenCV Integration Test - uncomment to verify OpenCV is working
    testOpenCV();

    m_logManager->Info("All managers initialized successfully", "Application");
  }
  catch (const std::exception& e) {
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

  // ✅ DEFERRED: TCP auto-connect moved to connectHardwareAsync()
  if (tcpConfigLoaded) {
    m_logManager->Info("TCP client connection deferred to background thread", "Application");
  }
}

void Application::initializeCameraManager()
{
  m_cameraManager = std::make_unique<CameraManager>();
  m_context.cameraManager = m_cameraManager.get();

  // Set logger for CameraManager
  m_cameraManager->SetLogger(m_logManager.get());

  // Load cameras from JSON config - try local first, then default
  bool configLoaded = false;
  std::string configPath;

  m_cameraConfigManager = std::make_unique<CameraConfigManager>("config/camera_config.local.json");
  if (m_cameraConfigManager->LoadConfig()) {
    configPath = "config/camera_config.local.json";
    configLoaded = true;
    m_logManager->Info("Camera configuration loaded from LOCAL file", "Application");
  }
  else {
    m_cameraConfigManager = std::make_unique<CameraConfigManager>("config/camera_config.json");
    if (m_cameraConfigManager->LoadConfig()) {
      configPath = "config/camera_config.json";
      configLoaded = true;
      m_logManager->Info("Camera configuration loaded from DEFAULT file", "Application");
    }
    else {
      m_logManager->Warning("No camera config found - no cameras loaded", "Application");
    }
  }

  if (configLoaded) {
    m_context.cameraConfigManager = m_cameraConfigManager.get();

    if (m_cameraConfigManager->InitializeCameraManager(*m_cameraManager)) {
      m_logManager->Info("CameraManager initialized from: " + configPath, "Application");

      // ✅ DEFERRED: Camera connection moved to connectHardwareAsync()
      m_logManager->Info("Camera connection deferred to background thread", "Application");
    }
    else {
      m_logManager->Warning("Failed to initialize CameraManager with config", "Application");
    }
  }

  m_logManager->Info("CameraManager initialized (connections deferred)", "Application");
}

void Application::initializeMotionManager()
{
  m_logManager->Debug("Initializing motion manager...", "Application");

  try {
    // 1. Create UnifiedMotionManager
    m_motionManager = std::make_unique<UnifiedMotionManager>();
    m_context.motionManager = m_motionManager.get();
    m_logManager->Info("UnifiedMotionManager created", "Application");

    // 2. Load config using MotionJsonLoader - try local first, then default
    int loadedCount = 0;
    std::string configPath;

    // Try local config first (not in git)
    loadedCount = MotionJsonLoader::LoadFromFile(*m_motionManager, "config/motor_devices.local.json");
    if (loadedCount > 0) {
      configPath = "config/motor_devices.local.json";
      m_logManager->Info("Loading motion config from LOCAL file", "Application");
    }
    else {
      // Fall back to default config
      loadedCount = MotionJsonLoader::LoadFromFile(*m_motionManager, "config/motor_devices.json");
      if (loadedCount > 0) {
        configPath = "config/motor_devices.json";
        m_logManager->Info("Loading motion config from DEFAULT file", "Application");
      }
      else {
        m_logManager->Warning("No motion config found - motion control not configured", "Application");
        return;
      }
    }

    m_logManager->Info(
      "Loaded " + std::to_string(loadedCount) + " motion devices from " + configPath,
      "Application"
    );

    // Store config path for position save operations (RetraceEdit feature)
    m_motionManager->SetConfigFilePath(configPath);

    // 3. Load PI pivot point configurations
    PIControllerManager* piManager = m_motionManager->GetPIManager();
    if (piManager) {
      bool pivotLoaded = false;
      std::string pivotConfigPath;

      // Try local config first (not in git)
      if (piManager->LoadPivotConfigurations("config/pi_pivot_config.local.json")) {
        pivotConfigPath = "config/pi_pivot_config.local.json";
        pivotLoaded = true;
        m_logManager->Info("PI pivot configurations loaded from LOCAL file", "Application");
      }
      // Fall back to default config
      else if (piManager->LoadPivotConfigurations("config/pi_pivot_config.json")) {
        pivotConfigPath = "config/pi_pivot_config.json";
        pivotLoaded = true;
        m_logManager->Info("PI pivot configurations loaded from default file", "Application");
      }

      if (!pivotLoaded) {
        m_logManager->Warning("PI pivot configurations not loaded - pivot verification disabled", "Application");
      }
    }

    // ✅ DEFERRED: Motion device connection moved to connectHardwareAsync()
    m_logManager->Info("Motion device connection deferred to background thread", "Application");

    // Set logger for motion manager (propagates to ACS/PI controllers)
    m_motionManager->SetLogger(m_logManager.get());

    m_logManager->Info("Motion manager initialized (connections deferred)", "Application");
  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("Motion manager initialization failed: ") + e.what(),
      "Application"
    );
    // Don't throw - allow app to continue without motion control
  }
}


void Application::initializeIOManager()
{
  m_logManager->Debug("Initializing IO manager...", "Application");

  try {
    // 1. Create EziIOManager
    m_ioManager = std::make_unique<EziIOManager>();
    m_context.ioManager = m_ioManager.get();
    m_logManager->Info("EziIOManager created", "Application");

    // 2. Initialize the manager
    if (!m_ioManager->Initialize()) {
      m_logManager->Error("Failed to initialize EziIOManager", "Application");
      return;
    }

    // 3. Load JSON config - try local first, then default
    std::string configPath;
    int loadedCount = 0;

    // Try local config first (not in git)
    loadedCount = EziIOJsonLoader::LoadFromFile(*m_ioManager, "config/io_devices.local.json");
    if (loadedCount > 0) {
      configPath = "config/io_devices.local.json";
      m_logManager->Info("Loading IO config from LOCAL file", "Application");
    }
    else {
      // Fall back to default config
      loadedCount = EziIOJsonLoader::LoadFromFile(*m_ioManager, "config/io_devices.json");
      if (loadedCount > 0) {
        configPath = "config/io_devices.json";
        m_logManager->Info("Loading IO config from DEFAULT file", "Application");
      }
      else {
        m_logManager->Warning("No IO config found - IO control not configured", "Application");
        return;
      }
    }

    m_logManager->Info(
      "Loaded " + std::to_string(loadedCount) + " IO devices from " + configPath,
      "Application"
    );

    // ✅ DEFERRED: IO device connection and polling moved to connectHardwareAsync()
    m_logManager->Info("IO device connection and polling deferred to background thread", "Application");

    m_logManager->Info("IO manager initialized (connections deferred)", "Application");
  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("IO manager initialization failed: ") + e.what(),
      "Application"
    );
    // Don't throw - allow app to continue without IO control
  }
}


void Application::testIOCommands()
{
  if (!m_ioManager) {
    m_logManager->Warning("IO Manager not available for testing", "Application");
    return;
  }

  m_logManager->Info("======================================", "IO Test");
  m_logManager->Info("Testing IO Commands with Vacuum_Base", "IO Test");
  m_logManager->Info("======================================", "IO Test");

  // Test 1: Turn ON Vacuum_Base
  m_logManager->Info("Test 1: Turning ON Vacuum_Base (pin 10)...", "IO Test");
  EziIOError result = m_ioManager->SetOutputByName("IOBottom", "Vacuum_Base", true);

  if (result == EziIOError::SUCCESS) {
    m_logManager->Info("✓ Vacuum_Base turned ON successfully!", "IO Test");
  }
  else {
    m_logManager->Error("✗ Failed to turn ON Vacuum_Base: " + GetErrorString(result), "IO Test");
    return;
  }

  // Test 2: Wait 1 second
  m_logManager->Info("Waiting 1 second...", "IO Test");
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Test 3: Turn OFF Vacuum_Base
  m_logManager->Info("Test 2: Turning OFF Vacuum_Base...", "IO Test");
  result = m_ioManager->SetOutputByName("IOBottom", "Vacuum_Base", false);

  if (result == EziIOError::SUCCESS) {
    m_logManager->Info("✓ Vacuum_Base turned OFF successfully!", "IO Test");
  }
  else {
    m_logManager->Error("✗ Failed to turn OFF Vacuum_Base: " + GetErrorString(result), "IO Test");
  }

  // Test 4: Verify output status
  m_logManager->Info("Test 3: Verifying output status...", "IO Test");
  uint64_t outputs = 0, status = 0;
  if (m_ioManager->ReadOutputs("IOBottom", outputs, status) == EziIOError::SUCCESS) {
    bool vacuum_state = (outputs & (1ULL << 10)) != 0;  // Pin 10
    m_logManager->Info(
      "Vacuum_Base current state: " + std::string(vacuum_state ? "ON" : "OFF"),
      "IO Test"
    );
  }

  m_logManager->Info("======================================", "IO Test");
  m_logManager->Info("IO Test completed!", "IO Test");
  m_logManager->Info("======================================", "IO Test");
}

// ============================================================================
// BACKGROUND HARDWARE CONNECTION (PARALLEL)
// ============================================================================

void Application::connectHardwareAsync()
{
  m_logManager->Info("========================================", "Application");
  m_logManager->Info("PARALLEL HARDWARE CONNECTION STARTING", "Application");
  m_logManager->Info("========================================", "Application");

  try {
    // Load connection configuration
    auto& config = Startup::ConnectionConfig::instance();
    config.loadFromFile("config/connection_config.json");

    // Create connection worker pool
    m_connectionPool = std::make_unique<Startup::ConnectionWorkerPool>(this);

    // Connect signals for splash screen updates
    connect(m_connectionPool.get(), &Startup::ConnectionWorkerPool::taskStarted,
            this, &Application::onConnectionTaskStarted);
    connect(m_connectionPool.get(), &Startup::ConnectionWorkerPool::taskCompleted,
            this, &Application::onConnectionTaskCompleted);
    connect(m_connectionPool.get(), &Startup::ConnectionWorkerPool::allTasksCompleted,
            this, &Application::onAllConnectionsCompleted);

    // Add all connection tasks
    addConnectionTasks();

    // Execute all tasks asynchronously (parallel within phases)
    m_connectionPool->executeAllAsync();

    m_logManager->Info("Parallel connections started in background", "Application");
  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("Failed to start parallel connections: ") + e.what(),
      "Application"
    );

    // Close splash on error
    if (m_splashScreen) {
      m_splashScreen->close();
    }
  }
}

void Application::addConnectionTasks()
{
  auto& config = Startup::ConnectionConfig::instance();

  // Phase 0: TCP Clients
  if (m_tcpClientManager && config.isEnabled("TcpClientManager")) {
    m_connectionPool->addTask(
      "TcpClientManager",
      "TCP Clients",
      [this]() -> int {
        m_logManager->Info("Connecting TCP clients...", "Application");
        m_tcpClientManager->ConnectAutoClients();
        return 1;  // Consider it success
      },
      static_cast<int>(config.getTimeout("TcpClientManager").count()),
      config.getPhase("TcpClientManager")
    );
  }

  // Phase 0: Cameras
  if (m_cameraManager && config.isEnabled("CameraManager")) {
    m_connectionPool->addTask(
      "CameraManager",
      "Cameras",
      [this]() -> int {
        m_logManager->Info("Connecting cameras...", "Application");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // Settle time
        int count = m_cameraManager->ConnectAllCameras();
        m_cameraManager->StartBroadcastSystem();
        m_cameraManager->StartGrabbingAll();

        // Apply default camera settings: auto exposure ON, auto gain OFF, gain 0, frame rate 10
        for (const auto& camId : m_cameraManager->GetCameraIds()) {
          auto status = m_cameraManager->GetCameraStatus(camId);
          if (status.connected) {
            auto settings = m_cameraManager->GetExposureSettings(camId);
            settings.auto_exposure = true;
            settings.auto_gain = false;
            settings.gain = 0.0;
            settings.frame_rate = 10.0;
            m_cameraManager->ApplyExposureSettings(camId, settings);
          }
        }
        m_logManager->Info("Cameras connected and broadcasting", "Application");
        return count;
      },
      static_cast<int>(config.getTimeout("CameraManager").count()),
      config.getPhase("CameraManager")
    );
  }

  // Phase 1: Motion Controllers
  if (m_motionManager && config.isEnabled("MotionManager")) {
    m_connectionPool->addTask(
      "MotionManager",
      "Motion Controllers",
      [this]() -> int {
        m_logManager->Info("Connecting motion controllers...", "Application");
        int count = m_motionManager->ConnectEnabledDevices();
        m_logManager->Info(
          "Connected " + std::to_string(count) + " motion devices",
          "Application"
        );
        return count;
      },
      static_cast<int>(config.getTimeout("MotionManager").count()),
      config.getPhase("MotionManager")
    );
  }

  // Phase 1: I/O Devices
  if (m_ioManager && config.isEnabled("IOManager")) {
    m_connectionPool->addTask(
      "IOManager",
      "I/O Devices",
      [this]() -> int {
        m_logManager->Info("Connecting I/O devices...", "Application");
        int count = m_ioManager->ConnectEnabledDevices();
        m_logManager->Info(
          "Connected " + std::to_string(count) + " I/O devices",
          "Application"
        );
        // Start polling
        m_ioManager->StartPolling(100);
        m_logManager->Info("I/O polling started (100ms interval)", "Application");
        return count;
      },
      static_cast<int>(config.getTimeout("IOManager").count()),
      config.getPhase("IOManager")
    );
  }

  // Phase 2: TEC Devices
  if (m_tecManager && config.isEnabled("TECManager")) {
    m_connectionPool->addTask(
      "TECManager",
      "TEC Devices",
      [this]() -> int {
        m_logManager->Info("Connecting TEC devices...", "Application");
        int count = m_tecManager->ConnectEnabledDevices();
        m_logManager->Info(
          "Connected " + std::to_string(count) + " TEC devices",
          "Application"
        );
        return count;
      },
      static_cast<int>(config.getTimeout("TECManager").count()),
      config.getPhase("TECManager")
    );
  }

  // Phase 2: Lighting Controllers
  if (m_lightingManager && config.isEnabled("LightingManager")) {
    m_connectionPool->addTask(
      "LightingManager",
      "Lighting",
      [this]() -> int {
        m_logManager->Info("Connecting lighting controllers...", "Application");
        int count = m_lightingManager->ConnectEnabledDevices();
        m_logManager->Info(
          "Connected " + std::to_string(count) + " lighting controllers",
          "Application"
        );

        // Print status report for all channels
        if (count > 0) {
          m_logManager->Info("========================================", "Lighting");
          m_logManager->Info("LIGHTING STATUS REPORT", "Lighting");
          m_logManager->Info("========================================", "Lighting");

          auto allStatus = m_lightingManager->GetAllStatus();
          for (const auto& ctrlStatus : allStatus) {
            m_logManager->Info("Controller: " + ctrlStatus.deviceName +
                      " (" + ctrlStatus.deviceId + ")", "Lighting");
            m_logManager->Info(std::string("Connected: ") + (ctrlStatus.connected ? "YES" : "NO"), "Lighting");

            if (ctrlStatus.connected) {
              for (const auto& chStatus : ctrlStatus.channelStatuses) {
                double percent = Lighting::IntensityToPercent(chStatus.intensity);
                std::stringstream ss;
                ss << "  L" << (chStatus.channelNumber + 1)
                   << " [" << chStatus.name << "]: "
                   << (chStatus.isOn ? "ON " : "OFF")
                   << " | Intensity: " << chStatus.intensity
                   << " (" << std::fixed << std::setprecision(1) << percent << "%)"
                   << " | Mode: " << chStatus.mode;
                m_logManager->Info(ss.str(), "Lighting");
              }
            }
          }
          m_logManager->Info("========================================", "Lighting");

          // Startup test: Cycle through Red, Green, Blue - 1 second each
          m_logManager->Info("Running startup test: RGB cycle...", "Lighting");

          int intensity50 = Lighting::PercentToIntensity(50.0);  // 50% intensity

          for (const auto& deviceId : m_lightingManager->GetControllerIds()) {
            // Red (channel 1) - 1 second
            m_logManager->Debug("RED ON", "Lighting");
            m_lightingManager->SetIntensityByName(deviceId, "Red", intensity50);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            m_lightingManager->SetIntensityByName(deviceId, "Red", 0);

            // Green (channel 2) - 1 second
            m_logManager->Debug("GREEN ON", "Lighting");
            m_lightingManager->SetIntensityByName(deviceId, "Green", intensity50);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            m_lightingManager->SetIntensityByName(deviceId, "Green", 0);

            // Blue (channel 3) - 1 second
            m_logManager->Debug("BLUE ON", "Lighting");
            m_lightingManager->SetIntensityByName(deviceId, "Blue", intensity50);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            m_lightingManager->SetIntensityByName(deviceId, "Blue", 0);
          }

          // Ensure all off
          m_lightingManager->AllOff();
          m_logManager->Info("Startup test complete - All channels OFF", "Lighting");

          // Apply saved default intensities from config
          m_logManager->Debug("Applying saved default intensities...", "Lighting");
          for (const auto& deviceId : m_lightingManager->GetControllerIds()) {
            m_lightingManager->ApplyDefaultIntensities(deviceId);
          }
        }

        return count;
      },
      static_cast<int>(config.getTimeout("LightingManager").count()),
      config.getPhase("LightingManager")
    );
  }
}

void Application::onConnectionTaskStarted(const QString& displayName)
{
  if (m_splashScreen) {
    m_splashScreen->startStep(displayName);
  }
}

void Application::onConnectionTaskCompleted(const Startup::ConnectionResult& result)
{
  if (m_splashScreen) {
    m_splashScreen->completeStep(result.displayName, result.isSuccess());
  }

  // Log the result
  if (result.isSuccess()) {
    m_logManager->Info(
      result.managerName.toStdString() + " connected (" +
      std::to_string(result.devicesConnected) + " devices, " +
      std::to_string(result.duration.count()) + "ms)",
      "Application"
    );
  }
  else if (result.status == Startup::ConnectionStatus::TimedOut) {
    m_logManager->Warning(
      result.managerName.toStdString() + " timed out after " +
      std::to_string(result.duration.count()) + "ms",
      "Application"
    );
  }
  else {
    m_logManager->Error(
      result.managerName.toStdString() + " failed: " +
      result.errorMessage.toStdString(),
      "Application"
    );
  }

  // Start safety watch after IO is connected (regardless of phase completion)
  if (result.managerName == "IOManager" && result.isSuccess()) {
    if (m_safetyWatchManager && m_safetyWatchManager->IsEnabled()) {
      if (!m_safetyWatchManager->StartWatching()) {
        m_logManager->Error("Failed to start safety watch", "Application");
      }
    }
  }
}

void Application::onAllConnectionsCompleted(const Startup::ConnectionSummary& summary)
{
  m_logManager->Info("========================================", "Application");
  m_logManager->Info("PARALLEL HARDWARE CONNECTION COMPLETE", "Application");
  m_logManager->Info("Total: " + std::to_string(summary.totalTasks) + " tasks", "Application");
  m_logManager->Info("Success: " + std::to_string(summary.successfulTasks), "Application");
  m_logManager->Info("Failed: " + std::to_string(summary.failedTasks), "Application");
  m_logManager->Info("Timed Out: " + std::to_string(summary.timedOutTasks), "Application");
  m_logManager->Info("Duration: " + std::to_string(summary.totalDuration.count()) + "ms", "Application");
  m_logManager->Info("========================================", "Application");

  if (summary.allSuccessful()) {
    m_logManager->Info("All hardware connections completed successfully", "Application");
  }
  else {
    m_logManager->Warning(
      "Hardware connections completed with " +
      std::to_string(summary.failedTasks) + " failures and " +
      std::to_string(summary.timedOutTasks) + " timeouts",
      "Application"
    );
  }

  // Notify splash screen that all connections are complete
  if (m_splashScreen) {
    m_splashScreen->onConnectionsComplete();
  }
}

void Application::performEStopCascade()
{
  int successCount = 0;
  int totalCount = 8;
  std::string details;

  // 1. Motion: emergency stop all axes
  if (m_motionManager) {
    bool ok = m_motionManager->EmergencyStopAll();
    if (ok) ++successCount;
    details += "Motion: " + std::string(ok ? "OK" : "FAIL");
  } else {
    details += "Motion: N/A";
  }

  // 2. TEC: disable all outputs
  details += ", ";
  if (m_tecManager) {
    m_tecManager->DisableAllOutputs();
    ++successCount;
    details += "TEC: OK";
  } else {
    details += "TEC: N/A";
  }

  // 3. PSU: disable all outputs
  details += ", ";
  if (m_powerSupplyManager) {
    m_powerSupplyManager->disableAllOutputs();
    ++successCount;
    details += "PSU: OK";
  } else {
    details += "PSU: N/A";
  }

  // 4. SMU Polling: stop polling
  details += ", ";
  if (m_smuPollingManager) {
    m_smuPollingManager->StopAllPolling();
    ++successCount;
    details += "SMU-Poll: OK";
  } else {
    details += "SMU-Poll: N/A";
  }

  // 5. SMU: disable all outputs
  details += ", ";
  if (m_smuManager) {
    m_smuManager->OutputOffAll();
    ++successCount;
    details += "SMU: OK";
  } else {
    details += "SMU: N/A";
  }

  // 6. Pneumatic: stop all slides
  details += ", ";
  if (m_pneumaticManager) {
    m_pneumaticManager->StopAll();
    ++successCount;
    details += "Pneumatic: OK";
  } else {
    details += "Pneumatic: N/A";
  }

  // 7. Lighting: all off
  details += ", ";
  if (m_lightingManager) {
    m_lightingManager->AllOff();
    ++successCount;
    details += "Lighting: OK";
  } else {
    details += "Lighting: N/A";
  }

  // 8. SWIR Beam: cancel any running optimization
  details += ", ";
  if (m_swirBeamWindowManager) {
    m_swirBeamWindowManager->emergencyStop();
    ++successCount;
    details += "SWIR: OK";
  } else {
    details += "SWIR: N/A";
  }

  // Build summary
  std::string summary = "E-Stop cascade: " + std::to_string(successCount) +
                         "/" + std::to_string(totalCount) +
                         " subsystems stopped (" + details + ")";

  // Store on SafetyWatchManager for dialog display
  if (m_safetyWatchManager) {
    m_safetyWatchManager->SetEStopSummary(summary);
  }

  // Log aggregate event to safety event log
  if (m_safetyEventLog) {
    m_safetyEventLog->LogEvent(SafetySeverity::CRITICAL, "Application",
      "ESTOP_CASCADE_COMPLETE", summary, ErrorCode::SafetyEStopCascadeComplete);
  }

  if (m_logManager) {
    m_logManager->Error(summary, "Application");
  }

  std::cout << "[Application] " << summary << std::endl;
}

void Application::shutdownManagers()
{
  if (m_logManager) m_logManager->Info("Shutting down managers...", "Application");

  // ✅ CRITICAL: Mark DataStore as shutting down FIRST
  // This prevents crash from pending callbacks trying to write during destruction
  if (m_dataStore) {
    if (m_logManager) m_logManager->Debug("Initiating DataStore shutdown...", "Application");
    m_dataStore->BeginShutdown();
  }

  // Stop safety watch first
  if (m_safetyWatchManager) {
    if (m_logManager) m_logManager->Debug("Stopping safety watch...", "Application");
    m_safetyWatchManager->StopWatching();
  }

  // ✅ CRITICAL: Stop SMU polling and disconnect signals BEFORE touching DataStore
  // This prevents crash where polling thread tries to access destroyed DataStore
  if (m_smuPollingManager) {
    if (m_logManager) m_logManager->Debug("Stopping SMU polling...", "Application");
    m_smuPollingManager->StopAllPolling();
    m_smuPollingManager->disconnect();  // Disconnect all Qt signals
  }

  // Disconnect all PSU devices
  if (m_context.powerSupplyManager) {
    m_context.powerSupplyManager->disconnectAll();
  }

  // Disconnect all SMU devices (after polling is stopped)
  if (m_context.smuManager) {
    m_context.smuManager->disconnect();  // Disconnect signals first
    m_context.smuManager->DisconnectAll();
  }


  // Shutdown in reverse order
  // BeamOptimizer must shut down before SwirBeamWindow (unsubscribes from broker)
  if (m_beamOptimizerManager) {
    m_beamOptimizerManager->Shutdown();
    m_beamOptimizerManager.reset();
  }
  if (m_beamResultBroker) {
    m_beamResultBroker->BeginShutdown();
    m_beamResultBroker.reset();
  }

  if (m_swirBeamWindowManager) {
    m_swirBeamWindowManager->shutdown();
    m_swirBeamWindowManager.reset();
  }

  if (m_gdiCameraWindowManager2) {
    m_gdiCameraWindowManager2->shutdown();
    m_gdiCameraWindowManager2.reset();
  }

  if (m_gdiCameraWindowManager) {
    m_gdiCameraWindowManager->shutdown();
    m_gdiCameraWindowManager.reset();
  }

#ifdef USE_QT_CAMERA_WINDOW
  if (m_qtCameraWindow2) {
    m_qtCameraWindow2->shutdown();
    m_qtCameraWindow2.reset();
  }
  if (m_qtCameraWindow) {
    m_qtCameraWindow->shutdown();
    m_qtCameraWindow.reset();
  }
#endif

  if (m_auxWindowManager) {
    m_auxWindowManager->shutdown();
    m_auxWindowManager.reset();
  }

  // Disconnect motion manager
  if (m_motionManager) {
    m_motionManager->DisconnectAll();
  }

  // Shutdown pneumatic manager (stops all slides)
  if (m_pneumaticManager) {
    m_pneumaticManager->StopAll();
  }

  // Shutdown IO manager
  if (m_ioManager) {
    m_ioManager->StopPolling();
    m_ioManager->DisconnectAll();
    m_ioManager->Shutdown();
  }

  // LOG BEFORE CLEARING UI CALLBACK
  if (m_logManager) {
    m_logManager->Info("Bobcat Engine shutting down...", "Application");
    m_logManager->Info("IO manager shut down", "Application");
    m_logManager->Info("Pneumatic manager shut down", "Application");
    m_logManager->ClearUICallback();  // Clear AFTER all logging
  }

  m_context = ApplicationContext();

  // ✅ CRITICAL: Reset SMU polling manager BEFORE DataStore (it emits to DataStore)
  m_smuPollingManager.reset();
  m_safetyWatchManager.reset();
  m_safetyEventLog.reset();
  m_deviceLinkManager.reset();
  m_interlockManager.reset();
  m_powerSupplyManager.reset();
  m_smuManager.reset();
  m_lightingManager.reset();
  m_tecManager.reset();
  m_productPositionManager.reset();
  m_pneumaticManager.reset();
  m_scanController.reset();
  m_ioManager.reset();
  m_motionManager.reset();
  m_cameraManager.reset();
  m_tcpClientManager.reset();
  m_dataStore.reset();
  m_beamServerClient.reset();
  m_sequenceEngine.reset();
  // Note: Log final message before resetting logger
  if (m_logManager) m_logManager->Info("All managers shut down", "Application");
  m_logManager.reset();
}


void Application::createMainWindow()
{
  m_logManager->Debug("Creating main window...", "Application");

  m_mainWindow = std::make_unique<MainWindow>(&m_context);
  m_mainWindow->setWindowTitle(QString::fromStdString(
    getApplicationName() + " v" + getVersion()));
  m_mainWindow->show();

  // ✅ AuxWindowManager is now initialized earlier (before SequenceEngine)
  // This ensures the AuxWindowCommandHandler gets a valid pointer
  // The window should already be visible at this point

  m_logManager->Info("Main window created", "Application");
}

void Application::testMotionCommands()
{
  if (!m_motionManager) return;

  m_logManager->Info("Testing motion commands...", "Application");

  // Test 1: Query positions
  double xPos = 0.0;
  if (m_motionManager->GetPosition("gantry-main", "X", xPos)) {
    m_logManager->Info("Gantry X position: " + std::to_string(xPos), "Motion");
  }

  // Test 2: Small relative move
  m_logManager->Info("Moving gantry X by 1mm...", "Motion");
  if (m_motionManager->MoveRelative("gantry-main", "X", 1.0, true)) {
    m_logManager->Info("Move completed!", "Motion");
  }

  // Test 3: Query hex position
  PositionStruct hexPos = m_motionManager->GetCurrentPosition("hex-left");
  m_logManager->Info("Hex-left position: X=" + std::to_string(hexPos.x) +
    " Y=" + std::to_string(hexPos.y) +
    " Z=" + std::to_string(hexPos.z), "Motion");
}

void Application::initializePneumaticManager()
{
  m_logManager->Debug("Initializing Pneumatic manager...", "Application");

  try {
    // IMPORTANT: Pneumatic manager requires IO manager to be initialized first!
    if (!m_ioManager) {
      m_logManager->Error("Cannot initialize Pneumatic manager - IO manager not available", "Application");
      return;
    }

    // 1. Create PneumaticManager (pass IO manager)
    m_pneumaticManager = std::make_unique<PneumaticManager>(m_ioManager.get());
    m_context.pneumaticManager = m_pneumaticManager.get();
    m_logManager->Info("PneumaticManager created", "Application");

    // 2. Load JSON config - try local first, then default
    std::string configPath;
    int loadedCount = 0;

    // Try local config first (not in git)
    loadedCount = PneumaticJsonLoader::LoadFromFile(*m_pneumaticManager, "config/pneumatic_slides.local.json");
    if (loadedCount > 0) {
      configPath = "config/pneumatic_slides.local.json";
      m_logManager->Info("Loading Pneumatic config from LOCAL file", "Application");
    }
    else {
      // Fall back to default config
      loadedCount = PneumaticJsonLoader::LoadFromFile(*m_pneumaticManager, "config/pneumatic_slides.json");
      if (loadedCount > 0) {
        configPath = "config/pneumatic_slides.json";
        m_logManager->Info("Loading Pneumatic config from DEFAULT file", "Application");
      }
      else {
        m_logManager->Warning("No Pneumatic config found - pneumatic control not configured", "Application");
        return;
      }
    }

    m_logManager->Info(
      "Loaded " + std::to_string(loadedCount) + " pneumatic slides from " + configPath,
      "Application"
    );

    // 3. Set up global callbacks for logging
    m_pneumaticManager->SetGlobalPositionCallback(
      [this](const std::string& slideName, PneumaticPosition oldPos, PneumaticPosition newPos) {
      std::string message = slideName + ": " +
        GetPositionString(oldPos) + " → " + GetPositionString(newPos);
      m_logManager->Info(message, "Pneumatic");
    }
    );

    m_pneumaticManager->SetGlobalErrorCallback(
      [this](const std::string& slideName, PneumaticError error, const std::string& message) {
      std::string errorMsg = slideName + ": " +
        GetPneumaticErrorString(error) + " - " + message;
      m_logManager->Error(errorMsg, "Pneumatic");
    }
    );

    m_logManager->Info("Pneumatic manager initialized successfully", "Application");
  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("Pneumatic manager initialization failed: ") + e.what(),
      "Application"
    );
    // Don't throw - allow app to continue without pneumatic control
  }
}


void Application::testPneumaticCommands()
{
  if (!m_pneumaticManager) {
    m_logManager->Warning("Pneumatic Manager not available for testing", "Application");
    return;
  }

  m_logManager->Info("======================================", "Pneumatic Test");
  m_logManager->Info("Testing Pneumatic Commands", "Pneumatic Test");
  m_logManager->Info("======================================", "Pneumatic Test");

  // Test 1: Check initial positions
  m_logManager->Info("Test 1: Reading initial positions...", "Pneumatic Test");

  auto slideNames = m_pneumaticManager->GetAllSlideNames();
  for (const auto& name : slideNames) {
    PneumaticPosition pos = m_pneumaticManager->GetPosition(name);
    m_logManager->Info(
      name + " position: " + GetPositionString(pos),
      "Pneumatic Test"
    );
  }

  // Test 2: Extend ALL slides (blocking with wait)
  m_logManager->Info("Test 2: Extending all slides...", "Pneumatic Test");

  for (const auto& name : slideNames) {
    m_logManager->Info("Extending " + name + "...", "Pneumatic Test");
    PneumaticError result = m_pneumaticManager->ExtendAndWait(name, 50);

    if (result == PneumaticError::SUCCESS) {
      m_logManager->Info("✓ " + name + " extended successfully!", "Pneumatic Test");
    }
    else {
      m_logManager->Error(
        "✗ Failed to extend " + name + ": " + GetPneumaticErrorString(result),
        "Pneumatic Test"
      );
    }

    // Small delay between slides
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  // Test 3: Wait a bit, then check all positions
  m_logManager->Info("Test 3: Waiting 2 seconds then checking positions...", "Pneumatic Test");
  std::this_thread::sleep_for(std::chrono::seconds(2));

  for (const auto& name : slideNames) {
    PneumaticStatus status = m_pneumaticManager->GetStatus(name);

    std::string statusMsg = name + ": " + GetPositionString(status.Position) +
      " | Output: " + (status.OutputActive ? "ON" : "OFF") +
      " | Extended sensor: " + (status.ExtendedSensorActive ? "ON" : "OFF") +
      " | Retracted sensor: " + (status.RetractedSensorActive ? "ON" : "OFF");

    m_logManager->Info(statusMsg, "Pneumatic Test");
  }

  // Test 4: Retract ALL slides (blocking with wait)
  m_logManager->Info("Test 4: Retracting all slides...", "Pneumatic Test");

  for (const auto& name : slideNames) {
    m_logManager->Info("Retracting " + name + "...", "Pneumatic Test");
    PneumaticError result = m_pneumaticManager->RetractAndWait(name, 50);

    if (result == PneumaticError::SUCCESS) {
      m_logManager->Info("✓ " + name + " retracted successfully!", "Pneumatic Test");
    }
    else {
      m_logManager->Error(
        "✗ Failed to retract " + name + ": " + GetPneumaticErrorString(result),
        "Pneumatic Test"
      );
    }

    // Small delay between slides
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  // Test 5: Final status
  m_logManager->Info("Test 5: Final status of all slides...", "Pneumatic Test");
  for (const auto& name : slideNames) {
    PneumaticStatus status = m_pneumaticManager->GetStatus(name);

    std::string statusMsg = name + ": " + GetPositionString(status.Position) +
      " | Output: " + (status.OutputActive ? "ON" : "OFF") +
      " | Extended sensor: " + (status.ExtendedSensorActive ? "ON" : "OFF") +
      " | Retracted sensor: " + (status.RetractedSensorActive ? "ON" : "OFF");

    m_logManager->Info(statusMsg, "Pneumatic Test");
  }

  m_logManager->Info("======================================", "Pneumatic Test");
  m_logManager->Info("Pneumatic Test completed!", "Pneumatic Test");
  m_logManager->Info("======================================", "Pneumatic Test");
}



// ✅ ADD THIS FUNCTION
void Application::initializeSequenceEngine()
{
  m_logManager->Debug("Initializing Sequence Engine...", "Application");

  try {
    // Create SequenceEngine
    m_sequenceEngine = std::make_unique<SequenceEngine>(&m_context);
    m_context.sequenceEngine = m_sequenceEngine.get();  // ✅ ADD THIS

    // Set logger
    m_sequenceEngine->SetLogger(m_logManager.get());

    // Build device registry AFTER all managers are loaded
    m_sequenceEngine->BuildDeviceRegistry();

    m_logManager->Info("SequenceEngine initialized", "Application");
  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("Sequence engine initialization failed: ") + e.what(),
      "Application"
    );
    // Don't throw - allow app to continue without sequence engine
  }
}

// ✅ ADD THIS FUNCTION (OPTIONAL TEST)
void Application::testSequenceEngine()
{
  if (!m_sequenceEngine) {
    m_logManager->Warning("SequenceEngine not available for testing", "Application");
    return;
  }

  m_logManager->Info("======================================", "Sequence Test");
  m_logManager->Info("Testing Sequence Engine - Gripper Test", "Sequence Test");
  m_logManager->Info("======================================", "Sequence Test");

  auto* seq = m_sequenceEngine->CreateSequence("GripperTest");

  // Build simple gripper test sequence
  m_logManager->Info("Building gripper test sequence...", "Sequence Test");

  // Open L_Gripper
  seq->AddAction("IOBottom", "SetOutput", { {"pinName", "L_Gripper"}, {"state", "true"} });
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "1000"} });  // Wait 1 second
  seq->AddAction("IOBottom", "SetOutput", { {"pinName", "L_Gripper"}, {"state", "false"} });

  // Open R_Gripper
  seq->AddAction("IOBottom", "SetOutput", { {"pinName", "R_Gripper"}, {"state", "true"} });
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "1000"} });  // Wait 1 second
  seq->AddAction("IOBottom", "SetOutput", { {"pinName", "R_Gripper"}, {"state", "false"} });

  // Validate and execute
  std::vector<std::string> errors;
  if (m_sequenceEngine->ValidateSequence(seq, errors)) {
    m_logManager->Info("Sequence validation passed, executing...", "Sequence Test");
    auto context = std::make_unique<SequenceContext>();
    (void)m_sequenceEngine->ExecuteSequence(seq, context.get());
  }
  else {
    m_logManager->Error("Sequence validation failed:", "Sequence Test");
    for (const auto& err : errors) {
      m_logManager->Error("  - " + err, "Sequence Test");
    }
  }

  delete seq;

  m_logManager->Info("======================================", "Sequence Test");
  m_logManager->Info("Gripper Test completed!", "Sequence Test");
  m_logManager->Info("======================================", "Sequence Test");
}

// ✅ ADD THIS FUNCTION
void Application::testPneumaticSequence()
{
  if (!m_sequenceEngine) {
    m_logManager->Warning("SequenceEngine not available for testing", "Application");
    return;
  }

  m_logManager->Info("======================================", "Pneumatic Sequence Test");
  m_logManager->Info("Testing Pneumatic Sequence Operations", "Pneumatic Sequence Test");
  m_logManager->Info("======================================", "Pneumatic Sequence Test");

  auto* seq = m_sequenceEngine->CreateSequence("PneumaticSequenceTest");

  // Build pneumatic test sequence
  m_logManager->Info("Building pneumatic test sequence...", "Pneumatic Sequence Test");

  // Test 1: Check initial status of all pneumatic slides
  m_logManager->Info("Test 1: Checking initial status", "Pneumatic Sequence Test");
  seq->AddAction("UV_Head", "GetStatus", {});
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "500"} });

  // Test 2: Extend UV_Head (blocking - waits for sensor)
  m_logManager->Info("Test 2: Extending UV_Head", "Pneumatic Sequence Test");
  seq->AddAction("UV_Head", "Extend", { {"blocking", "true"}, {"pollInterval", "50"} });
  seq->AddAction("UV_Head", "GetStatus", {});
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "1000"} });

  // Test 3: Retract UV_Head (blocking)
  m_logManager->Info("Test 3: Retracting UV_Head", "Pneumatic Sequence Test");
  seq->AddAction("UV_Head", "Retract", { {"blocking", "true"}, {"pollInterval", "50"} });
  seq->AddAction("UV_Head", "GetStatus", {});
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "1000"} });

  // Test 4: Non-blocking extend with manual wait
  m_logManager->Info("Test 4: Non-blocking extend with wait", "Pneumatic Sequence Test");
  seq->AddAction("UV_Head", "Extend", { {"blocking", "false"} });
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "200"} });
  seq->AddAction("UV_Head", "WaitForExtended", { {"timeout", "3000"}, {"pollInterval", "50"} });
  seq->AddAction("UV_Head", "GetStatus", {});
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "500"} });

  // Test 5: Quick retract
  m_logManager->Info("Test 5: Quick retract", "Pneumatic Sequence Test");
  seq->AddAction("UV_Head", "Retract", { {"blocking", "true"} });
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "500"} });

  // Test 6: Cycle test - extend and retract multiple times
  m_logManager->Info("Test 6: Cycle test (3 times)", "Pneumatic Sequence Test");
  seq->BeginLoop(3);
  seq->AddAction("UV_Head", "Extend", { {"blocking", "true"} });
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "500"} });
  seq->AddAction("UV_Head", "Retract", { {"blocking", "true"} });
  seq->AddAction("IOBottom", "Delay", { {"milliseconds", "500"} });
  seq->EndLoop();

  // Test 7: Final status check
  m_logManager->Info("Test 7: Final status check", "Pneumatic Sequence Test");
  seq->AddAction("UV_Head", "GetStatus", {});

  // Validate and execute
  m_logManager->Info("Validating sequence...", "Pneumatic Sequence Test");
  std::vector<std::string> errors;

  if (m_sequenceEngine->ValidateSequence(seq, errors)) {
    m_logManager->Info("✓ Sequence validation passed!", "Pneumatic Sequence Test");
    m_logManager->Info("Executing sequence...", "Pneumatic Sequence Test");

    auto context = std::make_unique<SequenceContext>();
    if (m_sequenceEngine->ExecuteSequence(seq, context.get())) {
      m_logManager->Info("✓ Sequence execution completed successfully!", "Pneumatic Sequence Test");
    }
    else {
      m_logManager->Error("✗ Sequence execution failed!", "Pneumatic Sequence Test");
    }
  }
  else {
    m_logManager->Error("✗ Sequence validation failed:", "Pneumatic Sequence Test");
    for (const auto& err : errors) {
      m_logManager->Error("  - " + err, "Pneumatic Sequence Test");
    }
  }

  delete seq;

  m_logManager->Info("======================================", "Pneumatic Sequence Test");
  m_logManager->Info("Pneumatic Sequence Test completed!", "Pneumatic Sequence Test");
  m_logManager->Info("======================================", "Pneumatic Sequence Test");
}


void Application::initializeScanController()
{
  m_logManager->Debug("Initializing Scan Controller...", "Application");

  try {
    // Check prerequisites
    if (!m_motionManager) {
      m_logManager->Error("Cannot initialize ScanController - MotionManager not available", "Application");
      return;
    }

    if (!m_dataStore) {
      m_logManager->Error("Cannot initialize ScanController - DataStore not available", "Application");
      return;
    }

    // Create scan controller
    // Parameters: motionManager, dataStore, deviceName, dataChannel
    m_scanController = std::make_unique<AdaptiveScanController>(
      *m_motionManager,
      *m_dataStore,
      "FiberAlign",        // ⚠️ CHANGE TO YOUR DEVICE NAME
      "PowerMeter.Power"   // ⚠️ CHANGE TO YOUR DATA CHANNEL
    );

    m_context.scanController = m_scanController.get();

    // Set up callbacks for logging
    m_scanController->SetProgressCallback(
      [this](const ScanProgressEvent& event) {
      std::string msg = "[" + std::to_string(static_cast<int>(event.progress * 100)) + "%] " + event.message;
      m_logManager->Info(msg, "Scan");
    });

    m_scanController->SetPeakFoundCallback(
      [this](const PeakInfo& peak) {
      std::stringstream ss;
      ss << "New peak found: " << std::fixed << std::setprecision(6) << peak.value
        << " at (" << peak.position.x << ", " << peak.position.y << ", " << peak.position.z << ")";
      m_logManager->Info(ss.str(), "Scan");
    });

    m_scanController->SetStateChangedCallback(
      [this](ScanState oldState, ScanState newState) {
      std::string msg = "State: " + ToString(oldState) + " → " + ToString(newState);
      m_logManager->Info(msg, "Scan");
    });

    m_scanController->SetCompletionCallback(
      [this](const ScanResults& results) {
      std::stringstream ss;
      ss << "Scan completed! Measurements: " << results.totalMeasurements
        << ", Peak: " << std::fixed << std::setprecision(6) << results.globalPeak.value;
      m_logManager->Info(ss.str(), "Scan");
    });

    m_scanController->SetErrorCallback(
      [this](const ScanError& error) {
      m_logManager->Error("Scan error: " + error.message, "Scan");
    });

    m_logManager->Info("ScanController initialized successfully", "Application");

		ILogger* logger = m_logManager.get();
		m_context.scanController->SetLogger(logger);

  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("Scan controller initialization failed: ") + e.what(),
      "Application"
    );
    // Don't throw - allow app to continue without scan controller
  }
}

// ============================================================================
// STEP 5: Add test function (OPTIONAL)
// ============================================================================

void Application::testScanController()
{
  if (!m_scanController) {
    m_logManager->Warning("ScanController not available for testing", "Application");
    return;
  }

  m_logManager->Info("======================================", "Scan Test");
  m_logManager->Info("Testing Adaptive Scan Controller", "Scan Test");
  m_logManager->Info("======================================", "Scan Test");

  // Configure scan parameters
  ScanParameters params;
  params.axesToScan = { "Z", "X", "Y" };  // Scan order
  params.stepSizes = { 0.001, 0.005, 0.010 };  // 1, 5, 10 microns
  params.motionSettleTimeMs = 400;
  params.consecutiveDecreasesLimit = 3;
  params.maxTotalDistance = 5.0;  // mm
  params.improvementThreshold = 0.01;  // 1%
  params.velocityPercent = 50.0;

  // Validate parameters
  std::string validationError;
  if (!params.Validate(validationError)) {
    m_logManager->Error("Invalid scan parameters: " + validationError, "Scan Test");
    return;
  }

  m_logManager->Info("Starting scan with CoarseToFine strategy...", "Scan Test");

  // Start scan (uses default CoarseToFine strategy)
  if (m_scanController->StartScan(params)) {
    m_logManager->Info("Scan started successfully", "Scan Test");

    // Monitor scan in background
    while (m_scanController->IsScanActive()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      // Optional: Display progress
      double progress = m_scanController->GetProgress();
      if (static_cast<int>(progress * 100) % 10 == 0) {
        PeakInfo peak = m_scanController->GetGlobalPeak();
        m_logManager->Info(
          "Progress: " + std::to_string(static_cast<int>(progress * 100)) +
          "%, Best: " + std::to_string(peak.value),
          "Scan Test"
        );
      }
    }

    // Get final results
    ScanResults results = m_scanController->GetResults();

    m_logManager->Info("Scan completed!", "Scan Test");
    m_logManager->Info("Total measurements: " + std::to_string(results.totalMeasurements), "Scan Test");
    m_logManager->Info("Peak value: " + std::to_string(results.globalPeak.value), "Scan Test");
    m_logManager->Info("Results saved to: logs/scanning/", "Scan Test");

  }
  else {
    m_logManager->Error("Failed to start scan", "Scan Test");
  }

  m_logManager->Info("======================================", "Scan Test");
  m_logManager->Info("Scan Test completed!", "Scan Test");
  m_logManager->Info("======================================", "Scan Test");
}

void Application::testOpenCV()
{
#ifdef HAS_OPENCV
  m_logManager->Debug("Running OpenCV Integration Tests...", "Application");

  OpenCVTest cvTest;
  bool passed = cvTest.runAllTests();

  if (passed) {
    m_logManager->Info("OpenCV integration tests PASSED", "Application");
  } else {
    m_logManager->Error("OpenCV test failed: " + cvTest.getLastError(), "Application");
  }
#else
  m_logManager->Warning("OpenCV not available (HAS_OPENCV not defined)", "Application");
#endif
}


void Application::initializeProductPositionManager()
{
  // Initialize product position manager
  m_logManager->Debug("Initializing ProductPositionManager...", "Application");
  m_productPositionManager = std::make_unique<ProductPositionManager>();
  m_context.productPositionManager = m_productPositionManager.get();
  m_logManager->Info("ProductPositionManager initialized", "Application");

  // Auto-load last used product from settings
  QSettings settings("Fabrinet", "BobcatEngine");
  QString lastProductPath = settings.value("Application/lastProductFile", "").toString();
  if (!lastProductPath.isEmpty()) {
    m_logManager->Debug("Attempting to auto-load last product: " +
              lastProductPath.toStdString(), "Application");

    if (m_productPositionManager->LoadProduct(lastProductPath.toStdString())) {
      m_logManager->Info("Auto-loaded product: " + m_productPositionManager->GetCurrentProductName(),
                         "Application");
    } else {
      m_logManager->Debug("Failed to auto-load product (file may not exist)", "Application");
    }
  }
}

void Application::initializeTECManager()
{
  m_logManager->Debug("Initializing TEC manager...", "Application");

  try {
    // 1. Create TECManager
    m_tecManager = std::make_unique<TECManager>();
    m_context.tecManager = m_tecManager.get();
    m_logManager->Info("TECManager created", "Application");

    // 2. Load JSON config - try local first, then default
    std::string configPath;
    int loadedCount = 0;

    // Try local config first (not in git)
    loadedCount = TECJsonLoader::LoadFromFile(*m_tecManager, "config/tec_devices.local.json");
    if (loadedCount > 0) {
      configPath = "config/tec_devices.local.json";
      m_logManager->Info("Loading TEC config from LOCAL file", "Application");
    }
    else {
      // Fall back to default config
      loadedCount = TECJsonLoader::LoadFromFile(*m_tecManager, "config/tec_devices.json");
      if (loadedCount > 0) {
        configPath = "config/tec_devices.json";
        m_logManager->Info("Loading TEC config from DEFAULT file", "Application");
      }
      else {
        m_logManager->Warning("No TEC config found - TEC control not configured", "Application");
        return;
      }
    }

    m_logManager->Info(
      "Loaded " + std::to_string(loadedCount) + " TEC devices from " + configPath,
      "Application"
    );

    // ✅ DEFERRED: TEC device connection moved to connectHardwareAsync()
    m_logManager->Info("TEC device connection deferred to background thread", "Application");

    m_logManager->Info("TEC manager initialized (connections deferred)", "Application");
  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("TEC manager initialization failed: ") + e.what(),
      "Application"
    );
    // Don't throw - allow app to continue without TEC control
  }
}

void Application::initializeAuxWindowManager()
{
  m_logManager->Debug("Initializing AuxWindowManager...", "Application");

  m_auxWindowManager = std::make_unique<AuxWindowManager>();

  // Set required dependencies
  m_auxWindowManager->setCameraManager(m_cameraManager.get());
  m_auxWindowManager->setMotionManager(m_context.motionManager);
  m_auxWindowManager->setDataStore(m_dataStore.get());
  m_auxWindowManager->setTcpClientManager(m_tcpClientManager.get());
  if (m_cameraConfigManager) {
    m_auxWindowManager->setCameraConfigManager(m_cameraConfigManager.get());
  }

  // Initialize and show the window
  m_auxWindowManager->initialize();

  // Add to context
  m_context.auxWindowManager = m_auxWindowManager.get();

  m_logManager->Info("AuxWindowManager initialized", "Application");
}

void Application::initializeGdiCameraWindow()
{
  m_logManager->Debug("Initializing GdiCameraWindow...", "Application");

  m_gdiCameraWindowManager = std::make_unique<GdiCameraWindowManager>();

  // Set camera manager (required)
  m_gdiCameraWindowManager->setCameraManager(m_cameraManager.get());

  // Set DataStore for chart overlay
  m_gdiCameraWindowManager->setDataStore(m_dataStore.get());

  // Set motion manager for click-to-move
  m_gdiCameraWindowManager->setMotionManager(m_motionManager.get());

  // Set camera config manager for per-camera calibration
  if (m_cameraConfigManager) {
    m_gdiCameraWindowManager->setCameraConfigManager(m_cameraConfigManager.get());
  }

  // Note: BeamServerClient is wired later (after it's created at line ~336)

  // Configure window title
  m_gdiCameraWindowManager->setWindowTitle("Camera Monitor (GDI)");

  // Initialize the window (position/size loaded from saved settings)
  if (m_gdiCameraWindowManager->initialize()) {
    m_context.gdiCameraWindowManager = m_gdiCameraWindowManager.get();

    // Restore visibility from last session
    if (m_gdiCameraWindowManager->shouldRestoreVisible()) {
      m_gdiCameraWindowManager->show();
      m_logManager->Info("GdiCameraWindow initialized and shown (restored from last session)", "Application");
    } else {
      m_logManager->Info("GdiCameraWindow initialized (hidden - restored from last session)", "Application");
    }
  } else {
    m_logManager->Warning("GdiCameraWindow failed to initialize", "Application");
  }
}

void Application::initializeGdiCameraWindow2()
{
  m_logManager->Debug("Initializing GdiCameraWindow2...", "Application");

  m_gdiCameraWindowManager2 = std::make_unique<GdiCameraWindowManager>("2");

  // Set camera manager (required)
  m_gdiCameraWindowManager2->setCameraManager(m_cameraManager.get());

  // Set DataStore for chart overlay
  m_gdiCameraWindowManager2->setDataStore(m_dataStore.get());

  // Set motion manager for click-to-move
  m_gdiCameraWindowManager2->setMotionManager(m_motionManager.get());

  // Set camera config manager for per-camera calibration
  if (m_cameraConfigManager) {
    m_gdiCameraWindowManager2->setCameraConfigManager(m_cameraConfigManager.get());
  }

  // Note: BeamServerClient is wired later (after it's created)

  // Configure window title
  m_gdiCameraWindowManager2->setWindowTitle("Camera Monitor 2 (GDI)");

  // Initialize the window (position/size loaded from saved settings)
  if (m_gdiCameraWindowManager2->initialize()) {
    m_context.gdiCameraWindowManager2 = m_gdiCameraWindowManager2.get();

    // Restore visibility from last session
    if (m_gdiCameraWindowManager2->shouldRestoreVisible()) {
      m_gdiCameraWindowManager2->show();
      m_logManager->Info("GdiCameraWindow2 initialized and shown (restored from last session)", "Application");
    } else {
      m_logManager->Info("GdiCameraWindow2 initialized (hidden - restored from last session)", "Application");
    }
  } else {
    m_logManager->Warning("GdiCameraWindow2 failed to initialize", "Application");
  }
}

#ifdef USE_QT_CAMERA_WINDOW
void Application::initializeQtCameraWindow()
{
  m_logManager->Debug("Initializing QtCameraWindow...", "Application");

  m_qtCameraWindow = std::make_unique<QtCameraWindow>();

  m_qtCameraWindow->setCameraManager(m_cameraManager.get());
  m_qtCameraWindow->setDataStore(m_dataStore.get());
  m_qtCameraWindow->setMotionManager(m_motionManager.get());

  if (m_cameraConfigManager) {
    m_qtCameraWindow->setCameraConfigManager(m_cameraConfigManager.get());
  }

  if (m_qtCameraWindow->initialize()) {
    m_context.qtCameraWindow = m_qtCameraWindow.get();

    // Respect saved visibility — show only if previously visible (default: true for first run)
    QSettings settings("Fabrinet", "BobcatEngine");
    const bool wasVisible = settings.value("GdiCameraWindow/visible", true).toBool();
    if (wasVisible) {
      m_qtCameraWindow->show();
    }
    m_logManager->Info("QtCameraWindow initialized", "Application");
  } else {
    m_logManager->Warning("QtCameraWindow failed to initialize", "Application");
  }
}

void Application::initializeQtCameraWindow2()
{
  m_logManager->Debug("Initializing QtCameraWindow2...", "Application");

  m_qtCameraWindow2 = std::make_unique<QtCameraWindow>("2");

  m_qtCameraWindow2->setCameraManager(m_cameraManager.get());
  m_qtCameraWindow2->setDataStore(m_dataStore.get());
  m_qtCameraWindow2->setMotionManager(m_motionManager.get());

  if (m_cameraConfigManager) {
    m_qtCameraWindow2->setCameraConfigManager(m_cameraConfigManager.get());
  }

  if (m_qtCameraWindow2->initialize()) {
    m_context.qtCameraWindow2 = m_qtCameraWindow2.get();

    // Respect saved visibility — hidden by default for second window
    QSettings settings("Fabrinet", "BobcatEngine");
    const bool wasVisible = settings.value("GdiCameraWindow2/visible", false).toBool();
    if (wasVisible) {
      m_qtCameraWindow2->show();
    }
    m_logManager->Info("QtCameraWindow2 initialized", "Application");
  } else {
    m_logManager->Warning("QtCameraWindow2 failed to initialize", "Application");
  }
}
#endif

void Application::initializeSwirBeamWindow()
{
  m_logManager->Debug("Initializing SwirBeamWindow...", "Application");

  m_swirBeamWindowManager = std::make_unique<SwirBeamWindowManager>();

  // Set camera manager (required)
  m_swirBeamWindowManager->setCameraManager(m_cameraManager.get());

  // Set power supply manager for PSU controls
  if (m_powerSupplyManager) {
    m_swirBeamWindowManager->setPowerSupplyManager(m_powerSupplyManager.get());
  }

  // Set motion manager for click-to-move
  if (m_motionManager) {
    m_swirBeamWindowManager->setMotionManager(m_motionManager.get());
  }

  // Set pneumatic manager for SWIR head control
  if (m_pneumaticManager) {
    m_swirBeamWindowManager->setPneumaticManager(m_pneumaticManager.get());
  }

  // Set TEC manager for temperature presets
  if (m_tecManager) {
    m_swirBeamWindowManager->setTECManager(m_tecManager.get());
  }

  // Set camera config manager for per-camera calibration
  if (m_cameraConfigManager) {
    m_swirBeamWindowManager->setCameraConfigManager(m_cameraConfigManager.get());
  }

  // Configure target camera and window title
  m_swirBeamWindowManager->setTargetCamera("swir_camera");
  m_swirBeamWindowManager->setWindowTitle("SWIR Beam Processing");

  // Initialize the window (position/size loaded from saved settings)
  if (m_swirBeamWindowManager->initialize()) {
    m_context.swirBeamWindowManager = m_swirBeamWindowManager.get();

    // Restore visibility from last session
    if (m_swirBeamWindowManager->shouldRestoreVisible()) {
      m_swirBeamWindowManager->show();
      m_logManager->Info("SwirBeamWindow initialized and shown (restored from last session)", "Application");
    } else {
      m_logManager->Info("SwirBeamWindow initialized (hidden - restored from last session)", "Application");
    }
  } else {
    m_logManager->Warning("SwirBeamWindow failed to initialize", "Application");
  }
}

void Application::initializeBeamOptimizer()
{
  m_logManager->Debug("Initializing BeamOptimizer...", "Application");

  // 1. Create broker
  m_beamResultBroker = std::make_unique<BeamResultBroker>();
  m_context.beamResultBroker = m_beamResultBroker.get();

  // 2. Wire broker into SwirBeamWindow (publisher side)
  if (m_swirBeamWindowManager) {
    m_swirBeamWindowManager->setBeamResultBroker(m_beamResultBroker.get());
  }

  // 3. Create optimizer manager (subscriber side)
  m_beamOptimizerManager = std::make_unique<BeamOptimizerManager>();
  m_beamOptimizerManager->SetMotionManager(m_motionManager.get());
  m_beamOptimizerManager->SetCameraManager(m_cameraManager.get());
  m_beamOptimizerManager->SetPowerSupplyManager(m_powerSupplyManager.get());
  m_beamOptimizerManager->SetLogManager(m_logManager.get());
  m_beamOptimizerManager->SetBeamResultBroker(m_beamResultBroker.get());

  if (m_beamOptimizerManager->Initialize() == ErrorCode::Success) {
    m_context.beamOptimizerManager = m_beamOptimizerManager.get();
    m_logManager->Info("BeamOptimizer initialized", "Application");
  } else {
    m_logManager->Warning("BeamOptimizer failed to initialize", "Application");
  }
}

void Application::initializeCoordinateSystemManager()
{
  m_logManager->Debug("Initializing CoordinateSystemManager...", "Application");

  m_coordinateSystemManager = std::make_unique<CoordinateSystemManager>();
  m_context.coordinateSystemManager = m_coordinateSystemManager.get();

  m_logManager->Info("CoordinateSystemManager initialized", "Application");
}

void Application::initializeLightingManager()
{
  m_logManager->Debug("Initializing LightingManager...", "Application");

  m_lightingManager = std::make_unique<Lighting::LightingManager>();
  m_context.lightingManager = m_lightingManager.get();

  // Try local config first, then fall back to default
  std::string localConfigPath = "config/lighting_config.local.json";
  std::string defaultConfigPath = "config/lighting_config.json";

  int loadedCount = 0;
  bool localFileExists = false;

  // Try local config first
  std::ifstream localFile(localConfigPath);
  if (localFile.good()) {
    localFile.close();
    localFileExists = true;
    loadedCount = Lighting::LightingJsonLoader::LoadFromFile(*m_lightingManager, localConfigPath);
    if (loadedCount > 0) {
      m_logManager->Info("Lighting configuration loaded from " +
                localConfigPath + " (" + std::to_string(loadedCount) + " controllers)", "Application");
    } else {
      m_logManager->Info("Lighting configuration loaded from " +
                localConfigPath + " (0 controllers - lighting disabled)", "Application");
    }
  }

  // Fall back to default config ONLY if local file doesn't exist
  if (!localFileExists) {
    loadedCount = Lighting::LightingJsonLoader::LoadFromFile(*m_lightingManager, defaultConfigPath);
    if (loadedCount > 0) {
      m_logManager->Info("Lighting configuration loaded from " +
                defaultConfigPath + " (" + std::to_string(loadedCount) + " controllers)", "Application");
    }
  }

  // Set config file path for saving (always save to local config for user changes)
  m_lightingManager->SetConfigFilePath(localConfigPath);

  if (loadedCount == 0) {
    m_logManager->Info("No lighting controllers configured", "Application");
  }

  m_logManager->Info("LightingManager initialized with " + std::to_string(loadedCount) + " controllers", "Application");
}

// In your Application constructor or initialization function:
void Application::initializePowerSupplySystem()
{
  m_logManager->Debug("Initializing Power Supply system...", "Application");

  // Create PowerSupplyManager using unique_ptr (matches your .h declaration)
  m_powerSupplyManager = std::make_unique<PowerSupplyManager>(this);

  // Assign to context for access by other components
  m_context.powerSupplyManager = m_powerSupplyManager.get();

  // Try local config first, then fall back to default
  std::string localConfigPath = "config/power_supply_config.local.json";
  std::string defaultConfigPath = "config/power_supply_config.json";

  bool configLoaded = false;

  // Try local config first
  if (PowerSupplyJsonLoader::loadConfiguration(localConfigPath, m_context.powerSupplyManager)) {
    m_logManager->Info("Power Supply configuration loaded from " + localConfigPath, "Application");
    configLoaded = true;
  }
  // Fall back to default config
  else if (PowerSupplyJsonLoader::loadConfiguration(defaultConfigPath, m_context.powerSupplyManager)) {
    m_logManager->Info("Power Supply configuration loaded from " + defaultConfigPath, "Application");
    configLoaded = true;
  }

  if (configLoaded) {
    m_logManager->Info("Power Supply system ready (use UI to connect)", "Application");

    // ⚠️ DO NOT auto-connect at startup - it blocks the UI thread!
    // Let users connect via the PowerSupplyControlPage UI instead
    // m_context.powerSupplyManager->connectAll();
  }
  else {
    m_logManager->Warning("Failed to load Power Supply configuration", "Application");
    m_logManager->Warning("Tried: " + localConfigPath + ", " + defaultConfigPath, "Application");
    m_logManager->Warning("Power Supply functionality will be limited", "Application");
  }
}

void Application::initializeSMUSystem()
{
  m_logManager->Debug("Initializing SMU system...", "Application");

  // Create SMUManager
  m_smuManager = std::make_unique<SMUManager>(this);

  // Assign to context for access by other components
  m_context.smuManager = m_smuManager.get();

  // ✅ CREATE SMU POLLING MANAGER
  m_smuPollingManager = std::make_unique<SMUPollingManager>(this);
  m_context.smuPollingManager = m_smuPollingManager.get();

  // ✅ CONNECT POLLING MANAGER TO DATASTORE
  // This is the primary path for polling data (thread-safe architecture)
  connect(m_smuPollingManager.get(), &SMUPollingManager::measurementReady,
          this, [this](QString deviceName, int channel, double value, QString /*timestamp*/) {
      // Guard against pending signals during shutdown
      if (!m_context.dataStore) return;

      QString channelName = QString("SMU_%1_Ch%2").arg(deviceName).arg(channel);
      m_context.dataStore->SetValue(channelName.toStdString(), static_cast<float>(value));
  });

  m_logManager->Debug("SMU Polling Manager initialized", "Application");

  // ========== Legacy: Direct SMU Manager connection (for non-polling measurements) ==========
  // Note: This may be redundant now that polling uses SMUPollingManager
  connect(m_smuManager.get(), &SMUManager::measurementReady,
          this, [this](QString deviceName, int channel, double value, QString /*timestamp*/) {
      // Guard against pending signals during shutdown
      if (!m_context.dataStore) return;

      QString channelName = QString("SMU_%1_Ch%2").arg(deviceName).arg(channel);
      m_context.dataStore->SetValue(channelName.toStdString(), static_cast<float>(value));
  });

  m_logManager->Debug("SMU measurements auto-subscribed to DataStore", "Application");

  // Try local config first, then fall back to default
  std::string localConfigPath = "config/smu_devices.local.json";
  std::string defaultConfigPath = "config/smu_devices.json";

  bool configLoaded = false;

  // Try local config first
  if (SMUJsonLoader::LoadConfiguration(localConfigPath, m_context.smuManager)) {
    m_logManager->Info("SMU configuration loaded from " + localConfigPath, "Application");
    configLoaded = true;
  }
  // Fall back to default config
  else if (SMUJsonLoader::LoadConfiguration(defaultConfigPath, m_context.smuManager)) {
    m_logManager->Info("SMU configuration loaded from " + defaultConfigPath, "Application");
    configLoaded = true;
  }

  if (configLoaded) {
    m_logManager->Info("SMU system ready", "Application");
    m_logManager->Info("Found " + std::to_string(m_context.smuManager->GetDeviceCount()) +
              " SMU device(s)", "Application");
  }
  else {
    m_logManager->Warning("Failed to load SMU configuration", "Application");
    m_logManager->Warning("Tried: " + localConfigPath + ", " + defaultConfigPath, "Application");
    m_logManager->Warning("SMU functionality will be unavailable", "Application");
  }
}

void Application::initializeSafetyWatch()
{
  m_logManager->Debug("Initializing Safety Watch...", "Application");

  try {
    // Create SafetyWatchManager
    m_safetyWatchManager = std::make_unique<SafetyWatchManager>(this);
    m_context.safetyWatchManager = m_safetyWatchManager.get();

    // Set dependencies
    m_safetyWatchManager->SetIOManager(m_ioManager.get());
    m_safetyWatchManager->SetLogManager(m_logManager.get());
    if (m_safetyEventLog) {
      m_safetyWatchManager->SetSafetyEventLog(m_safetyEventLog.get());
    }

    // Try local config first, then fall back to default
    std::string localConfigPath = "config/safety_watch.local.json";
    std::string defaultConfigPath = "config/safety_watch.json";

    bool configLoaded = false;

    // Try local config first
    std::ifstream localFile(localConfigPath);
    if (localFile.good()) {
      localFile.close();
      if (m_safetyWatchManager->LoadConfig(localConfigPath)) {
        m_logManager->Info("Safety watch configuration loaded from " + localConfigPath, "Application");
        configLoaded = true;
      }
    }

    // Fall back to default config
    if (!configLoaded) {
      if (m_safetyWatchManager->LoadConfig(defaultConfigPath)) {
        m_logManager->Info("Safety watch configuration loaded from " + defaultConfigPath, "Application");
        configLoaded = true;
      }
    }

    if (configLoaded) {
      if (m_safetyWatchManager->IsEnabled()) {
        m_logManager->Info("SafetyWatchManager initialized (watching will start after IO connects)", "Application");
      } else {
        m_logManager->Info("SafetyWatchManager initialized but disabled in config", "Application");
      }
    } else {
      m_logManager->Warning("Safety watch config not found - safety monitoring disabled", "Application");
    }

    // Connect motion manager to safety watch for emergency stop
    if (m_motionManager && m_safetyWatchManager) {
      m_motionManager->SetSafetyWatchManager(m_safetyWatchManager.get());
    }

    // E-Stop broadcast: single consolidated connection for all hardware managers
    if (m_safetyWatchManager) {
      connect(m_safetyWatchManager.get(), &SafetyWatchManager::safetyTriggered,
              this, [this](QString /*title*/, QString /*message*/, bool critical) {
        if (critical) {
          performEStopCascade();
        }
      });
    }

    m_logManager->Info("Safety Watch initialized", "Application");
  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("Safety watch initialization failed: ") + e.what(),
      "Application"
    );
    // Don't throw - allow app to continue without safety watch
  }
}

void Application::initializeDeviceLinkManager()
{
  m_logManager->Debug("Initializing Device Link Manager...", "Application");

  try {
    // Create DeviceLinkManager
    m_deviceLinkManager = std::make_unique<DeviceLinkManager>();
    m_context.deviceLinkManager = m_deviceLinkManager.get();

    // Set motion manager dependency
    if (m_motionManager) {
      m_deviceLinkManager->SetMotionManager(m_motionManager.get());
    }

    // Create and load transformation matrix loader
    static TransformationMatrixLoader matrixLoader;
    std::string matrixPath = "config/transformation_matrix.json";
    if (matrixLoader.LoadFromFile(matrixPath)) {
      m_deviceLinkManager->SetMatrixLoader(&matrixLoader);
      m_logManager->Info("Transformation matrices loaded for DeviceLinkManager", "Application");
    } else {
      m_logManager->Warning("DeviceLinkManager: Could not load transformation matrices", "Application");
    }

    // Try to load existing device links
    std::string linksPath = "config/device_links.json";
    if (m_deviceLinkManager->LoadFromFile(linksPath)) {
      m_logManager->Info("Device links loaded from " + linksPath, "Application");
    } else {
      m_logManager->Debug("No existing device links found (will be created on first registration)", "Application");
    }

    m_logManager->Info("Device Link Manager initialized", "Application");
  }
  catch (const std::exception& e) {
    m_logManager->Error(
      std::string("Device Link Manager initialization failed: ") + e.what(),
      "Application"
    );
    // Don't throw - allow app to continue without device link manager
  }
}

void Application::initializeApplicationSettings()
{
  m_logManager->Debug("Initializing Application Settings...", "Application");

  m_applicationSettings = std::make_unique<ApplicationSettings>();
  m_context.applicationSettings = m_applicationSettings.get();

  // Try local config first, then default
  std::string localPath = "config/system/applicationSettings.local.ini";
  std::string defaultPath = "config/system/applicationSettings.ini";

  if (m_applicationSettings->LoadFromFile(localPath)) {
    m_logManager->Info("Application settings loaded from local config: " + localPath, "Application");
  } else if (m_applicationSettings->LoadFromFile(defaultPath)) {
    m_logManager->Info("Application settings loaded from: " + defaultPath, "Application");
  } else {
    m_logManager->Warning("No application settings file found - using defaults", "Application");
  }

  // Override with any user-changed values from QSettings (Windows Registry)
  {
    QSettings qsettings("Fabrinet", "BobcatEngine");
    QStringList keys = qsettings.allKeys();
    for (const QString& key : keys) {
      // QSettings keys are "Section/Key" format
      int slashPos = key.indexOf('/');
      if (slashPos > 0) {
        std::string section = key.left(slashPos).toStdString();
        std::string settingKey = key.mid(slashPos + 1).toStdString();
        std::string value = qsettings.value(key).toString().toStdString();
        m_applicationSettings->Set(section, settingKey, value);
      }
    }
    if (!keys.isEmpty()) {
      m_logManager->Debug("Applied " + std::to_string(keys.size()) + " QSettings overrides", "Application");
    }
  }

  // Log key settings for verification
  int chainTimeout = m_applicationSettings->GetChainExecutionTimeoutSeconds();
  m_logManager->Debug("  ChainExecution.AutoContinueSeconds = " + std::to_string(chainTimeout), "Application");
}
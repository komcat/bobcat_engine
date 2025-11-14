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
#include "ProcessPluginRegistry.h"
#include "DarkTheme.h"  // ✅ NEW: Centralized dark theme
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QSettings>
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

Application::Application(int argc, char* argv[])
  : QObject(nullptr)
  , m_initialized(false)
  , m_argc(argc)
  , m_argv(argv)
{
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);  // ← ADD HERE (inside constructor)
#endif

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

  // Initialize plugin registry (triggers static registration in plugin files)
  auto& pluginRegistry = ProcessPluginRegistry::Instance();
  std::cout << "[Application] Plugin registry initialized with "
            << pluginRegistry.GetAllProcessTypes().size() << " plugins" << std::endl;

  std::cout << "[Application] About to call exec()..." << std::endl;
  std::cout << "[Application] m_qApplication pointer: " << m_qApplication << std::endl;

  if (!m_qApplication) {
    std::cerr << "[Application] ERROR: m_qApplication is NULL!" << std::endl;
    return -1;
  }

  std::cout << "[Application] Calling exec()..." << std::endl;
  int result = m_qApplication->exec();
  std::cout << "[Application] exec() returned: " << result << std::endl;

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

    // Load and set custom font from assets folder
    QString fontPath = "assets/fonts/RobotoMono-Regular.ttf";
    int fontId = QFontDatabase::addApplicationFont(fontPath);

    if (fontId != -1) {
      QStringList fontFamilies = QFontDatabase::applicationFontFamilies(fontId);
      if (!fontFamilies.isEmpty()) {
        QFont appFont(fontFamilies.at(0), 11);  // Use loaded Roboto font, size 9
        m_qApplication->setFont(appFont);
        std::cout << "[Application] Custom font loaded: " << fontFamilies.at(0).toStdString() << std::endl;
      }
      else {
        std::cerr << "[Application] Warning: Could not get font family from " << fontPath.toStdString() << std::endl;
        QFont fallbackFont("Segoe UI", 11);
        m_qApplication->setFont(fallbackFont);
      }
    }
    else {
      std::cerr << "[Application] Warning: Could not load custom font from " << fontPath.toStdString() << std::endl;
      std::cerr << "[Application] Using fallback font: Segoe UI" << std::endl;
      QFont fallbackFont("Segoe UI", 9);
      m_qApplication->setFont(fallbackFont);
    }

    // ✅ Set Fusion style for better stylesheet support (prevents Windows native style interference)
    m_qApplication->setStyle("Fusion");

    // ✅ UPDATED: Apply centralized dark theme globally to all Qt widgets
    m_qApplication->setStyleSheet(DarkTheme::getStyleSheet());

    std::cout << "[Application] " << getApplicationName()
      << " v" << getVersion() << std::endl;

    // ✅ Check BOBCAT_SIMULATION environment variable
    const char* simEnv = std::getenv("BOBCAT_SIMULATION");
    if (simEnv != nullptr) {
      std::string simValue(simEnv);
      m_context.simulationMode = (simValue == "1" || simValue == "true" || simValue == "TRUE");
      std::cout << "[Application] BOBCAT_SIMULATION environment variable: " << simValue << std::endl;
    } else {
      // Default to false if environment variable not set
      m_context.simulationMode = false;
      std::cout << "[Application] BOBCAT_SIMULATION not set - defaulting to HARDWARE MODE" << std::endl;
    }

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

  // Check for simulation mode
  if (m_context.simulationMode) {
    std::cout << "======================================" << std::endl;
    std::cout << "  🔧 SIMULATION MODE ENABLED 🔧" << std::endl;
    std::cout << "  Hardware connections will be SKIPPED" << std::endl;
    std::cout << "  Use this mode for UI/file development" << std::endl;
    std::cout << "======================================" << std::endl;
  }

  try {
    initializeLogManager();
    initializeDataStore();
    initializeTcpClientManager();
    initializeCameraManager();
    initializeMotionManager();
    initializeIOManager();  // Add IO manager initialization
		initializePneumaticManager();  // ✅ ADD THIS
		initializeTECManager();  // ✅ ADD THIS LINE
    initializePowerSupplySystem();  // ✅ MOVED BEFORE SequenceEngine
    initializeSMUSystem();  // ✅ ADD SMU System initialization
    initializeAuxWindowManager();  // ✅ MOVED HERE: Initialize before SequenceEngine so handler gets valid pointer
		initializeProductPositionManager();	// ✅ CRITICAL: Must be BEFORE SequenceEngine so MotionCommandHandler gets valid pointer
    initializeSequenceEngine();  // ✅ ADD THIS LAST (needs all managers initialized)
    initializeScanController();  // ✅ ADD THIS - Must be AFTER motion & datastore
    initializeCoordinateSystemManager();

    // Optional: run test commands to verify managers
    //testMotionCommands();
    //testIOCommands();  // Test IO with Vacuum_Base
    
		//testPneumaticCommands();  // ✅ TEST PNEUMATICS

		//testSequenceEngine();  // ✅ TEST SEQUENCE ENGINE
		//testPneumaticSequence();	// ✅ TEST PNEUMATIC SEQUENCE

     // Optional: run test commands
    // testScanController();  // ✅ UNCOMMENT TO TEST


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

  // Auto-connect if config was loaded (skip in simulation mode)
  if (tcpConfigLoaded && !m_context.simulationMode) {
    m_tcpClientManager->ConnectAutoClients();
  } else if (m_context.simulationMode) {
    m_logManager->Info("Simulation mode: Skipping TCP auto-connect", "Application");
  }
}

void Application::initializeCameraManager()
{
  m_cameraManager = std::make_unique<CameraManager>();
  m_context.cameraManager = m_cameraManager.get();

  // Load cameras from JSON config - try local first, then default
  bool configLoaded = false;
  std::string configPath;

  auto configManager = std::make_unique<CameraConfigManager>("config/camera_config.local.json");
  if (configManager->LoadConfig()) {
    configPath = "config/camera_config.local.json";
    configLoaded = true;
    m_logManager->Info("Camera configuration loaded from LOCAL file", "Application");
  }
  else {
    configManager = std::make_unique<CameraConfigManager>("config/camera_config.json");
    if (configManager->LoadConfig()) {
      configPath = "config/camera_config.json";
      configLoaded = true;
      m_logManager->Info("Camera configuration loaded from DEFAULT file", "Application");
    }
    else {
      m_logManager->Warning("No camera config found - no cameras loaded", "Application");
    }
  }

  if (configLoaded) {
    if (configManager->InitializeCameraManager(*m_cameraManager)) {
      m_logManager->Info("CameraManager initialized from: " + configPath, "Application");

      // Connect all cameras with delay (skip in simulation mode)
      if (!m_context.simulationMode) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        m_cameraManager->ConnectAllCameras();
      } else {
        m_logManager->Info("Simulation mode: Skipping camera connections", "Application");
      }
    }
    else {
      m_logManager->Warning("Failed to initialize CameraManager with config", "Application");
    }
  }

  // Skip camera operations in simulation mode
  if (!m_context.simulationMode) {
    m_cameraManager->StartBroadcastSystem();
    m_cameraManager->StartGrabbingAll();
  } else {
    m_logManager->Info("Simulation mode: Skipping camera broadcast and grabbing", "Application");
  }

  m_logManager->Info("CameraManager initialized", "Application");
}

void Application::initializeMotionManager()
{
  std::cout << "[Application] Initializing motion manager..." << std::endl;

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

    // 3. Auto-connect enabled devices (skip in simulation mode)
    if (!m_context.simulationMode) {
      int connectedCount = m_motionManager->ConnectEnabledDevices();
      m_logManager->Info(
        "Connected " + std::to_string(connectedCount) + " devices",
        "Application"
      );
    } else {
      m_logManager->Info("Simulation mode: Skipping motion device connections", "Application");
    }

		/*
		TODO: Set logger for motion manager if needed
    ILogger* logger = m_logManager.get();
		m_motionManager->SetLogger(logger);
    
    */

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


void Application::initializeIOManager()
{
  std::cout << "[Application] Initializing IO manager..." << std::endl;

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

    // ADD DEBUG: Check which file exists
    std::cout << "[Application] Checking for IO config files..." << std::endl;
    std::ifstream testFile1("config/io_devices.local.json");
    if (testFile1.good()) {
      std::cout << "[Application] ✓ Found: config/io_devices.local.json" << std::endl;
      testFile1.close();
    }
    else {
      std::cout << "[Application] ✗ Not found: config/io_devices.local.json" << std::endl;
    }

    std::ifstream testFile2("config/io_devices.json");
    if (testFile2.good()) {
      std::cout << "[Application] ✓ Found: config/io_devices.json" << std::endl;
      testFile2.close();
    }
    else {
      std::cout << "[Application] ✗ Not found: config/io_devices.json" << std::endl;
    }

    // Try local config first (not in git)
    std::cout << "[Application] Attempting to load LOCAL config..." << std::endl;
    loadedCount = EziIOJsonLoader::LoadFromFile(*m_ioManager, "config/io_devices.local.json");
    if (loadedCount > 0) {
      configPath = "config/io_devices.local.json";
      m_logManager->Info("Loading IO config from LOCAL file", "Application");
      std::cout << "[Application] ✓ Loaded " << loadedCount << " devices from LOCAL config" << std::endl;
    }
    else {
      // Fall back to default config
      std::cout << "[Application] LOCAL config not found, trying DEFAULT config..." << std::endl;
      loadedCount = EziIOJsonLoader::LoadFromFile(*m_ioManager, "config/io_devices.json");
      if (loadedCount > 0) {
        configPath = "config/io_devices.json";
        m_logManager->Info("Loading IO config from DEFAULT file", "Application");
        std::cout << "[Application] ✓ Loaded " << loadedCount << " devices from DEFAULT config" << std::endl;
      }
      else {
        m_logManager->Warning("No IO config found - IO control not configured", "Application");
        std::cout << "[Application] ✗ No IO config could be loaded!" << std::endl;
        return;
      }
    }

    // ADD DEBUG: Test pin name retrieval
    std::cout << "\n[Application] ===== TESTING PIN NAME MAPPINGS =====" << std::endl;
    IEziIODevice* ioBottomDevice = m_ioManager->GetDevice("IOBottom");
    if (ioBottomDevice) {
      std::cout << "[Application] IOBottom device found" << std::endl;

      // Test a few output pin names
      std::string pin0Name = ioBottomDevice->GetOutputPinName(0);
      std::string pin4Name = ioBottomDevice->GetOutputPinName(4);
      std::string pin10Name = ioBottomDevice->GetOutputPinName(10);

      std::cout << "[Application]   Output Pin 0: " << pin0Name << " (should be L_Gripper)" << std::endl;
      std::cout << "[Application]   Output Pin 4: " << pin4Name << " (should be UV_Head)" << std::endl;
      std::cout << "[Application]   Output Pin 10: " << pin10Name << " (should be Vacuum_Base)" << std::endl;
    }
    else {
      std::cout << "[Application] ERROR: IOBottom device NOT FOUND!" << std::endl;
    }

    IEziIODevice* ioTopDevice = m_ioManager->GetDevice("IOTop");
    if (ioTopDevice) {
      std::cout << "[Application] IOTop device found" << std::endl;

      // Test a few input pin names
      std::string pin1Name = ioTopDevice->GetInputPinName(1);
      std::string pin2Name = ioTopDevice->GetInputPinName(2);

      std::cout << "[Application]   Input Pin 1: " << pin1Name << " (should be UV_Head_Up)" << std::endl;
      std::cout << "[Application]   Input Pin 2: " << pin2Name << " (should be UV_Head_Down)" << std::endl;
    }
    else {
      std::cout << "[Application] ERROR: IOTop device NOT FOUND!" << std::endl;
    }
    std::cout << "[Application] ===== END PIN NAME TEST =====\n" << std::endl;

    m_logManager->Info(
      "Loaded " + std::to_string(loadedCount) + " IO devices from " + configPath,
      "Application"
    );

    // 4. Auto-connect enabled devices (skip in simulation mode)
    if (!m_context.simulationMode) {
      int connectedCount = m_ioManager->ConnectEnabledDevices();
      m_logManager->Info(
        "Connected " + std::to_string(connectedCount) + " IO devices",
        "Application"
      );

      // 5. Start automatic polling (100ms interval)
      m_ioManager->StartPolling(100);
      m_logManager->Info("IO polling started (100ms interval)", "Application");
    } else {
      m_logManager->Info("Simulation mode: Skipping IO device connections and polling", "Application");
    }

    m_logManager->Info("IO manager initialized successfully", "Application");
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
  uint32_t outputs = 0, status = 0;
  if (m_ioManager->ReadOutputs("IOBottom", outputs, status) == EziIOError::SUCCESS) {
    bool vacuum_state = (outputs & (1U << 10)) != 0;  // Pin 10
    m_logManager->Info(
      "Vacuum_Base current state: " + std::string(vacuum_state ? "ON" : "OFF"),
      "IO Test"
    );
  }

  m_logManager->Info("======================================", "IO Test");
  m_logManager->Info("IO Test completed!", "IO Test");
  m_logManager->Info("======================================", "IO Test");
}

void Application::shutdownManagers()
{
  std::cout << "[Application] Shutting down managers..." << std::endl;


  // Disconnect all PSU devices
  if (m_context.powerSupplyManager) {
    m_context.powerSupplyManager->disconnectAll();
  }

  // Disconnect all SMU devices
  if (m_context.smuManager) {
    m_context.smuManager->DisconnectAll();
  }


  // Shutdown in reverse order
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

	m_powerSupplyManager.reset();
  m_smuManager.reset();  // ✅ ADD SMU Manager cleanup
  m_tecManager.reset();  // ✅ ADD THIS LINE
  m_productPositionManager.reset();  // ✅ ADD THIS
  m_pneumaticManager.reset();
  m_scanController.reset();   // ✅ ADD THIS
  m_ioManager.reset();
  m_motionManager.reset();
  m_cameraManager.reset();
  m_tcpClientManager.reset();
  m_dataStore.reset();
  m_sequenceEngine.reset();
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

  // ✅ AuxWindowManager is now initialized earlier (before SequenceEngine)
  // This ensures the AuxWindowCommandHandler gets a valid pointer
  // The window should already be visible at this point

  if (m_logManager) {
    m_logManager->Info("Main window created", "Application");
  }
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
  std::cout << "[Application] Initializing Pneumatic manager..." << std::endl;

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
  std::cout << "[Application] Initializing Sequence Engine..." << std::endl;

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
    m_sequenceEngine->ExecuteSequence(seq, context.get());
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
  std::cout << "[Application] Initializing Scan Controller..." << std::endl;

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


void Application::initializeProductPositionManager()
{
  // Initialize product position manager
  std::cout << "[Application] Initializing ProductPositionManager..." << std::endl;
  m_productPositionManager = std::make_unique<ProductPositionManager>();
  m_context.productPositionManager = m_productPositionManager.get();
  std::cout << "[Application] ProductPositionManager initialized" << std::endl;

  // Auto-load last used product from settings
  QSettings settings("Fabrinet", "BobcatEngine");
  QString lastProductPath = settings.value("Application/lastProductFile", "").toString();
  if (!lastProductPath.isEmpty()) {
    std::cout << "[Application] Attempting to auto-load last product: "
              << lastProductPath.toStdString() << std::endl;

    if (m_productPositionManager->LoadProduct(lastProductPath.toStdString())) {
      std::cout << "[Application] Product loaded successfully: "
                << m_productPositionManager->GetCurrentProductName() << std::endl;

      if (m_logManager) {
        m_logManager->Info("Auto-loaded product: " + m_productPositionManager->GetCurrentProductName(),
                           "Application");
      }
    } else {
      std::cout << "[Application] Failed to auto-load product (file may not exist)" << std::endl;
    }
  }
}

void Application::initializeTECManager()
{
  std::cout << "[Application] Initializing TEC manager..." << std::endl;

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

    // 3. Auto-connect enabled devices (skip in simulation mode)
    if (!m_context.simulationMode) {
      int connectedCount = m_tecManager->ConnectEnabledDevices();
      m_logManager->Info(
        "Connected " + std::to_string(connectedCount) + " TEC devices",
        "Application"
      );
    } else {
      m_logManager->Info("Simulation mode: Skipping TEC device connections", "Application");
    }

    m_logManager->Info("TEC manager initialized successfully", "Application");
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
  std::cout << "[Application] Initializing AuxWindowManager..." << std::endl;

  m_auxWindowManager = std::make_unique<AuxWindowManager>();

  // Set required dependencies
  m_auxWindowManager->setCameraManager(m_cameraManager.get());
  m_auxWindowManager->setMotionManager(m_context.motionManager);
  m_auxWindowManager->setDataStore(m_dataStore.get());
  m_auxWindowManager->setTcpClientManager(m_tcpClientManager.get());

  // Initialize and show the window
  m_auxWindowManager->initialize();

  // Add to context
  m_context.auxWindowManager = m_auxWindowManager.get();

  std::cout << "[Application] AuxWindowManager initialized successfully" << std::endl;
}

void Application::initializeCoordinateSystemManager()
{
  std::cout << "[Application] Initializing CoordinateSystemManager..." << std::endl;

  m_coordinateSystemManager = std::make_unique<CoordinateSystemManager>();
  m_context.coordinateSystemManager = m_coordinateSystemManager.get();

  m_logManager->Info("CoordinateSystemManager initialized", "Application");
  std::cout << "[Application] CoordinateSystemManager initialized successfully" << std::endl;
}

// In your Application constructor or initialization function:
void Application::initializePowerSupplySystem()
{
  std::cout << "[Application] Initializing Power Supply system..." << std::endl;

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
    std::cout << "[Application] Power Supply configuration loaded from "
      << localConfigPath << std::endl;
    configLoaded = true;
  }
  // Fall back to default config
  else if (PowerSupplyJsonLoader::loadConfiguration(defaultConfigPath, m_context.powerSupplyManager)) {
    std::cout << "[Application] Power Supply configuration loaded from "
      << defaultConfigPath << std::endl;
    configLoaded = true;
  }

  if (configLoaded) {
    std::cout << "[Application] Power Supply system ready (use UI to connect)" << std::endl;

    // ⚠️ DO NOT auto-connect at startup - it blocks the UI thread!
    // Let users connect via the PowerSupplyControlPage UI instead
    // m_context.powerSupplyManager->connectAll();
  }
  else {
    std::cerr << "[Application] Warning: Failed to load Power Supply configuration" << std::endl;
    std::cerr << "[Application] Tried: " << localConfigPath << ", " << defaultConfigPath << std::endl;
    std::cerr << "[Application] Power Supply functionality will be limited" << std::endl;
  }
}

void Application::initializeSMUSystem()
{
  std::cout << "[Application] Initializing SMU system..." << std::endl;

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
          this, [this](QString deviceName, int channel, double value, QString timestamp) {
      QString channelName = QString("SMU_%1_Ch%2").arg(deviceName).arg(channel);
      m_context.dataStore->SetValue(channelName.toStdString(), static_cast<float>(value));

      // Debug output (disabled - too verbose)
      // static int debugCounter = 0;
      // if (debugCounter++ < 5 || debugCounter % 10 == 0) {
      //   std::cout << "[SMU→DataStore] " << channelName.toStdString()
      //             << " = " << value << " A (count=" << debugCounter << ")" << std::endl;
      // }
  });

  std::cout << "[Application] SMU Polling Manager initialized" << std::endl;

  // ========== Legacy: Direct SMU Manager connection (for non-polling measurements) ==========
  // Note: This may be redundant now that polling uses SMUPollingManager
  connect(m_smuManager.get(), &SMUManager::measurementReady,
          this, [this](QString deviceName, int channel, double value, QString timestamp) {
      // Add to DataStore with channel naming: "SMU_DeviceName_Ch1"
      QString channelName = QString("SMU_%1_Ch%2").arg(deviceName).arg(channel);
      m_context.dataStore->SetValue(channelName.toStdString(), static_cast<float>(value));

      // Debug output (disabled - too verbose)
      // static int debugCounter = 0;
      // if (debugCounter++ < 5 || debugCounter % 10 == 0) {  // Log first 5, then every 10th
      //   std::cout << "[SMU→DataStore] " << channelName.toStdString()
      //             << " = " << value << " A (count=" << debugCounter << ")" << std::endl;
      // }
  });

  std::cout << "[Application] SMU measurements auto-subscribed to DataStore" << std::endl;

  // Try local config first, then fall back to default
  std::string localConfigPath = "config/smu_devices.local.json";
  std::string defaultConfigPath = "config/smu_devices.json";

  bool configLoaded = false;

  // Try local config first
  if (SMUJsonLoader::LoadConfiguration(localConfigPath, m_context.smuManager)) {
    std::cout << "[Application] SMU configuration loaded from "
      << localConfigPath << std::endl;
    configLoaded = true;
  }
  // Fall back to default config
  else if (SMUJsonLoader::LoadConfiguration(defaultConfigPath, m_context.smuManager)) {
    std::cout << "[Application] SMU configuration loaded from "
      << defaultConfigPath << std::endl;
    configLoaded = true;
  }

  if (configLoaded) {
    std::cout << "[Application] SMU system ready" << std::endl;
    std::cout << "[Application] Found " << m_context.smuManager->GetDeviceCount()
              << " SMU device(s)" << std::endl;
  }
  else {
    std::cerr << "[Application] Warning: Failed to load SMU configuration" << std::endl;
    std::cerr << "[Application] Tried: " << localConfigPath << ", " << defaultConfigPath << std::endl;
    std::cerr << "[Application] SMU functionality will be unavailable" << std::endl;
  }
}
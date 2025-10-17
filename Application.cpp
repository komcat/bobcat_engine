// Application.cpp
#include "Application.h"
#include "MainWindow.h"
#include "CameraManager.h"
#include "LogManager.h"
#include "TcpClientManager.h"
#include "EziIO_JsonLoader.h"
#include "PneumaticJsonLoader.h"  // ✅ ADD THIS
#include "MotionJsonLoader.h"
#include <QApplication>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
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
    initializeMotionManager();
    initializeIOManager();  // Add IO manager initialization
		initializePneumaticManager();  // ✅ ADD THIS
    initializeSequenceEngine();  // ✅ ADD THIS LAST

    // Optional: run test commands to verify managers
    //testMotionCommands();
    //testIOCommands();  // Test IO with Vacuum_Base
    
		//testPneumaticCommands();  // ✅ TEST PNEUMATICS

		//testSequenceEngine();  // ✅ TEST SEQUENCE ENGINE
		//testPneumaticSequence();	// ✅ TEST PNEUMATIC SEQUENCE

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

    // 3. Auto-connect enabled devices
    int connectedCount = m_motionManager->ConnectEnabledDevices();
    m_logManager->Info(
      "Connected " + std::to_string(connectedCount) + " devices",
      "Application"
    );

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

    // 4. Auto-connect enabled devices
    int connectedCount = m_ioManager->ConnectEnabledDevices();
    m_logManager->Info(
      "Connected " + std::to_string(connectedCount) + " IO devices",
      "Application"
    );

    // 5. Start automatic polling (100ms interval)
    m_ioManager->StartPolling(100);
    m_logManager->Info("IO polling started (100ms interval)", "Application");

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

  // Disconnect motion manager
  if (m_motionManager) {
    m_motionManager->DisconnectAll();
  }

  // Shutdown pneumatic manager (stops all slides)
  if (m_pneumaticManager) {
    m_pneumaticManager->StopAll();
    m_logManager->Info("Pneumatic manager shut down", "Application");
  }

  // Shutdown IO manager
  if (m_ioManager) {
    m_ioManager->StopPolling();
    m_ioManager->DisconnectAll();
    m_ioManager->Shutdown();
    m_logManager->Info("IO manager shut down", "Application");
  }

  // Clear UI callback FIRST before any logging
  if (m_logManager) {
    m_logManager->ClearUICallback();
    m_logManager->Info("Bobcat Engine shutting down...", "Application");
  }

  m_context = ApplicationContext();

  m_pneumaticManager.reset();  // ✅ ADD THIS
  m_ioManager.reset();
  m_motionManager.reset();
  m_cameraManager.reset();
  m_tcpClientManager.reset();
  m_dataStore.reset();
  m_logManager.reset();

  m_sequenceEngine.reset();  // ✅ ADD THIS

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
    m_sequenceEngine->ExecuteSequence(seq);
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

    if (m_sequenceEngine->ExecuteSequence(seq)) {
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
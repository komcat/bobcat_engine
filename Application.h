// Application.h
#pragma once

#include <QObject>
#include <memory>
#include <string>
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "EziIOManager.h"  // Add EziIO include
#include "IO/IoWatchManager.h"
#include "PneumaticManager.h"
#include "SequenceEngine.h"
#include "AdaptiveScanController.h"  // ✅ ADD THIS
#include "TECManager.h"    // ✅ ADD THIS
#include "AuxWindowManager.h"  // ✅ ADD THIS
#include "GdiWindow/GdiCameraWindowManager.h"  // GDI camera monitor window
#include "SwirBeamWindow/SwirBeamWindowManager.h"  // SWIR beam processing window
#include "SMU/SMUPollingManager.h"  // ✅ ADD SMU Polling Manager
#include "CoordinateSystemManager.h"
#include "lighting/LightingManager.h"
#include "safety/SafetyWatchManager.h"
#include "customserver/BeamServerClient.h"
#include "motions/DeviceLinkManager.h"
#include "motions/InterlockManager.h"
#include "version.h"
#include "startup/ConnectionWorkerPool.h"
#include "settings/ApplicationSettings.h"
#include "CameraConfigManager.h"
#include "BeamOptimizer/BeamResultBroker.h"
#include "BeamOptimizer/BeamOptimizerManager.h"
#include "scripting/ScriptEngine.h"

#ifdef USE_QT_CAMERA_WINDOW
#include "QtCameraWindow/QtCameraWindow.h"
#endif

class QApplication;
class MainWindow;
class CameraManager;
class LogManager;
class DataStore;
class TcpClientManager;
class ConnectionSplashScreen;  // ✅ NEW: Splash screen for hardware connection
class SafetyEventLog;          // Structured safety event logging
class ScanActivityTracker;     // Scan-busy broadcaster for TopBar indicator
class TransformationMatrixLoader;  // Per-device global<->local frame matrices
class StateSnapshotManager;        // On-demand labeled state snapshots

class Application : public QObject
{
  Q_OBJECT

public:
  explicit Application(int argc, char* argv[]);
  ~Application();

  int run();
  void shutdown();

  ApplicationContext* getContext() { return &m_context; }

  bool isInitialized() const { return m_initialized; }
  std::string getVersion() const { return BobcatVersion::VERSION_STRING; }
  std::string getApplicationName() const { return "Bobcat Engine"; }

  // Simulation mode control
  void setSimulationMode(bool enable) { m_context.simulationMode = enable; }
  bool isSimulationMode() const { return m_context.simulationMode; }

private:
  bool initialize();
  void createMainWindow();
  void initializeManagers();

  // Declare initialization methods for each manager
  void initializeCameraManager();
  void initializeLogManager();
  void initializeDataStore();
  void initializeTcpClientManager();
  void initializeMotionManager();
  void initializeIOManager();  
  void initializePneumaticManager();
  void initializeSequenceEngine();  
  void initializeScanController();      // ✅ ADD THIS
	void initializeProductPositionManager();  // ✅ ADD THIS
  void initializeTECManager();  // ✅ ADD THIS LINE
  void initializeAuxWindowManager();  // ✅ ADD THIS
  void initializeGdiCameraWindow();   // GDI camera monitor window
  void initializeGdiCameraWindow2();  // Second GDI camera monitor window
#ifdef USE_QT_CAMERA_WINDOW
  void initializeQtCameraWindow();   // Qt-based camera monitor
  void initializeQtCameraWindow2();  // Second Qt-based camera monitor
#endif
  void initializeSwirBeamWindow();   // SWIR beam processing window
  void initializePowerSupplySystem();
  void initializeSMUSystem();  // ✅ ADD SMU System initialization
  void initializeCoordinateSystemManager();
  void initializeLightingManager();
  void initializeSafetyWatch();
  void initializeIoWatch();
  void initializeDeviceLinkManager();
  void initializeApplicationSettings();
  void initializeBeamOptimizer();
  void initializeScriptEngine();

  void testMotionCommands();
  void testIOCommands();
  void testPneumaticCommands();
  void testSequenceEngine();
  void testPneumaticSequence();
  void testScanController();            // ✅ ADD THIS (optional)
  void testOpenCV();                    // ✅ OpenCV integration test

  // ✅ NEW: Background hardware connection (parallel)
  void connectHardwareAsync();
  void addConnectionTasks();
  void onConnectionTaskStarted(const QString& displayName);
  void onConnectionTaskCompleted(const Startup::ConnectionResult& result);
  void onAllConnectionsCompleted(const Startup::ConnectionSummary& summary);

  void performEStopCascade();
  void shutdownManagers();

  std::unique_ptr<QApplication> m_qApplication;
  std::unique_ptr<MainWindow> m_mainWindow;
  ConnectionSplashScreen* m_splashScreen;  // ✅ NEW: Connection progress splash

  std::unique_ptr<CameraManager> m_cameraManager;
  std::unique_ptr<LogManager> m_logManager;
  std::unique_ptr<DataStore> m_dataStore;
  std::unique_ptr<TcpClientManager> m_tcpClientManager;
  std::unique_ptr<UnifiedMotionManager> m_motionManager;
  std::unique_ptr<EziIOManager> m_ioManager;  // Add IO manager
  std::unique_ptr<PneumaticManager> m_pneumaticManager;  
  std::unique_ptr<SequenceEngine> m_sequenceEngine;  
  std::unique_ptr<AdaptiveScanController> m_scanController;  // ✅ ADD THIS
  std::unique_ptr<ProductPositionManager> m_productPositionManager;  // ✅ ADD THIS
  std::unique_ptr<TECManager> m_tecManager;  // ✅ ADD THIS LINE
  std::unique_ptr<AuxWindowManager> m_auxWindowManager;  // ✅ ADD THIS
	std::unique_ptr<PowerSupplyManager> m_powerSupplyManager;
  std::unique_ptr<SMUManager> m_smuManager;  // ✅ ADD SMU Manager
  std::unique_ptr<SMUPollingManager> m_smuPollingManager;  // ✅ ADD SMU Polling Manager
  std::unique_ptr<CoordinateSystemManager> m_coordinateSystemManager;
  std::unique_ptr<Lighting::LightingManager> m_lightingManager;
  std::unique_ptr<SafetyWatchManager> m_safetyWatchManager;
  std::unique_ptr<IoWatchManager> m_ioWatchManager;
  std::unique_ptr<DeviceLinkManager> m_deviceLinkManager;
  std::unique_ptr<InterlockManager> m_interlockManager;
  std::unique_ptr<GdiCameraWindowManager> m_gdiCameraWindowManager;   // GDI camera monitor
  std::unique_ptr<GdiCameraWindowManager> m_gdiCameraWindowManager2;  // Second GDI camera monitor
#ifdef USE_QT_CAMERA_WINDOW
  std::unique_ptr<QtCameraWindow> m_qtCameraWindow;                  // Qt camera monitor
  std::unique_ptr<QtCameraWindow> m_qtCameraWindow2;                 // Second Qt camera monitor
#endif
  std::unique_ptr<SwirBeamWindowManager> m_swirBeamWindowManager;    // SWIR beam processing window
  std::unique_ptr<Startup::ConnectionWorkerPool> m_connectionPool;
  std::unique_ptr<ApplicationSettings> m_applicationSettings;  // Application-wide settings
  std::unique_ptr<SafetyEventLog> m_safetyEventLog;           // Structured safety event log
  std::unique_ptr<BeamServerClient> m_beamServerClient;       // Beam detection TCP client
  std::unique_ptr<CameraConfigManager> m_cameraConfigManager; // Camera config (per-camera calibration)
  std::unique_ptr<BeamResultBroker> m_beamResultBroker;       // Beam detection pub-sub broker
  std::unique_ptr<BeamOptimizerManager> m_beamOptimizerManager; // Beam optimization coordinator
  std::unique_ptr<ScanActivityTracker> m_scanActivityTracker; // Scan-busy broadcaster for TopBar indicator
  std::unique_ptr<ScriptEngine> m_scriptEngine;              // Lua 5.4 scripting engine
  std::unique_ptr<TransformationMatrixLoader> m_transformationMatrixLoader;  // Per-device global<->local matrices
  std::unique_ptr<StateSnapshotManager> m_stateSnapshotManager;              // On-demand labeled state snapshots

  ApplicationContext m_context;

  bool m_initialized;
  int m_argc;
  char** m_argv;
};
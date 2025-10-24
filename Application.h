// Application.h
#pragma once

#include <QObject>
#include <memory>
#include <string>
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "EziIOManager.h"  // Add EziIO include
#include "PneumaticManager.h"  
#include "SequenceEngine.h"  
#include "AdaptiveScanController.h"  // ✅ ADD THIS
#include "TECManager.h"    // ✅ ADD THIS
#include "AuxWindowManager.h"  // ✅ ADD THIS

class QApplication;
class MainWindow;
class CameraManager;
class LogManager;
class DataStore;
class TcpClientManager;

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
  std::string getVersion() const { return "1.0.0"; }
  std::string getApplicationName() const { return "Bobcat Engine"; }

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
  void initializePowerSupplySystem();

  void testMotionCommands();   
  void testIOCommands();      
  void testPneumaticCommands();  
  void testSequenceEngine();  
  void testPneumaticSequence();  
  void testScanController();            // ✅ ADD THIS (optional)


  void shutdownManagers();

  std::unique_ptr<QApplication> m_qApplication;
  std::unique_ptr<MainWindow> m_mainWindow;

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
  
  ApplicationContext m_context;

  bool m_initialized;
  int m_argc;
  char** m_argv;
};
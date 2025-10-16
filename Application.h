// Application.h
#pragma once

#include <QObject>
#include <memory>
#include <string>
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "EziIOManager.h"  // Add EziIO include
#include "PneumaticManager.h"  // ✅ ADD THIS

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
  void initializeIOManager();  // Add IO manager initialization
  void initializePneumaticManager();

  void testMotionCommands();   // Test method
  void testIOCommands();       // Test IO method
  void testPneumaticCommands();  // ✅ TEST PNEUMATICS

  void shutdownManagers();

  std::unique_ptr<QApplication> m_qApplication;
  std::unique_ptr<MainWindow> m_mainWindow;

  std::unique_ptr<CameraManager> m_cameraManager;
  std::unique_ptr<LogManager> m_logManager;
  std::unique_ptr<DataStore> m_dataStore;
  std::unique_ptr<TcpClientManager> m_tcpClientManager;
  std::unique_ptr<UnifiedMotionManager> m_motionManager;
  std::unique_ptr<EziIOManager> m_ioManager;  // Add IO manager
  std::unique_ptr<PneumaticManager> m_pneumaticManager;  // ✅ ADD THIS

  ApplicationContext m_context;

  bool m_initialized;
  int m_argc;
  char** m_argv;
};
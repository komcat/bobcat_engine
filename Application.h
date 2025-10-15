// Application.h
#pragma once

#include <QObject>
#include <memory>
#include <string>
#include "ApplicationContext.h"
#include "unified_motion_manager.h"  // Add include for UnifiedMotionManager


class QApplication;
class MainWindow;
class CameraManager;
class LogManager;
class DataStore;              // ← NEW
class TcpClientManager; // ← NEW

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

	//declare initialization methods for each manager
  void initializeCameraManager();
	void initializeLogManager();
	void initializeDataStore();
	void initializeTcpClientManager();
  void initializeMotionManager();  // Add declaration
	void testMotionCommands(); // Add test method



  void shutdownManagers();

  std::unique_ptr<QApplication> m_qApplication;
  std::unique_ptr<MainWindow> m_mainWindow;

  std::unique_ptr<CameraManager> m_cameraManager;
  std::unique_ptr<LogManager> m_logManager;  // ✅ NEW
  std::unique_ptr<DataStore> m_dataStore;              // ← NEW
  std::unique_ptr<TcpClientManager> m_tcpClientManager; // ← NEW
  std::unique_ptr<UnifiedMotionManager> m_motionManager;  // Add member


  ApplicationContext m_context;

  bool m_initialized;
  int m_argc;
  char** m_argv;
};
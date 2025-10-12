// Application.h
#pragma once

#include <QObject>
#include <memory>
#include <string>
#include "ApplicationContext.h"

class QApplication;
class MainWindow;
class CameraManager;

/**
 * @brief Main application controller for Bobcat Engine
 *
 * Owns all managers and provides unified access via ApplicationContext
 */
class Application : public QObject
{
  Q_OBJECT

public:
  explicit Application(int argc, char* argv[]);
  ~Application();

  // Application lifecycle
  int run();
  void shutdown();

  // Main API - Get access to all managers
  ApplicationContext* getContext() { return &m_context; }

  // Info
  bool isInitialized() const { return m_initialized; }
  std::string getVersion() const { return "1.0.0"; }
  std::string getApplicationName() const { return "Bobcat Engine"; }

private:
  bool initialize();
  void createMainWindow();
  void initializeManagers();
  void shutdownManagers();

  // Qt application
  std::unique_ptr<QApplication> m_qApplication;
  std::unique_ptr<MainWindow> m_mainWindow;

  // Manager ownership
  std::unique_ptr<CameraManager> m_cameraManager;

  // Context provides pointer access
  ApplicationContext m_context;

  bool m_initialized;
  int m_argc;
  char** m_argv;
};
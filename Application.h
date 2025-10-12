#pragma once

#include <QObject>
#include <memory>
#include <string>

class QApplication;
class MultiCameraView;

/**
 * @brief Main application controller for Bobcat Engine
 *
 * Simple lifecycle management: initialize, run, shutdown
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

  // Info
  bool isInitialized() const { return m_initialized; }
  std::string getVersion() const { return "1.0.0"; }
  std::string getApplicationName() const { return "Bobcat Engine"; }

private:
  bool initialize();
  void createMainWindow();

  std::unique_ptr<QApplication> m_qApplication;
  std::unique_ptr<MultiCameraView> m_mainWindow;

  bool m_initialized;
  int m_argc;
  char** m_argv;
};
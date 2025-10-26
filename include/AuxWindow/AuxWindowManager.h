// AuxWindowManager.h
#pragma once

#include <QObject>
#include <memory>

class CameraManager;
class DataStore;
class TcpClientManager;
class UnifiedMotionManager;
class AuxViewWindow;

/**
 * @brief Manager for auxiliary display window (business logic only)
 *
 * Responsibilities:
 * - Manages references to CameraManager, UnifiedMotionManager, and DataStore
 * - Controls auxiliary window lifecycle
 * - Provides interface for setting data sources
 *
 * Window Configuration (hardcoded):
 * - Left Panel: Camera view with click-to-move
 * - Right Panel: Live chart view
 */
class AuxWindowManager : public QObject
{
  Q_OBJECT

public:
  explicit AuxWindowManager(QObject* parent = nullptr);
  ~AuxWindowManager();

  // Initialize and show the auxiliary window
  void initialize();

  // Shutdown and cleanup
  void shutdown();

  // Set data sources (must be called before initialize)
  void setCameraManager(CameraManager* manager);
  void setMotionManager(UnifiedMotionManager* manager);
  void setDataStore(DataStore* dataStore);
  void setTcpClientManager(TcpClientManager* tcpManager);

  // Window control
  void show();
  void hide();
  bool isVisible() const;

  // Get window pointer (for advanced control if needed)
  AuxViewWindow* getWindow() const { return m_window.get(); }

private:
  CameraManager* m_cameraManager;
  UnifiedMotionManager* m_motionManager;
  DataStore* m_dataStore;
  TcpClientManager* m_tcpManager;

  std::unique_ptr<AuxViewWindow> m_window;
  bool m_initialized;
};
// AuxWindowManager.h
#pragma once

#include <QObject>
#include <memory>

class CameraConfigManager;
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

  // Build the auxiliary window. Idempotent. Called lazily by show() and
  // ensureWindow() - startup no longer calls this, because constructing
  // AuxViewWindow (camera widget + live chart + world overlay) costs ~0.9s
  // and most sessions never open the window.
  void initialize();

  // Shutdown and cleanup
  void shutdown();

  // Set data sources (must be called before initialize)
  void setCameraManager(CameraManager* manager);
  void setMotionManager(UnifiedMotionManager* manager);
  void setDataStore(DataStore* dataStore);
  void setTcpClientManager(TcpClientManager* tcpManager);
  void setCameraConfigManager(CameraConfigManager* configMgr);

  // Window control
  void show();
  void hide();
  bool isVisible() const;

  // Get window pointer WITHOUT building it (nullptr if never opened).
  // Use this for teardown/observation paths - notably MainWindow::closeEvent,
  // which must not resurrect a window while the app is shutting down.
  AuxViewWindow* getWindow() const { return m_window.get(); }

  // Get window pointer, building it on first use. Use this from any path that
  // needs to act on the window (recipe auxwindow commands, show()).
  AuxViewWindow* ensureWindow();

private:
  CameraConfigManager* m_cameraConfigManager = nullptr;
  CameraManager* m_cameraManager;
  UnifiedMotionManager* m_motionManager;
  DataStore* m_dataStore;
  TcpClientManager* m_tcpManager;

  std::unique_ptr<AuxViewWindow> m_window;
  bool m_initialized;
};
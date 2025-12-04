// AuxViewWindow.h
#pragma once

#include <QMainWindow>
#include <QHBoxLayout>
#include <QWidget>
#include <QTabWidget>

class CameraManager;
class DataStore;
class CameraClickToMoveWidget;
class LiveChartWidget;
class TcpClientManager;
class UnifiedMotionManager;
class WorldOverlayWidget;
class ProductPositionManager;

/**
 * @brief Auxiliary display window with tabbed interface
 *
 * Tab 1 - Live View:
 * - Left Panel (50%): Camera view with click-to-move
 * - Right Panel (50%): Live chart view
 *
 * Tab 2 - World Overlay:
 * - Camera view with world-locked overlay markers
 * - Layer management panel
 *
 * Features:
 * - Prevents user from closing (hides instead)
 * - Always visible on startup
 * - Designed for second screen display
 * - Click-to-move feature for gantry alignment (Camera_1 only)
 * - World-locked overlay markers that track with gantry position
 */
class AuxViewWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit AuxViewWindow(CameraManager* cameraManager,
                          UnifiedMotionManager* motionManager,
                          DataStore* dataStore,
                          TcpClientManager* tcpManager,
                          QWidget* parent = nullptr);
  ~AuxViewWindow();

  // ✅ ADD: Trigger delayed camera selection after window is fully initialized
  void initializeCameraSelection();

  // Set ProductPositionManager for overlay system
  void setProductPositionManager(ProductPositionManager* positionManager);

  // Get widget pointers for command handler access
  CameraClickToMoveWidget* getCameraWidget() const { return m_cameraClickToMoveWidget; }
  LiveChartWidget* getChartWidget() const { return m_liveChartWidget; }
  WorldOverlayWidget* getOverlayWidget() const { return m_worldOverlayWidget; }

  // Tab control
  void switchToLiveViewTab();
  void switchToOverlayTab();

protected:
  // Override close event to prevent closing (hide instead)
  void closeEvent(QCloseEvent* event) override;

private:
  void setupUI();
  void setupTabs();
  QWidget* createLiveViewTab();    // Tab 1: Camera + Chart
  QWidget* createOverlayTab();     // Tab 2: World Overlay

  CameraManager* m_cameraManager;
  UnifiedMotionManager* m_motionManager;
  DataStore* m_dataStore;
  TcpClientManager* m_tcpManager;
  ProductPositionManager* m_positionManager = nullptr;

  // Main layout
  QWidget* m_centralWidget;
  QTabWidget* m_tabWidget;

  // Tab 1: Live View widgets
  CameraClickToMoveWidget* m_cameraClickToMoveWidget;
  LiveChartWidget* m_liveChartWidget;

  // Tab 2: World Overlay widget
  WorldOverlayWidget* m_worldOverlayWidget = nullptr;
};
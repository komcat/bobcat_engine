// AuxViewWindow.h
#pragma once

#include <QMainWindow>
#include <QHBoxLayout>
#include <QWidget>
#include <QTabWidget>
#include <QSettings>
#include <QSplitter>
#include <QPushButton>

// Enum for Live View layout orientation
enum class LiveViewLayout {
    Horizontal,  // Camera left, Chart right
    Vertical     // Camera top, Chart bottom
};

class CameraConfigManager;
class CameraManager;
class CameraSettingsWidget;
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

  // Set CameraConfigManager for per-camera calibration
  void setCameraConfigManager(CameraConfigManager* configMgr);

  // Set ProductPositionManager for overlay system
  void setProductPositionManager(ProductPositionManager* positionManager);

  // Get widget pointers for command handler access
  CameraClickToMoveWidget* getCameraWidget() const { return m_cameraClickToMoveWidget; }
  LiveChartWidget* getChartWidget() const { return m_liveChartWidget; }
  WorldOverlayWidget* getOverlayWidget() const { return m_worldOverlayWidget; }

  // Tab control
  void switchToLiveViewTab();
  void switchToOverlayTab();

  // Layout control for Live View tab
  void setLiveViewLayout(LiveViewLayout layout, bool saveSettings = true);
  LiveViewLayout liveViewLayout() const { return m_liveViewLayout; }

  // Allow the next close event to actually close (called during app shutdown)
  void allowClose() { m_allowClose = true; }

  // Window settings persistence
  void saveWindowSettings();
  void restoreWindowSettings();

private slots:
  void toggleLiveViewLayout();

protected:
  // Override close event to prevent closing (hide instead)
  void closeEvent(QCloseEvent* event) override;
  void hideEvent(QHideEvent* event) override;

private:
  void setupUI();
  void setupTabs();
  QWidget* createLiveViewTab();    // Tab 1: Camera + Chart
  QWidget* createOverlayTab();     // Tab 2: World Overlay
  QWidget* createExposureTab();    // Tab 3: Exposure Settings
  void saveLiveViewLayoutSettings();
  void restoreLiveViewLayoutSettings();

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
  QSplitter* m_liveViewSplitter = nullptr;
  LiveViewLayout m_liveViewLayout = LiveViewLayout::Horizontal;
  QPushButton* m_layoutToggleButton = nullptr;

  // Tab 2: World Overlay widget
  WorldOverlayWidget* m_worldOverlayWidget = nullptr;

  // Tab 3: Exposure settings widget
  CameraSettingsWidget* m_cameraSettingsWidget = nullptr;

  // When true, closeEvent accepts instead of hiding
  bool m_allowClose = false;
};
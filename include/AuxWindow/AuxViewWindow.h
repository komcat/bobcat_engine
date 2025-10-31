// AuxViewWindow.h
#pragma once

#include <QMainWindow>
#include <QHBoxLayout>
#include <QWidget>

class CameraManager;
class DataStore;
class CameraClickToMoveWidget;
class LiveChartWidget;
class TcpClientManager;
class UnifiedMotionManager;

/**
 * @brief Auxiliary display window with 50/50 split panels
 *
 * Window Configuration (hardcoded):
 * - Left Panel (50%): Camera view with click-to-move
 * - Right Panel (50%): Live chart view
 *
 * Features:
 * - Prevents user from closing (hides instead)
 * - Always visible on startup
 * - Designed for second screen display
 * - Click-to-move feature for gantry alignment (Camera_1 only)
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

protected:
  // Override close event to prevent closing (hide instead)
  void closeEvent(QCloseEvent* event) override;

private:
  void setupUI();
  void setupLeftPanel();   // Camera view
  void setupRightPanel();  // Chart view

  CameraManager* m_cameraManager;
  UnifiedMotionManager* m_motionManager;
  DataStore* m_dataStore;
  TcpClientManager* m_tcpManager;

  // Main layout
  QWidget* m_centralWidget;
  QHBoxLayout* m_mainLayout;

  // Panels
  QWidget* m_leftPanel;
  QWidget* m_rightPanel;

  // Widgets
  CameraClickToMoveWidget* m_cameraClickToMoveWidget;
  LiveChartWidget* m_liveChartWidget;
};
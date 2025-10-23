// AuxViewWindow.h
#pragma once

#include <QMainWindow>
#include <QHBoxLayout>
#include <QWidget>

class CameraManager;
class DataStore;
class LiveViewCameraWidget;
class LiveChartWidget;
class TcpClientManager;

/**
 * @brief Auxiliary display window with 50/50 split panels
 *
 * Window Configuration (hardcoded):
 * - Left Panel (50%): Live camera view
 * - Right Panel (50%): Live chart view
 *
 * Features:
 * - Prevents user from closing (hides instead)
 * - Always visible on startup
 * - Designed for second screen display
 */
class AuxViewWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit AuxViewWindow(CameraManager* cameraManager, DataStore* dataStore, TcpClientManager* tcpManager, QWidget* parent = nullptr);
  ~AuxViewWindow();

protected:
  // Override close event to prevent closing (hide instead)
  void closeEvent(QCloseEvent* event) override;

private:
  void setupUI();
  void setupLeftPanel();   // Camera view
  void setupRightPanel();  // Chart view

  CameraManager* m_cameraManager;
  DataStore* m_dataStore;
  TcpClientManager* m_tcpManager;

  // Main layout
  QWidget* m_centralWidget;
  QHBoxLayout* m_mainLayout;

  // Panels
  QWidget* m_leftPanel;
  QWidget* m_rightPanel;

  // Widgets
  LiveViewCameraWidget* m_liveViewWidget;
  LiveChartWidget* m_liveChartWidget;
};
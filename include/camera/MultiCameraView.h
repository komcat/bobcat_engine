// MultiCameraView.h - Main window for managing multiple cameras
#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <memory>
#include <vector>
#include <string>

// Forward declarations
class CameraManager;
class CameraConfigManager;
class CameraView;

/**
 * @brief Main widget for multi-camera system
 *
 * Features:
 * - Tab-based interface (one tab per camera)
 * - Dynamic camera loading from config
 * - Connect all / Disconnect all buttons
 * - Status summary
 * - Independent camera controls per tab
 */
class MultiCameraView : public QWidget
{
  Q_OBJECT

public:
  explicit MultiCameraView(QWidget* parent = nullptr);
  ~MultiCameraView();

private slots:
  void onConnectAllClicked();
  void onDisconnectAllClicked();
  void onConnectEnabledClicked();

  // Camera status updates
  void onCameraConnected(QString cameraId);
  void onCameraDisconnected(QString cameraId);
  void onCameraError(QString cameraId, QString error);

private:
  void setupUI();
  void loadCameraConfig();
  void updateStatusSummary();

  // UI Components
  QTabWidget* m_tabWidget;
  QPushButton* m_connectAllButton;
  QPushButton* m_connectEnabledButton;
  QPushButton* m_disconnectAllButton;
  QLabel* m_statusSummary;

  // Camera components
  std::unique_ptr<CameraManager> m_cameraManager;
  std::unique_ptr<CameraConfigManager> m_configManager;

  // Camera views (one per camera)
  std::vector<CameraView*> m_cameraViews;

  // Camera info
  struct CameraInfo {
    std::string id;
    std::string displayName;
    bool enabled;
  };
  std::vector<CameraInfo> m_cameras;
};
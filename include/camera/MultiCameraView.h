// MultiCameraView.h
#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <memory>
#include <vector>
#include <string>

struct ApplicationContext;
class CameraConfigManager;
class CameraView;

class MultiCameraView : public QWidget
{
  Q_OBJECT

public:
  explicit MultiCameraView(ApplicationContext* context, QWidget* parent = nullptr);
  ~MultiCameraView();

private slots:
  void onConnectAllClicked();
  void onDisconnectAllClicked();
  void onConnectEnabledClicked();

  void onCameraConnected(QString cameraId);
  void onCameraDisconnected(QString cameraId);
  void onCameraError(QString cameraId, QString error);

private:
  void setupUI();
  void loadCameraConfig();
  void updateStatusSummary();

  ApplicationContext* m_context;

  QTabWidget* m_tabWidget;
  QPushButton* m_connectAllButton;
  QPushButton* m_connectEnabledButton;
  QPushButton* m_disconnectAllButton;
  QLabel* m_statusSummary;

  std::unique_ptr<CameraConfigManager> m_configManager;
  std::vector<CameraView*> m_cameraViews;

  struct CameraInfo {
    std::string id;
    std::string displayName;
    bool enabled;
  };
  std::vector<CameraInfo> m_cameras;
};
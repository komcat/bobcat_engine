// MultiCameraView.cpp
#include "MultiCameraView.h"
#include "ApplicationContext.h"
#include "CameraManager.h"
#include "CameraView.h"
#include "CameraConfigManager.h"
#include <QMessageBox>
#include <QHBoxLayout>
#include <iostream>

MultiCameraView::MultiCameraView(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
{
  setupUI();

  // Create config manager (no longer creating CameraManager!)
  m_configManager = std::make_unique<CameraConfigManager>("config/camera_config.json");

  loadCameraConfig();

  std::cout << "[MultiCameraView] Created with " << m_cameras.size() << " cameras" << std::endl;
}

MultiCameraView::~MultiCameraView()
{
  std::cout << "[MultiCameraView] Destructor called" << std::endl;

  for (auto* view : m_cameraViews) {
    if (view) {
      view->disconnectCamera();
    }
  }
}


void MultiCameraView::setupUI()
{
  // Set dark theme for the entire MultiCameraView widget
  setStyleSheet(
    "MultiCameraView { "
    "  background-color: #2d2d30; "
    "} "
    "QWidget { "
    "  background-color: #2d2d30; "
    "  color: #d4d4d4; "
    "} "
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  border: 1px solid #0c5280; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1177bb; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0c5280; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #3c3c3c; "
    "  color: #666666; "
    "  border-color: #555555; "
    "}"
  );

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  m_statusSummary = new QLabel("Cameras: Loading configuration...");
  m_statusSummary->setStyleSheet(
    "QLabel { background-color: #444444; color: white; padding: 8px; font-weight: bold; font-size: 14px; }");
  mainLayout->addWidget(m_statusSummary);

  m_tabWidget = new QTabWidget();
  m_tabWidget->setTabPosition(QTabWidget::North);
  m_tabWidget->setStyleSheet(
    "QTabWidget::pane { "
    "  border: none; "
    "  background-color: #2d2d30; "
    "} "
    "QTabBar { "
    "  background-color: #2d2d30; "
    "  border: none; "
    "} "
    "QTabBar::tab { "
    "  background-color: #1e1e1e; "
    "  color: #888888; "
    "  padding: 10px 20px; "
    "  margin-right: 2px; "
    "  border: none; "
    "  border-top-left-radius: 4px; "
    "  border-top-right-radius: 4px; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "  border-bottom: 3px solid transparent; "
    "} "
    "QTabBar::tab:selected { "
    "  background-color: #0e639c; "
    "  color: #ffffff; "
    "  border-bottom: 3px solid #0e639c; "
    "} "
    "QTabBar::tab:hover { "
    "  background-color: #2a2d2e; "
    "  color: #d4d4d4; "
    "}"
  );
  mainLayout->addWidget(m_tabWidget);

  QHBoxLayout* buttonLayout = new QHBoxLayout();

  m_connectEnabledButton = new QPushButton("Connect Enabled Cameras");
  m_connectEnabledButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  border: 1px solid #229954; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2ecc71; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #3c3c3c; "
    "  color: #666666; "
    "  border-color: #555555; "
    "}"
  );
  m_connectEnabledButton->setEnabled(false);
  connect(m_connectEnabledButton, &QPushButton::clicked, this, &MultiCameraView::onConnectEnabledClicked);
  buttonLayout->addWidget(m_connectEnabledButton);

  m_connectAllButton = new QPushButton("Connect All Cameras");
  m_connectAllButton->setEnabled(false);
  connect(m_connectAllButton, &QPushButton::clicked, this, &MultiCameraView::onConnectAllClicked);
  buttonLayout->addWidget(m_connectAllButton);

  m_disconnectAllButton = new QPushButton("Disconnect All Cameras");
  m_disconnectAllButton->setEnabled(false);
  connect(m_disconnectAllButton, &QPushButton::clicked, this, &MultiCameraView::onDisconnectAllClicked);
  buttonLayout->addWidget(m_disconnectAllButton);

  mainLayout->addLayout(buttonLayout);

  setLayout(mainLayout);
  setWindowTitle("Bobcat Engine - Multi-Camera System");
  resize(1200, 800);
}



void MultiCameraView::loadCameraConfig()
{
  if (!m_configManager->LoadConfig()) {
    QMessageBox::warning(this, "Config Error",
      "Failed to load camera_config.json. Using default settings.");
    m_statusSummary->setText("Error: Failed to load configuration");
    return;
  }

  const auto& cameraConfigs = m_configManager->GetCameraConfigs();

  if (cameraConfigs.empty()) {
    QMessageBox::warning(this, "No Cameras",
      "No cameras found in camera_config.json");
    m_statusSummary->setText("No cameras configured");
    return;
  }

  std::cout << "[MultiCameraView] Loading " << cameraConfigs.size() << " cameras from config" << std::endl;


  // Note: Cameras already initialized in Application.cpp - just create UI views
  // m_configManager->InitializeCameraManager(*m_context->cameraManager);  // REMOVED: Already done in Application

  // Create camera views
  for (const auto& camConfig : cameraConfigs) {
    CameraInfo info;
    info.id = camConfig.id;
    info.displayName = camConfig.displayName;
    info.enabled = camConfig.enabled;
    m_cameras.push_back(info);

    // Pass context to CameraView
    CameraView* cameraView = new CameraView(m_context,
      camConfig.id,
      camConfig.displayName,
      this);

    connect(cameraView, &CameraView::cameraConnected,
      this, &MultiCameraView::onCameraConnected);
    connect(cameraView, &CameraView::cameraDisconnected,
      this, &MultiCameraView::onCameraDisconnected);
    connect(cameraView, &CameraView::cameraError,
      this, &MultiCameraView::onCameraError);

    m_cameraViews.push_back(cameraView);

    QString tabLabel = QString::fromStdString(camConfig.displayName);
    if (!camConfig.enabled) {
      tabLabel += " [DISABLED]";
    }
    m_tabWidget->addTab(cameraView, tabLabel);

    std::cout << "[MultiCameraView] Added camera: " << camConfig.id
      << " (enabled: " << (camConfig.enabled ? "yes" : "no") << ")" << std::endl;
  }

  m_connectEnabledButton->setEnabled(true);
  m_connectAllButton->setEnabled(true);
  m_disconnectAllButton->setEnabled(true);

  updateStatusSummary();
}



void MultiCameraView::onConnectAllClicked()
{
  std::cout << "[MultiCameraView] Connecting all cameras..." << std::endl;

  int successCount = 0;
  int failCount = 0;

  for (auto* view : m_cameraViews) {
    if (view->connectCamera()) {
      successCount++;
    }
    else {
      failCount++;
    }
  }

  std::cout << "[MultiCameraView] Connected: " << successCount
    << ", Failed: " << failCount << std::endl;

  if (failCount > 0) {
    QMessageBox::warning(this, "Connection Issues",
      QString("Successfully connected %1 camera(s).\n"
        "Failed to connect %2 camera(s).\n"
        "Check individual camera tabs for details.")
      .arg(successCount).arg(failCount));
  }

  updateStatusSummary();
}



void MultiCameraView::onConnectEnabledClicked()
{
  std::cout << "[MultiCameraView] Connecting enabled cameras..." << std::endl;

  int successCount = 0;
  int failCount = 0;
  int skippedCount = 0;

  for (size_t i = 0; i < m_cameraViews.size(); ++i) {
    if (!m_cameras[i].enabled) {
      skippedCount++;
      std::cout << "[MultiCameraView] Skipping disabled camera: "
        << m_cameras[i].id << std::endl;
      continue;
    }

    if (m_cameraViews[i]->connectCamera()) {
      successCount++;
    }
    else {
      failCount++;
    }
  }

  std::cout << "[MultiCameraView] Connected: " << successCount
    << ", Failed: " << failCount
    << ", Skipped: " << skippedCount << std::endl;

  if (failCount > 0) {
    QMessageBox::warning(this, "Connection Issues",
      QString("Successfully connected %1 enabled camera(s).\n"
        "Failed to connect %2 camera(s).\n"
        "Skipped %3 disabled camera(s).")
      .arg(successCount).arg(failCount).arg(skippedCount));
  }

  updateStatusSummary();
}



void MultiCameraView::onDisconnectAllClicked()
{
  std::cout << "[MultiCameraView] Disconnecting all cameras..." << std::endl;

  for (auto* view : m_cameraViews) {
    view->disconnectCamera();
  }

  updateStatusSummary();
}

void MultiCameraView::onCameraConnected(QString cameraId)
{
  std::cout << "[MultiCameraView] Camera connected: "
    << cameraId.toStdString() << std::endl;
  updateStatusSummary();
}

void MultiCameraView::onCameraDisconnected(QString cameraId)
{
  std::cout << "[MultiCameraView] Camera disconnected: "
    << cameraId.toStdString() << std::endl;
  updateStatusSummary();
}

void MultiCameraView::onCameraError(QString cameraId, QString error)
{
  std::cerr << "[MultiCameraView] Camera error ["
    << cameraId.toStdString() << "]: "
    << error.toStdString() << std::endl;
}


void MultiCameraView::updateStatusSummary()
{
  int totalCameras = static_cast<int>(m_cameraViews.size());
  int connectedCameras = 0;
  int enabledCameras = 0;

  // Query CameraManager directly for accurate connection status
  for (size_t i = 0; i < m_cameras.size(); ++i) {
    if (m_cameras[i].enabled) {
      enabledCameras++;
    }

    // Get status from CameraManager, not from UI state
    if (m_context && m_context->cameraManager) {
      auto status = m_context->cameraManager->GetCameraStatus(m_cameras[i].id);
      if (status.connected && status.grabbing) {
        connectedCameras++;
      }
    }
    else if (i < m_cameraViews.size() && m_cameraViews[i]->isConnected()) {
      // Fallback to UI state if no CameraManager
      connectedCameras++;
    }
  }

  QString status = QString("Cameras: %1 Total | %2 Enabled | %3 Connected")
    .arg(totalCameras)
    .arg(enabledCameras)
    .arg(connectedCameras);

  m_statusSummary->setText(status);

  if (connectedCameras == 0) {
    m_statusSummary->setStyleSheet(
      "QLabel { background-color: #ff4444; color: white; padding: 8px; font-weight: bold; font-size: 14px; }");
  }
  else if (connectedCameras < enabledCameras) {
    m_statusSummary->setStyleSheet(
      "QLabel { background-color: #ffaa44; color: black; padding: 8px; font-weight: bold; font-size: 14px; }");
  }
  else {
    m_statusSummary->setStyleSheet(
      "QLabel { background-color: #44ff44; color: black; padding: 8px; font-weight: bold; font-size: 14px; }");
  }

  std::cout << "[MultiCameraView] Status: " << status.toStdString() << std::endl;
}

void MultiCameraView::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);

  // Update status summary when page is shown to reflect current camera states
  updateStatusSummary();

  std::cout << "[MultiCameraView] Page shown - refreshed status summary" << std::endl;
}
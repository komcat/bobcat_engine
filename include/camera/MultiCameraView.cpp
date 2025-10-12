// MultiCameraView.cpp - Multi-camera manager implementation
#include "MultiCameraView.h"
#include "CameraView.h"
#include "CameraManager.h"
#include "CameraConfigManager.h"
#include <QMessageBox>
#include <QHBoxLayout>
#include <iostream>

MultiCameraView::MultiCameraView(QWidget* parent)
  : QWidget(parent)
{
  setupUI();

  // Create camera manager and config manager
  m_cameraManager = std::make_unique<CameraManager>();
  m_configManager = std::make_unique<CameraConfigManager>("config/camera_config.json");

  // Load camera configuration
  loadCameraConfig();

  std::cout << "[MultiCameraView] Created with " << m_cameras.size() << " cameras" << std::endl;
}

MultiCameraView::~MultiCameraView()
{
  std::cout << "[MultiCameraView] Destructor called" << std::endl;

  // ✅ FIX: Force disconnect ALL cameras before destruction
  if (m_cameraManager) {
    std::cout << "[MultiCameraView] Stopping all cameras..." << std::endl;
    m_cameraManager->StopGrabbingAll();

    // Give cameras time to stop cleanly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Disconnect each camera explicitly
    for (auto* view : m_cameraViews) {
      if (view) {
        view->disconnectCamera();
      }
    }

    // Another brief pause before destroying manager
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::cout << "[MultiCameraView] Cleanup complete" << std::endl;
}

void MultiCameraView::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Status summary
  m_statusSummary = new QLabel("Cameras: Loading configuration...");
  m_statusSummary->setStyleSheet(
    "QLabel { background-color: #444444; color: white; padding: 8px; font-weight: bold; font-size: 14px; }");
  mainLayout->addWidget(m_statusSummary);

  // Tab widget for cameras
  m_tabWidget = new QTabWidget();
  m_tabWidget->setTabPosition(QTabWidget::North);
  mainLayout->addWidget(m_tabWidget);

  // Control buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();

  m_connectEnabledButton = new QPushButton("Connect Enabled Cameras");
  m_connectEnabledButton->setStyleSheet("QPushButton { background-color: #44aa44; color: white; padding: 8px; font-weight: bold; }");
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

  // Get all camera configurations
  const auto& cameraConfigs = m_configManager->GetCameraConfigs();

  if (cameraConfigs.empty()) {
    QMessageBox::warning(this, "No Cameras",
      "No cameras found in camera_config.json");
    m_statusSummary->setText("No cameras configured");
    return;
  }

  std::cout << "[MultiCameraView] Loading " << cameraConfigs.size() << " cameras from config" << std::endl;

  // Initialize camera manager with all cameras
  m_configManager->InitializeCameraManager(*m_cameraManager);

  // Create camera views for each configured camera
  for (const auto& camConfig : cameraConfigs) {
    // Store camera info
    CameraInfo info;
    info.id = camConfig.id;
    info.displayName = camConfig.displayName;
    info.enabled = camConfig.enabled;
    m_cameras.push_back(info);

    // Create camera view widget
    CameraView* cameraView = new CameraView(m_cameraManager.get(),
      camConfig.id,
      camConfig.displayName,
      this);

    // Connect signals
    connect(cameraView, &CameraView::cameraConnected,
      this, &MultiCameraView::onCameraConnected);
    connect(cameraView, &CameraView::cameraDisconnected,
      this, &MultiCameraView::onCameraDisconnected);
    connect(cameraView, &CameraView::cameraError,
      this, &MultiCameraView::onCameraError);

    m_cameraViews.push_back(cameraView);

    // Add as tab
    QString tabLabel = QString::fromStdString(camConfig.displayName);
    if (!camConfig.enabled) {
      tabLabel += " [DISABLED]";
    }
    m_tabWidget->addTab(cameraView, tabLabel);

    std::cout << "[MultiCameraView] Added camera: " << camConfig.id
      << " (enabled: " << (camConfig.enabled ? "yes" : "no") << ")" << std::endl;
  }

  // Enable control buttons
  m_connectEnabledButton->setEnabled(true);
  m_connectAllButton->setEnabled(true);
  m_disconnectAllButton->setEnabled(true);

  // Update status
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
  int totalCameras = m_cameraViews.size();
  int connectedCameras = 0;
  int enabledCameras = 0;

  for (size_t i = 0; i < m_cameraViews.size(); ++i) {
    if (m_cameraViews[i]->isConnected()) {
      connectedCameras++;
    }
    if (m_cameras[i].enabled) {
      enabledCameras++;
    }
  }

  QString status = QString("Cameras: %1 Total | %2 Enabled | %3 Connected")
    .arg(totalCameras)
    .arg(enabledCameras)
    .arg(connectedCameras);

  m_statusSummary->setText(status);

  // Change color based on status
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
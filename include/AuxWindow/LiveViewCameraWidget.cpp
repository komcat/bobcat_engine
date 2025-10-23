// LiveViewCameraWidget.cpp
#include "LiveViewCameraWidget.h"
#include "CameraManager.h"
#include "CameraImageWidget.h"
#include "LiveVideoSubscriber.h"
#include <QTimer>
#include <iostream>

LiveViewCameraWidget::LiveViewCameraWidget(CameraManager* cameraManager, QWidget* parent)
  : QWidget(parent)
  , m_cameraManager(cameraManager)
  , m_subscriber(nullptr)
{
  setupUI();

  // Frame update timer - poll at 30 FPS
  m_frameUpdateTimer = new QTimer(this);
  connect(m_frameUpdateTimer, &QTimer::timeout, this, &LiveViewCameraWidget::onUpdateFrame);
  m_frameUpdateTimer->start(33);  // ~30 FPS

  // Auto-refresh camera list every 5 seconds
  m_refreshTimer = new QTimer(this);
  connect(m_refreshTimer, &QTimer::timeout, this, &LiveViewCameraWidget::onRefreshCameras);
  m_refreshTimer->start(5000);

  // Initial camera list
  refreshCameraList();

  std::cout << "[LiveViewCameraWidget] Created" << std::endl;
}

LiveViewCameraWidget::~LiveViewCameraWidget()
{
  unsubscribeFromCameraFeed();

  if (m_frameUpdateTimer) {
    m_frameUpdateTimer->stop();
  }

  if (m_refreshTimer) {
    m_refreshTimer->stop();
  }

  std::cout << "[LiveViewCameraWidget] Destroyed" << std::endl;
}

void LiveViewCameraWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(10);

  // Header with camera selector
  QWidget* headerWidget = new QWidget();
  headerWidget->setStyleSheet("QWidget { background-color: #2d2d2d; border-radius: 6px; }");
  QVBoxLayout* headerLayout = new QVBoxLayout(headerWidget);
  headerLayout->setContentsMargins(15, 10, 15, 10);
  headerLayout->setSpacing(8);

  // Title
  QLabel* titleLabel = new QLabel("Live Camera View");
  titleLabel->setStyleSheet("QLabel { font-size: 18px; font-weight: bold; color: #ffffff; background: transparent; }");
  headerLayout->addWidget(titleLabel);

  // Camera selector row
  QHBoxLayout* selectorRow = new QHBoxLayout();

  QLabel* selectorLabel = new QLabel("Camera:");
  selectorLabel->setStyleSheet("QLabel { color: #aaaaaa; font-size: 14px; background: transparent; }");
  selectorRow->addWidget(selectorLabel);

  m_cameraSelector = new QComboBox();
  m_cameraSelector->setStyleSheet(
    "QComboBox { "
    "  background-color: #3d3d3d; "
    "  color: white; "
    "  padding: 5px 10px; "
    "  border: 1px solid #505050; "
    "  border-radius: 4px; "
    "  font-size: 14px; "
    "} "
    "QComboBox::drop-down { border: none; } "
    "QComboBox QAbstractItemView { "
    "  background-color: #3d3d3d; "
    "  color: white; "
    "  selection-background-color: #0078d4; "
    "}");
  connect(m_cameraSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &LiveViewCameraWidget::onCameraSelected);
  selectorRow->addWidget(m_cameraSelector);

  selectorRow->addStretch();

  headerLayout->addLayout(selectorRow);

  // Camera info label
  m_cameraInfoLabel = new QLabel("No camera selected");
  m_cameraInfoLabel->setStyleSheet("QLabel { color: #888888; font-size: 12px; background: transparent; }");
  headerLayout->addWidget(m_cameraInfoLabel);

  mainLayout->addWidget(headerWidget);

  // Camera image display
  m_imageWidget = new CameraImageWidget();
  m_imageWidget->setMinimumDisplaySize(640, 480);
  m_imageWidget->setBackgroundColor(QColor(30, 30, 30));
  m_imageWidget->setPlaceholderText("Select a camera to view live feed");
  mainLayout->addWidget(m_imageWidget);
}

void LiveViewCameraWidget::onCameraSelected(int index)
{
  if (index < 0) return;

  std::string cameraId = m_cameraSelector->itemData(index).toString().toStdString();
  setCamera(cameraId);
}

void LiveViewCameraWidget::setCamera(const std::string& cameraId)
{
  if (cameraId == m_currentCameraId) return;

  std::cout << "[LiveViewCameraWidget] Switching to camera: " << cameraId << std::endl;

  // Unsubscribe from previous camera
  unsubscribeFromCameraFeed();

  m_currentCameraId = cameraId;

  if (cameraId.empty()) {
    m_imageWidget->clearDisplay();
    m_cameraInfoLabel->setText("No camera selected");
    return;
  }

  // Subscribe to new camera feed
  subscribeToCameraFeed(cameraId);

  // Update info label
  m_cameraInfoLabel->setText(QString("Streaming: %1").arg(QString::fromStdString(cameraId)));

  emit cameraChanged(cameraId);
}

void LiveViewCameraWidget::subscribeToCameraFeed(const std::string& cameraId)
{
  if (!m_cameraManager || cameraId.empty()) return;

  // Create a shared_ptr LiveVideoSubscriber (callback-based, not QObject)
  m_subscriber = std::make_shared<LiveVideoSubscriber>(cameraId);

  // Initialize status from camera manager if camera is already running
  m_subscriber->InitializeStatusFromManager(m_cameraManager);

  // Register subscriber with CameraManager using shared_ptr
  m_cameraManager->SubscribeToFrames(m_subscriber);

  std::cout << "[LiveViewCameraWidget] Subscribed to camera: " << cameraId << std::endl;
}

void LiveViewCameraWidget::unsubscribeFromCameraFeed()
{
  if (!m_subscriber) return;

  // Unregister from CameraManager using subscriber ID
  if (m_cameraManager) {
    m_cameraManager->UnsubscribeFromFrames(m_subscriber->GetSubscriberId());
  }

  // Reset shared_ptr (auto-deletes if no other references)
  m_subscriber.reset();

  std::cout << "[LiveViewCameraWidget] Unsubscribed from camera feed" << std::endl;
}

void LiveViewCameraWidget::onRefreshCameras()
{
  refreshCameraList();
}

void LiveViewCameraWidget::onUpdateFrame()
{
  // Poll subscriber for new frames
  if (m_subscriber && m_subscriber->HasNewFrame()) {
    CameraFrameData frame = m_subscriber->GetLatestFrame();
    m_imageWidget->setFrame(frame);
    m_subscriber->MarkFrameConsumed();
  }
}

void LiveViewCameraWidget::refreshCameraList()
{
  if (!m_cameraManager) return;

  // Store current selection
  std::string currentCamera = getCurrentCamera();

  // Block signals to prevent unwanted camera switches
  m_cameraSelector->blockSignals(true);
  m_cameraSelector->clear();

  auto cameraIds = m_cameraManager->GetCameraIds();

  if (cameraIds.empty()) {
    m_cameraSelector->addItem("No cameras available", QVariant(QString("")));
    m_cameraSelector->blockSignals(false);
    return;
  }

  int currentIndex = -1;
  int idx = 0;

  for (const auto& cameraId : cameraIds) {
    QString displayName = QString::fromStdString(cameraId);
    m_cameraSelector->addItem(displayName, QVariant(QString::fromStdString(cameraId)));

    if (cameraId == currentCamera) {
      currentIndex = idx;
    }
    idx++;
  }

  // Restore selection or select first
  if (currentIndex >= 0) {
    m_cameraSelector->setCurrentIndex(currentIndex);
  }
  else if (m_cameraSelector->count() > 0) {
    m_cameraSelector->setCurrentIndex(0);
  }

  m_cameraSelector->blockSignals(false);

  // If selection changed, update camera
  std::string newCamera = getCurrentCamera();
  if (newCamera != currentCamera && !newCamera.empty()) {
    setCamera(newCamera);
  }
  // ✅ NEW: Auto-subscribe to first camera if nothing was selected before
  else if (currentCamera.empty() && !newCamera.empty()) {
    setCamera(newCamera);
    std::cout << "[LiveViewCameraWidget] Auto-subscribed to first camera: " << newCamera << std::endl;
  }
}

std::string LiveViewCameraWidget::getCurrentCamera() const
{
  if (m_cameraSelector->currentIndex() < 0) {
    return "";
  }
  return m_cameraSelector->currentData().toString().toStdString();
}
// CameraClickToMoveWidget.cpp
#include "CameraClickToMoveWidget.h"
#include "CameraManager.h"
#include "ICameraHardware.h"  // ✅ ADD: For IsGrabbing() check
#include "LiveVideoSubscriber.h"
#include "unified_motion_manager.h"
#include "CameraFrameData.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QShowEvent>  // ✅ ADD: For showEvent()
#include <QCoreApplication>
#include <QFile>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

// JSON config file for calibration (shared with VisionManager)
static constexpr const char* CALIBRATION_CONFIG_FILE = "config/SYSTEM/val_pixelmm.json";

CameraClickToMoveWidget::CameraClickToMoveWidget(CameraManager* cameraManager,
                                                   UnifiedMotionManager* motionManager,
                                                   QWidget* parent)
  : QWidget(parent)
  , m_cameraManager(cameraManager)
  , m_motionManager(motionManager)
  , m_subscriber(nullptr)
  , m_zoomPercent(100)
  , m_clickToMoveEnabled(false)
  , m_centerCrosshairEnabled(false)
  , m_mouseOverImage(false)
  , m_pixelToMmFactorX(0.00248)
  , m_pixelToMmFactorY(0.00248)
  , m_initialCameraSelected(false)  // ✅ ADD: Initialize flag
{
  // Load saved settings
  loadSettingsFromQSettings();

  setupUI();

  // Frame update timer - 20 FPS
  m_frameUpdateTimer = new QTimer(this);
  connect(m_frameUpdateTimer, &QTimer::timeout, this, &CameraClickToMoveWidget::onUpdateFrame);
  m_frameUpdateTimer->start(50);  // 20 FPS

  // Auto-refresh camera list every 5 seconds
  m_refreshTimer = new QTimer(this);
  connect(m_refreshTimer, &QTimer::timeout, this, &CameraClickToMoveWidget::onRefreshCameras);
  m_refreshTimer->start(5000);

  // ✅ CHANGED: Populate camera list but DON'T auto-select yet
  // Camera will be selected in showEvent() after UI is fully rendered
  if (m_cameraManager) {
    m_cameraSelector->blockSignals(true);
    auto cameraIds = m_cameraManager->GetCameraIds();

    if (cameraIds.empty()) {
      m_cameraSelector->addItem("No cameras available", QVariant(QString("")));
    } else {
      for (const auto& cameraId : cameraIds) {
        m_cameraSelector->addItem(QString::fromStdString(cameraId), QVariant(QString::fromStdString(cameraId)));
      }
      // ✅ FIX: Set to no selection (-1) so delayed selection will properly trigger signal
      m_cameraSelector->setCurrentIndex(-1);
      std::cout << "[CameraClickToMoveWidget] Populated " << cameraIds.size() << " cameras (no selection, delayed)" << std::endl;
    }

    m_cameraSelector->blockSignals(false);
  }

  // Enable mouse tracking for hover crosshair
  setMouseTracking(true);

  std::cout << "[CameraClickToMoveWidget] Created (Pixel-to-mm: X=" << m_pixelToMmFactorX
            << ", Y=" << m_pixelToMmFactorY << ")" << std::endl;
}

CameraClickToMoveWidget::~CameraClickToMoveWidget()
{
  unsubscribeFromCameraFeed();

  if (m_frameUpdateTimer) {
    m_frameUpdateTimer->stop();
  }

  if (m_refreshTimer) {
    m_refreshTimer->stop();
  }

  std::cout << "[CameraClickToMoveWidget] Destroyed" << std::endl;
}

void CameraClickToMoveWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(10, 10, 10, 10);
  mainLayout->setSpacing(10);

  // Header
  QWidget* headerWidget = new QWidget();
  headerWidget->setStyleSheet("QWidget { background-color: #2d2d2d; border-radius: 6px; }");
  headerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  QVBoxLayout* headerLayout = new QVBoxLayout(headerWidget);
  headerLayout->setContentsMargins(15, 10, 15, 10);
  headerLayout->setSpacing(8);

  // Title
  QLabel* titleLabel = new QLabel("Camera View with Click-to-Move");
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
    this, &CameraClickToMoveWidget::onCameraSelected);
  selectorRow->addWidget(m_cameraSelector);

  selectorRow->addStretch();

  headerLayout->addLayout(selectorRow);

  // Camera info label
  m_cameraInfoLabel = new QLabel("No camera selected");
  m_cameraInfoLabel->setStyleSheet("QLabel { color: #888888; font-size: 12px; background: transparent; }");
  headerLayout->addWidget(m_cameraInfoLabel);

  // Controls row 1: Click to Move and Crosshair
  QHBoxLayout* controls1Row = new QHBoxLayout();

  m_clickToMoveCheckbox = new QCheckBox("Click to Move");
  m_clickToMoveCheckbox->setStyleSheet("QCheckBox { color: #ffaa00; font-size: 13px; font-weight: bold; background: transparent; }");
  connect(m_clickToMoveCheckbox, &QCheckBox::toggled, this, &CameraClickToMoveWidget::onClickToMoveToggled);
  controls1Row->addWidget(m_clickToMoveCheckbox);

  m_crosshairCheckbox = new QCheckBox("Crosshair Overlay");
  m_crosshairCheckbox->setStyleSheet("QCheckBox { color: #aaaaaa; font-size: 13px; background: transparent; }");
  connect(m_crosshairCheckbox, &QCheckBox::toggled, this, &CameraClickToMoveWidget::onCrosshairToggled);
  controls1Row->addWidget(m_crosshairCheckbox);

  controls1Row->addStretch();

  headerLayout->addLayout(controls1Row);

  // Controls row 2: Zoom buttons
  QHBoxLayout* controls2Row = new QHBoxLayout();

  QLabel* zoomLabel = new QLabel("Zoom:");
  zoomLabel->setStyleSheet("QLabel { color: #aaaaaa; font-size: 13px; background: transparent; }");
  controls2Row->addWidget(zoomLabel);

  QString buttonStyle =
    "QPushButton { "
    "  background-color: #3d3d3d; "
    "  color: white; "
    "  padding: 4px 12px; "
    "  border: 1px solid #505050; "
    "  border-radius: 3px; "
    "  font-size: 13px; "
    "} "
    "QPushButton:hover { background-color: #4d4d4d; } "
    "QPushButton:pressed { background-color: #0078d4; }";

  m_zoom1xButton = new QPushButton("1.0x");
  m_zoom1xButton->setStyleSheet(buttonStyle);
  m_zoom1xButton->setFixedWidth(50);
  connect(m_zoom1xButton, &QPushButton::clicked, [this]() { onZoomChanged(100); });
  controls2Row->addWidget(m_zoom1xButton);

  m_zoom15xButton = new QPushButton("1.5x");
  m_zoom15xButton->setStyleSheet(buttonStyle);
  m_zoom15xButton->setFixedWidth(50);
  connect(m_zoom15xButton, &QPushButton::clicked, [this]() { onZoomChanged(67); });
  controls2Row->addWidget(m_zoom15xButton);

  m_zoom2xButton = new QPushButton("2.0x");
  m_zoom2xButton->setStyleSheet(buttonStyle);
  m_zoom2xButton->setFixedWidth(50);
  connect(m_zoom2xButton, &QPushButton::clicked, [this]() { onZoomChanged(50); });
  controls2Row->addWidget(m_zoom2xButton);

  controls2Row->addStretch();

  headerLayout->addLayout(controls2Row);

  mainLayout->addWidget(headerWidget);

  // Image display label
  m_imageLabel = new QLabel();
  m_imageLabel->setAlignment(Qt::AlignCenter);
  m_imageLabel->setScaledContents(false);
  m_imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_imageLabel->setMinimumSize(200, 200);
  m_imageLabel->setStyleSheet(
    "QLabel { "
    "  background-color: #2b2b2b; "
    "  color: #888888; "
    "  border: 1px solid #555555; "
    "}");
  m_imageLabel->setText("Select a camera to view live feed");
  m_imageLabel->setMouseTracking(true);

  mainLayout->addWidget(m_imageLabel, 1);
}

void CameraClickToMoveWidget::loadSettingsFromQSettings()
{
  // Try to load from JSON config file first (shared with VisionManager)
  QString appDir = QCoreApplication::applicationDirPath();
  QString configPath = appDir + "/" + CALIBRATION_CONFIG_FILE;

  if (QFile::exists(configPath)) {
    try {
      std::ifstream file(configPath.toStdString());
      if (file.is_open()) {
        nlohmann::json j;
        file >> j;

        if (j.contains("calibration")) {
          auto& cal = j["calibration"];
          m_pixelToMmFactorX = cal.value("pixel_to_mm_x", 0.00248);
          m_pixelToMmFactorY = cal.value("pixel_to_mm_y", 0.00248);
          std::cout << "[CameraClickToMoveWidget] Loaded from JSON: PixelToMm X=" << m_pixelToMmFactorX
                    << ", Y=" << m_pixelToMmFactorY << std::endl;

          // Sync to QSettings for legacy compatibility
          QSettings settings("BobcatEngine", "Application");
          settings.setValue(SETTINGS_KEY_PIXEL_TO_MM_X, m_pixelToMmFactorX);
          settings.setValue(SETTINGS_KEY_PIXEL_TO_MM_Y, m_pixelToMmFactorY);
          return;
        }
      }
    } catch (const std::exception& e) {
      std::cerr << "[CameraClickToMoveWidget] Error loading JSON config: " << e.what() << std::endl;
    }
  }

  // Fallback to QSettings
  QSettings settings("BobcatEngine", "Application");
  m_pixelToMmFactorX = settings.value(SETTINGS_KEY_PIXEL_TO_MM_X, 0.00248).toDouble();
  m_pixelToMmFactorY = settings.value(SETTINGS_KEY_PIXEL_TO_MM_Y, 0.00248).toDouble();

  std::cout << "[CameraClickToMoveWidget] Loaded from QSettings: PixelToMm X=" << m_pixelToMmFactorX
            << ", Y=" << m_pixelToMmFactorY << std::endl;
}

void CameraClickToMoveWidget::saveSettingsToQSettings()
{
  // Save to JSON config file (shared with VisionManager)
  QString appDir = QCoreApplication::applicationDirPath();
  QString configPath = appDir + "/" + CALIBRATION_CONFIG_FILE;

  try {
    nlohmann::json j;
    j["description"] = "Pixel to millimeter calibration values for vision system";
    j["calibration"]["pixel_to_mm_x"] = m_pixelToMmFactorX;
    j["calibration"]["pixel_to_mm_y"] = m_pixelToMmFactorY;
    j["notes"] = "These values are hardware-specific and should be calibrated for each camera setup";

    std::ofstream file(configPath.toStdString());
    if (file.is_open()) {
      file << j.dump(2);
      file.close();
      std::cout << "[CameraClickToMoveWidget] Saved to JSON: " << configPath.toStdString() << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "[CameraClickToMoveWidget] Error saving JSON config: " << e.what() << std::endl;
  }

  // Also save to QSettings for legacy compatibility
  QSettings settings("BobcatEngine", "Application");
  settings.setValue(SETTINGS_KEY_PIXEL_TO_MM_X, m_pixelToMmFactorX);
  settings.setValue(SETTINGS_KEY_PIXEL_TO_MM_Y, m_pixelToMmFactorY);

  std::cout << "[CameraClickToMoveWidget] Saved settings" << std::endl;
}

void CameraClickToMoveWidget::setPixelToMmFactorX(double factor)
{
  m_pixelToMmFactorX = factor;
  saveSettingsToQSettings();
}

void CameraClickToMoveWidget::setPixelToMmFactorY(double factor)
{
  m_pixelToMmFactorY = factor;
  saveSettingsToQSettings();
}

// ✅ NEW: Programmatic control methods for command handler
void CameraClickToMoveWidget::setClickToMoveEnabled(bool enabled)
{
  if (m_clickToMoveCheckbox) {
    m_clickToMoveCheckbox->setChecked(enabled);
    // The onClickToMoveToggled slot will be called automatically
  }
}

void CameraClickToMoveWidget::setCrosshairEnabled(bool enabled)
{
  if (m_crosshairCheckbox) {
    m_crosshairCheckbox->setChecked(enabled);
    // The onCrosshairToggled slot will be called automatically
  }
}

void CameraClickToMoveWidget::onCameraSelected(int index)
{
  if (index < 0) return;

  std::string cameraId = m_cameraSelector->itemData(index).toString().toStdString();
  setCamera(cameraId);
}

void CameraClickToMoveWidget::setCamera(const std::string& cameraId)
{
  if (cameraId == m_currentCameraId) return;

  std::cout << "[CameraClickToMoveWidget] Switching to camera: " << cameraId << std::endl;

  // Unsubscribe from previous camera
  unsubscribeFromCameraFeed();

  m_currentCameraId = cameraId;

  if (cameraId.empty()) {
    m_imageLabel->clear();
    m_imageLabel->setText("No camera selected");
    m_cameraInfoLabel->setText("No camera selected");

    // Disable click-to-move if no camera
    m_clickToMoveCheckbox->setEnabled(false);
    return;
  }

  // Subscribe to new camera feed
  subscribeToCameraFeed(cameraId);

  // Update info label
  m_cameraInfoLabel->setText(QString("Streaming: %1").arg(QString::fromStdString(cameraId)));

  // Enable click-to-move only for camera_1
  if (cameraId == "camera_1") {
    m_clickToMoveCheckbox->setEnabled(true);
  }
  else {
    m_clickToMoveCheckbox->setEnabled(false);
    m_clickToMoveCheckbox->setChecked(false);
    m_cameraInfoLabel->setText(QString("Streaming: %1 (Click-to-move only available for camera_1)")
      .arg(QString::fromStdString(cameraId)));
  }

  emit cameraChanged(cameraId);
}

std::string CameraClickToMoveWidget::getCurrentCamera() const
{
  if (m_cameraSelector->currentIndex() < 0) {
    return "";
  }
  return m_cameraSelector->currentData().toString().toStdString();
}

void CameraClickToMoveWidget::subscribeToCameraFeed(const std::string& cameraId)
{
  if (!m_cameraManager || cameraId.empty()) return;

  // ✅ FIX: Only start grabbing if not already running (avoid unnecessary camera restart)
  ICameraHardware* camera = m_cameraManager->GetCameraHardware(cameraId);
  if (camera && !camera->IsGrabbing()) {
    std::cout << "[CameraClickToMoveWidget] Camera not grabbing - starting now..." << std::endl;
    m_cameraManager->StartGrabbing(cameraId);
  }

  m_subscriber = std::make_shared<LiveVideoSubscriber>(cameraId, "AuxWindow_ClickToMove");
  m_subscriber->InitializeStatusFromManager(m_cameraManager);
  m_cameraManager->SubscribeToFrames(m_subscriber);

  std::cout << "[CameraClickToMoveWidget] Subscribed to camera: " << cameraId << std::endl;
}

void CameraClickToMoveWidget::unsubscribeFromCameraFeed()
{
  if (!m_subscriber) return;

  if (m_cameraManager) {
    m_cameraManager->UnsubscribeFromFrames(m_subscriber->GetSubscriberId());
  }

  m_subscriber.reset();

  std::cout << "[CameraClickToMoveWidget] Unsubscribed from camera feed" << std::endl;
}

void CameraClickToMoveWidget::onRefreshCameras()
{
  refreshCameraList();
}

void CameraClickToMoveWidget::refreshCameraList()
{
  if (!m_cameraManager) return;

  std::string currentCamera = getCurrentCamera();

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

  if (currentIndex >= 0) {
    m_cameraSelector->setCurrentIndex(currentIndex);
  }
  else if (m_cameraSelector->count() > 0 && m_initialCameraSelected) {
    // ✅ CHANGED: Only auto-select if initial selection already happened
    m_cameraSelector->setCurrentIndex(0);
  }

  m_cameraSelector->blockSignals(false);

  // ✅ CHANGED: Only trigger camera change if we've already done initial selection
  if (m_initialCameraSelected) {
    std::string newCamera = getCurrentCamera();
    if (newCamera != currentCamera && !newCamera.empty()) {
      setCamera(newCamera);
    }
    else if (currentCamera.empty() && !newCamera.empty()) {
      setCamera(newCamera);
    }
  }
}

void CameraClickToMoveWidget::onUpdateFrame()
{
  if (m_subscriber && m_subscriber->HasNewFrame()) {
    CameraFrameData frame = m_subscriber->GetLatestFrame();

    if (!frame.IsValid() || frame.imageData.empty()) {
      return;
    }

    // Convert to QImage
    QImage::Format format = (frame.channels == 3)
      ? QImage::Format_RGB888
      : QImage::Format_Grayscale8;

    m_currentImage = QImage(
      frame.imageData.data(),
      frame.width,
      frame.height,
      frame.width * frame.channels,
      format
    ).copy();  // Deep copy

    // Apply zoom if needed
    if (m_zoomPercent < 100) {
      int centerX = m_currentImage.width() / 2;
      int centerY = m_currentImage.height() / 2;
      int newWidth = m_currentImage.width() * m_zoomPercent / 100;
      int newHeight = m_currentImage.height() * m_zoomPercent / 100;
      int cropX = centerX - newWidth / 2;
      int cropY = centerY - newHeight / 2;

      m_currentImage = m_currentImage.copy(cropX, cropY, newWidth, newHeight);
    }

    m_subscriber->MarkFrameConsumed();

    // Update display with new image
    updateDisplay();
  }
}

void CameraClickToMoveWidget::updateDisplay()
{
  if (m_currentImage.isNull()) {
    return;
  }

  // Convert to pixmap and scale to fit label
  QPixmap pixmap = QPixmap::fromImage(m_currentImage).scaled(
    m_imageLabel->size(),
    Qt::KeepAspectRatio,
    Qt::FastTransformation
  );

  // Draw crosshairs directly on the pixmap
  QPainter painter(&pixmap);

  int centerX = pixmap.width() / 2;
  int centerY = pixmap.height() / 2;

  // Draw center crosshair (green, full width/height) if enabled
  if (m_centerCrosshairEnabled) {
    painter.setPen(QPen(Qt::green, 2));
    // Full width horizontal line
    painter.drawLine(0, centerY, pixmap.width(), centerY);
    // Full height vertical line
    painter.drawLine(centerX, 0, centerX, pixmap.height());
  }

  // Draw tracking crosshair (cyan) if click-to-move enabled and mouse over image
  if (m_clickToMoveEnabled && m_mouseOverImage) {
    painter.setPen(QPen(Qt::cyan, 1));

    // Full crosshair lines
    painter.drawLine(m_mousePosInPixmap.x(), 0, m_mousePosInPixmap.x(), pixmap.height());
    painter.drawLine(0, m_mousePosInPixmap.y(), pixmap.width(), m_mousePosInPixmap.y());

    // Small cross at mouse position
    int crossSize = 15;
    painter.drawLine(m_mousePosInPixmap.x() - crossSize, m_mousePosInPixmap.y(), m_mousePosInPixmap.x() + crossSize, m_mousePosInPixmap.y());
    painter.drawLine(m_mousePosInPixmap.x(), m_mousePosInPixmap.y() - crossSize, m_mousePosInPixmap.x(), m_mousePosInPixmap.y() + crossSize);
  }

  painter.end();

  // Set pixmap to label
  m_imageLabel->setPixmap(pixmap);
}

void CameraClickToMoveWidget::onClickToMoveToggled(bool checked)
{
  m_clickToMoveEnabled = checked;

  if (checked && m_currentCameraId != "camera_1") {
    std::cerr << "[CameraClickToMoveWidget] Click-to-move only works with camera_1!" << std::endl;
    m_clickToMoveCheckbox->setChecked(false);
    return;
  }

  std::cout << "[CameraClickToMoveWidget] Click-to-move " << (checked ? "ENABLED" : "DISABLED") << std::endl;

  // Update display to show/hide tracking crosshair
  updateDisplay();
}

void CameraClickToMoveWidget::onCrosshairToggled(bool checked)
{
  m_centerCrosshairEnabled = checked;
  std::cout << "[CameraClickToMoveWidget] Center crosshair " << (checked ? "enabled" : "disabled") << std::endl;

  // Update display to show/hide center crosshair
  updateDisplay();
}

void CameraClickToMoveWidget::onZoomChanged(int zoomPercent)
{
  m_zoomPercent = zoomPercent;
  double zoomLevel = 100.0 / zoomPercent;
  std::cout << "[CameraClickToMoveWidget] Zoom set to " << zoomLevel << "x" << std::endl;
}

void CameraClickToMoveWidget::paintEvent(QPaintEvent* event)
{
  // No custom painting needed - everything is drawn on the pixmap in onUpdateFrame()
  QWidget::paintEvent(event);
}

void CameraClickToMoveWidget::mouseMoveEvent(QMouseEvent* event)
{
  if (!m_clickToMoveEnabled) {
    if (m_mouseOverImage) {
      m_mouseOverImage = false;
      setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
    return;
  }

  bool wasOverImage = m_mouseOverImage;
  m_mouseOverImage = false;

  // Get the current pixmap
  QPixmap pixmap = m_imageLabel->pixmap();
  if (!pixmap.isNull()) {
    // Convert mouse position to label coordinates
    QPoint posInLabel = m_imageLabel->mapFromParent(event->pos());

    // Calculate where the pixmap is positioned within the label (centered)
    int pixmapX = (m_imageLabel->width() - pixmap.width()) / 2;
    int pixmapY = (m_imageLabel->height() - pixmap.height()) / 2;

    // Calculate mouse position relative to the pixmap itself
    int mouseInPixmapX = posInLabel.x() - pixmapX;
    int mouseInPixmapY = posInLabel.y() - pixmapY;

    // Check if mouse is actually over the pixmap (not just the label)
    if (mouseInPixmapX >= 0 && mouseInPixmapX < pixmap.width() &&
        mouseInPixmapY >= 0 && mouseInPixmapY < pixmap.height()) {
      m_mouseOverImage = true;
      // Store position relative to the pixmap
      m_mousePosInPixmap = QPoint(mouseInPixmapX, mouseInPixmapY);
      setCursor(Qt::CrossCursor);
    }
    else {
      setCursor(Qt::ArrowCursor);
    }
  }
  else {
    setCursor(Qt::ArrowCursor);
  }

  // Redraw display if mouse state changed or if tracking crosshair is active
  if (m_mouseOverImage || wasOverImage != m_mouseOverImage) {
    updateDisplay();
  }

  QWidget::mouseMoveEvent(event);
}

void CameraClickToMoveWidget::mousePressEvent(QMouseEvent* event)
{
  if (m_clickToMoveEnabled && event->button() == Qt::LeftButton && m_mouseOverImage) {
    // Use the already-calculated position in pixmap coordinates
    handleImageClick(m_mousePosInPixmap);
  }

  QWidget::mousePressEvent(event);
}

void CameraClickToMoveWidget::leaveEvent(QEvent* event)
{
  m_mouseOverImage = false;
  setCursor(Qt::ArrowCursor);

  // Update display to hide tracking crosshair
  updateDisplay();

  QWidget::leaveEvent(event);
}

void CameraClickToMoveWidget::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  // Note: Auto-selection is now handled via selectFirstCameraDelayed() called from AuxWindowManager
}

void CameraClickToMoveWidget::selectFirstCameraDelayed()
{
  // ✅ FIX: Public method called by AuxWindowManager after full initialization
  if (!m_initialCameraSelected) {
    std::cout << "[CameraClickToMoveWidget] selectFirstCameraDelayed called (camera count: "
              << m_cameraSelector->count() << ")" << std::endl;

    if (m_cameraSelector->count() > 0) {
      std::cout << "[CameraClickToMoveWidget] Scheduling delayed camera selection in 800ms..." << std::endl;

      // Use QTimer::singleShot to delay until event loop processes and UI is fully rendered
      QTimer::singleShot(800, this, [this]() {
        std::cout << "[CameraClickToMoveWidget] Timer fired! Camera count: " << m_cameraSelector->count()
                  << ", Initial selected: " << m_initialCameraSelected << std::endl;

        if (m_cameraSelector->count() > 0 && !m_initialCameraSelected) {
          m_initialCameraSelected = true;

          // ✅ DEBUG: Print all cameras in dropdown
          std::cout << "[CameraClickToMoveWidget] ===== CAMERA DROPDOWN CONTENTS =====" << std::endl;
          for (int i = 0; i < m_cameraSelector->count(); i++) {
            QString displayText = m_cameraSelector->itemText(i);
            QString cameraId = m_cameraSelector->itemData(i).toString();
            std::cout << "[CameraClickToMoveWidget]   Index " << i << ": "
                      << "Display='" << displayText.toStdString() << "', "
                      << "Data='" << cameraId.toStdString() << "'" << std::endl;
          }
          std::cout << "[CameraClickToMoveWidget] ======================================" << std::endl;

          // ✅ FIX: Search for "camera_1" by name instead of assuming index 0
          int camera1Index = -1;
          for (int i = 0; i < m_cameraSelector->count(); i++) {
            QString cameraId = m_cameraSelector->itemData(i).toString();
            std::cout << "[CameraClickToMoveWidget] Checking index " << i << ": '" << cameraId.toStdString()
                      << "' == 'camera_1'? " << (cameraId == "camera_1" ? "YES" : "NO") << std::endl;
            if (cameraId == "camera_1") {
              camera1Index = i;
              std::cout << "[CameraClickToMoveWidget] *** FOUND camera_1 at index " << i << " ***" << std::endl;
              break;
            }
          }

          if (camera1Index >= 0) {
            QString cameraName = m_cameraSelector->itemData(camera1Index).toString();
            std::cout << "[CameraClickToMoveWidget] ✓ Found camera_1 at index " << camera1Index
                      << ", selecting now..." << std::endl;
            std::cout << "[CameraClickToMoveWidget] About to call setCurrentIndex(" << camera1Index << ")" << std::endl;
            m_cameraSelector->setCurrentIndex(camera1Index);
            std::cout << "[CameraClickToMoveWidget] setCurrentIndex() called, camera selection triggered" << std::endl;
          } else {
            // Fallback to first camera if camera_1 not found
            QString cameraName = m_cameraSelector->itemData(0).toString();
            std::cout << "[CameraClickToMoveWidget] ✗ camera_1 NOT FOUND in dropdown!" << std::endl;
            std::cout << "[CameraClickToMoveWidget] Falling back to first camera: "
                      << cameraName.toStdString() << " (index 0)" << std::endl;
            m_cameraSelector->setCurrentIndex(0);
            std::cout << "[CameraClickToMoveWidget] Fallback camera selection triggered" << std::endl;
          }
        } else {
          std::cout << "[CameraClickToMoveWidget] Skipping selection - count: " << m_cameraSelector->count()
                    << ", already selected: " << m_initialCameraSelected << std::endl;
        }
      });
    } else {
      std::cout << "[CameraClickToMoveWidget] No cameras available for auto-selection" << std::endl;
    }
  } else {
    std::cout << "[CameraClickToMoveWidget] selectFirstCameraDelayed called but camera already selected" << std::endl;
  }
}

void CameraClickToMoveWidget::handleImageClick(const QPoint& pixmapClickPos)
{
  if (!m_motionManager || m_currentImage.isNull()) {
    std::cerr << "[CameraClickToMoveWidget] Cannot move - no motion manager or image" << std::endl;
    return;
  }

  // Get the displayed pixmap size
  QPixmap currentPixmap = m_imageLabel->pixmap();
  if (currentPixmap.isNull()) {
    return;
  }

  // Calculate pixel offset from center (in displayed pixmap coordinates)
  int centerX = currentPixmap.width() / 2;
  int centerY = currentPixmap.height() / 2;

  int pixelOffsetX = pixmapClickPos.x() - centerX;
  int pixelOffsetY = pixmapClickPos.y() - centerY;

  // Scale to original image size (account for scaling when displaying)
  double scaleFactorX = (double)m_currentImage.width() / currentPixmap.width();
  double scaleFactorY = (double)m_currentImage.height() / currentPixmap.height();

  pixelOffsetX *= scaleFactorX;
  pixelOffsetY *= scaleFactorY;

  // Convert to mm
  double offsetX_mm = pixelOffsetX * m_pixelToMmFactorX;
  double offsetY_mm = pixelOffsetY * m_pixelToMmFactorY;

  std::cout << "[CameraClickToMoveWidget] Click detected: Pixel offset (" << pixelOffsetX << ", " << pixelOffsetY << ")"
            << " = (" << offsetX_mm << " mm, " << offsetY_mm << " mm)" << std::endl;

  // Get current position
  PositionStruct currentPos = m_motionManager->GetCurrentPosition("gantry-main");

  // Calculate new absolute position (keep Z the same, offset X and Y)
  double newX = currentPos.x + offsetX_mm;
  double newY = currentPos.y + offsetY_mm;
  double newZ = currentPos.z;  // Keep Z unchanged

  std::cout << "[CameraClickToMoveWidget] Current position: X=" << currentPos.x
            << ", Y=" << currentPos.y << ", Z=" << currentPos.z << std::endl;
  std::cout << "[CameraClickToMoveWidget] Target position: X=" << newX
            << ", Y=" << newY << ", Z=" << newZ << std::endl;

  // Move to absolute position (coordinated XY move, Z stays same, slow speed for precision)
  bool success = m_motionManager->MoveToAbs("gantry-main", newX, newY, newZ, 10.0, false);

  if (success) {
    std::cout << "[CameraClickToMoveWidget] Gantry move command sent successfully" << std::endl;
    emit clickToMoveTriggered(offsetX_mm, offsetY_mm);
  }
  else {
    std::cerr << "[CameraClickToMoveWidget] Failed to send gantry move command!" << std::endl;
  }
}


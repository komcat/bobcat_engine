// ManualAlignPointWidget.cpp
#include "ManualAlignPointWidget.h"
#include "../../motions/unified_motion_manager.h"
#include "../../motions/MotionTypes.h"
#include "../../context/ApplicationContext.h"
#include "../../camera/CameraManager.h"
#include "../../camera/CameraFrameData.h"
#include "../../products/product_position_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QPixmap>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <filesystem>
#include <iostream>

ManualAlignPointWidget::ManualAlignPointWidget(
  UnifiedMotionManager* motionManager,
  ApplicationContext* context,
  QWidget* parent)
  : QWidget(parent)
  , m_motionManager(motionManager)
  , m_context(context)
  , m_currentDevice("gantry-main")
  , m_enableX(true)
  , m_enableY(true)
  , m_enableZ(true)
{
  setupUI();

  // Create position update timer
  m_positionTimer = new QTimer(this);
  connect(m_positionTimer, &QTimer::timeout, this, &ManualAlignPointWidget::onUpdatePosition);
}

void ManualAlignPointWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create centered container
  QWidget* container = new QWidget();
  container->setMaximumWidth(900);
  container->setStyleSheet(
    "QWidget { "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #0e639c; "
    "}");

  QVBoxLayout* containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(20, 15, 20, 15);
  containerLayout->setSpacing(10);

  // Title
  m_titleLabel = new QLabel("Select Manual Alignment Point");
  m_titleLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  color: #0e639c; "
    "  background: transparent; "
    "  border: none; "
    "}");
  containerLayout->addWidget(m_titleLabel);

  // Message
  m_messageLabel = new QLabel("Align the crosshair to the target point");
  m_messageLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 13px; "
    "  color: #d4d4d4; "
    "  background: transparent; "
    "  border: none; "
    "  padding: 5px 0px; "
    "}");
  m_messageLabel->setWordWrap(true);
  containerLayout->addWidget(m_messageLabel);

  // Instruction label
  m_instructionLabel = new QLabel(
    "📷 Use <b>Click-to-Move</b> on the Auxiliary Camera Window to align the gantry.<br>"
    "The crosshair shows the target alignment position."
  );
  m_instructionLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  color: #ffa500; "
    "  background: #2a2a2a; "
    "  border: 1px solid #555555; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "}");
  m_instructionLabel->setWordWrap(true);
  containerLayout->addWidget(m_instructionLabel);

  // Current position display
  m_positionLabel = new QLabel("Position: X: 0.000 | Y: 0.000 | Z: 0.000 mm");
  m_positionLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-family: 'Consolas', monospace; "
    "  color: #ffffff; "
    "  background: #3c3c3c; "
    "  border: 1px solid #555555; "
    "  padding: 5px; "
    "}");
  containerLayout->addWidget(m_positionLabel);

  // Reference image display
  QLabel* refImageTitle = new QLabel("Reference Image:");
  refImageTitle->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #ffffff; "
    "  background: transparent; "
    "  border: none; "
    "  margin-top: 8px; "
    "}");
  containerLayout->addWidget(refImageTitle);

  m_referenceImageLabel = new QLabel();
  m_referenceImageLabel->setFixedSize(200, 200);
  m_referenceImageLabel->setAlignment(Qt::AlignCenter);
  m_referenceImageLabel->setStyleSheet(
    "QLabel { "
    "  background: #3c3c3c; "
    "  border: 2px solid #555555; "
    "  color: #888888; "
    "}");
  m_referenceImageLabel->setText("No reference\nimage available");
  containerLayout->addWidget(m_referenceImageLabel);

  // Separator
  QFrame* separator = new QFrame();
  separator->setFrameShape(QFrame::HLine);
  separator->setStyleSheet("QFrame { background: #555555; border: none; max-height: 1px; }");
  containerLayout->addWidget(separator);

  // Action buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(10);

  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #d32f2f; "
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "  border: none; "
    "  padding: 8px 20px; "
    "  min-width: 100px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #f44336; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #b71c1c; "
    "}");
  connect(m_cancelButton, &QPushButton::clicked, this, &ManualAlignPointWidget::onCancelClicked);
  buttonLayout->addWidget(m_cancelButton);

  m_continueButton = new QPushButton("Continue");
  m_continueButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "  border: none; "
    "  padding: 8px 20px; "
    "  min-width: 100px; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2ecc71; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #229954; "
    "}");
  connect(m_continueButton, &QPushButton::clicked, this, &ManualAlignPointWidget::onContinueClicked);
  buttonLayout->addWidget(m_continueButton);

  containerLayout->addLayout(buttonLayout);

  // Add container to main layout with centering
  QHBoxLayout* centeringLayout = new QHBoxLayout();
  centeringLayout->addStretch();
  centeringLayout->addWidget(container);
  centeringLayout->addStretch();

  mainLayout->addStretch();
  mainLayout->addLayout(centeringLayout);
  mainLayout->addStretch();
}

void ManualAlignPointWidget::showAlignment(
  const QString& pointName,
  const QString& message,
  bool enableX,
  bool enableY,
  bool enableZ)
{
  m_pointName = pointName.toStdString();
  m_enableX = enableX;
  m_enableY = enableY;
  m_enableZ = enableZ;

  // Update title
  m_titleLabel->setText("Select Manual Alignment Point: " + pointName);

  // Update message
  m_messageLabel->setText(message);

  // Try to load and auto-navigate to saved position
  double savedX = 0.0, savedY = 0.0, savedZ = 0.0;
  if (loadNavigationData(savedX, savedY, savedZ)) {
    // Navigation data exists - auto-move to saved position
    if (m_motionManager) {
      std::cout << "[ManualAlignPointWidget] Auto-navigating to saved position: "
                << "X=" << savedX << ", Y=" << savedY << ", Z=" << savedZ << std::endl;

      // MoveToAbs with blocking=true will wait for motion to complete
      if (!m_motionManager->MoveToAbs(m_currentDevice, savedX, savedY, savedZ, 20.0, true)) {
        std::cerr << "[ManualAlignPointWidget] Auto-navigation failed" << std::endl;
      }

      std::cout << "[ManualAlignPointWidget] Auto-navigation complete" << std::endl;
    }
  }

  // Load and display reference image
  loadReferenceImage();

  // Enable click-to-move and crosshair
  enableClickToMove(true);
  enableCrosshair(true);

  // Update position display
  updatePositionDisplay();

  // Start position update timer (500ms refresh)
  m_positionTimer->start(500);

  // Show widget
  show();

  std::cout << "[ManualAlignPointWidget] Showing alignment dialog for point: "
            << m_pointName << std::endl;
}

void ManualAlignPointWidget::hideAlignment()
{
  // Stop position updates
  m_positionTimer->stop();

  // Disable click-to-move and crosshair
  enableClickToMove(false);
  enableCrosshair(false);

  // Hide widget
  hide();
}

void ManualAlignPointWidget::onContinueClicked()
{
  std::cout << "[ManualAlignPointWidget] User clicked Continue - saving point: "
            << m_pointName << std::endl;

  // Capture and save reference image
  captureAndSaveImage();

  // Get current position from gantry-main
  if (m_motionManager) {
    PositionStruct pos = m_motionManager->GetCurrentPosition(m_currentDevice);
    double x = pos.x;
    double y = pos.y;
    double z = pos.z;

    // Save navigation data to file
    saveNavigationData(x, y, z);

    // Save to context with point name prefix
    if (m_context && m_context->sequenceEngine) {
      // Note: GetCurrentContext() doesn't exist, context is passed during execution
      // For now, just log the values
      std::cout << "[ManualAlignPointWidget] Captured point '" << m_pointName << "': "
                << "X=" << x << ", Y=" << y << ", Z=" << z << std::endl;
    }
  }

  // Hide and emit response
  hideAlignment();
  emit responseReceived(true);
}

void ManualAlignPointWidget::onCancelClicked()
{
  std::cout << "[ManualAlignPointWidget] User clicked Cancel" << std::endl;
  hideAlignment();
  emit responseReceived(false);
}

void ManualAlignPointWidget::onUpdatePosition()
{
  updatePositionDisplay();
}

void ManualAlignPointWidget::updatePositionDisplay()
{
  if (!m_motionManager) {
    return;
  }

  PositionStruct pos = m_motionManager->GetCurrentPosition(m_currentDevice);
  QString posText = QString("Position: X: %1 | Y: %2 | Z: %3 mm")
                      .arg(pos.x, 0, 'f', 3)
                      .arg(pos.y, 0, 'f', 3)
                      .arg(pos.z, 0, 'f', 3);
  m_positionLabel->setText(posText);
}

void ManualAlignPointWidget::enableClickToMove(bool enable)
{
  // TODO: Implement auxiliaryWindowManager integration
  /*
  if (!m_context || !m_context->auxiliaryWindowManager) {
    return;
  }

  if (enable) {
    m_context->auxiliaryWindowManager->enableClickToMove(true);
    std::cout << "[ManualAlignPointWidget] Enabled click-to-move on auxiliary window" << std::endl;
  } else {
    m_context->auxiliaryWindowManager->enableClickToMove(false);
    std::cout << "[ManualAlignPointWidget] Disabled click-to-move on auxiliary window" << std::endl;
  }
  */
}

void ManualAlignPointWidget::enableCrosshair(bool enable)
{
  // TODO: Implement auxiliaryWindowManager integration
  /*
  if (!m_context || !m_context->auxiliaryWindowManager) {
    return;
  }

  if (enable) {
    m_context->auxiliaryWindowManager->enableCrosshair(true);
    std::cout << "[ManualAlignPointWidget] Enabled crosshair on auxiliary window" << std::endl;
  } else {
    m_context->auxiliaryWindowManager->enableCrosshair(false);
    std::cout << "[ManualAlignPointWidget] Disabled crosshair on auxiliary window" << std::endl;
  }
  */
}

void ManualAlignPointWidget::loadReferenceImage()
{
  QString imagePath = getReferenceImagePath(false);  // Get existing image

  if (imagePath.isEmpty() || !QFile::exists(imagePath)) {
    m_referenceImageLabel->setText("No reference\nimage available");
    m_referenceImageLabel->setPixmap(QPixmap());
    std::cout << "[ManualAlignPointWidget] No reference image found for point: "
              << m_pointName << std::endl;
    return;
  }

  // Load and scale image to 200x200
  QPixmap pixmap(imagePath);
  if (pixmap.isNull()) {
    m_referenceImageLabel->setText("Failed to\nload image");
    std::cout << "[ManualAlignPointWidget] Failed to load reference image: "
              << imagePath.toStdString() << std::endl;
    return;
  }

  QPixmap scaledPixmap = pixmap.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  m_referenceImageLabel->setPixmap(scaledPixmap);
  m_referenceImageLabel->setText("");  // Clear text

  std::cout << "[ManualAlignPointWidget] Loaded reference image: "
            << imagePath.toStdString() << std::endl;
}

void ManualAlignPointWidget::captureAndSaveImage()
{
  if (!m_context || !m_context->cameraManager) {
    std::cerr << "[ManualAlignPointWidget] CameraManager not available for image capture" << std::endl;
    return;
  }

  // Get the camera name from auxiliary window (assume camera_1 for now)
  std::string cameraName = "camera_1";  // TODO: Get from aux window config

  // Get camera hardware
  auto camera = m_context->cameraManager->GetCameraHardware(cameraName);
  if (!camera) {
    std::cerr << "[ManualAlignPointWidget] Camera not found: " << cameraName << std::endl;
    return;
  }

  // Capture frame from camera
  CameraFrameData frameData;
  frameData.cameraId = cameraName;

  if (!camera->CaptureFrame(frameData)) {
    std::cerr << "[ManualAlignPointWidget] Failed to capture frame from camera: "
              << cameraName << std::endl;
    return;
  }

  if (!frameData.IsValid()) {
    std::cerr << "[ManualAlignPointWidget] Captured frame is invalid" << std::endl;
    return;
  }

  // Get save path (with timestamp if file exists)
  QString savePath = getReferenceImagePath(true);  // Create new with timestamp if needed

  // Create directory if needed
  QFileInfo fileInfo(savePath);
  QDir dir = fileInfo.absoluteDir();
  if (!dir.exists()) {
    dir.mkpath(".");
    std::cout << "[ManualAlignPointWidget] Created directory: "
              << dir.absolutePath().toStdString() << std::endl;
  }

  // Convert frame data to QImage and save
  QImage::Format format;
  if (frameData.channels == 3) {
    format = QImage::Format_RGB888;
  } else if (frameData.channels == 1) {
    format = QImage::Format_Grayscale8;
  } else if (frameData.channels == 4) {
    format = QImage::Format_RGBA8888;
  } else {
    std::cerr << "[ManualAlignPointWidget] Unsupported channel count: "
              << frameData.channels << std::endl;
    return;
  }

  QImage image(
    frameData.imageData.data(),
    frameData.width,
    frameData.height,
    frameData.width * frameData.channels,
    format
  );

  if (image.save(savePath, "PNG")) {
    std::cout << "[ManualAlignPointWidget] Saved reference image to: "
              << savePath.toStdString() << std::endl;
  } else {
    std::cerr << "[ManualAlignPointWidget] Failed to save reference image to: "
              << savePath.toStdString() << std::endl;
  }

  // TODO: Store the captured frame in context for vision processing
  /*
  if (m_context && m_context->sequenceEngine) {
    auto context = m_context->sequenceEngine->GetCurrentContext();
    if (context) {
      std::string contextKey = m_pointName + "_image";
      context->SetFrameData(contextKey, frameData, true, cameraName, "ManualAlignPoint");

      std::cout << "[ManualAlignPointWidget] Stored captured image in context["
                << contextKey << "]" << std::endl;
    }
  }
  */
}

QString ManualAlignPointWidget::getReferenceImagePath(bool createNew)
{
  QString productName = getProductName();
  if (productName.isEmpty()) {
    std::cerr << "[ManualAlignPointWidget] No product name available" << std::endl;
    return QString();
  }

  // Base path: data/{product}/referenceimages/
  QString basePath = QString("data/%1/referenceimages").arg(productName);
  QString baseFilename = QString::fromStdString(m_pointName);

  if (!createNew) {
    // Find latest existing image with this point name
    QDir dir(basePath);
    if (!dir.exists()) {
      return QString();
    }

    // Look for files matching pattern: {pointName}*.png
    QStringList filters;
    filters << baseFilename + ".png" << baseFilename + "_*.png";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files);
    dir.setSorting(QDir::Time);  // Most recent first (no Reversed flag)

    QStringList files = dir.entryList();
    if (!files.isEmpty()) {
      return basePath + "/" + files.first();
    }
    return QString();
  } else {
    // Always use the same filename (overwrite existing)
    QString filepath = basePath + "/" + baseFilename + ".png";
    return filepath;
  }
}

QString ManualAlignPointWidget::getProductName()
{
  if (!m_context || !m_context->productPositionManager) {
    return QString();
  }

  if (!m_context->productPositionManager->HasLoadedProduct()) {
    return QString();
  }

  std::string productName = m_context->productPositionManager->GetCurrentProductName();
  return QString::fromStdString(productName);
}

QString ManualAlignPointWidget::getNavigationFilePath()
{
  QString productName = getProductName();
  if (productName.isEmpty()) {
    std::cerr << "[ManualAlignPointWidget] No product name available for navigation file" << std::endl;
    return QString();
  }

  // Path: data/{product}/referenceimages/navigation/{pointName}.json
  QString navPath = QString("data/%1/referenceimages/navigation/%2.json")
    .arg(productName)
    .arg(QString::fromStdString(m_pointName));

  return navPath;
}

void ManualAlignPointWidget::saveNavigationData(double x, double y, double z)
{
  QString filePath = getNavigationFilePath();
  if (filePath.isEmpty()) {
    std::cerr << "[ManualAlignPointWidget] Cannot save navigation data - invalid file path" << std::endl;
    return;
  }

  // Create directory if needed
  QFileInfo fileInfo(filePath);
  QDir dir = fileInfo.absoluteDir();
  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      std::cerr << "[ManualAlignPointWidget] Failed to create navigation directory: "
                << dir.absolutePath().toStdString() << std::endl;
      return;
    }
    std::cout << "[ManualAlignPointWidget] Created navigation directory: "
              << dir.absolutePath().toStdString() << std::endl;
  }

  // Build JSON structure
  QJsonObject root;
  root["point_name"] = QString::fromStdString(m_pointName);
  root["device"] = QString::fromStdString(m_currentDevice);

  QJsonObject position;
  position["x"] = x;
  position["y"] = y;
  position["z"] = z;
  root["position"] = position;

  // Add timestamp
  root["last_saved"] = QDateTime::currentDateTime().toString(Qt::ISODate);

  // Write to file
  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    std::cerr << "[ManualAlignPointWidget] Failed to open navigation file for writing: "
              << filePath.toStdString() << std::endl;
    return;
  }

  QJsonDocument doc(root);
  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();

  std::cout << "[ManualAlignPointWidget] Saved navigation data to: "
            << filePath.toStdString() << " (X:" << x << ", Y:" << y << ", Z:" << z << ")"
            << std::endl;
}

bool ManualAlignPointWidget::loadNavigationData(double& x, double& y, double& z)
{
  QString filePath = getNavigationFilePath();
  if (filePath.isEmpty()) {
    return false;
  }

  if (!QFile::exists(filePath)) {
    std::cout << "[ManualAlignPointWidget] No saved navigation data found for: "
              << m_pointName << std::endl;
    return false;
  }

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    std::cerr << "[ManualAlignPointWidget] Failed to open navigation file for reading: "
              << filePath.toStdString() << std::endl;
    return false;
  }

  QByteArray fileData = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(fileData);
  if (doc.isNull() || !doc.isObject()) {
    std::cerr << "[ManualAlignPointWidget] Invalid JSON in navigation file: "
              << filePath.toStdString() << std::endl;
    return false;
  }

  QJsonObject root = doc.object();
  if (!root.contains("position")) {
    std::cerr << "[ManualAlignPointWidget] Missing position data in navigation file" << std::endl;
    return false;
  }

  QJsonObject position = root["position"].toObject();
  x = position["x"].toDouble();
  y = position["y"].toDouble();
  z = position["z"].toDouble();

  std::cout << "[ManualAlignPointWidget] Loaded navigation data from: "
            << filePath.toStdString() << " (X:" << x << ", Y:" << y << ", Z:" << z << ")"
            << std::endl;

  return true;
}

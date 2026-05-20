// ManualAdjustWidget.cpp
#include "ManualAdjustWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateTransformer.h"
#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include "CameraManager.h"
#include "LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QTimer>
#include <QScrollArea>
#include <QPixmap>
#include <QFileInfo>
#include <cmath>
#include <iostream>

ManualAdjustWidget::ManualAdjustWidget(
  ApplicationContext* context,
  TransformationMatrixLoader* matrixLoader,
  QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_motionManager(context ? context->motionManager : nullptr)
  , m_matrixLoader(matrixLoader)
  , m_stepSizeUm(10.0)
  , m_enableX(true)
  , m_enableY(true)
  , m_enableZ(true)
{
  setupUI();

  // Create position update timer
  m_positionTimer = new QTimer(this);
  connect(m_positionTimer, &QTimer::timeout, this, &ManualAdjustWidget::onUpdatePosition);
}

void ManualAdjustWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create centered container
  QWidget* container = new QWidget();
  container->setMaximumWidth(900);
  container->setStyleSheet(
    "QWidget { "
    "  background-color: #1e1e1e; "  // ✅ Dark theme background
    "  border: 2px solid #0e639c; "  // ✅ Dark theme border (blue accent)
    "}");

  QVBoxLayout* containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(20, 15, 20, 15);
  containerLayout->setSpacing(10);

  // Title
  QLabel* titleLabel = new QLabel("Manual Adjustment Required");
  titleLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  color: #0e639c; "  // ✅ Dark theme blue accent
    "  background: transparent; "
    "  border: none; "
    "}");
  containerLayout->addWidget(titleLabel);

  // Message
  m_messageLabel = new QLabel("Please adjust the position as needed");
  m_messageLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 13px; "
    "  color: #d4d4d4; "  // ✅ Dark theme secondary text
    "  background: transparent; "
    "  border: none; "
    "  padding: 5px 0px; "
    "}");
  m_messageLabel->setWordWrap(true);
  containerLayout->addWidget(m_messageLabel);

  // Device and step size info
  QHBoxLayout* infoLayout = new QHBoxLayout();

  m_deviceLabel = new QLabel("Device: gantry-main");
  m_deviceLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #1177bb; "  // ✅ Dark theme blue
    "  background: transparent; "
    "  border: none; "
    "}");
  infoLayout->addWidget(m_deviceLabel);

  infoLayout->addSpacing(15);

  m_stepSizeLabel = new QLabel("Step: 10 μm");
  m_stepSizeLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #27ae60; "  // ✅ Dark theme green
    "  background: transparent; "
    "  border: none; "
    "}");
  infoLayout->addWidget(m_stepSizeLabel);

  infoLayout->addStretch();
  containerLayout->addLayout(infoLayout);

  // Preset step size buttons (override the parameter-config step size at runtime)
  createPresetStepSizePanel(containerLayout);

  // Current position display
  m_positionLabel = new QLabel("Position: X: 0.000 | Y: 0.000 | Z: 0.000 mm");
  m_positionLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  color: #ffffff; "  // ✅ Dark theme white text
    "  background: #3c3c3c; "  // ✅ Dark theme input background
    "  border: 1px solid #555555; "  // ✅ Dark theme border
    "  padding: 5px; "
    "}");
  containerLayout->addWidget(m_positionLabel);

  // Jog controls label
  QLabel* jogLabel = new QLabel("Jog Controls:");
  jogLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #ffffff; "  // ✅ Dark theme white text
    "  background: transparent; "
    "  border: none; "
    "  margin-top: 3px; "
    "}");
  containerLayout->addWidget(jogLabel);

  // Jog controls - Single row layout
  QHBoxLayout* jogLayout = new QHBoxLayout();
  jogLayout->setSpacing(5);

  // Minimal button style
  QString buttonStyle =
    "QPushButton { "
    "  background-color: #0e639c; "  // ✅ Dark theme primary blue
    "  color: white; "
    "  font-size: 12px; "
    "  font-weight: normal; "
    "  border: 1px solid #0c5280; "
    "  padding: 5px 10px; "
    "  min-width: 60px; "
    "  min-height: 30px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1177bb; "  // ✅ Dark theme primary hover
    "} "
    "QPushButton:pressed { "
    "  background-color: #005a9e; "  // ✅ Dark theme pressed
    "} "
    "QPushButton:disabled { "
    "  background-color: #555555; "  // ✅ Dark theme disabled
    "  color: #888888; "
    "  border: 1px solid #444444; "
    "}";

  // Create jog buttons with minimal text
  m_btnXMinus = new QPushButton("← X-");
  m_btnXMinus->setStyleSheet(buttonStyle);
  connect(m_btnXMinus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogXMinus);

  m_btnXPlus = new QPushButton("X+ →");
  m_btnXPlus->setStyleSheet(buttonStyle);
  connect(m_btnXPlus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogXPlus);

  m_btnYMinus = new QPushButton("⊗ Y- In");
  m_btnYMinus->setStyleSheet(buttonStyle);
  connect(m_btnYMinus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogYMinus);

  m_btnYPlus = new QPushButton("⊙ Y+ Out");
  m_btnYPlus->setStyleSheet(buttonStyle);
  connect(m_btnYPlus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogYPlus);

  m_btnZMinus = new QPushButton("↓ Z-");
  m_btnZMinus->setStyleSheet(buttonStyle);
  connect(m_btnZMinus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogZMinus);

  m_btnZPlus = new QPushButton("Z+ ↑");
  m_btnZPlus->setStyleSheet(buttonStyle);
  connect(m_btnZPlus, &QPushButton::clicked, this, &ManualAdjustWidget::onJogZPlus);

  // Add buttons to horizontal layout
  jogLayout->addWidget(m_btnXMinus);
  jogLayout->addWidget(m_btnXPlus);
  jogLayout->addWidget(m_btnYMinus);
  jogLayout->addWidget(m_btnYPlus);
  jogLayout->addWidget(m_btnZMinus);
  jogLayout->addWidget(m_btnZPlus);

  containerLayout->addLayout(jogLayout);

  // Auto Focus panel
  createAutoFocusPanel(containerLayout);

  containerLayout->addSpacing(10);

  // OK / Cancel buttons
  QHBoxLayout* actionLayout = new QHBoxLayout();
  actionLayout->setSpacing(10);

  m_okButton = new QPushButton("✓ OK (Complete)");
  m_okButton->setMinimumHeight(35);
  m_okButton->setMinimumWidth(150);
  m_okButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #27ae60; "  // ✅ Dark theme success green
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: normal; "
    "  border: 1px solid #229954; "
    "  padding: 8px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #2ecc71; "  // ✅ Dark theme success hover
    "} "
    "QPushButton:pressed { "
    "  background-color: #1e8449; "  // ✅ Dark theme success pressed
    "}");
  connect(m_okButton, &QPushButton::clicked, this, &ManualAdjustWidget::onOkClicked);

  m_cancelButton = new QPushButton("✗ Cancel (Stop Sequence)");
  m_cancelButton->setMinimumHeight(35);
  m_cancelButton->setMinimumWidth(150);
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #e74c3c; "  // ✅ Dark theme danger red
    "  color: white; "
    "  font-size: 13px; "
    "  font-weight: normal; "
    "  border: 1px solid #c0392b; "
    "  padding: 8px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #ec7063; "  // ✅ Dark theme danger hover
    "} "
    "QPushButton:pressed { "
    "  background-color: #c0392b; "  // ✅ Dark theme danger pressed
    "}");
  connect(m_cancelButton, &QPushButton::clicked, this, &ManualAdjustWidget::onCancelClicked);

  actionLayout->addStretch();
  actionLayout->addWidget(m_okButton);
  actionLayout->addWidget(m_cancelButton);
  actionLayout->addStretch();

  containerLayout->addLayout(actionLayout);

  // Image gallery under OK/Cancel: up to 3 images at 400x300, horizontally scrollable
  createImageGallery(containerLayout);

  // Center the container
  mainLayout->addStretch();
  QHBoxLayout* centerLayout = new QHBoxLayout();
  centerLayout->addStretch();
  centerLayout->addWidget(container);
  centerLayout->addStretch();
  mainLayout->addLayout(centerLayout);
  mainLayout->addStretch();

  // Start hidden
  hide();
}

void ManualAdjustWidget::showAdjustment(
  const QString& message,
  const QString& deviceName,
  double stepSizeUm,
  bool enableX,
  bool enableY,
  bool enableZ,
  const QStringList& imagePaths)
{
  m_messageLabel->setText(message);
  m_deviceLabel->setText("Device: " + deviceName);
  applyStepSize(stepSizeUm);

  m_currentDevice = deviceName.toStdString();
  m_enableX = enableX;
  m_enableY = enableY;
  m_enableZ = enableZ;

  setAdjustmentImages(imagePaths);

  // Debug output
  std::cout << "[ManualAdjustWidget] showAdjustment called:" << std::endl;
  std::cout << "  Device: " << m_currentDevice << std::endl;
  std::cout << "  Step size: " << m_stepSizeUm << " um" << std::endl;
  std::cout << "  m_motionManager: " << (m_motionManager ? "valid" : "NULL") << std::endl;
  std::cout << "  m_matrixLoader: " << (m_matrixLoader ? "valid" : "NULL") << std::endl;

  // Enable/disable buttons based on axis enables
  m_btnXMinus->setEnabled(enableX);
  m_btnXPlus->setEnabled(enableX);
  m_btnYMinus->setEnabled(enableY);
  m_btnYPlus->setEnabled(enableY);
  m_btnZMinus->setEnabled(enableZ);
  m_btnZPlus->setEnabled(enableZ);

  // Update position display immediately
  updatePositionDisplay();

  // Start position update timer (100ms refresh)
  m_positionTimer->start(100);

  show();

  // Set focus to OK button
  m_okButton->setFocus();
}

void ManualAdjustWidget::hideAdjustment()
{
  m_positionTimer->stop();
  hide();
}

void ManualAdjustWidget::onJogXMinus()
{
  sendJogCommand("X", -1.0);
}

void ManualAdjustWidget::onJogXPlus()
{
  sendJogCommand("X", 1.0);
}

void ManualAdjustWidget::onJogYMinus()
{
  sendJogCommand("Y", -1.0);
}

void ManualAdjustWidget::onJogYPlus()
{
  sendJogCommand("Y", 1.0);
}

void ManualAdjustWidget::onJogZMinus()
{
  sendJogCommand("Z", -1.0);
}

void ManualAdjustWidget::onJogZPlus()
{
  sendJogCommand("Z", 1.0);
}

void ManualAdjustWidget::onOkClicked()
{
  emit responseReceived(true);
  hideAdjustment();
}

void ManualAdjustWidget::onCancelClicked()
{
  emit responseReceived(false);
  hideAdjustment();
}

void ManualAdjustWidget::onUpdatePosition()
{
  updatePositionDisplay();
}

void ManualAdjustWidget::sendJogCommand(const std::string& axis, double direction)
{
  if (m_currentDevice.empty()) {
    std::cerr << "[ManualAdjustWidget] Invalid state: m_currentDevice is empty" << std::endl;
    return;
  }
  if (!m_motionManager) {
    std::cerr << "[ManualAdjustWidget] Invalid state: m_motionManager is null" << std::endl;
    return;
  }

  // Global delta in mm. MoveRelativeGlobal handles the per-device matrix
  // transform internally and dispatches one simultaneous multi-axis move,
  // so we avoid the 3 sequential per-axis calls that serialized on the
  // controller's idle-wait and caused the click-to-motion lag.
  double stepMm = (m_stepSizeUm * direction) / 1000.0;
  double dx = (axis == "X") ? stepMm : 0.0;
  double dy = (axis == "Y") ? stepMm : 0.0;
  double dz = (axis == "Z") ? stepMm : 0.0;

  if (!m_motionManager->MoveRelativeGlobal(m_currentDevice, dx, dy, dz, false)) {
    std::cerr << "[ManualAdjustWidget] MoveRelativeGlobal failed for device: "
              << m_currentDevice << std::endl;
  }
}

void ManualAdjustWidget::updatePositionDisplay()
{
  if (m_currentDevice.empty() || !m_motionManager) {
    return;
  }

  // Non-blocking cached read fed by UnifiedMotionManager's background poll —
  // GetPosition() is a synchronous controller round-trip and must not run on
  // the UI thread on a fast cadence (see unified_motion_manager.h:175).
  PositionStruct pos;
  if (!m_motionManager->GetCachedPosition(m_currentDevice, pos)) {
    return; // No cached sample yet (device just connected)
  }
  double x = pos.x, y = pos.y, z = pos.z;

  m_positionLabel->setText(
    QString("Position: X: %1 | Y: %2 | Z: %3 mm")
      .arg(x, 0, 'f', 4)
      .arg(y, 0, 'f', 4)
      .arg(z, 0, 'f', 4)
  );
}

void ManualAdjustWidget::createAutoFocusPanel(QVBoxLayout* containerLayout)
{
  if (!m_context || !m_context->cameraManager) return;

  std::vector<std::string> cameraIds = m_context->cameraManager->GetCameraIds();
  if (cameraIds.empty()) return;

  struct AutoFocusConfig {
    std::string cameraId;
    std::string motionDevice;
    double zRangeMm;
    double zStepMm;
    std::string label;
  };

  std::vector<AutoFocusConfig> configs = {
    {"main_camera",   "gantry-main", 0.1, 0.02, "AF Main"},
    {"swir_camera",   "gantry-main", 0.1, 0.01, "AF SWIR"},
  };

  // Filter to only installed cameras
  std::vector<AutoFocusConfig> activeConfigs;
  for (const auto& cfg : configs) {
    for (const auto& id : cameraIds) {
      if (id == cfg.cameraId) {
        activeConfigs.push_back(cfg);
        break;
      }
    }
  }

  if (activeConfigs.empty()) return;

  QHBoxLayout* afLayout = new QHBoxLayout();
  afLayout->setSpacing(5);
  afLayout->setContentsMargins(0, 3, 0, 0);

  QLabel* afLabel = new QLabel("Auto Focus:");
  afLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #14ffec; "
    "  background: transparent; "
    "  border: none; "
    "}");
  afLayout->addWidget(afLabel);

  QString afButtonStyle =
    "QPushButton { "
    "  background-color: #2d5a2d; "
    "  color: white; "
    "  border: 1px solid #3d8b3d; "
    "  border-radius: 3px; "
    "  padding: 5px 10px; "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  min-width: 60px; "
    "  min-height: 30px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #3d8b3d; "
    "  border-color: #5cb85c; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #555; "
    "  color: #888; "
    "  border-color: #666; "
    "}";

  for (const auto& cfg : activeConfigs) {
    QPushButton* btn = new QPushButton(QString::fromStdString(cfg.label));
    btn->setStyleSheet(afButtonStyle);

    connect(btn, &QPushButton::clicked, this,
      [this, camera = cfg.cameraId, device = cfg.motionDevice,
       range = cfg.zRangeMm, step = cfg.zStepMm, btn]() {
        runAutoFocus(camera, device, range, step, btn);
      });

    afLayout->addWidget(btn);
  }

  afLayout->addStretch();
  containerLayout->addLayout(afLayout);
}

void ManualAdjustWidget::runAutoFocus(const std::string& camera, const std::string& motionDevice,
                                       double zRangeMm, double zStepMm, QPushButton* button)
{
  // Check if autofocus is already running
  if (m_autoFocusFuture.valid() &&
      m_autoFocusFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    if (m_context && m_context->logManager) {
      m_context->logManager->Warning("Auto focus already running", "ManualAdjust");
    }
    return;
  }

  if (!m_context) return;

  if (button) button->setEnabled(false);

  if (m_context->logManager) {
    m_context->logManager->Info(
      "Starting auto focus: camera=" + camera + " device=" + motionDevice +
      " range=" + std::to_string(zRangeMm) + "mm step=" + std::to_string(zStepMm) + "mm",
      "ManualAdjust");
  }

  ApplicationContext* ctx = m_context;
  m_autoFocusFuture = std::async(std::launch::async,
    [ctx, camera, motionDevice, zRangeMm, zStepMm, button]() {
      VisionCommandHandler visionHandler(ctx);
      SequenceContext seqCtx;
      std::string error;

      std::map<std::string, std::string> params = {
        {"motion_device", motionDevice},
        {"camera", camera},
        {"z_range_mm", std::to_string(zRangeMm)},
        {"z_step_mm", std::to_string(zStepMm)},
        {"settle_time_ms", "100"},
        {"use_center_roi", "true"},
        {"roi_width", "300"},
        {"roi_height", "300"},
        {"context_prefix", "autofocus"}
      };

      bool ok = visionHandler.ExecuteCommand(
        nullptr, "vision", "auto_focus", params, error, &seqCtx);

      if (ok) {
        std::cout << "[ManualAdjust] Auto focus completed successfully" << std::endl;
      }
      else {
        std::cout << "[ManualAdjust] Auto focus failed: " << error << std::endl;
      }

      if (button) {
        QMetaObject::invokeMethod(button, [button]() {
          button->setEnabled(true);
        }, Qt::QueuedConnection);
      }
    });
}

void ManualAdjustWidget::createPresetStepSizePanel(QVBoxLayout* containerLayout)
{
  static const QList<double> kPresets = {0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0};

  QHBoxLayout* presetLayout = new QHBoxLayout();
  presetLayout->setSpacing(5);
  presetLayout->setContentsMargins(0, 0, 0, 0);

  QLabel* label = new QLabel("Preset:");
  label->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  color: #ffffff; "
    "  background: transparent; "
    "  border: none; "
    "}");
  presetLayout->addWidget(label);

  QString presetStyle =
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  font-size: 11px; "
    "  font-weight: normal; "
    "  border: 1px solid #555555; "
    "  padding: 3px 8px; "
    "  min-width: 40px; "
    "  min-height: 22px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4a; "
    "  border: 1px solid #1177bb; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #1177bb; "
    "  color: white; "
    "}";

  m_presetStepButtons.clear();
  for (double um : kPresets) {
    QString text = (um < 1.0)
      ? QString::number(um, 'f', 1) + " μm"
      : QString::number(um, 'f', 0) + " μm";
    QPushButton* btn = new QPushButton(text);
    btn->setStyleSheet(presetStyle);
    btn->setCursor(Qt::PointingHandCursor);
    connect(btn, &QPushButton::clicked, this, [this, um]() {
      applyStepSize(um);
    });
    presetLayout->addWidget(btn);
    m_presetStepButtons.append(btn);
  }

  presetLayout->addStretch();
  containerLayout->addLayout(presetLayout);
}

void ManualAdjustWidget::createImageGallery(QVBoxLayout* containerLayout)
{
  m_imageContainer = new QWidget();
  m_imageContainer->setStyleSheet("QWidget { background: transparent; border: none; }");

  m_imageLayout = new QHBoxLayout(m_imageContainer);
  m_imageLayout->setContentsMargins(0, 0, 0, 0);
  m_imageLayout->setSpacing(8);
  m_imageLayout->addStretch();

  m_imageScrollArea = new QScrollArea();
  m_imageScrollArea->setWidget(m_imageContainer);
  m_imageScrollArea->setWidgetResizable(true);
  m_imageScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_imageScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_imageScrollArea->setFixedHeight(320);  // 300 image + scrollbar/padding
  m_imageScrollArea->setStyleSheet(
    "QScrollArea { background: #1e1e1e; border: 1px solid #3c3c3c; }"
    "QScrollBar:horizontal { background: #2a2a2a; height: 12px; }"
    "QScrollBar::handle:horizontal { background: #555555; min-width: 20px; }"
    "QScrollBar::handle:horizontal:hover { background: #1177bb; }");

  m_imageScrollArea->hide();  // Hidden until images are set
  containerLayout->addWidget(m_imageScrollArea);
}

void ManualAdjustWidget::setAdjustmentImages(const QStringList& imagePaths)
{
  if (!m_imageScrollArea || !m_imageLayout) return;

  // Remove all existing image labels (keep the trailing stretch)
  QLayoutItem* item = nullptr;
  while ((item = m_imageLayout->takeAt(0)) != nullptr) {
    if (QWidget* w = item->widget()) {
      w->deleteLater();
    }
    delete item;
  }

  const int kMaxImages = 3;
  int added = 0;
  for (const QString& path : imagePaths) {
    if (added >= kMaxImages) break;

    QPixmap pixmap(path);
    QLabel* imgLabel = new QLabel();
    imgLabel->setFixedSize(400, 300);
    imgLabel->setAlignment(Qt::AlignCenter);
    imgLabel->setStyleSheet(
      "QLabel { background: #2a2a2a; border: 1px solid #555555; color: #888888; }");

    if (pixmap.isNull()) {
      QFileInfo fi(path);
      imgLabel->setText("Image not found:\n" + (fi.fileName().isEmpty() ? path : fi.fileName()));
    }
    else {
      imgLabel->setPixmap(pixmap.scaled(400, 300, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    m_imageLayout->addWidget(imgLabel);
    ++added;
  }
  m_imageLayout->addStretch();

  m_imageScrollArea->setVisible(added > 0);
}

void ManualAdjustWidget::applyStepSize(double stepUm)
{
  m_stepSizeUm = stepUm;
  if (m_stepSizeLabel) {
    m_stepSizeLabel->setText(QString("Step: %1 μm").arg(stepUm, 0, 'f', 1));
  }
}

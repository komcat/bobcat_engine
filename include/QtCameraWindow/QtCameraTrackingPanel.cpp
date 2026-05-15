// QtCameraTrackingPanel.cpp
#include "QtCameraTrackingPanel.h"

#include "QtCameraViewport.h"
#include "motions/CoordinateTransformer.h"
#include "motions/MotionTypes.h"
#include "motions/TransformationMatrixLoader.h"
#include "motions/unified_motion_manager.h"

#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

QtCameraTrackingPanel::QtCameraTrackingPanel(QtCameraViewport* viewport,
                                             std::string instanceId,
                                             QWidget* parent)
  : QGroupBox("Device Tracking", parent)
  , m_viewport(viewport)
  , m_instanceId(std::move(instanceId))
{
  setStyleSheet(
      "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
      "            margin-top: 6px; padding-top: 4px; }"
      "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
  );

  setupUI();
  loadSettings();

  m_matrixLoader = std::make_unique<TransformationMatrixLoader>();
  m_matrixLoader->LoadFromFile("config/transformation_matrix.json");

  m_trackingTimer = new QTimer(this);
  m_trackingTimer->setObjectName("trackingTimer");
  connect(m_trackingTimer, &QTimer::timeout, this, &QtCameraTrackingPanel::onTrackingTimerTick);
  m_trackingTimer->start(100);  // 10 Hz

  updateStatusLabels();
}

QtCameraTrackingPanel::~QtCameraTrackingPanel() = default;

void QtCameraTrackingPanel::setupUI()
{
  auto* v = new QVBoxLayout(this);
  v->setContentsMargins(6, 8, 6, 6);
  v->setSpacing(4);

  static const QString btnStyle =
      "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
      "              color: #cccccc; padding: 4px; min-height: 24px; "
      "              border-radius: 3px; font-size: 11px; }"
      "QPushButton:hover { background-color: #505054; }"
      "QPushButton:pressed { background-color: #007acc; }";

  m_zeroP0Btn        = new QPushButton("Zero (P0 = gantry XY)");
  m_setHexLeftBtn    = new QPushButton("Set hex-left");
  m_setHexRightBtn   = new QPushButton("Set hex-right");
  m_clearTrackingBtn = new QPushButton("Clear");
  for (auto* b : {m_zeroP0Btn, m_setHexLeftBtn, m_setHexRightBtn, m_clearTrackingBtn}) {
    b->setStyleSheet(btnStyle);
  }

  static const QString labelStyle =
      "QLabel { color: #aaaaaa; font-family: Consolas; font-size: 10px; }";
  m_p0StatusLabel       = new QLabel("P0: --");
  m_hexLeftStatusLabel  = new QLabel("hex-L: --");
  m_hexRightStatusLabel = new QLabel("hex-R: --");
  for (auto* l : {m_p0StatusLabel, m_hexLeftStatusLabel, m_hexRightStatusLabel}) {
    l->setStyleSheet(labelStyle);
    l->setWordWrap(true);
  }

  v->addWidget(m_zeroP0Btn);
  v->addWidget(m_setHexLeftBtn);
  v->addWidget(m_setHexRightBtn);
  v->addWidget(m_clearTrackingBtn);
  v->addWidget(m_p0StatusLabel);
  v->addWidget(m_hexLeftStatusLabel);
  v->addWidget(m_hexRightStatusLabel);

  connect(m_zeroP0Btn,        &QPushButton::clicked, this, &QtCameraTrackingPanel::onZeroP0Clicked);
  connect(m_setHexLeftBtn,    &QPushButton::clicked, this, &QtCameraTrackingPanel::onSetHexLeftClicked);
  connect(m_setHexRightBtn,   &QPushButton::clicked, this, &QtCameraTrackingPanel::onSetHexRightClicked);
  connect(m_clearTrackingBtn, &QPushButton::clicked, this, &QtCameraTrackingPanel::onClearTrackingClicked);

  m_setHexLeftBtn->setContextMenuPolicy(Qt::CustomContextMenu);
  m_setHexRightBtn->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_setHexLeftBtn, &QPushButton::customContextMenuRequested,
          this, &QtCameraTrackingPanel::onSetHexLeftRightClicked);
  connect(m_setHexRightBtn, &QPushButton::customContextMenuRequested,
          this, &QtCameraTrackingPanel::onSetHexRightRightClicked);
}

bool QtCameraTrackingPanel::getGantryMainXY(double& x, double& y) const
{
  if (!m_motionManager) return false;
  // Non-blocking cache read — onTrackingTimerTick() runs on the UI thread
  // at 10 Hz and was previously stalling for the duration of each
  // controller round-trip. The background polling thread in
  // UnifiedMotionManager keeps the cache fresh (~200ms).
  PositionStruct pos;
  if (!m_motionManager->GetCachedPosition("gantry-main", pos)) return false;
  x = pos.x;
  y = pos.y;
  return true;
}

bool QtCameraTrackingPanel::getHexLocalXYZ(const std::string& hexDevice,
                                           double& lx, double& ly, double& lz) const
{
  if (!m_motionManager) return false;
  PositionStruct pos;
  if (!m_motionManager->GetCachedPosition(hexDevice, pos)) return false;
  lx = pos.x; ly = pos.y; lz = pos.z;
  return true;
}

void QtCameraTrackingPanel::onZeroP0Clicked()
{
  double x = 0.0, y = 0.0;
  if (!getGantryMainXY(x, y)) return;
  m_p0X = x;
  m_p0Y = y;
  m_p0Set = true;
  updateStatusLabels();
}

void QtCameraTrackingPanel::onSetHexLeftClicked()
{
  if (!m_p0Set) return;
  double gx = 0.0, gy = 0.0;
  if (!getGantryMainXY(gx, gy)) return;
  m_hexLeftRegDX = gx - m_p0X;
  m_hexLeftRegDY = gy - m_p0Y;
  getHexLocalXYZ("hex-left", m_hexLeftRegLocalX, m_hexLeftRegLocalY, m_hexLeftRegLocalZ);
  m_hexLeftReg = true;
  updateStatusLabels();
}

void QtCameraTrackingPanel::onSetHexRightClicked()
{
  if (!m_p0Set) return;
  double gx = 0.0, gy = 0.0;
  if (!getGantryMainXY(gx, gy)) return;
  m_hexRightRegDX = gx - m_p0X;
  m_hexRightRegDY = gy - m_p0Y;
  getHexLocalXYZ("hex-right", m_hexRightRegLocalX, m_hexRightRegLocalY, m_hexRightRegLocalZ);
  m_hexRightReg = true;
  updateStatusLabels();
}

void QtCameraTrackingPanel::onClearTrackingClicked()
{
  m_p0Set = false;
  m_p0X = m_p0Y = 0.0;
  m_hexLeftReg = false;
  m_hexLeftRegDX = m_hexLeftRegDY = 0.0;
  m_hexLeftRegLocalX = m_hexLeftRegLocalY = m_hexLeftRegLocalZ = 0.0;
  m_hexRightReg = false;
  m_hexRightRegDX = m_hexRightRegDY = 0.0;
  m_hexRightRegLocalX = m_hexRightRegLocalY = m_hexRightRegLocalZ = 0.0;
  updateStatusLabels();
}

void QtCameraTrackingPanel::updateStatusLabels()
{
  auto fmt3 = [](double v) {
    return QString("%1%2").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', 4));
  };
  if (m_p0Set) {
    m_p0StatusLabel->setText(QString("P0: %1, %2").arg(fmt3(m_p0X), fmt3(m_p0Y)));
  } else {
    m_p0StatusLabel->setText("P0: --");
  }
  if (m_hexLeftReg) {
    m_hexLeftStatusLabel->setText(
        QString("hex-L reg: %1, %2").arg(fmt3(m_hexLeftRegDX), fmt3(m_hexLeftRegDY)));
  } else {
    m_hexLeftStatusLabel->setText("hex-L: --");
  }
  if (m_hexRightReg) {
    m_hexRightStatusLabel->setText(
        QString("hex-R reg: %1, %2").arg(fmt3(m_hexRightRegDX), fmt3(m_hexRightRegDY)));
  } else {
    m_hexRightStatusLabel->setText("hex-R: --");
  }
}

void QtCameraTrackingPanel::onTrackingTimerTick()
{
  if (!m_viewport) return;

  QtCameraViewport::DeviceTrackingOverlayData data;
  data.enabled = true;
  data.p0Set = m_p0Set;
  data.p0X = m_p0X;
  data.p0Y = m_p0Y;

  auto liveHexOffset = [&](const std::string& dev,
                           double regDX, double regDY,
                           double regLX, double regLY, double regLZ,
                           double& outDX, double& outDY) -> bool {
    if (!m_matrixLoader) return false;
    const auto* matrix = m_matrixLoader->GetMatrix(dev);
    if (!matrix) {
      outDX = regDX; outDY = regDY;  // matrix missing — show registered offset
      return true;
    }
    double lx = 0.0, ly = 0.0, lz = 0.0;
    if (!getHexLocalXYZ(dev, lx, ly, lz)) return false;
    CoordinateTransformer::Vector6D dLocal(
        lx - regLX, ly - regLY, lz - regLZ, 0.0, 0.0, 0.0);
    auto dGlobal = CoordinateTransformer::TransformLocalToGlobal(dLocal, *matrix);
    outDX = regDX + dGlobal.x;
    outDY = regDY + dGlobal.y;
    return true;
  };

  // Hysteresis: enter alert at dist < threshold; exit only when dist > 1.5*threshold.
  // Prevents flicker when distance hovers near the threshold.
  auto applyHysteresis = [](double dist, double threshold, bool wasActive) {
    if (wasActive) return dist < (threshold * 1.5);
    return dist < threshold;
  };

  if (m_p0Set && m_hexLeftReg) {
    if (liveHexOffset("hex-left",
                      m_hexLeftRegDX, m_hexLeftRegDY,
                      m_hexLeftRegLocalX, m_hexLeftRegLocalY, m_hexLeftRegLocalZ,
                      data.hexLeftDX, data.hexLeftDY)) {
      data.hexLeftActive = true;
      const double dist = std::hypot(data.hexLeftDX, data.hexLeftDY);
      data.hexLeftAlert = applyHysteresis(
          dist, m_hexLeftAlertThreshold, m_hexLeftAlertActive);
    }
  }
  if (m_p0Set && m_hexRightReg) {
    if (liveHexOffset("hex-right",
                      m_hexRightRegDX, m_hexRightRegDY,
                      m_hexRightRegLocalX, m_hexRightRegLocalY, m_hexRightRegLocalZ,
                      data.hexRightDX, data.hexRightDY)) {
      data.hexRightActive = true;
      const double dist = std::hypot(data.hexRightDX, data.hexRightDY);
      data.hexRightAlert = applyHysteresis(
          dist, m_hexRightAlertThreshold, m_hexRightAlertActive);
    }
  }

  if (data.hexLeftAlert != m_hexLeftAlertActive) {
    m_hexLeftAlertActive = data.hexLeftAlert;
    applyHexButtonAlertStyle(m_setHexLeftBtn, m_hexLeftAlertActive);
  }
  if (data.hexRightAlert != m_hexRightAlertActive) {
    m_hexRightAlertActive = data.hexRightAlert;
    applyHexButtonAlertStyle(m_setHexRightBtn, m_hexRightAlertActive);
  }

  m_viewport->setDeviceTrackingOverlayData(data);
  m_viewport->update();
}

void QtCameraTrackingPanel::applyHexButtonAlertStyle(QPushButton* btn, bool alert)
{
  if (!btn) return;
  static const QString normalStyle =
      "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
      "              color: #cccccc; padding: 4px; min-height: 24px; "
      "              border-radius: 3px; font-size: 11px; }"
      "QPushButton:hover { background-color: #505054; }"
      "QPushButton:pressed { background-color: #007acc; }";
  static const QString alertStyle =
      "QPushButton { background-color: #b00020; border: 1px solid #ff5050; "
      "              color: #ffffff; padding: 4px; min-height: 24px; "
      "              border-radius: 3px; font-size: 11px; font-weight: bold; }"
      "QPushButton:hover { background-color: #c8002a; }"
      "QPushButton:pressed { background-color: #007acc; }";
  btn->setStyleSheet(alert ? alertStyle : normalStyle);
}

void QtCameraTrackingPanel::promptThresholdDialog(const QString& which, double& threshold)
{
  bool ok = false;
  const double v = QInputDialog::getDouble(
      this,
      QString("%1 alert threshold").arg(which),
      "Trigger alert when |distance| < threshold (mm):",
      threshold,
      0.0,        // min
      1000.0,     // max
      4,          // decimals
      &ok);
  if (!ok) return;
  threshold = v;
  saveSettings();
}

void QtCameraTrackingPanel::onSetHexLeftRightClicked()
{
  promptThresholdDialog("hex-left", m_hexLeftAlertThreshold);
}

void QtCameraTrackingPanel::onSetHexRightRightClicked()
{
  promptThresholdDialog("hex-right", m_hexRightAlertThreshold);
}

void QtCameraTrackingPanel::loadSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  m_hexLeftAlertThreshold = settings.value(
      settingsKey("HexLeftAlertThreshold"), DEFAULT_HEX_ALERT_THRESHOLD).toDouble();
  m_hexRightAlertThreshold = settings.value(
      settingsKey("HexRightAlertThreshold"), DEFAULT_HEX_ALERT_THRESHOLD).toDouble();
}

void QtCameraTrackingPanel::saveSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue(settingsKey("HexLeftAlertThreshold"),  m_hexLeftAlertThreshold);
  settings.setValue(settingsKey("HexRightAlertThreshold"), m_hexRightAlertThreshold);
}

QString QtCameraTrackingPanel::settingsKey(const QString& key) const
{
  // Settings key prefix matches QtCameraWindow::toolsKey() so saved
  // thresholds survive the refactor.
  const QString prefix = m_instanceId.empty()
      ? QStringLiteral("Tools")
      : QStringLiteral("Tools") + QString::fromStdString(m_instanceId);
  return prefix + "/" + key;
}

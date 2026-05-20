// M4AngleWidget.cpp - see M4AngleWidget.h for overview.

#include "M4AngleWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "CameraManager.h"
#include "ICameraHardware.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QFont>
#include <QMetaObject>
#include <QShowEvent>
#include <Qt>

#include <cmath>

namespace {
constexpr double kRadToDeg = 57.29577951308232;  // 180 / pi

constexpr const char* kGantryDevice = "gantry-main";

QString fmtPoint(double x, double y, double z) {
  return QString("X=%1  Y=%2  Z=%3 mm")
    .arg(x, 0, 'f', 4)
    .arg(y, 0, 'f', 4)
    .arg(z, 0, 'f', 4);
}

QFont boldOf(const QLabel* src) {
  QFont f = src->font();
  f.setBold(true);
  return f;
}
}  // namespace

M4AngleWidget::M4AngleWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent), m_context(context) {
  setupUI();
  // Pre-populate from saved named positions so the Move-to buttons are
  // immediately usable when the program starts (after a product is loaded).
  loadFromSavedPositions();
  recompute();
}

void M4AngleWidget::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  // Late-load: if no slot has been populated yet (product may have been
  // loaded after the widget's constructor ran), pull saved values now.
  const bool anyPopulated = m_a1.captured || m_a2.captured ||
                            m_ma1.captured || m_ma2.captured;
  if (!anyPopulated) {
    loadFromSavedPositions();
    recompute();
  }
}

void M4AngleWidget::adoptPoint(Point& dst, QLabel* readout, QPushButton* moveBtn,
                               const PositionStruct& src) {
  dst.captured = true;
  dst.x = src.x;
  dst.y = src.y;
  dst.z = src.z;
  if (readout) {
    readout->setText(fmtPoint(dst.x, dst.y, dst.z) + QStringLiteral("  (saved)"));
    readout->setStyleSheet("color: #7986cb;");  // bluish: distinguishes loaded vs freshly captured
  }
  if (moveBtn && !m_moveInFlight) moveBtn->setEnabled(true);
}

void M4AngleWidget::loadFromSavedPositions() {
  if (!m_context || !m_context->motionManager) return;
  auto* motionMgr = m_context->motionManager;
  const std::string device = kGantryDevice;

  auto tryLoad = [&](const std::string& name, Point& pt, QLabel* readout,
                     QPushButton* moveBtn) {
    PositionStruct p;
    if (motionMgr->GetNamedPosition(device, name, p)) {
      adoptPoint(pt, readout, moveBtn, p);
    }
  };
  tryLoad("a1",  m_a1,  m_a1Readout,  m_a1MoveBtn);
  tryLoad("a2",  m_a2,  m_a2Readout,  m_a2MoveBtn);
  tryLoad("ma1", m_ma1, m_ma1Readout, m_ma1MoveBtn);
  tryLoad("ma2", m_ma2, m_ma2Readout, m_ma2MoveBtn);

  // After populating any of a1/a2 the nominal MOE-A1 button must reflect that.
  refreshMoveButtonsFromCaptureState();
}

void M4AngleWidget::setupUI() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  auto* header = new QLabel(
    "<b>M4 Angle Calculator</b> &mdash; capture 4 points from <code>gantry-main</code> "
    "(positions in mm). Baseline line a1&minus;a2 vs measured line ma1&minus;ma2; "
    "roll uses the actual XY distance between the two captured points. "
    "Correct with hex-right U (delta U) and W (delta W).");
  header->setWordWrap(true);
  root->addWidget(header);

  // --- Capture group ---------------------------------------------------------
  auto* captureGroup = new QGroupBox("Capture points (gantry-main current position)");
  auto* grid = new QGridLayout(captureGroup);
  grid->setHorizontalSpacing(10);
  grid->setVerticalSpacing(6);

  auto addCaptureRow = [&](int row, const QString& label, QLabel*& readout,
                           QPushButton*& moveBtn,
                           void (M4AngleWidget::*captureSlot)(),
                           void (M4AngleWidget::*moveSlot)()) {
    auto* nameLabel = new QLabel(label);
    nameLabel->setFont(boldOf(nameLabel));

    auto* captureBtn = new QPushButton(QStringLiteral("Capture %1").arg(label));
    captureBtn->setMinimumWidth(140);

    readout = new QLabel("(not captured)");
    readout->setStyleSheet("color: #888;");

    moveBtn = new QPushButton(QStringLiteral("Move to %1").arg(label));
    moveBtn->setMinimumWidth(110);
    moveBtn->setEnabled(false);  // enabled once captured
    moveBtn->setToolTip("Move gantry-main back to the captured X/Y/Z so you can re-check it.");

    grid->addWidget(nameLabel,  row, 0);
    grid->addWidget(captureBtn, row, 1);
    grid->addWidget(readout,    row, 2);
    grid->addWidget(moveBtn,    row, 3);

    connect(captureBtn, &QPushButton::clicked, this, captureSlot);
    connect(moveBtn,    &QPushButton::clicked, this, moveSlot);
  };

  addCaptureRow(0, "a1",  m_a1Readout,  m_a1MoveBtn,
                &M4AngleWidget::onCaptureA1,  &M4AngleWidget::onMoveToA1);
  addCaptureRow(1, "a2",  m_a2Readout,  m_a2MoveBtn,
                &M4AngleWidget::onCaptureA2,  &M4AngleWidget::onMoveToA2);

  // Between a2 and ma1: button to drive gantry-main to the nominal MOE-A1
  // station (a1 + dX=-1.50 mm, dZ=-0.15 mm) so the operator can hand-align
  // the gripper/MOE to the camera cross-hair, then click Capture ma1.
  m_moveToMoeA1NominalBtn = new QPushButton("Move to a1 + nominal MOE-A1 (dX=-1.50, dZ=-0.15 mm)");
  m_moveToMoeA1NominalBtn->setMinimumWidth(140);
  m_moveToMoeA1NominalBtn->setEnabled(false);
  m_moveToMoeA1NominalBtn->setToolTip(
    "Move gantry-main to a1 offset by the nominal MOE-A1 location, then align "
    "the gripper / MOE to the cross-hair and click Capture ma1.");
  grid->addWidget(m_moveToMoeA1NominalBtn, 2, 1, 1, 3);  // spans the Capture / readout / move columns
  connect(m_moveToMoeA1NominalBtn, &QPushButton::clicked,
          this, &M4AngleWidget::onMoveToMoeA1Nominal);

  addCaptureRow(3, "ma1", m_ma1Readout, m_ma1MoveBtn,
                &M4AngleWidget::onCaptureMa1, &M4AngleWidget::onMoveToMa1);
  addCaptureRow(4, "ma2", m_ma2Readout, m_ma2MoveBtn,
                &M4AngleWidget::onCaptureMa2, &M4AngleWidget::onMoveToMa2);

  // Save-as-MOE buttons, sitting at col 4 next to the Move-to-ma{1,2} buttons.
  // Each saves gantry-main's CURRENT position under the named-position slot
  // "MOE-A1" / "MOE-A2" IF current pos is within 0.1 mm of the corresponding
  // ma{1,2} reference. Otherwise refuses with a warning.
  m_saveMoeA1Btn = new QPushButton("Save MOE-A1");
  m_saveMoeA1Btn->setMinimumWidth(110);
  m_saveMoeA1Btn->setEnabled(false);
  m_saveMoeA1Btn->setToolTip(
    "Save the current gantry-main position as named position 'MOE-A1'. "
    "Only succeeds when current position is within 0.1 mm of ma1.");
  grid->addWidget(m_saveMoeA1Btn, 3, 4);
  connect(m_saveMoeA1Btn, &QPushButton::clicked,
          this, &M4AngleWidget::onSaveMoeA1);

  m_saveMoeA2Btn = new QPushButton("Save MOE-A2");
  m_saveMoeA2Btn->setMinimumWidth(110);
  m_saveMoeA2Btn->setEnabled(false);
  m_saveMoeA2Btn->setToolTip(
    "Save the current gantry-main position as named position 'MOE-A2'. "
    "Only succeeds when current position is within 0.1 mm of ma2.");
  grid->addWidget(m_saveMoeA2Btn, 4, 4);
  connect(m_saveMoeA2Btn, &QPushButton::clicked,
          this, &M4AngleWidget::onSaveMoeA2);

  auto* clearBtn = new QPushButton("Clear all");
  grid->addWidget(clearBtn, 5, 1);
  connect(clearBtn, &QPushButton::clicked, this, &M4AngleWidget::onClearAll);

  // SWIR exposure presets — quick toggles on the swir_camera so the operator
  // can flip between a known fixed exposure (300 us, useful for alignment with
  // a saturating beam) and auto-exposure (useful for free-running imaging).
  auto* swirFixedBtn = new QPushButton("SWIR 300 us (auto off)");
  swirFixedBtn->setMinimumWidth(170);
  swirFixedBtn->setToolTip(
    "swir_camera: set exposure_time=300 us, auto_exposure=off, auto_gain=off.");
  swirFixedBtn->setStyleSheet(
    "QPushButton { background-color: #fb8c00; color: white; font-weight: bold; padding: 6px 12px; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #ef6c00; }"
    "QPushButton:pressed { background-color: #e65100; }");
  grid->addWidget(swirFixedBtn, 5, 2);
  connect(swirFixedBtn, &QPushButton::clicked, this, &M4AngleWidget::onSwirSet300us);

  auto* swirAutoBtn = new QPushButton("SWIR auto exposure");
  swirAutoBtn->setMinimumWidth(170);
  swirAutoBtn->setToolTip("swir_camera: enable auto_exposure.");
  swirAutoBtn->setStyleSheet(
    "QPushButton { background-color: #ec407a; color: white; font-weight: bold; padding: 6px 12px; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #d81b60; }"
    "QPushButton:pressed { background-color: #ad1457; }");
  grid->addWidget(swirAutoBtn, 5, 3);
  connect(swirAutoBtn, &QPushButton::clicked, this, &M4AngleWidget::onSwirSetAutoExposure);

  grid->setColumnStretch(2, 1);
  root->addWidget(captureGroup);

  // --- Result group ----------------------------------------------------------
  auto* resultGroup = new QGroupBox("Computed deltas");
  auto* resultLayout = new QGridLayout(resultGroup);
  resultLayout->setHorizontalSpacing(12);
  resultLayout->setVerticalSpacing(6);

  auto makeBigValue = [](QLabel*& lbl) {
    lbl = new QLabel("--");
    QFont f = lbl->font();
    f.setPointSizeF(f.pointSizeF() + 2.0);
    f.setBold(true);
    lbl->setFont(f);
  };

  auto addResultRow = [&](int row, const QString& name, QLabel*& valueLabel,
                          const QString& tip) {
    auto* nameLabel = new QLabel(name);
    nameLabel->setFont(boldOf(nameLabel));

    makeBigValue(valueLabel);

    auto* tipLabel = new QLabel(tip);
    tipLabel->setStyleSheet("color: #888;");

    resultLayout->addWidget(nameLabel,  row, 0);
    resultLayout->addWidget(valueLabel, row, 1);
    resultLayout->addWidget(tipLabel,   row, 2);
  };

  addResultRow(0, "Angle a1-a2 (XY)",    m_angleAXyLabel,
               "atan2(Y_a1 - Y_a2, X_a1 - X_a2)");
  addResultRow(1, "Angle ma1-ma2 (XY)",  m_angleMaXyLabel,
               "atan2(Y_ma1 - Y_ma2, X_ma1 - X_ma2)");
  addResultRow(2, "delta U",             m_deltaULabel,
               "angle(a1a2) - angle(ma1ma2) -> adjust hex-right U");
  // Apply U correction: hex-right U -= delta_U at system velocity 0.1.
  m_applyDeltaUBtn = new QPushButton("Apply U correction");
  m_applyDeltaUBtn->setEnabled(false);
  m_applyDeltaUBtn->setToolTip(
    "Move hex-right U by -delta_U (at system velocity 0.1). Positive delta -> "
    "U moves negative; negative delta -> U moves positive.");
  resultLayout->addWidget(m_applyDeltaUBtn, 2, 3);
  connect(m_applyDeltaUBtn, &QPushButton::clicked,
          this, &M4AngleWidget::onApplyDeltaU);

  // Visual gap between U-block and W-block.
  auto* gap = new QLabel(" ");
  gap->setFixedHeight(8);
  resultLayout->addWidget(gap, 3, 0, 1, 4);

  addResultRow(4, "Roll a1-a2 (XZ)",   m_rollAXzLabel,
               "atan2(Z_a2 - Z_a1, |a1-a2| in XY)");
  addResultRow(5, "Roll ma1-ma2 (XZ)", m_rollMaXzLabel,
               "atan2(Z_ma2 - Z_ma1, |ma1-ma2| in XY)");
  addResultRow(6, "delta W",           m_deltaWLabel,
               "roll(a1a2) - roll(ma1ma2) -> adjust hex-right W");
  m_applyDeltaWBtn = new QPushButton("Apply W correction");
  m_applyDeltaWBtn->setEnabled(false);
  m_applyDeltaWBtn->setToolTip(
    "Move hex-right W by -delta_W (at system velocity 0.1). Positive delta -> "
    "W moves negative; negative delta -> W moves positive.");
  resultLayout->addWidget(m_applyDeltaWBtn, 6, 3);
  connect(m_applyDeltaWBtn, &QPushButton::clicked,
          this, &M4AngleWidget::onApplyDeltaW);

  resultLayout->setColumnStretch(2, 1);
  root->addWidget(resultGroup);

  m_advisoryLabel = new QLabel("Capture all 4 points to see deltas.");
  m_advisoryLabel->setWordWrap(true);
  m_advisoryLabel->setStyleSheet("color: #888;");
  root->addWidget(m_advisoryLabel);

  m_moveStatusLabel = new QLabel();
  m_moveStatusLabel->setStyleSheet("color: #888;");
  root->addWidget(m_moveStatusLabel);

  root->addStretch(1);
}

void M4AngleWidget::setMoveButtonsEnabledForBusy(bool busy) {
  if (busy) {
    for (QPushButton* b : {m_a1MoveBtn, m_a2MoveBtn, m_ma1MoveBtn, m_ma2MoveBtn,
                           m_moveToMoeA1NominalBtn,
                           m_applyDeltaUBtn, m_applyDeltaWBtn,
                           m_saveMoeA1Btn, m_saveMoeA2Btn}) {
      if (b) b->setEnabled(false);
    }
  } else {
    refreshMoveButtonsFromCaptureState();
    refreshApplyButtonsEnabledState();
  }
}

void M4AngleWidget::refreshApplyButtonsEnabledState() {
  const bool deltaUValid =
    m_a1.captured && m_a2.captured && m_ma1.captured && m_ma2.captured;
  const bool deltaWValid = deltaUValid;  // both deltas need all 4 points
  if (m_applyDeltaUBtn) m_applyDeltaUBtn->setEnabled(deltaUValid && !m_moveInFlight);
  if (m_applyDeltaWBtn) m_applyDeltaWBtn->setEnabled(deltaWValid && !m_moveInFlight);
}

void M4AngleWidget::refreshMoveButtonsFromCaptureState() {
  if (m_a1MoveBtn)  m_a1MoveBtn->setEnabled(m_a1.captured);
  if (m_a2MoveBtn)  m_a2MoveBtn->setEnabled(m_a2.captured);
  if (m_ma1MoveBtn) m_ma1MoveBtn->setEnabled(m_ma1.captured);
  if (m_ma2MoveBtn) m_ma2MoveBtn->setEnabled(m_ma2.captured);
  // Nominal MOE-A1 needs a1 captured (it's the reference for the offset).
  if (m_moveToMoeA1NominalBtn) m_moveToMoeA1NominalBtn->setEnabled(m_a1.captured);
  // Save MOE-Ax needs the matching ma{1,2} captured (used as tolerance ref).
  if (m_saveMoeA1Btn) m_saveMoeA1Btn->setEnabled(m_ma1.captured);
  if (m_saveMoeA2Btn) m_saveMoeA2Btn->setEnabled(m_ma2.captured);
}

bool M4AngleWidget::capture(Point& dst, QLabel* readout, QPushButton* moveBtn,
                            const char* tag) {
  if (!m_context || !m_context->motionManager) {
    readout->setText(QString("(motion manager unavailable for %1)").arg(tag));
    readout->setStyleSheet("color: #ef5350;");
    return false;
  }
  auto* motionMgr  = m_context->motionManager;
  auto* productMgr = m_context->productPositionManager;

  // Snapshot the previous value before overwriting so we can show a delta if
  // this slot was already populated (either from a saved named position
  // loaded at startup or from an earlier capture this session).
  const bool   hadPrev = dst.captured;
  const double prevX = dst.x;
  const double prevY = dst.y;
  const double prevZ = dst.z;

  PositionStruct pos = motionMgr->GetCurrentPosition(kGantryDevice);
  dst.captured = true;
  dst.x = pos.x;
  dst.y = pos.y;
  dst.z = pos.z;
  readout->setText(fmtPoint(dst.x, dst.y, dst.z));
  readout->setStyleSheet("color: #4caf50;");

  // Persist this capture as a gantry-main named position ("a1" / "a2" /
  // "ma1" / "ma2"). Same flow MotionPage uses for its position editor:
  //   1) update the motion manager's in-memory named-position map
  //   2) import that into the product manager
  //   3) save the product so positions.{product}.json picks the change up
  const std::string device = kGantryDevice;
  const std::string posName = tag;
  motionMgr->AddNamedPosition(device, posName, pos);

  QString saveHint;
  if (productMgr && productMgr->HasLoadedProduct()) {
    productMgr->ImportFromMotionManager(*motionMgr);
    const bool saved = productMgr->SaveProduct();
    saveHint = saved
      ? QStringLiteral("[capture] saved gantry-main '%1' to product positions.").arg(tag)
      : QStringLiteral("[capture] gantry-main '%1' updated in memory, but SaveProduct() FAILED.").arg(tag);
  } else {
    saveHint = QStringLiteral(
      "[capture] gantry-main '%1' updated in memory; no product loaded, so the "
      "named position was not written to disk.").arg(tag);
  }

  // Append a delta line if this slot already had a value: shows how far the
  // operator moved between the old saved/captured position and now.
  if (hadPrev) {
    const double dx = dst.x - prevX;
    const double dy = dst.y - prevY;
    const double dz = dst.z - prevZ;
    const QString deltaLine = QStringLiteral(
      "\n[delta] %1: dX=%2  dY=%3  dZ=%4 mm  (was X=%5 Y=%6 Z=%7)")
      .arg(tag)
      .arg(dx, 0, 'f', 4)
      .arg(dy, 0, 'f', 4)
      .arg(dz, 0, 'f', 4)
      .arg(prevX, 0, 'f', 4)
      .arg(prevY, 0, 'f', 4)
      .arg(prevZ, 0, 'f', 4);
    saveHint += deltaLine;
  }

  if (m_moveStatusLabel) {
    m_moveStatusLabel->setText(saveHint);
    m_moveStatusLabel->setStyleSheet(
      (productMgr && productMgr->HasLoadedProduct()) ? "color: #4caf50;" : "color: #888;");
  }

  // Enable the Move button now that this point is captured — unless a move
  // is currently in flight, in which case refreshMoveButtonsFromCaptureState
  // will pick the new capture flag up when the move completes.
  if (moveBtn && !m_moveInFlight) moveBtn->setEnabled(true);
  return true;
}

void M4AngleWidget::moveTo(const Point& src, const QString& tag) {
  if (!src.captured) return;

  PositionStruct target;
  target.x = src.x;
  target.y = src.y;
  target.z = src.z;
  startBackgroundMove(
    target,
    QStringLiteral("[move] moving gantry-main to %1...").arg(tag),
    QStringLiteral("[move] arrived at %1").arg(tag),
    QStringLiteral("[move] move to %1 FAILED").arg(tag));
}

void M4AngleWidget::startBackgroundMove(const PositionStruct& target,
                                        const QString& movingMsg,
                                        const QString& arrivedOkMsg,
                                        const QString& arrivedFailMsg) {
  if (!m_context || !m_context->motionManager) return;
  auto* motionMgr = m_context->motionManager;
  runInBackground(movingMsg,
    [motionMgr, target, arrivedOkMsg, arrivedFailMsg]() -> std::pair<bool, QString> {
      const bool ok = motionMgr->MoveToAbs6(kGantryDevice, target, true);
      return {ok, ok ? arrivedOkMsg : arrivedFailMsg};
    });
}

void M4AngleWidget::runInBackground(const QString& movingMsg, BackgroundTask task) {
  if (m_moveInFlight) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(
        QStringLiteral("[busy] waiting for current move to finish"));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }

  m_moveInFlight = true;
  setMoveButtonsEnabledForBusy(true);
  if (m_moveStatusLabel) {
    m_moveStatusLabel->setText(movingMsg);
    m_moveStatusLabel->setStyleSheet("color: #4caf50;");
  }

  // Same pattern as ManualAlignPointWidget: std::async runs the blocking
  // motion call on a worker thread; QMetaObject::invokeMethod marshals the
  // result back to the UI thread. m_moveFuture keeps the task alive so the
  // future's destructor doesn't block on overlap.
  m_moveFuture = std::async(std::launch::async,
    [this, task = std::move(task)]() {
      const std::pair<bool, QString> result = task();
      const bool    ok  = result.first;
      const QString msg = result.second;
      QMetaObject::invokeMethod(this, [this, ok, msg]() {
        m_moveInFlight = false;
        setMoveButtonsEnabledForBusy(false);
        if (m_moveStatusLabel) {
          m_moveStatusLabel->setText(msg);
          m_moveStatusLabel->setStyleSheet(ok ? "color: #4caf50;" : "color: #ef5350;");
        }
      }, Qt::QueuedConnection);
    });
}

void M4AngleWidget::onCaptureA1()  { capture(m_a1,  m_a1Readout,  m_a1MoveBtn,  "a1");  recompute(); }
void M4AngleWidget::onCaptureA2()  { capture(m_a2,  m_a2Readout,  m_a2MoveBtn,  "a2");  recompute(); }
void M4AngleWidget::onCaptureMa1() { capture(m_ma1, m_ma1Readout, m_ma1MoveBtn, "ma1"); recompute(); }
void M4AngleWidget::onCaptureMa2() { capture(m_ma2, m_ma2Readout, m_ma2MoveBtn, "ma2"); recompute(); }

void M4AngleWidget::onMoveToA1()  { moveTo(m_a1,  QStringLiteral("a1"));  }
void M4AngleWidget::onMoveToA2()  { moveTo(m_a2,  QStringLiteral("a2"));  }
void M4AngleWidget::onMoveToMa1() { moveTo(m_ma1, QStringLiteral("ma1")); }
void M4AngleWidget::onMoveToMa2() { moveTo(m_ma2, QStringLiteral("ma2")); }

void M4AngleWidget::onSwirSet300us() {
  if (!m_context || !m_context->cameraManager) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral("[swir] camera manager unavailable."));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }
  // Read current settings so we preserve gain / frame_rate; only override
  // exposure_time and the auto-exposure flag.
  auto settings = m_context->cameraManager->GetExposureSettings("swir_camera");
  settings.exposure_time = 300.0;  // microseconds
  settings.auto_exposure = false;
  settings.auto_gain     = false;
  const bool ok =
    m_context->cameraManager->ApplyExposureSettings("swir_camera", settings);
  if (m_moveStatusLabel) {
    m_moveStatusLabel->setText(ok
      ? QStringLiteral("[swir] applied: exposure=300 us, auto_exposure=off.")
      : QStringLiteral("[swir] FAILED to apply 300 us / auto-off settings."));
    m_moveStatusLabel->setStyleSheet(ok ? "color: #4caf50;" : "color: #ef5350;");
  }
}

void M4AngleWidget::onSwirSetAutoExposure() {
  if (!m_context || !m_context->cameraManager) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral("[swir] camera manager unavailable."));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }
  auto settings = m_context->cameraManager->GetExposureSettings("swir_camera");
  settings.auto_exposure = true;
  const bool ok =
    m_context->cameraManager->ApplyExposureSettings("swir_camera", settings);
  if (m_moveStatusLabel) {
    m_moveStatusLabel->setText(ok
      ? QStringLiteral("[swir] applied: auto_exposure=on.")
      : QStringLiteral("[swir] FAILED to enable auto exposure."));
    m_moveStatusLabel->setStyleSheet(ok ? "color: #4caf50;" : "color: #ef5350;");
  }
}

void M4AngleWidget::onSaveMoeA1() {
  saveMoeNamedPosition("MOE-A1", m_ma1, "ma1");
}

void M4AngleWidget::onSaveMoeA2() {
  saveMoeNamedPosition("MOE-A2", m_ma2, "ma2");
}

void M4AngleWidget::saveMoeNamedPosition(const char* moeName,
                                         const Point& referencePoint,
                                         const char* referenceTag) {
  if (!m_context || !m_context->motionManager) return;
  auto* motionMgr  = m_context->motionManager;
  auto* productMgr = m_context->productPositionManager;

  // Need the reference (ma1 / ma2) captured to know what "close enough" means.
  if (!referencePoint.captured) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral(
        "[save %1] reference '%2' not captured yet. Capture it first.")
        .arg(moeName).arg(referenceTag));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }

  constexpr double kMoeSaveToleranceMm = 0.1;

  const PositionStruct cur = motionMgr->GetCurrentPosition(kGantryDevice);
  const double dx = cur.x - referencePoint.x;
  const double dy = cur.y - referencePoint.y;
  const double dz = cur.z - referencePoint.z;
  const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

  if (dist > kMoeSaveToleranceMm) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral(
        "[save %1] WARNING: current gantry-main is %2 mm from %3 "
        "(dX=%4 dY=%5 dZ=%6), > %7 mm tolerance. NOT SAVED. "
        "Move closer to %3 or save manually from the Motion page.")
        .arg(moeName)
        .arg(dist, 0, 'f', 4)
        .arg(referenceTag)
        .arg(dx, 0, 'f', 4)
        .arg(dy, 0, 'f', 4)
        .arg(dz, 0, 'f', 4)
        .arg(kMoeSaveToleranceMm, 0, 'f', 3));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }

  // Inside tolerance — persist using the same flow as Capture: motion-manager
  // map -> product manager import -> SaveProduct on disk.
  motionMgr->AddNamedPosition(kGantryDevice, moeName, cur);

  QString status;
  bool ok = false;
  if (productMgr && productMgr->HasLoadedProduct()) {
    productMgr->ImportFromMotionManager(*motionMgr);
    const bool saved = productMgr->SaveProduct();
    ok = saved;
    status = saved
      ? QStringLiteral(
          "[save %1] saved gantry-main current pos as '%1' "
          "(%2 mm from %3, within %4 mm tolerance).")
          .arg(moeName)
          .arg(dist, 0, 'f', 4)
          .arg(referenceTag)
          .arg(kMoeSaveToleranceMm, 0, 'f', 3)
      : QStringLiteral(
          "[save %1] updated in memory, but SaveProduct() FAILED.")
          .arg(moeName);
  } else {
    status = QStringLiteral(
      "[save %1] no product loaded; named position updated in memory only "
      "(not written to disk).").arg(moeName);
  }
  if (m_moveStatusLabel) {
    m_moveStatusLabel->setText(status);
    m_moveStatusLabel->setStyleSheet(ok ? "color: #4caf50;" : "color: #888;");
  }
}

void M4AngleWidget::onApplyDeltaU() {
  const bool ready = m_a1.captured && m_a2.captured && m_ma1.captured && m_ma2.captured;
  if (!ready) return;
  const double deltaU = lineAngleDegXY(m_a1, m_a2) - lineAngleDegXY(m_ma1, m_ma2);
  applyHexRightCorrection(deltaU, 'U');
}

void M4AngleWidget::onApplyDeltaW() {
  const bool ready = m_a1.captured && m_a2.captured && m_ma1.captured && m_ma2.captured;
  if (!ready) return;
  const double deltaW = lineRollDeg(m_a1, m_a2) - lineRollDeg(m_ma1, m_ma2);
  applyHexRightCorrection(deltaW, 'W');
}

void M4AngleWidget::applyHexRightCorrection(double deltaDeg, char axisLetter) {
  if (!m_context || !m_context->motionManager) return;
  if (axisLetter != 'U' && axisLetter != 'W') return;  // sanity: only U or W

  auto* motionMgr = m_context->motionManager;
  const std::string hexDevice = "hex-right";
  constexpr double kCorrectionVelocity = 0.1;

  const QString movingMsg = QStringLiteral(
    "[apply] correcting hex-right %1 by %2 deg (sys vel 0.1)...")
    .arg(QChar(axisLetter))
    .arg(-deltaDeg, 0, 'f', 4);

  // Same background-worker primitive as the gantry-main Move-to buttons: the
  // task closure runs on a std::async thread, returns (ok, statusMsg), and
  // runInBackground marshals the result back to the UI thread.
  runInBackground(movingMsg,
    [motionMgr, hexDevice, deltaDeg, axisLetter]() -> std::pair<bool, QString> {
      double originalVelocity = 0.0;
      const bool gotVel = motionMgr->GetSystemVelocity(hexDevice, originalVelocity);

      bool    ok = false;
      double  newVal = 0.0;
      QString failReason;

      do {
        if (!motionMgr->SetSystemVelocity(hexDevice, kCorrectionVelocity)) {
          failReason = "SetSystemVelocity(0.1) failed";
          break;
        }
        PositionStruct current = motionMgr->GetCurrentPosition(hexDevice);
        PositionStruct target = current;
        // Positive delta -> axis moves negative; negative delta -> axis moves
        // positive. Single formula: target = current - delta.
        if (axisLetter == 'U') {
          target.u = current.u - deltaDeg;
          newVal = target.u;
        } else {
          target.w = current.w - deltaDeg;
          newVal = target.w;
        }
        ok = motionMgr->MoveToAbs6(hexDevice, target, true);
        if (!ok) failReason = "MoveToAbs6 failed";
      } while (false);

      // Always restore the original velocity if we read it.
      if (gotVel) motionMgr->SetSystemVelocity(hexDevice, originalVelocity);

      if (ok) {
        return {true, QStringLiteral(
          "[apply] hex-right %1 corrected by %2 deg (now %3 deg). "
          "Re-capture ma1/ma2 to verify.")
          .arg(QChar(axisLetter))
          .arg(-deltaDeg, 0, 'f', 4)
          .arg(newVal, 0, 'f', 4)};
      }
      return {false, QStringLiteral(
        "[apply] hex-right %1 correction FAILED: %2")
        .arg(QChar(axisLetter))
        .arg(failReason)};
    });
}

void M4AngleWidget::onMoveToMoeA1Nominal() {
  if (!m_a1.captured) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(
        QStringLiteral("[MOE] capture a1 first - it is the reference for the nominal MOE-A1 offset."));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }
  constexpr double kMoeA1DxMm = -1.50;
  constexpr double kMoeA1DzMm = -0.15;
  PositionStruct target;
  target.x = m_a1.x + kMoeA1DxMm;
  target.y = m_a1.y;
  target.z = m_a1.z + kMoeA1DzMm;

  startBackgroundMove(
    target,
    QStringLiteral(
      "[MOE] moving to MOE-A1 nominal (a1 + dX=-1.50 mm, dZ=-0.15 mm)..."),
    QStringLiteral(
      "[MOE] At MOE-A1 nominal. Move the gripper / MOE to the cross-hair, then click 'Capture ma1' to record the position."),
    QStringLiteral("[MOE] move to MOE-A1 nominal FAILED"));
}

void M4AngleWidget::onClearAll() {
  // Drop all in-memory captures, then re-sync from saved named positions on
  // disk. Disk is the source of truth — captures write through to it — so
  // "Clear all" effectively means "discard session edits and reload saved
  // a1/a2/ma1/ma2". Slots with no saved value stay empty.
  m_a1 = m_a2 = m_ma1 = m_ma2 = Point{};
  for (QLabel* l : {m_a1Readout, m_a2Readout, m_ma1Readout, m_ma2Readout}) {
    l->setText("(not captured)");
    l->setStyleSheet("color: #888;");
  }
  for (QPushButton* b : {m_a1MoveBtn, m_a2MoveBtn, m_ma1MoveBtn, m_ma2MoveBtn,
                         m_moveToMoeA1NominalBtn,
                         m_saveMoeA1Btn, m_saveMoeA2Btn}) {
    if (b) b->setEnabled(false);
  }
  loadFromSavedPositions();
  if (m_moveStatusLabel) {
    m_moveStatusLabel->setText(QStringLiteral(
      "[clear] cleared in-memory captures and reloaded saved positions (if any)."));
    m_moveStatusLabel->setStyleSheet("color: #888;");
  }
  recompute();
}

double M4AngleWidget::lineAngleDegXY(const Point& p1, const Point& p2) {
  return std::atan2(p1.y - p2.y, p1.x - p2.x) * kRadToDeg;
}

double M4AngleWidget::lineRollDeg(const Point& p1, const Point& p2) {
  // Project Z difference over the actual XY distance between the two points.
  // Both inputs are in mm; the ratio is dimensionless so the unit cancels.
  const double dx = p2.x - p1.x;
  const double dy = p2.y - p1.y;
  const double dxy = std::sqrt(dx * dx + dy * dy);
  if (dxy <= 0.0) return 0.0;  // degenerate: a1 and a2 are the same point
  return std::atan2(p2.z - p1.z, dxy) * kRadToDeg;
}

void M4AngleWidget::recompute() {
  const QString placeholder = "--";
  const bool aReady  = m_a1.captured  && m_a2.captured;
  const bool maReady = m_ma1.captured && m_ma2.captured;

  auto degStr = [](double v) {
    return QString::number(v, 'f', 4) + QStringLiteral(" deg");
  };

  // XY angles + delta U.
  m_angleAXyLabel->setText(aReady  ? degStr(lineAngleDegXY(m_a1,  m_a2))  : placeholder);
  m_angleMaXyLabel->setText(maReady ? degStr(lineAngleDegXY(m_ma1, m_ma2)) : placeholder);
  m_deltaULabel->setText((aReady && maReady)
    ? degStr(lineAngleDegXY(m_a1, m_a2) - lineAngleDegXY(m_ma1, m_ma2))
    : placeholder);

  // XZ rolls + delta W.
  m_rollAXzLabel->setText(aReady  ? degStr(lineRollDeg(m_a1,  m_a2))  : placeholder);
  m_rollMaXzLabel->setText(maReady ? degStr(lineRollDeg(m_ma1, m_ma2)) : placeholder);
  m_deltaWLabel->setText((aReady && maReady)
    ? degStr(lineRollDeg(m_a1, m_a2) - lineRollDeg(m_ma1, m_ma2))
    : placeholder);

  m_advisoryLabel->setText((aReady && maReady)
    ? QStringLiteral("Apply: hex-right U -= delta U, hex-right W -= delta W (at sys vel 0.1).")
    : QStringLiteral("Capture all 4 points to see deltas."));

  // Enable/disable Apply U/W buttons based on delta availability.
  refreshApplyButtonsEnabledState();
}

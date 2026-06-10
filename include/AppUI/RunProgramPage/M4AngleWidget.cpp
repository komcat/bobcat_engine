// M4AngleWidget.cpp - see M4AngleWidget.h for overview.

#include "M4AngleWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "CameraManager.h"
#include "ICameraHardware.h"
#include "QtCameraWindow/QtCameraWindow.h"
#include "PSU/PowerSupplyManager.h"
#include "PSU/IPowerSupply.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QTimer>
#include <QFont>
#include <QPixmap>
#include <QMetaObject>
#include <QShowEvent>
#include <Qt>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace {
constexpr double kRadToDeg = 57.29577951308232;  // 180 / pi

constexpr const char* kGantryDevice = "gantry-main";
constexpr const char* kSwirCamera   = "swir_camera";

// SWIR-source PSU controls — mirror the SWIR monitor window's behaviour.
constexpr int    kPsuChannel        = 1;      // SWIR source is wired to ch1
constexpr double kPsuStepCoarseMa   = 10.0;   // coarse current step (+/-10 mA)
constexpr double kPsuStepFineMa     = 5.0;    // fine current step (+/-5 mA)
constexpr double kPsuDefaultVoltage = 2.0;    // applied on first connect (V)
constexpr double kPsuDefaultCurrent = 0.3;    // applied on first connect (A)
constexpr int    kPsuPollIntervalMs = 1000;

// Nominal MOE offset applied to a1 / a2 to reach the ma1 / ma2 stations
// (same values the "Move to a1 + nominal MOE-A1" button uses).
constexpr double kMoeNominalDxMm = -1.50;
constexpr double kMoeNominalDzMm = -0.15;

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
  // Reflect the live beam-stream state (it may have been toggled from the
  // camera monitor's own Beam tab since this widget was last shown).
  if (QtCameraWindow* win = findSwirBeamWindow()) {
    updateBeamStreamButton(win->isBeamStreamActive());
  } else {
    updateBeamStreamButton(false);
  }
  // PSU devices may register after this widget is constructed; (re)try discovery
  // each time the tab is shown, then refresh the live readouts immediately.
  discoverPsuDevices();
  pollSelectedPsu();
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
  m_captureGroupBox = captureGroup;
  auto* grid = new QGridLayout(captureGroup);
  grid->setHorizontalSpacing(10);
  grid->setVerticalSpacing(6);

  // Column order per row: name | Move-to (yellow) | Capture (cyan) | readout |
  // right-side action (Y jog / Save MOE). Yellow = recall a taught point,
  // cyan = capture the current position into that slot.
  const char* kMoveBtnStyle =
    "QPushButton { background-color: #ffea00; color: #1a1a1a; font-weight: bold; padding: 8px 12px; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #fff350; }"
    "QPushButton:pressed { background-color: #fdd835; }"
    "QPushButton:disabled { background-color: #5a5a3a; color: #9a9a8a; }";
  const char* kCaptureBtnStyle =
    "QPushButton { background-color: #18ffff; color: #00363a; font-weight: bold; padding: 8px 12px; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #6effff; }"
    "QPushButton:pressed { background-color: #00e5ff; }"
    "QPushButton:disabled { background-color: #9e9e9e; color: #5a5a5a; }";

  auto addCaptureRow = [&](int row, const QString& label, QLabel*& readout,
                           QPushButton*& moveBtn, QPushButton*& captureBtnOut,
                           void (M4AngleWidget::*captureSlot)(),
                           void (M4AngleWidget::*moveSlot)()) {
    auto* nameLabel = new QLabel(label);
    nameLabel->setFont(boldOf(nameLabel));

    moveBtn = new QPushButton(QStringLiteral("Move to %1").arg(label));
    moveBtn->setMinimumWidth(110);
    moveBtn->setEnabled(false);  // enabled once captured
    moveBtn->setStyleSheet(kMoveBtnStyle);
    moveBtn->setToolTip(QStringLiteral(
      "Move gantry-main back to the captured %1 X/Y/Z so you can re-check it.").arg(label));

    auto* captureBtn = new QPushButton(QStringLiteral("Capture %1").arg(label));
    captureBtn->setMinimumWidth(120);
    captureBtn->setStyleSheet(kCaptureBtnStyle);
    captureBtn->setToolTip(QStringLiteral(
      "Capture gantry-main's current position into slot %1.").arg(label));
    captureBtnOut = captureBtn;

    readout = new QLabel("(not captured)");
    readout->setStyleSheet("color: #888;");

    grid->addWidget(nameLabel,  row, 0);
    grid->addWidget(moveBtn,    row, 1);
    grid->addWidget(captureBtn, row, 2);
    grid->addWidget(readout,    row, 3);

    connect(captureBtn, &QPushButton::clicked, this, captureSlot);
    connect(moveBtn,    &QPushButton::clicked, this, moveSlot);
  };

  QPushButton* unusedCaptureBtn = nullptr;  // capture buttons we don't need to keep a handle to
  addCaptureRow(0, "a1",  m_a1Readout,  m_a1MoveBtn, unusedCaptureBtn,
                &M4AngleWidget::onCaptureA1,  &M4AngleWidget::onMoveToA1);

  // Y +/- 3.25 mm relative jog on gantry-main, in the right-hand column of the
  // a1 / a2 rows. The two reference points are ~3.25 mm apart in Y, so these
  // are the rough one-shot jogs to hop between them while teaching.
  m_yPlus35Btn = new QPushButton("Y +3.25");
  m_yPlus35Btn->setMinimumWidth(110);
  m_yPlus35Btn->setToolTip("gantry-main: relative move Y +3.25 mm.");
  grid->addWidget(m_yPlus35Btn, 0, 4);
  connect(m_yPlus35Btn, &QPushButton::clicked, this, &M4AngleWidget::onMoveYPlus35);

  addCaptureRow(1, "a2",  m_a2Readout,  m_a2MoveBtn, unusedCaptureBtn,
                &M4AngleWidget::onCaptureA2,  &M4AngleWidget::onMoveToA2);

  m_yMinus35Btn = new QPushButton("Y -3.25");
  m_yMinus35Btn->setMinimumWidth(110);
  m_yMinus35Btn->setToolTip("gantry-main: relative move Y -3.25 mm.");
  grid->addWidget(m_yMinus35Btn, 1, 4);
  connect(m_yMinus35Btn, &QPushButton::clicked, this, &M4AngleWidget::onMoveYMinus35);

  // Between a2 and ma1: button to drive gantry-main to the nominal MOE-A1
  // station (a1 + dX=-1.50 mm, dZ=-0.15 mm) so the operator can hand-align
  // the gripper/MOE to the camera cross-hair, then click Capture ma1.
  m_moveToMoeA1NominalBtn = new QPushButton("Move to");
  m_moveToMoeA1NominalBtn->setMinimumWidth(110);
  m_moveToMoeA1NominalBtn->setEnabled(false);
  m_moveToMoeA1NominalBtn->setStyleSheet(kMoveBtnStyle);
  m_moveToMoeA1NominalBtn->setToolTip(
    "Move gantry-main to a1 offset by the nominal MOE-A1 location, then align "
    "the gripper / MOE to the cross-hair and click Capture ma1.");
  grid->addWidget(m_moveToMoeA1NominalBtn, 2, 1);
  connect(m_moveToMoeA1NominalBtn, &QPushButton::clicked,
          this, &M4AngleWidget::onMoveToMoeA1Nominal);

  // Descriptive label for the nominal MOE-A1 move, sitting in the Capture /
  // readout columns next to its yellow Move-to button.
  auto* moeDescLabel = new QLabel("Move to a1 + nominal MOE-A1 (dX=-1.50, dZ=-0.15 mm)");
  moeDescLabel->setAlignment(Qt::AlignCenter);
  moeDescLabel->setStyleSheet(
    "QLabel { background-color: #1565c0; color: #e3f2fd; font-weight: bold; "
    "padding: 6px 10px; border-radius: 3px; }");
  grid->addWidget(moeDescLabel, 2, 2, 1, 2);  // spans the Capture + readout columns

  addCaptureRow(3, "ma1", m_ma1Readout, m_ma1MoveBtn, m_captureMa1Btn,
                &M4AngleWidget::onCaptureMa1, &M4AngleWidget::onMoveToMa1);
  // ma1 is captured automatically when 'Move to a1 + nominal MOE-A1' finishes,
  // so the manual capture button stays disabled to remove an unsafe race where
  // the operator clicks it before the move has settled.
  if (m_captureMa1Btn) {
    m_captureMa1Btn->setEnabled(false);
    m_captureMa1Btn->setToolTip(
      "Disabled - ma1 is captured automatically when the 'Move to a1 + nominal MOE-A1' move completes.");
  }
  addCaptureRow(4, "ma2", m_ma2Readout, m_ma2MoveBtn, unusedCaptureBtn,
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

  // Beam-stream toggle — remote-controls the "Enable Beam Stream" controller in
  // whichever Qt camera monitor is currently showing swir_camera, so the
  // operator can start/stop beam streaming without leaving the M4 Angle tab.
  m_beamStreamBtn = new QPushButton();
  m_beamStreamBtn->setMinimumWidth(110);
  m_beamStreamBtn->setToolTip(
    "Enable/disable beam streaming on swir_camera. Drives the 'Enable Beam "
    "Stream' control in the Qt camera monitor currently displaying swir_camera "
    "(open one with Ctrl+R / Ctrl+T and select swir_camera first).");
  grid->addWidget(m_beamStreamBtn, 5, 4);
  connect(m_beamStreamBtn, &QPushButton::clicked, this, &M4AngleWidget::onToggleBeamStream);
  updateBeamStreamButton(false);

  grid->setColumnStretch(3, 1);
  root->addWidget(captureGroup);

  // --- SWIR-source power supply ----------------------------------------------
  // Minimal mirror of the PSU controls in the SWIR monitor window so the
  // operator can drive the SWIR illumination source (current on channel 1)
  // without leaving the M4 Angle tab. All hardware access goes through the
  // shared PowerSupplyManager and runs off the UI thread.
  auto* psuGroup = new QGroupBox("SWIR-source power supply (channel 1)");
  m_psuGroupBox = psuGroup;
  auto* pv = new QVBoxLayout(psuGroup);
  pv->setContentsMargins(10, 10, 10, 10);
  pv->setSpacing(0);

  // --- Device tabs: one cyan-when-active tab button per discovered PSU. The
  // tabs are (re)built in rebuildPsuTabs() once devices register; the row may
  // show a placeholder until then.
  m_psuTabGroup = new QButtonGroup(this);
  m_psuTabGroup->setExclusive(true);
  auto* tabRow = new QWidget();
  m_psuTabLayout = new QHBoxLayout(tabRow);
  m_psuTabLayout->setContentsMargins(0, 0, 0, 0);
  m_psuTabLayout->setSpacing(4);
  pv->addWidget(tabRow);
  connect(m_psuTabGroup, QOverload<int>::of(&QButtonGroup::idClicked),
          this, &M4AngleWidget::onPsuTabSelected);
  rebuildPsuTabs();  // shows the "(no PSU)" placeholder until discovery runs

  // --- Full-width On/Off bar (red when off, green when on, grey when no link).
  m_psuOutputBtn = new QPushButton("On/Off");
  m_psuOutputBtn->setMinimumHeight(36);
  m_psuOutputBtn->setToolTip(
    "Toggle the selected PSU output on channel 1. On first connect the channel "
    "is initialised to 2.0 V / 300 mA and turned on (same as the SWIR monitor).");
  pv->addWidget(m_psuOutputBtn);

  pv->addSpacing(10);

  // --- Stacked readouts (Set Current / Voltage / Current) with the +/- step
  // grid below them, all left-aligned to mirror the minimal-control mockup.
  m_psuSetLabel     = new QLabel("Set Current : -- mA");
  m_psuVoltageLabel = new QLabel("Voltage: -- V");
  m_psuCurrentLabel = new QLabel("Current: -- mA");
  for (QLabel* l : {m_psuSetLabel, m_psuVoltageLabel, m_psuCurrentLabel}) {
    QFont f = boldOf(l);
    l->setFont(f);
    l->setStyleSheet("color: #888;");
  }
  pv->addWidget(m_psuSetLabel);
  pv->addWidget(m_psuVoltageLabel);
  pv->addWidget(m_psuCurrentLabel);

  pv->addSpacing(8);

  m_psuPlus10Btn  = new QPushButton("+10");
  m_psuMinus10Btn = new QPushButton("-10");
  m_psuPlus5Btn   = new QPushButton("+5");
  m_psuMinus5Btn  = new QPushButton("-5");
  const char* kPsuStepStyle =
    "QPushButton { background-color: #c2c7cc; color: #1a1a1a; font-weight: bold; "
    "min-width: 64px; min-height: 36px; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #d4d9de; }"
    "QPushButton:pressed { background-color: #a8adb2; }"
    "QPushButton:disabled { background-color: #555; color: #888; }";
  for (QPushButton* b : {m_psuPlus10Btn, m_psuMinus10Btn, m_psuPlus5Btn, m_psuMinus5Btn}) {
    b->setStyleSheet(kPsuStepStyle);
    b->setToolTip("Adjust the channel-1 set current of the selected PSU (mA).");
  }
  auto* stepGrid = new QGridLayout();
  stepGrid->setHorizontalSpacing(6);
  stepGrid->setVerticalSpacing(6);
  stepGrid->addWidget(m_psuPlus10Btn,  0, 0);
  stepGrid->addWidget(m_psuMinus10Btn, 0, 1);
  stepGrid->addWidget(m_psuPlus5Btn,   1, 0);
  stepGrid->addWidget(m_psuMinus5Btn,  1, 1);
  // Wrap the grid in a row with a trailing stretch so the buttons hug the left.
  auto* stepRow = new QHBoxLayout();
  stepRow->addLayout(stepGrid);
  stepRow->addStretch(1);
  pv->addLayout(stepRow);

  connect(m_psuOutputBtn, &QPushButton::clicked, this, &M4AngleWidget::onPsuOutputToggle);
  connect(m_psuPlus10Btn,  &QPushButton::clicked, this, [this]() { onPsuCurrentStep(+kPsuStepCoarseMa); });
  connect(m_psuMinus10Btn, &QPushButton::clicked, this, [this]() { onPsuCurrentStep(-kPsuStepCoarseMa); });
  connect(m_psuPlus5Btn,   &QPushButton::clicked, this, [this]() { onPsuCurrentStep(+kPsuStepFineMa); });
  connect(m_psuMinus5Btn,  &QPushButton::clicked, this, [this]() { onPsuCurrentStep(-kPsuStepFineMa); });
  updatePsuOutputButton(false, false);

  // Periodic background poll of the selected PSU's live readings (only while
  // this tab is visible — see pollSelectedPsu()).
  m_psuPollTimer = new QTimer(this);
  m_psuPollTimer->setInterval(kPsuPollIntervalMs);
  connect(m_psuPollTimer, &QTimer::timeout, this, &M4AngleWidget::pollSelectedPsu);
  m_psuPollTimer->start();

  // --- Auto-correction wizard -------------------------------------------------
  // Guided sequence that drives the full M4 correction workflow (teach a1/a2/
  // ma1/ma2 with operator confirmation + SWIR thumbnails, auto-apply U/W, set
  // exposure, then focus-and-save MOE-A1/A2). Placed to the right of the PSU
  // group in a single 2-column row (see the row layout at the end of this block).
  auto* autoGroup = new QGroupBox("Auto correction");
  auto* ag = new QGridLayout(autoGroup);
  ag->setHorizontalSpacing(10);
  ag->setVerticalSpacing(8);

  m_autoStartBtn = new QPushButton("Start correction");
  m_autoStartBtn->setMinimumHeight(40);
  m_autoStartBtn->setStyleSheet(
    "QPushButton { background-color: #ffea00; color: #1a1a1a; font-weight: bold; padding: 8px 12px; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #fff350; }"
    "QPushButton:pressed { background-color: #fdd835; }"
    "QPushButton:disabled { background-color: #5a5a3a; color: #9a9a8a; }");
  m_autoStartBtn->setToolTip(
    "Run the guided M4 correction sequence. Requires a1 and a2 already taught.");
  ag->addWidget(m_autoStartBtn, 0, 0);

  // Thumbnail box (top-right), spans both content rows on the right column.
  m_autoThumbnailLabel = new QLabel("(no thumbnail)");
  m_autoThumbnailLabel->setFixedSize(180, 130);
  m_autoThumbnailLabel->setAlignment(Qt::AlignCenter);
  m_autoThumbnailLabel->setStyleSheet(
    "QLabel { background-color: #2b2b2b; color: #888; border: 1px solid #555; }");
  ag->addWidget(m_autoThumbnailLabel, 0, 1, 2, 1, Qt::AlignRight | Qt::AlignTop);

  // Step indicator chips: A1, A2, MA1, MA2 (order shown left→right).
  auto* chipsRow = new QHBoxLayout();
  chipsRow->setSpacing(6);
  const char* chipLabels[4] = {"A1", "A2", "MA1", "MA2"};
  for (int i = 0; i < 4; ++i) {
    auto* chip = new QLabel(chipLabels[i]);
    chip->setAlignment(Qt::AlignCenter);
    chip->setFixedSize(44, 44);
    m_autoStepChips[i] = chip;
    chipsRow->addWidget(chip);
  }
  chipsRow->addStretch(1);
  ag->addLayout(chipsRow, 1, 0);

  // Overwrite-image checkbox (default on).
  m_autoOverwriteChk = new QCheckBox("Overwrite image on capture");
  m_autoOverwriteChk->setChecked(true);
  m_autoOverwriteChk->setToolTip(
    "When checked, Capture overwrites the saved SWIR thumbnail for the current "
    "point; when unchecked, an existing thumbnail is preserved.");
  ag->addWidget(m_autoOverwriteChk, 2, 1, Qt::AlignRight);

  // Operator instruction text.
  m_autoInstructionLabel = new QLabel("Idle. Click \"Start correction\" to begin.");
  m_autoInstructionLabel->setWordWrap(true);
  m_autoInstructionLabel->setMinimumHeight(40);
  {
    QFont f = boldOf(m_autoInstructionLabel);
    f.setPointSizeF(f.pointSizeF() + 1.0);
    m_autoInstructionLabel->setFont(f);
  }
  m_autoInstructionLabel->setStyleSheet("color: #ffd54f;");
  ag->addWidget(m_autoInstructionLabel, 2, 0);

  // Capture (cyan) + Abort (red) along the bottom.
  m_autoCaptureBtn = new QPushButton("Capture");
  m_autoCaptureBtn->setMinimumHeight(48);
  m_autoCaptureBtn->setEnabled(false);
  m_autoCaptureBtn->setStyleSheet(
    "QPushButton { background-color: #18ffff; color: #00363a; font-weight: bold; padding: 8px 12px; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #6effff; }"
    "QPushButton:pressed { background-color: #00e5ff; }"
    "QPushButton:disabled { background-color: #9e9e9e; color: #5a5a5a; }");
  ag->addWidget(m_autoCaptureBtn, 3, 0);

  m_autoAbortBtn = new QPushButton("Abort");
  m_autoAbortBtn->setMinimumHeight(48);
  m_autoAbortBtn->setEnabled(false);
  m_autoAbortBtn->setStyleSheet(
    "QPushButton { background-color: #e53935; color: white; font-weight: bold; padding: 8px 12px; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #d32f2f; }"
    "QPushButton:pressed { background-color: #c62828; }"
    "QPushButton:disabled { background-color: #5a3a3a; color: #9a8a8a; }");
  ag->addWidget(m_autoAbortBtn, 3, 1);

  ag->setColumnStretch(0, 1);
  connect(m_autoStartBtn,   &QPushButton::clicked, this, &M4AngleWidget::onAutoStart);
  connect(m_autoCaptureBtn, &QPushButton::clicked, this, &M4AngleWidget::onAutoCapture);
  connect(m_autoAbortBtn,   &QPushButton::clicked, this, &M4AngleWidget::onAutoAbort);
  updateAutoUi(AutoStep::Idle, QString());  // paint the chips in their idle state

  // Two-column row: SWIR-source PSU (left) | Auto correction (right).
  auto* psuAutoRow = new QHBoxLayout();
  psuAutoRow->addWidget(psuGroup,  1);
  psuAutoRow->addWidget(autoGroup, 1);
  root->addLayout(psuAutoRow);

  // --- Result group ----------------------------------------------------------
  // Two columns: the computed-deltas grid on the left, a miniature gantry-main
  // jog panel on the right (so the operator can nudge gantry-main without
  // leaving this tab — including during the auto-correction focus steps).
  auto* resultGroup = new QGroupBox("Computed deltas");
  auto* resultOuter = new QHBoxLayout(resultGroup);
  auto* deltasWidget = new QWidget();
  auto* resultLayout = new QGridLayout(deltasWidget);
  resultLayout->setContentsMargins(0, 0, 0, 0);
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

  resultOuter->addWidget(deltasWidget, 1);
  resultOuter->addWidget(buildGantryJogPanel(), 0, Qt::AlignTop);
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
                           m_yPlus35Btn, m_yMinus35Btn,
                           m_applyDeltaUBtn, m_applyDeltaWBtn,
                           m_saveMoeA1Btn, m_saveMoeA2Btn}) {
      if (b) b->setEnabled(false);
    }
    setJogButtonsEnabled(false);   // no overlapping moves while one is in flight
    if (m_autoCaptureBtn) m_autoCaptureBtn->setEnabled(false);  // no capture mid-move
  } else {
    refreshMoveButtonsFromCaptureState();
    refreshApplyButtonsEnabledState();
    setJogButtonsEnabled(true);    // jog stays usable (incl. during the wizard)
    // Re-arm the wizard Capture only when it is genuinely waiting for input;
    // autoMoveThenAwait's continuation sets this immediately after arrival.
    if (m_autoCaptureBtn)
      m_autoCaptureBtn->setEnabled(m_autoActive && m_autoAwaitingCapture);
  }
}

void M4AngleWidget::refreshApplyButtonsEnabledState() {
  const bool deltaUValid =
    m_a1.captured && m_a2.captured && m_ma1.captured && m_ma2.captured;
  const bool deltaWValid = deltaUValid;  // both deltas need all 4 points
  // Disabled during a move and while the auto-correction wizard owns the
  // sequence (the wizard applies U/W itself).
  if (m_applyDeltaUBtn) m_applyDeltaUBtn->setEnabled(deltaUValid && !m_moveInFlight && !m_autoActive);
  if (m_applyDeltaWBtn) m_applyDeltaWBtn->setEnabled(deltaWValid && !m_moveInFlight && !m_autoActive);
}

void M4AngleWidget::refreshMoveButtonsFromCaptureState() {
  if (m_a1MoveBtn)  m_a1MoveBtn->setEnabled(m_a1.captured);
  if (m_a2MoveBtn)  m_a2MoveBtn->setEnabled(m_a2.captured);
  if (m_ma1MoveBtn) m_ma1MoveBtn->setEnabled(m_ma1.captured);
  if (m_ma2MoveBtn) m_ma2MoveBtn->setEnabled(m_ma2.captured);
  // Nominal MOE-A1 needs a1 captured (it's the reference for the offset).
  if (m_moveToMoeA1NominalBtn) m_moveToMoeA1NominalBtn->setEnabled(m_a1.captured);
  // Y +/- 3.25 are pure relative jogs - always available when idle.
  if (m_yPlus35Btn)  m_yPlus35Btn->setEnabled(true);
  if (m_yMinus35Btn) m_yMinus35Btn->setEnabled(true);
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

void M4AngleWidget::runInBackground(const QString& movingMsg, BackgroundTask task,
                                   std::function<void(bool)> onComplete) {
  if (m_moveInFlight) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(
        QStringLiteral("[busy] waiting for current move to finish"));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }

  m_moveInFlight = true;
  m_moveOnComplete = std::move(onComplete);
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
        // Consume the continuation (move out first so a re-entrant
        // runInBackground inside it can set a fresh one).
        if (m_moveOnComplete) {
          auto cb = std::move(m_moveOnComplete);
          m_moveOnComplete = nullptr;
          cb(ok);
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

QtCameraWindow* M4AngleWidget::findSwirBeamWindow() const {
  if (!m_context) return nullptr;
  QtCameraWindow* windows[] = { m_context->qtCameraWindow, m_context->qtCameraWindow2 };
  // Prefer a window already streaming so toggle-off always finds the live one,
  // even if its displayed camera changed after streaming started.
  for (QtCameraWindow* w : windows) {
    if (w && w->isBeamStreamActive()) return w;
  }
  // Otherwise the window currently displaying swir_camera (primary or, in dual
  // mode, secondary) — that's where swir frames are actually arriving.
  for (QtCameraWindow* w : windows) {
    if (!w) continue;
    if (w->getTargetCamera() == kSwirCamera) return w;
    if (w->isDualCameraMode() && w->getSecondaryCameraId() == kSwirCamera) return w;
  }
  return nullptr;
}

void M4AngleWidget::updateBeamStreamButton(bool on) {
  if (!m_beamStreamBtn) return;
  m_beamStreamBtn->setText(on ? "Beam stream: ON" : "Beam stream: OFF");
  m_beamStreamBtn->setStyleSheet(on
    ? "QPushButton { background-color: #43a047; color: white; font-weight: bold; padding: 6px 12px; border: none; border-radius: 3px; }"
      "QPushButton:hover { background-color: #388e3c; }"
      "QPushButton:pressed { background-color: #2e7d32; }"
    : "QPushButton { background-color: #455a64; color: #eeeeee; font-weight: bold; padding: 6px 12px; border: none; border-radius: 3px; }"
      "QPushButton:hover { background-color: #546e7a; }"
      "QPushButton:pressed { background-color: #37474f; }");
}

void M4AngleWidget::onToggleBeamStream() {
  QtCameraWindow* win = findSwirBeamWindow();
  if (!win) {
    updateBeamStreamButton(false);
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral(
        "[beam] no Qt camera monitor is showing swir_camera. Open one "
        "(Ctrl+R / Ctrl+T), select swir_camera, then retry."));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }

  const bool enable = !win->isBeamStreamActive();
  win->setBeamStreamEnabled(enable, kSwirCamera);
  const bool nowActive = win->isBeamStreamActive();
  updateBeamStreamButton(nowActive);

  if (m_moveStatusLabel) {
    QString msg;
    bool ok = true;
    if (enable && !nowActive) {
      msg = QStringLiteral(
        "[beam] failed to start swir_camera stream (could not reach beam "
        "server?). See console for details.");
      ok = false;
    } else {
      msg = nowActive
        ? QStringLiteral("[beam] swir_camera beam streaming enabled.")
        : QStringLiteral("[beam] swir_camera beam streaming disabled.");
    }
    m_moveStatusLabel->setText(msg);
    m_moveStatusLabel->setStyleSheet(ok ? "color: #4caf50;" : "color: #ef5350;");
  }
}

// ---------------------------------------------------------------------------
// SWIR-source PSU controls (minimal mirror of the SWIR monitor window).
// ---------------------------------------------------------------------------

void M4AngleWidget::rebuildPsuTabs() {
  if (!m_psuTabLayout) return;

  // Tear down any existing tab buttons (and their group membership).
  for (QPushButton* b : m_psuTabs) {
    if (m_psuTabGroup) m_psuTabGroup->removeButton(b);
    m_psuTabLayout->removeWidget(b);
    b->deleteLater();
  }
  m_psuTabs.clear();

  // Remove the trailing stretch (if any) so we can re-add it last.
  while (QLayoutItem* item = m_psuTabLayout->takeAt(0)) {
    delete item;  // only stretch spacers remain here; widgets were removed above
  }

  const char* kTabStyle =
    "QPushButton { background-color: #b9bec3; color: #1a1a1a; font-weight: bold; "
    "padding: 8px 18px; border: none; border-top-left-radius: 8px; "
    "border-top-right-radius: 8px; }"
    "QPushButton:hover { background-color: #cfd4d9; }"
    "QPushButton:checked { background-color: #19e0f7; color: #002b30; }";

  if (m_psuNames.empty()) {
    // Placeholder tab shown until devices register.
    auto* placeholder = new QPushButton("(no PSU)");
    placeholder->setEnabled(false);
    placeholder->setStyleSheet(kTabStyle);
    m_psuTabLayout->addWidget(placeholder);
    m_psuTabs.push_back(placeholder);
    m_psuTabLayout->addStretch(1);
    return;
  }

  for (std::size_t i = 0; i < m_psuNames.size(); ++i) {
    auto* tab = new QPushButton(QString::fromStdString(m_psuNames[i]));
    tab->setCheckable(true);
    tab->setStyleSheet(kTabStyle);
    tab->setToolTip("Power-supply device that feeds the SWIR source.");
    if (m_psuTabGroup) m_psuTabGroup->addButton(tab, static_cast<int>(i));
    m_psuTabLayout->addWidget(tab);
    m_psuTabs.push_back(tab);
  }
  m_psuTabLayout->addStretch(1);

  // Default to the first device.
  m_psuSelectedIndex = 0;
  m_psuTabs.front()->setChecked(true);
}

void M4AngleWidget::onPsuTabSelected(int index) {
  if (index < 0 || index >= static_cast<int>(m_psuNames.size())) return;
  m_psuSelectedIndex = index;
  // Switching device: drop stale readouts, then poll the newly-selected one.
  updatePsuReadouts(false, 0.0, 0.0, -1.0, false);
  pollSelectedPsu();
}

void M4AngleWidget::discoverPsuDevices() {
  if (m_psuDiscovered) return;
  if (!m_context || !m_context->powerSupplyManager) return;

  auto* mgr = m_context->powerSupplyManager;
  const std::vector<std::string> names = mgr->getDeviceNames();
  if (names.empty()) return;  // no devices registered yet — retry on next show

  m_psuNames = names;
  rebuildPsuTabs();
  m_psuDiscovered = true;

  // Background auto-connect of any disconnected device (same as SWIR monitor),
  // so the live readouts start populating without an explicit connect step.
  std::vector<std::string> disconnected;
  for (const std::string& name : names) {
    auto psu = mgr->getPowerSupply(name);
    if (psu && !psu->isConnected()) disconnected.push_back(name);
  }
  if (!disconnected.empty()) {
    m_psuConnectFuture = std::async(std::launch::async,
      [this, mgr, disconnected]() {
        for (const std::string& name : disconnected) {
          auto psu = mgr->getPowerSupply(name);
          if (!psu) continue;
          std::lock_guard<std::mutex> lock(m_psuHwMutex);
          psu->connect();
        }
        QMetaObject::invokeMethod(this, [this]() { pollSelectedPsu(); },
                                  Qt::QueuedConnection);
      });
  }
}

std::string M4AngleWidget::selectedPsuName() const {
  if (m_psuSelectedIndex < 0 ||
      m_psuSelectedIndex >= static_cast<int>(m_psuNames.size())) {
    return std::string();
  }
  return m_psuNames[static_cast<std::size_t>(m_psuSelectedIndex)];
}

void M4AngleWidget::setPsuControlsEnabled(bool enabled) {
  // Only the On/Off bar is briefly disabled while an action is in flight. The
  // +/- current presets stay enabled so they never appear greyed-out mid-
  // operation; overlapping clicks are already dropped by the m_psuActionInFlight
  // guard. (During the auto-correction wizard the whole PSU group box is
  // disabled, which still greys everything as intended.)
  if (m_psuOutputBtn) m_psuOutputBtn->setEnabled(enabled);
}

void M4AngleWidget::updatePsuOutputButton(bool connected, bool outputOn) {
  if (!m_psuOutputBtn) return;
  m_psuOutputBtn->setText(connected ? (outputOn ? "On/Off  —  ON" : "On/Off  —  OFF")
                                    : "On/Off");
  // Green when output is on; red bar (mockup) when connected-but-off; grey when
  // there is no link yet.
  const char* style;
  if (outputOn) {
    style =
      "QPushButton { background-color: #43a047; color: white; font-weight: bold; padding: 8px 12px; border: none; border-radius: 3px; }"
      "QPushButton:hover { background-color: #388e3c; }"
      "QPushButton:pressed { background-color: #2e7d32; }";
  } else if (connected) {
    style =
      "QPushButton { background-color: #e53935; color: white; font-weight: bold; padding: 8px 12px; border: none; border-radius: 3px; }"
      "QPushButton:hover { background-color: #d32f2f; }"
      "QPushButton:pressed { background-color: #c62828; }";
  } else {
    style =
      "QPushButton { background-color: #455a64; color: #eeeeee; font-weight: bold; padding: 8px 12px; border: none; border-radius: 3px; }"
      "QPushButton:hover { background-color: #546e7a; }"
      "QPushButton:pressed { background-color: #37474f; }";
  }
  m_psuOutputBtn->setStyleSheet(style);
}

void M4AngleWidget::updatePsuReadouts(bool connected, double voltageV,
                                      double currentA, double setCurrentA,
                                      bool outputOn) {
  if (!m_psuSetLabel) return;
  if (connected) {
    const double setMa = (setCurrentA < 0.0 ? 0.0 : setCurrentA) * 1000.0;
    m_psuSetLabel->setText(QStringLiteral("Set Current : %1 mA").arg(setMa, 0, 'f', 1));
    m_psuVoltageLabel->setText(QStringLiteral("Voltage: %1 V").arg(voltageV, 0, 'f', 3));
    m_psuCurrentLabel->setText(QStringLiteral("Current: %1 mA").arg(currentA * 1000.0, 0, 'f', 1));
    const char* live = "color: #cfd8dc;";
    m_psuSetLabel->setStyleSheet(live);
    m_psuVoltageLabel->setStyleSheet(live);
    m_psuCurrentLabel->setStyleSheet(live);
  } else {
    m_psuSetLabel->setText(QStringLiteral("Set Current : -- mA"));
    m_psuVoltageLabel->setText(QStringLiteral("Voltage: -- V"));
    m_psuCurrentLabel->setText(QStringLiteral("Current: -- mA"));
    const char* dim = "color: #888;";
    m_psuSetLabel->setStyleSheet(dim);
    m_psuVoltageLabel->setStyleSheet(dim);
    m_psuCurrentLabel->setStyleSheet(dim);
  }
  updatePsuOutputButton(connected, outputOn);
}

void M4AngleWidget::pollSelectedPsu() {
  if (!isVisible()) return;  // no VISA traffic while the tab is hidden
  if (!m_context || !m_context->powerSupplyManager) return;
  const std::string name = selectedPsuName();
  if (name.empty()) return;
  // Skip if a previous poll is still in flight (VISA reads can be slow).
  if (m_psuPollInFlight.exchange(true)) return;

  auto* mgr = m_context->powerSupplyManager;
  m_psuPollFuture = std::async(std::launch::async, [this, mgr, name]() {
    bool   connected = false, outputOn = false;
    double v = 0.0, c = 0.0, setc = -1.0;
    {
      auto psu = mgr->getPowerSupply(name);
      if (psu && psu->isConnected()) {
        std::lock_guard<std::mutex> lock(m_psuHwMutex);
        connected = true;
        v        = psu->readVoltage(kPsuChannel);
        c        = psu->readCurrent(kPsuChannel);
        setc     = psu->readSetCurrent(kPsuChannel);
        outputOn = psu->readOutputState(kPsuChannel);
      }
    }
    QMetaObject::invokeMethod(this,
      [this, connected, v, c, setc, outputOn]() {
        m_psuPollInFlight = false;
        updatePsuReadouts(connected, v, c, setc, outputOn);
      }, Qt::QueuedConnection);
  });
}

void M4AngleWidget::onPsuCurrentStep(double deltaMa) {
  if (!m_context || !m_context->powerSupplyManager) return;
  const std::string name = selectedPsuName();
  if (name.empty()) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral(
        "[psu] no power supply available. Devices appear once registered."));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }
  if (m_psuActionInFlight.exchange(true)) return;  // ignore clicks while busy
  setPsuControlsEnabled(false);

  auto* mgr = m_context->powerSupplyManager;
  m_psuActionFuture = std::async(std::launch::async, [this, mgr, name, deltaMa]() {
    bool    ok = false;
    QString msg;
    {
      auto psu = mgr->getPowerSupply(name);
      if (!psu) {
        msg = QStringLiteral("[psu] device '%1' not found.").arg(QString::fromStdString(name));
      } else {
        std::lock_guard<std::mutex> lock(m_psuHwMutex);
        if (!psu->isConnected()) psu->connect();
        if (!psu->isConnected()) {
          msg = QStringLiteral("[psu] %1 not connected.").arg(QString::fromStdString(name));
        } else {
          double setA = psu->readSetCurrent(kPsuChannel);
          if (setA < 0.0) setA = 0.0;
          double newA = setA + deltaMa / 1000.0;
          if (newA < 0.0) newA = 0.0;
          ok = psu->setCurrent(newA, kPsuChannel);
          msg = ok
            ? QStringLiteral("[psu] %1 set current -> %2 mA.")
                .arg(QString::fromStdString(name)).arg(newA * 1000.0, 0, 'f', 1)
            : QStringLiteral("[psu] %1 set current FAILED.")
                .arg(QString::fromStdString(name));
        }
      }
    }
    QMetaObject::invokeMethod(this, [this, ok, msg]() {
      m_psuActionInFlight = false;
      setPsuControlsEnabled(true);
      if (m_moveStatusLabel) {
        m_moveStatusLabel->setText(msg);
        m_moveStatusLabel->setStyleSheet(ok ? "color: #4caf50;" : "color: #ef5350;");
      }
      pollSelectedPsu();  // reflect the new set-point immediately
    }, Qt::QueuedConnection);
  });
}

void M4AngleWidget::onPsuOutputToggle() {
  if (!m_context || !m_context->powerSupplyManager) return;
  const std::string name = selectedPsuName();
  if (name.empty()) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral(
        "[psu] no power supply available. Devices appear once registered."));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }
  if (m_psuActionInFlight.exchange(true)) return;
  setPsuControlsEnabled(false);

  auto* mgr = m_context->powerSupplyManager;
  m_psuActionFuture = std::async(std::launch::async, [this, mgr, name]() {
    bool    ok = false;
    QString msg;
    {
      auto psu = mgr->getPowerSupply(name);
      if (!psu) {
        msg = QStringLiteral("[psu] device '%1' not found.").arg(QString::fromStdString(name));
      } else {
        std::lock_guard<std::mutex> lock(m_psuHwMutex);
        const QString dev = QString::fromStdString(name);
        if (!psu->isConnected()) {
          // First use: connect, initialise to sane SWIR-source defaults, turn on
          // — exactly what the SWIR monitor's OUTPUT toggle does on first press.
          if (!psu->connect()) {
            msg = QStringLiteral("[psu] %1 connect FAILED.").arg(dev);
          } else {
            psu->setVoltage(kPsuDefaultVoltage, kPsuChannel);
            psu->setCurrent(kPsuDefaultCurrent, kPsuChannel);
            ok = psu->setOutputOn(kPsuChannel);
            msg = ok
              ? QStringLiteral("[psu] %1 connected; ch1 set 2.0 V / 300 mA, output ON.").arg(dev)
              : QStringLiteral("[psu] %1 connected but output-on FAILED.").arg(dev);
          }
        } else {
          const bool on = psu->readOutputState(kPsuChannel);
          ok = on ? psu->setOutputOff(kPsuChannel) : psu->setOutputOn(kPsuChannel);
          msg = ok
            ? QStringLiteral("[psu] %1 output %2.").arg(dev, on ? QStringLiteral("OFF") : QStringLiteral("ON"))
            : QStringLiteral("[psu] %1 output toggle FAILED.").arg(dev);
        }
      }
    }
    QMetaObject::invokeMethod(this, [this, ok, msg]() {
      m_psuActionInFlight = false;
      setPsuControlsEnabled(true);
      if (m_moveStatusLabel) {
        m_moveStatusLabel->setText(msg);
        m_moveStatusLabel->setStyleSheet(ok ? "color: #4caf50;" : "color: #ef5350;");
      }
      pollSelectedPsu();
    }, Qt::QueuedConnection);
  });
}

void M4AngleWidget::onSaveMoeA1() {
  saveMoeNamedPosition("MOE-A1", m_ma1, "ma1");
}

void M4AngleWidget::onSaveMoeA2() {
  saveMoeNamedPosition("MOE-A2", m_ma2, "ma2");
}

void M4AngleWidget::saveMoeNamedPosition(const char* moeName,
                                         const Point& referencePoint,
                                         const char* referenceTag,
                                         bool enforceTolerance) {
  if (!m_context || !m_context->motionManager) return;
  auto* motionMgr  = m_context->motionManager;
  auto* productMgr = m_context->productPositionManager;

  // Need the reference (ma1 / ma2) captured to know what "close enough" means
  // (only when the tolerance guard is active).
  if (enforceTolerance && !referencePoint.captured) {
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

  if (enforceTolerance && dist > kMoeSaveToleranceMm) {
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

void M4AngleWidget::applyHexRightCorrection(double deltaDeg, char axisLetter,
                                            std::function<void(bool)> onComplete) {
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
    },
    std::move(onComplete));
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
  if (!m_context || !m_context->motionManager) return;
  auto* motionMgr = m_context->motionManager;

  PositionStruct target;
  target.x = m_a1.x + kMoeNominalDxMm;
  target.y = m_a1.y;
  target.z = m_a1.z + kMoeNominalDzMm;

  const QString movingMsg = QStringLiteral(
    "[MOE] moving to MOE-A1 nominal (a1 + dX=-1.50 mm, dZ=-0.15 mm)...");

  // After the blocking move returns ok, queue an auto-capture of ma1 on the UI
  // thread so the operator does not need (and the Capture ma1 button is no
  // longer wired) to click capture themselves.
  runInBackground(movingMsg,
    [this, motionMgr, target]() -> std::pair<bool, QString> {
      const bool ok = motionMgr->MoveToAbs6(kGantryDevice, target, true);
      if (ok) {
        QMetaObject::invokeMethod(this, [this]() { onCaptureMa1(); },
                                  Qt::QueuedConnection);
        return {true, QStringLiteral(
          "[MOE] At MOE-A1 nominal. ma1 auto-captured from current gantry-main position.")};
      }
      return {false, QStringLiteral("[MOE] move to MOE-A1 nominal FAILED")};
    });
}

void M4AngleWidget::onMoveYPlus35() {
  if (!m_context || !m_context->motionManager) return;
  auto* motionMgr = m_context->motionManager;
  runInBackground(QStringLiteral("[jog] gantry-main Y +3.25 mm..."),
    [motionMgr]() -> std::pair<bool, QString> {
      const bool ok = motionMgr->MoveRelative(kGantryDevice, "Y", 3.25, true);
      return {ok, ok ? QStringLiteral("[jog] gantry-main Y +3.25 mm done.")
                     : QStringLiteral("[jog] gantry-main Y +3.25 mm FAILED.")};
    });
}

void M4AngleWidget::onMoveYMinus35() {
  if (!m_context || !m_context->motionManager) return;
  auto* motionMgr = m_context->motionManager;
  runInBackground(QStringLiteral("[jog] gantry-main Y -3.25 mm..."),
    [motionMgr]() -> std::pair<bool, QString> {
      const bool ok = motionMgr->MoveRelative(kGantryDevice, "Y", -3.25, true);
      return {ok, ok ? QStringLiteral("[jog] gantry-main Y -3.25 mm done.")
                     : QStringLiteral("[jog] gantry-main Y -3.25 mm FAILED.")};
    });
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

// ---------------------------------------------------------------------------
// Miniature gantry-main jog panel.
// ---------------------------------------------------------------------------

QWidget* M4AngleWidget::buildGantryJogPanel() {
  auto* group = new QGroupBox("Jog gantry-main");
  auto* v = new QVBoxLayout(group);
  v->setContentsMargins(10, 10, 10, 10);
  v->setSpacing(8);

  // Step-size selector (2 / 5 / 10 um). Exclusive highlight via selectJogStep().
  auto* stepRow = new QHBoxLayout();
  stepRow->setSpacing(6);
  const double stepVals[3]  = {2.0, 5.0, 10.0};
  const char*  stepLabels[3] = {"2um", "5um", "10um"};
  for (int i = 0; i < 3; ++i) {
    auto* b = new QPushButton(stepLabels[i]);
    b->setCheckable(true);
    b->setMinimumSize(56, 34);
    m_jogStepBtns[i] = b;
    const double val = stepVals[i];
    connect(b, &QPushButton::clicked, this, [this, val]() { selectJogStep(val); });
    stepRow->addWidget(b);
  }
  stepRow->addStretch(1);
  v->addLayout(stepRow);

  // Arrow pads: X/Y (teal) on the left, Z (blue) on the right.
  const char* kTeal =
    "QPushButton { background-color:#4f8f8f; color:#10242a; font-weight:bold; font-size:16px; min-width:44px; min-height:44px; border:none; border-radius:3px; }"
    "QPushButton:hover { background-color:#5fa3a3; }"
    "QPushButton:pressed { background-color:#3f7373; }"
    "QPushButton:disabled { background-color:#3a4a4a; color:#6a7a7a; }";
  const char* kBlue =
    "QPushButton { background-color:#6aa0d8; color:#0a1a2a; font-weight:bold; font-size:16px; min-width:44px; min-height:44px; border:none; border-radius:3px; }"
    "QPushButton:hover { background-color:#7fb0e0; }"
    "QPushButton:pressed { background-color:#5a90c8; }"
    "QPushButton:disabled { background-color:#3a4656; color:#6a7686; }";

  m_jogXPlusBtn  = new QPushButton(QString::fromUtf8("\xE2\x86\x91"));  // up    = X+
  m_jogXMinusBtn = new QPushButton(QString::fromUtf8("\xE2\x86\x93"));  // down  = X-
  m_jogYMinusBtn = new QPushButton(QString::fromUtf8("\xE2\x86\x90"));  // left  = Y-
  m_jogYPlusBtn  = new QPushButton(QString::fromUtf8("\xE2\x86\x92"));  // right = Y+
  m_jogZMinusBtn = new QPushButton(QString::fromUtf8("\xE2\x86\x91"));  // up    = Z-
  m_jogZPlusBtn  = new QPushButton(QString::fromUtf8("\xE2\x86\x93"));  // down  = Z+
  for (QPushButton* b : {m_jogXPlusBtn, m_jogXMinusBtn, m_jogYMinusBtn, m_jogYPlusBtn})
    b->setStyleSheet(kTeal);
  for (QPushButton* b : {m_jogZMinusBtn, m_jogZPlusBtn})
    b->setStyleSheet(kBlue);
  m_jogXPlusBtn->setToolTip("gantry-main X+");
  m_jogXMinusBtn->setToolTip("gantry-main X-");
  m_jogYMinusBtn->setToolTip("gantry-main Y-");
  m_jogYPlusBtn->setToolTip("gantry-main Y+");
  m_jogZMinusBtn->setToolTip("gantry-main Z-");
  m_jogZPlusBtn->setToolTip("gantry-main Z+");

  auto* xy = new QGridLayout();
  xy->setSpacing(4);
  xy->addWidget(m_jogXPlusBtn,  0, 1);   // top-centre
  xy->addWidget(m_jogYMinusBtn, 1, 0);
  xy->addWidget(m_jogXMinusBtn, 1, 1);
  xy->addWidget(m_jogYPlusBtn,  1, 2);
  auto* z = new QGridLayout();
  z->setSpacing(4);
  z->addWidget(m_jogZMinusBtn, 0, 0);
  z->addWidget(m_jogZPlusBtn,  1, 0);

  auto* pads = new QHBoxLayout();
  pads->setSpacing(10);
  pads->addLayout(xy);
  pads->addSpacing(8);
  pads->addLayout(z, 0);
  pads->setAlignment(z, Qt::AlignTop);
  pads->addStretch(1);
  v->addLayout(pads);

  connect(m_jogXPlusBtn,  &QPushButton::clicked, this, [this]() { gantryJog("X", +m_jogStepUm / 1000.0); });
  connect(m_jogXMinusBtn, &QPushButton::clicked, this, [this]() { gantryJog("X", -m_jogStepUm / 1000.0); });
  connect(m_jogYPlusBtn,  &QPushButton::clicked, this, [this]() { gantryJog("Y", +m_jogStepUm / 1000.0); });
  connect(m_jogYMinusBtn, &QPushButton::clicked, this, [this]() { gantryJog("Y", -m_jogStepUm / 1000.0); });
  connect(m_jogZPlusBtn,  &QPushButton::clicked, this, [this]() { gantryJog("Z", +m_jogStepUm / 1000.0); });
  connect(m_jogZMinusBtn, &QPushButton::clicked, this, [this]() { gantryJog("Z", -m_jogStepUm / 1000.0); });

  selectJogStep(m_jogStepUm);  // paint the initial (2 um) selection
  return group;
}

void M4AngleWidget::selectJogStep(double stepUm) {
  m_jogStepUm = stepUm;
  const double vals[3] = {2.0, 5.0, 10.0};
  for (int i = 0; i < 3; ++i) {
    if (!m_jogStepBtns[i]) continue;
    const bool sel = (std::abs(vals[i] - stepUm) < 1e-9);
    m_jogStepBtns[i]->setChecked(sel);
    m_jogStepBtns[i]->setStyleSheet(sel
      ? "QPushButton { background-color:#1976d2; color:white; font-weight:bold; border:none; border-radius:3px; }"
      : "QPushButton { background-color:#c2c7cc; color:#1a1a1a; font-weight:bold; border:none; border-radius:3px; }"
        "QPushButton:hover { background-color:#d4d9de; }");
  }
}

void M4AngleWidget::setJogButtonsEnabled(bool enabled) {
  for (QPushButton* b : {m_jogXPlusBtn, m_jogXMinusBtn, m_jogYPlusBtn,
                         m_jogYMinusBtn, m_jogZPlusBtn, m_jogZMinusBtn}) {
    if (b) b->setEnabled(enabled);
  }
}

void M4AngleWidget::gantryJog(const std::string& axis, double deltaMm) {
  if (!m_context || !m_context->motionManager) return;
  auto* motionMgr = m_context->motionManager;
  const QString axisQ = QString::fromStdString(axis);
  const QString msg = QStringLiteral("[jog] gantry-main %1 %2 um...")
    .arg(axisQ).arg(deltaMm * 1000.0, 0, 'f', 1);
  runInBackground(msg,
    [motionMgr, axis, deltaMm, axisQ]() -> std::pair<bool, QString> {
      const bool ok = motionMgr->MoveRelative(kGantryDevice, axis, deltaMm, true);
      return {ok, ok
        ? QStringLiteral("[jog] gantry-main %1 %2 um done.").arg(axisQ).arg(deltaMm * 1000.0, 0, 'f', 1)
        : QStringLiteral("[jog] gantry-main %1 jog FAILED.").arg(axisQ)};
    });
}

// ---------------------------------------------------------------------------
// Auto-correction wizard.
// ---------------------------------------------------------------------------

std::string M4AngleWidget::thumbnailDir() const {
  if (!m_context || !m_context->productPositionManager) return std::string();
  auto* productMgr = m_context->productPositionManager;
  if (!productMgr->HasLoadedProduct()) return std::string();
  return productMgr->GetCurrentProductPath() + "/m4_thumbnails";
}

void M4AngleWidget::loadThumbnailForStep(const char* tag) {
  if (!m_autoThumbnailLabel) return;
  auto showPlaceholder = [this]() {
    m_autoThumbnailLabel->setPixmap(QPixmap());
    m_autoThumbnailLabel->setText("(no thumbnail)");
  };
  const std::string dir = thumbnailDir();
  if (dir.empty()) { showPlaceholder(); return; }
  const std::string path = dir + "/thumbnail_" + tag + ".bmp";
  if (!std::filesystem::exists(path)) { showPlaceholder(); return; }

  QPixmap pm(QString::fromStdString(path));
  if (pm.isNull()) { showPlaceholder(); return; }
  m_autoThumbnailLabel->setText(QString());
  m_autoThumbnailLabel->setPixmap(
    pm.scaled(m_autoThumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void M4AngleWidget::saveThumbnail(const char* tag) {
  if (!m_context || !m_context->cameraManager) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral("[auto] camera manager unavailable; thumbnail skipped."));
      m_moveStatusLabel->setStyleSheet("color: #ef5350;");
    }
    return;
  }
  const std::string dir = thumbnailDir();
  if (dir.empty()) {
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral("[auto] no product loaded; thumbnail not saved."));
      m_moveStatusLabel->setStyleSheet("color: #888;");
    }
    return;
  }

  const std::string filename = std::string("thumbnail_") + tag + ".bmp";
  const std::string fullPath = dir + "/" + filename;
  const bool exists    = std::filesystem::exists(fullPath);
  const bool overwrite = m_autoOverwriteChk && m_autoOverwriteChk->isChecked();

  // Keep the existing reference image when overwrite is off.
  if (exists && !overwrite) {
    loadThumbnailForStep(tag);
    if (m_moveStatusLabel) {
      m_moveStatusLabel->setText(QStringLiteral(
        "[auto] kept existing thumbnail_%1 (overwrite off).").arg(tag));
      m_moveStatusLabel->setStyleSheet("color: #888;");
    }
    return;
  }

  // SetImageOutputDirectory creates the directory; CaptureImage grabs a SWIR
  // frame and writes it as BMP under that directory with our custom name.
  m_context->cameraManager->SetImageOutputDirectory(dir);
  const bool ok = m_context->cameraManager->CaptureImage(kSwirCamera, filename);
  loadThumbnailForStep(tag);
  if (m_moveStatusLabel) {
    m_moveStatusLabel->setText(ok
      ? QStringLiteral("[auto] saved thumbnail_%1 from swir_camera.").arg(tag)
      : QStringLiteral("[auto] thumbnail_%1 capture FAILED (swir_camera not grabbing?).").arg(tag));
    m_moveStatusLabel->setStyleSheet(ok ? "color: #4caf50;" : "color: #ef5350;");
  }
}

void M4AngleWidget::setAutoModeUiLock(bool locked) {
  // Lock the capture + PSU groups so the operator can only drive the wizard via
  // its own Capture / Abort buttons. The computed-deltas group is intentionally
  // NOT locked, so the miniature jog panel beside it stays usable during the
  // teach/focus steps; the Apply U/W buttons are instead gated by the
  // m_autoActive guard inside refreshApplyButtonsEnabledState().
  if (m_captureGroupBox) m_captureGroupBox->setEnabled(!locked);
  if (m_psuGroupBox)     m_psuGroupBox->setEnabled(!locked);
  if (!locked) refreshMoveButtonsFromCaptureState();
  refreshApplyButtonsEnabledState();
}

void M4AngleWidget::updateAutoUi(AutoStep step, const QString& instruction) {
  if (m_autoInstructionLabel && !instruction.isEmpty()) {
    m_autoInstructionLabel->setText(instruction);
  }
  if (!m_autoStepChips[0]) return;

  // Per-step chip states: 0=pending, 1=current, 2=done. Chips are A1,A2,MA1,MA2.
  int s[4] = {0, 0, 0, 0};
  switch (step) {
    case AutoStep::Idle:                                              break;
    case AutoStep::TeachA1:  s[0]=1;                                  break;
    case AutoStep::TeachA2:  s[0]=2; s[1]=1;                          break;
    case AutoStep::TeachMa1: s[0]=2; s[1]=2; s[2]=1;                  break;
    case AutoStep::TeachMa2: s[0]=2; s[1]=2; s[2]=2; s[3]=1;          break;
    case AutoStep::ApplyU:
    case AutoStep::ApplyW:
    case AutoStep::SetExposure: s[0]=s[1]=s[2]=s[3]=2;                break;
    case AutoStep::FocusMoeA1: s[0]=2; s[1]=2; s[2]=1; s[3]=2;        break;
    case AutoStep::FocusMoeA2: s[0]=2; s[1]=2; s[2]=2; s[3]=1;        break;
    case AutoStep::Done:     s[0]=s[1]=s[2]=s[3]=2;                   break;
  }
  const char* kBase =
    "QLabel { border-radius: 22px; font-weight: bold; %1 }";
  for (int i = 0; i < 4; ++i) {
    const char* fill = (s[i] == 1) ? "background-color:#ffea00; color:#1a1a1a;"
                     : (s[i] == 2) ? "background-color:#43a047; color:white;"
                                   : "background-color:#555; color:#bbb;";
    m_autoStepChips[i]->setStyleSheet(QString(kBase).arg(fill));
  }
}

void M4AngleWidget::onAutoStart() {
  if (m_autoActive) return;
  if (!m_context || !m_context->motionManager) {
    if (m_autoInstructionLabel)
      m_autoInstructionLabel->setText("Motion manager unavailable — cannot start.");
    return;
  }
  // Need a1/a2 already taught so the wizard can navigate to them.
  if (!m_a1.captured || !m_a2.captured) {
    if (m_autoInstructionLabel)
      m_autoInstructionLabel->setText(
        "Teach a1 and a2 first (Capture a1 and a2) — they are the navigation "
        "references for auto-correction.");
    return;
  }

  m_autoActive = true;
  m_autoAwaitingCapture = false;
  setAutoModeUiLock(true);
  if (m_autoStartBtn)   m_autoStartBtn->setEnabled(false);
  if (m_autoAbortBtn)   m_autoAbortBtn->setEnabled(true);
  if (m_autoCaptureBtn) m_autoCaptureBtn->setEnabled(false);
  autoEnterStep(AutoStep::TeachA1);
}

void M4AngleWidget::onAutoAbort() {
  if (!m_autoActive) return;
  m_autoActive = false;
  m_autoAwaitingCapture = false;
  m_autoStep = AutoStep::Idle;
  // Drop any pending continuation so an in-flight move does not advance us.
  m_moveOnComplete = nullptr;
  setAutoModeUiLock(false);
  if (m_autoStartBtn)   m_autoStartBtn->setEnabled(true);
  if (m_autoAbortBtn)   m_autoAbortBtn->setEnabled(false);
  if (m_autoCaptureBtn) m_autoCaptureBtn->setEnabled(false);
  updateAutoUi(AutoStep::Idle, "Auto correction aborted. Manual controls re-enabled.");
}

void M4AngleWidget::autoMoveThenAwait(const PositionStruct& target,
                                      const QString& movingMsg,
                                      const QString& awaitInstruction) {
  if (!m_context || !m_context->motionManager) return;
  auto* motionMgr = m_context->motionManager;
  if (m_autoCaptureBtn) m_autoCaptureBtn->setEnabled(false);
  runInBackground(movingMsg,
    [motionMgr, target]() -> std::pair<bool, QString> {
      const bool ok = motionMgr->MoveToAbs6(kGantryDevice, target, true);
      return {ok, ok ? QStringLiteral("[auto] arrived.")
                     : QStringLiteral("[auto] move FAILED.")};
    },
    [this, awaitInstruction](bool ok) {
      if (!m_autoActive) return;  // aborted while moving
      if (!ok) {
        if (m_autoInstructionLabel)
          m_autoInstructionLabel->setText("[auto] move failed — aborting.");
        onAutoAbort();
        return;
      }
      m_autoAwaitingCapture = true;
      setAutoModeUiLock(true);  // re-assert lock (the move re-enabled buttons)
      if (m_autoCaptureBtn) m_autoCaptureBtn->setEnabled(true);
      updateAutoUi(m_autoStep, awaitInstruction);
    });
}

void M4AngleWidget::autoEnterStep(AutoStep step) {
  m_autoStep = step;
  m_autoAwaitingCapture = false;
  if (m_autoActive) setAutoModeUiLock(true);

  switch (step) {
    case AutoStep::TeachA1: {
      loadThumbnailForStep("a1");
      PositionStruct t; t.x = m_a1.x; t.y = m_a1.y; t.z = m_a1.z;
      updateAutoUi(step, "Step 1/4 — moving gantry-main to A1...");
      autoMoveThenAwait(t, "[auto] moving to A1...",
        "Step 1/4 — verify gantry-main alignment at A1, then click Capture.");
      break;
    }
    case AutoStep::TeachA2: {
      loadThumbnailForStep("a2");
      PositionStruct t; t.x = m_a2.x; t.y = m_a2.y; t.z = m_a2.z;
      updateAutoUi(step, "Step 2/4 — moving gantry-main to A2...");
      autoMoveThenAwait(t, "[auto] moving to A2...",
        "Step 2/4 — verify gantry-main alignment at A2, then click Capture.");
      break;
    }
    case AutoStep::TeachMa1: {
      loadThumbnailForStep("ma1");
      PositionStruct t;
      t.x = m_a1.x + kMoeNominalDxMm; t.y = m_a1.y; t.z = m_a1.z + kMoeNominalDzMm;
      updateAutoUi(step, "Step 3/4 — moving gantry-main to MA1 (a1 + nominal MOE-A1)...");
      autoMoveThenAwait(t, "[auto] moving to MA1...",
        "Step 3/4 — move hex-right into position at MA1, then click Capture.");
      break;
    }
    case AutoStep::TeachMa2: {
      loadThumbnailForStep("ma2");
      PositionStruct t;
      t.x = m_a2.x + kMoeNominalDxMm; t.y = m_a2.y; t.z = m_a2.z + kMoeNominalDzMm;
      updateAutoUi(step, "Step 4/4 — moving gantry-main to MA2 (a2 + nominal MOE-A1)...");
      autoMoveThenAwait(t, "[auto] moving to MA2...",
        "Step 4/4 — verify alignment at MA2, then click Capture.");
      break;
    }
    case AutoStep::ApplyU: {
      updateAutoUi(step, "[auto] applying U correction to hex-right...");
      const double deltaU = lineAngleDegXY(m_a1, m_a2) - lineAngleDegXY(m_ma1, m_ma2);
      applyHexRightCorrection(deltaU, 'U',
        [this](bool) { if (m_autoActive) autoAdvance(); });
      break;
    }
    case AutoStep::ApplyW: {
      updateAutoUi(step, "[auto] applying W correction to hex-right...");
      const double deltaW = lineRollDeg(m_a1, m_a2) - lineRollDeg(m_ma1, m_ma2);
      applyHexRightCorrection(deltaW, 'W',
        [this](bool) { if (m_autoActive) autoAdvance(); });
      break;
    }
    case AutoStep::SetExposure: {
      updateAutoUi(step, "[auto] setting swir_camera exposure to 300 us...");
      onSwirSet300us();      // synchronous
      autoAdvance();         // proceed to the focus phase
      break;
    }
    case AutoStep::FocusMoeA1: {
      loadThumbnailForStep("ma1");
      PositionStruct t; t.x = m_ma1.x; t.y = m_ma1.y; t.z = m_ma1.z;
      updateAutoUi(step, "[auto] moving gantry-main to MA1 for MOE-A1 focus...");
      autoMoveThenAwait(t, "[auto] moving to MA1 (focus)...",
        "Adjust gantry-main until Tx is at the beam-focus waist on MOE-A1, then click Capture.");
      break;
    }
    case AutoStep::FocusMoeA2: {
      loadThumbnailForStep("ma2");
      PositionStruct t; t.x = m_ma2.x; t.y = m_ma2.y; t.z = m_ma2.z;
      updateAutoUi(step, "[auto] moving gantry-main to MA2 for MOE-A2 focus...");
      autoMoveThenAwait(t, "[auto] moving to MA2 (focus)...",
        "Adjust gantry-main until Tx is at the beam-focus waist on MOE-A2, then click Capture.");
      break;
    }
    case AutoStep::Done: {
      m_autoActive = false;
      m_autoAwaitingCapture = false;
      setAutoModeUiLock(false);
      if (m_autoStartBtn)   m_autoStartBtn->setEnabled(true);
      if (m_autoAbortBtn)   m_autoAbortBtn->setEnabled(false);
      if (m_autoCaptureBtn) m_autoCaptureBtn->setEnabled(false);
      updateAutoUi(step, "Auto correction complete — MOE-A1 and MOE-A2 saved.");
      break;
    }
    case AutoStep::Idle:
      break;
  }
}

void M4AngleWidget::autoAdvance() {
  m_autoAwaitingCapture = false;
  AutoStep next = AutoStep::Done;
  switch (m_autoStep) {
    case AutoStep::TeachA1:     next = AutoStep::TeachA2;     break;
    case AutoStep::TeachA2:     next = AutoStep::TeachMa1;    break;
    case AutoStep::TeachMa1:    next = AutoStep::TeachMa2;    break;
    case AutoStep::TeachMa2:    next = AutoStep::ApplyU;      break;
    case AutoStep::ApplyU:      next = AutoStep::ApplyW;      break;
    case AutoStep::ApplyW:      next = AutoStep::SetExposure; break;
    case AutoStep::SetExposure: next = AutoStep::FocusMoeA1;  break;
    case AutoStep::FocusMoeA1:  next = AutoStep::FocusMoeA2;  break;
    case AutoStep::FocusMoeA2:  next = AutoStep::Done;        break;
    default:                    next = AutoStep::Done;        break;
  }
  autoEnterStep(next);
}

void M4AngleWidget::onAutoCapture() {
  if (!m_autoActive || !m_autoAwaitingCapture) return;
  m_autoAwaitingCapture = false;
  if (m_autoCaptureBtn) m_autoCaptureBtn->setEnabled(false);

  // Run the current step's capture/save action, then advance.
  switch (m_autoStep) {
    case AutoStep::TeachA1:
      capture(m_a1, m_a1Readout, m_a1MoveBtn, "a1");  saveThumbnail("a1");  recompute();  break;
    case AutoStep::TeachA2:
      capture(m_a2, m_a2Readout, m_a2MoveBtn, "a2");  saveThumbnail("a2");  recompute();  break;
    case AutoStep::TeachMa1:
      capture(m_ma1, m_ma1Readout, m_ma1MoveBtn, "ma1"); saveThumbnail("ma1"); recompute(); break;
    case AutoStep::TeachMa2:
      capture(m_ma2, m_ma2Readout, m_ma2MoveBtn, "ma2"); saveThumbnail("ma2"); recompute(); break;
    case AutoStep::FocusMoeA1:
      saveMoeNamedPosition("MOE-A1", m_ma1, "ma1", /*enforceTolerance=*/false);  break;
    case AutoStep::FocusMoeA2:
      saveMoeNamedPosition("MOE-A2", m_ma2, "ma2", /*enforceTolerance=*/false);  break;
    default:
      return;  // not a capture step
  }
  autoAdvance();
}

// PlacementRetraceWidget.cpp - Retrace wizard for placement correction
//
// 6-state wizard: Idle → WaitRefP1 → WaitRefP2 → WaitNrmP1 → WaitNrmP2 → Review
//
// Key methods and their roles:
//   autoNavigateToRefP1/NrmP1  — MoveToAbs to taught P1 positions (async)
//   onConfirmRefP1/NrmP1       — Capture current position, MoveToAbs to P2 via delta
//   onConfirmRefP2/NrmP2       — Capture P2, compute line angle, advance state
//   computeRetraceResults      — Core math: angle delta, XY correction, matrix transform
//   onSaveToTarget             — Write corrected XYZUVW to motor_devices.json
//   onMoveToTarget             — Move hex to saved position (user-initiated)
//   onSkip                     — Continue recipe without saving (or after saving)
//   onEnd                      — Abort retrace, recipe stops
//
// See PlacementRetraceWidget.h header comments for recipe integration details.
#include "PlacementRetraceWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "CoordinateTransformer.h"
#include "MotionJsonLoader.h"
#include "CameraManager.h"
#include "product_position_manager.h"
#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include <QDateTime>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QPixmap>
#include <QPainter>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <thread>

PlacementRetraceWidget::PlacementRetraceWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
    , m_motionPollTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &PlacementRetraceWidget::updatePositionDisplay);
    connect(m_motionPollTimer, &QTimer::timeout, this, [this]() {
        if (m_motionFuture.valid() &&
            m_motionFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            m_motionPollTimer->stop();
            bool result = m_motionFuture.get();
            if (m_motionCallback) m_motionCallback(result);
        }
    });

    m_matrixLoaded = m_matrixLoader.LoadFromFile("config/transformation_matrix.json");
    if (!m_matrixLoaded) {
        m_matrixLoaded = m_matrixLoader.LoadFromFile("config/transformation_matrix.local.json");
    }
}

void PlacementRetraceWidget::SetData(PlacementCorrectionSet* set)
{
    m_set = set;
    m_autoDetectFromRecipe = false;
    m_autoSaveContinue = false;
    setState(Idle);
    loadTuneOffsetsFromConfig();
}

void PlacementRetraceWidget::AutoStart()
{
    if (!(m_state == Idle && m_set)) return;

    // Recipe-mode safety: if the set is not retraceable (v1/v2 without normal
    // placement, or missing ref/normal teach), emit retraceEnded immediately
    // so RunProgramPage's QEventLoop unblocks and the recipe fails cleanly
    // instead of hanging indefinitely.
    if (!m_set->designOffsetValid
        || !m_set->referenceLine.valid
        || !m_set->normalPlacementLine.valid) {
        std::cerr << "[PlacementRetrace] AutoStart refused: set '"
                  << m_set->name
                  << "' not retraceable (designOffsetValid="
                  << m_set->designOffsetValid
                  << " refValid=" << m_set->referenceLine.valid
                  << " nrmValid=" << m_set->normalPlacementLine.valid
                  << ")" << std::endl;
        writeLogEntry("failed", "preflight",
                      "designOffsetValid/ref/nrm not all true");
        // Stay on Idle so the UI shows the "Cannot retrace" summary, but
        // emit retraceEnded so the recipe's event loop exits with failure.
        emit retraceEnded();
        return;
    }

    if (m_autoDetectFromRecipe) {
        m_autoRunAll = true;
    }

    onStartRetrace();
}

void PlacementRetraceWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Retrace & Review");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    m_wizardStack = new QStackedWidget();

    QString blueBtnStyle =
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    QString greenBtnStyle =
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    QString orangeBtnStyle =
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #f39c12; }";

    QString redStopBtnStyle =
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }";

    auto makeStopBtn = [this, &redStopBtnStyle]() {
        auto* b = new QPushButton("Stop");
        b->setFixedHeight(44);
        b->setFixedWidth(120);
        b->setStyleSheet(redStopBtnStyle);
        b->setToolTip("Abort retrace and return to Idle");
        connect(b, &QPushButton::clicked, this, &PlacementRetraceWidget::onEnd);
        return b;
    };

    auto makeAutoDetectBtn = [this, &orangeBtnStyle](void (PlacementRetraceWidget::*slot)()) {
        auto* b = new QPushButton("Auto-Detect");
        b->setFixedHeight(44);
        b->setFixedWidth(140);
        b->setStyleSheet(orangeBtnStyle);
        b->setToolTip("Run vision profile to auto-locate the feature (iterates until <5 um)");
        b->setVisible(false);
        connect(b, &QPushButton::clicked, this, slot);
        return b;
    };

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    QString posLabelStyle = "font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;";
    QString instructionStyle =
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;";

    // ========================================
    // Page 0: Idle
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_idleSummaryLabel = new QLabel("Load a training set with taught ref/target lines to begin retrace.");
    m_idleSummaryLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_idleSummaryLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleSummaryLabel);

    idleLayout->addStretch();

    {
        QHBoxLayout* startRow = new QHBoxLayout();
        m_startBtn = new QPushButton("Start Retrace");
        m_startBtn->setFixedSize(180, 44);
        m_startBtn->setStyleSheet(greenBtnStyle);
        connect(m_startBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onStartRetrace);
        startRow->addWidget(m_startBtn);

        m_autoRunAllBtn = new QPushButton("Auto-Run All 4");
        m_autoRunAllBtn->setFixedSize(180, 44);
        m_autoRunAllBtn->setStyleSheet(orangeBtnStyle);
        m_autoRunAllBtn->setToolTip("(Disabled) Auto-run is currently turned off");
        m_autoRunAllBtn->setEnabled(false);
        m_autoRunAllBtn->setVisible(false);
        connect(m_autoRunAllBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onAutoRunAll);
        startRow->addWidget(m_autoRunAllBtn);
        startRow->addStretch(1);

        idleLayout->addLayout(startRow);
    }

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: WaitRefP1
    // ========================================
    QWidget* refP1Page = new QWidget();
    QVBoxLayout* refP1Layout = new QVBoxLayout(refP1Page);

    m_refP1InstructionLabel = new QLabel("Step 1/4: Navigating to Reference Point 1...");
    m_refP1InstructionLabel->setStyleSheet(instructionStyle);
    m_refP1InstructionLabel->setWordWrap(true);
    m_refP1InstructionLabel->setAlignment(Qt::AlignCenter);
    refP1Layout->addWidget(m_refP1InstructionLabel);

    QGroupBox* refP1Group = new QGroupBox("Current Gantry Position");
    refP1Group->setStyleSheet(groupStyle);
    QVBoxLayout* refP1PosLayout = new QVBoxLayout(refP1Group);
    m_refP1PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_refP1PositionLabel->setStyleSheet(posLabelStyle);
    m_refP1PositionLabel->setAlignment(Qt::AlignCenter);
    refP1PosLayout->addWidget(m_refP1PositionLabel);
    refP1Layout->addWidget(refP1Group);

    m_refP1ImageLabel = new QLabel("No reference image");
    m_refP1ImageLabel->setFixedSize(320, 240);
    m_refP1ImageLabel->setAlignment(Qt::AlignCenter);
    m_refP1ImageLabel->setStyleSheet(
        "QLabel { color: #555; font-size: 11px; border: 1px solid #3d5a8c; "
        "border-radius: 4px; background-color: #0a0f1e; }");
    refP1Layout->addWidget(m_refP1ImageLabel, 0, Qt::AlignCenter);

    refP1Layout->addStretch();

    m_confirmRefP1Btn = new QPushButton("Confirm Reference P1");
    m_confirmRefP1Btn->setFixedHeight(44);
    m_confirmRefP1Btn->setStyleSheet(blueBtnStyle);
    m_confirmRefP1Btn->setEnabled(false);
    connect(m_confirmRefP1Btn, &QPushButton::clicked, this, &PlacementRetraceWidget::onConfirmRefP1);
    m_autoDetectRefP1Btn = makeAutoDetectBtn(&PlacementRetraceWidget::onAutoDetectRefP1);
    {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(m_autoDetectRefP1Btn);
        row->addWidget(m_confirmRefP1Btn, 1);
        row->addWidget(makeStopBtn());
        refP1Layout->addLayout(row);
    }

    m_wizardStack->addWidget(refP1Page);  // index 1

    // ========================================
    // Page 2: WaitRefP2
    // ========================================
    QWidget* refP2Page = new QWidget();
    QVBoxLayout* refP2Layout = new QVBoxLayout(refP2Page);

    m_refP2InstructionLabel = new QLabel("Step 2/4: Verify Reference Point 2...");
    m_refP2InstructionLabel->setStyleSheet(instructionStyle);
    m_refP2InstructionLabel->setWordWrap(true);
    m_refP2InstructionLabel->setAlignment(Qt::AlignCenter);
    refP2Layout->addWidget(m_refP2InstructionLabel);

    QGroupBox* refP2Group = new QGroupBox("Current Gantry Position");
    refP2Group->setStyleSheet(groupStyle);
    QVBoxLayout* refP2PosLayout = new QVBoxLayout(refP2Group);
    m_refP2PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_refP2PositionLabel->setStyleSheet(posLabelStyle);
    m_refP2PositionLabel->setAlignment(Qt::AlignCenter);
    refP2PosLayout->addWidget(m_refP2PositionLabel);
    refP2Layout->addWidget(refP2Group);

    m_refP2ImageLabel = new QLabel("No reference image");
    m_refP2ImageLabel->setFixedSize(320, 240);
    m_refP2ImageLabel->setAlignment(Qt::AlignCenter);
    m_refP2ImageLabel->setStyleSheet(
        "QLabel { color: #555; font-size: 11px; border: 1px solid #3d5a8c; "
        "border-radius: 4px; background-color: #0a0f1e; }");
    refP2Layout->addWidget(m_refP2ImageLabel, 0, Qt::AlignCenter);

    refP2Layout->addStretch();

    m_confirmRefP2Btn = new QPushButton("Confirm Reference P2");
    m_confirmRefP2Btn->setFixedHeight(44);
    m_confirmRefP2Btn->setStyleSheet(blueBtnStyle);
    connect(m_confirmRefP2Btn, &QPushButton::clicked, this, &PlacementRetraceWidget::onConfirmRefP2);
    m_autoDetectRefP2Btn = makeAutoDetectBtn(&PlacementRetraceWidget::onAutoDetectRefP2);
    {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(m_autoDetectRefP2Btn);
        row->addWidget(m_confirmRefP2Btn, 1);
        row->addWidget(makeStopBtn());
        refP2Layout->addLayout(row);
    }

    m_wizardStack->addWidget(refP2Page);  // index 2

    // ========================================
    // Page 3: WaitNrmP1
    // ========================================
    QWidget* tgtP1Page = new QWidget();
    QVBoxLayout* tgtP1Layout = new QVBoxLayout(tgtP1Page);

    m_nrmP1InstructionLabel = new QLabel("Step 3/4: Navigating to Normal Point 1...");
    m_nrmP1InstructionLabel->setStyleSheet(instructionStyle);
    m_nrmP1InstructionLabel->setWordWrap(true);
    m_nrmP1InstructionLabel->setAlignment(Qt::AlignCenter);
    tgtP1Layout->addWidget(m_nrmP1InstructionLabel);

    QGroupBox* tgtP1Group = new QGroupBox("Current Gantry Position");
    tgtP1Group->setStyleSheet(groupStyle);
    QVBoxLayout* tgtP1PosLayout = new QVBoxLayout(tgtP1Group);
    m_nrmP1PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_nrmP1PositionLabel->setStyleSheet(posLabelStyle);
    m_nrmP1PositionLabel->setAlignment(Qt::AlignCenter);
    tgtP1PosLayout->addWidget(m_nrmP1PositionLabel);
    tgtP1Layout->addWidget(tgtP1Group);

    m_nrmP1ImageLabel = new QLabel("No reference image");
    m_nrmP1ImageLabel->setFixedSize(320, 240);
    m_nrmP1ImageLabel->setAlignment(Qt::AlignCenter);
    m_nrmP1ImageLabel->setStyleSheet(
        "QLabel { color: #555; font-size: 11px; border: 1px solid #3d5a8c; "
        "border-radius: 4px; background-color: #0a0f1e; }");
    tgtP1Layout->addWidget(m_nrmP1ImageLabel, 0, Qt::AlignCenter);

    tgtP1Layout->addStretch();

    m_confirmNrmP1Btn = new QPushButton("Confirm Normal P1");
    m_confirmNrmP1Btn->setFixedHeight(44);
    m_confirmNrmP1Btn->setStyleSheet(blueBtnStyle);
    m_confirmNrmP1Btn->setEnabled(false);
    connect(m_confirmNrmP1Btn, &QPushButton::clicked, this, &PlacementRetraceWidget::onConfirmNrmP1);
    m_autoDetectNrmP1Btn = makeAutoDetectBtn(&PlacementRetraceWidget::onAutoDetectNrmP1);
    {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(m_autoDetectNrmP1Btn);
        row->addWidget(m_confirmNrmP1Btn, 1);
        row->addWidget(makeStopBtn());
        tgtP1Layout->addLayout(row);
    }

    m_wizardStack->addWidget(tgtP1Page);  // index 3

    // ========================================
    // Page 4: WaitNrmP2
    // ========================================
    QWidget* tgtP2Page = new QWidget();
    QVBoxLayout* tgtP2Layout = new QVBoxLayout(tgtP2Page);

    m_nrmP2InstructionLabel = new QLabel("Step 4/4: Verify Normal Point 2...");
    m_nrmP2InstructionLabel->setStyleSheet(instructionStyle);
    m_nrmP2InstructionLabel->setWordWrap(true);
    m_nrmP2InstructionLabel->setAlignment(Qt::AlignCenter);
    tgtP2Layout->addWidget(m_nrmP2InstructionLabel);

    QGroupBox* tgtP2Group = new QGroupBox("Current Gantry Position");
    tgtP2Group->setStyleSheet(groupStyle);
    QVBoxLayout* tgtP2PosLayout = new QVBoxLayout(tgtP2Group);
    m_nrmP2PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_nrmP2PositionLabel->setStyleSheet(posLabelStyle);
    m_nrmP2PositionLabel->setAlignment(Qt::AlignCenter);
    tgtP2PosLayout->addWidget(m_nrmP2PositionLabel);
    tgtP2Layout->addWidget(tgtP2Group);

    m_nrmP2ImageLabel = new QLabel("No reference image");
    m_nrmP2ImageLabel->setFixedSize(320, 240);
    m_nrmP2ImageLabel->setAlignment(Qt::AlignCenter);
    m_nrmP2ImageLabel->setStyleSheet(
        "QLabel { color: #555; font-size: 11px; border: 1px solid #3d5a8c; "
        "border-radius: 4px; background-color: #0a0f1e; }");
    tgtP2Layout->addWidget(m_nrmP2ImageLabel, 0, Qt::AlignCenter);

    tgtP2Layout->addStretch();

    m_confirmNrmP2Btn = new QPushButton("Confirm Normal P2");
    m_confirmNrmP2Btn->setFixedHeight(44);
    m_confirmNrmP2Btn->setStyleSheet(blueBtnStyle);
    connect(m_confirmNrmP2Btn, &QPushButton::clicked, this, &PlacementRetraceWidget::onConfirmNrmP2);
    m_autoDetectNrmP2Btn = makeAutoDetectBtn(&PlacementRetraceWidget::onAutoDetectNrmP2);
    {
        QHBoxLayout* row = new QHBoxLayout();
        row->addWidget(m_autoDetectNrmP2Btn);
        row->addWidget(m_confirmNrmP2Btn, 1);
        row->addWidget(makeStopBtn());
        tgtP2Layout->addLayout(row);
    }

    m_wizardStack->addWidget(tgtP2Page);  // index 4

    // ========================================
    // Page 5: Review
    // ========================================
    QWidget* reviewPage = new QWidget();
    QVBoxLayout* reviewOuterLayout = new QVBoxLayout(reviewPage);
    reviewOuterLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* reviewScroll = new QScrollArea();
    reviewScroll->setWidgetResizable(true);
    reviewScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    QWidget* reviewContent = new QWidget();
    QVBoxLayout* reviewLayout = new QVBoxLayout(reviewContent);

    m_reviewLabel = new QLabel("Calculating...");
    m_reviewLabel->setStyleSheet(
        "font-size: 12px; color: #ccc; font-family: monospace; padding: 10px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_reviewLabel->setWordWrap(true);
    m_reviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    reviewLayout->addWidget(m_reviewLabel);

    // Apply U correction checkbox
    m_applyUCheckbox = new QCheckBox("Apply U (rotation) correction");
    m_applyUCheckbox->setChecked(true);
    m_applyUCheckbox->setStyleSheet(
        "QCheckBox { color: #f0c040; font-size: 13px; font-weight: bold; padding: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; }"
        "QCheckBox::indicator:unchecked { border: 1px solid #4d6e9f; background: #1a1f2e; border-radius: 3px; }"
        "QCheckBox::indicator:checked { border: 1px solid #4d6e9f; background: #2980b9; border-radius: 3px; }");
    connect(m_applyUCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_set) {
            m_set->config.applyUCorrection = checked;
            if (m_state == Review) {
                m_saveBtn->setEnabled(true);
                QString targetName = QString::fromStdString(m_set->config.targetPositionName);
                m_saveBtn->setText(QString("Save to \"%1\"").arg(targetName));
                computeRetraceResults();
            }
        }
    });
    reviewLayout->addWidget(m_applyUCheckbox);

    // ----------------------------------------------------------------
    // Manual Offset Tuner — added to corrected position at Save time.
    // Fixed width + top-left alignment (UI convention going forward).
    // ----------------------------------------------------------------
    QGroupBox* tuneGroup = new QGroupBox("Manual Offset Tuner");
    tuneGroup->setStyleSheet(
        "QGroupBox { color: #f0c040; font-size: 12px; font-weight: bold; "
        "border: 1px solid #3d5a8c; border-radius: 6px; "
        "margin-top: 10px; padding-top: 14px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    tuneGroup->setFixedWidth(420);
    tuneGroup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    QVBoxLayout* tuneOuter = new QVBoxLayout(tuneGroup);
    tuneOuter->setContentsMargins(8, 6, 8, 8);
    tuneOuter->setSpacing(6);

    QLabel* tuneHint = new QLabel(
        "Added directly to the corrected position at Save time.\n"
        "XYZ in mm; UVW in degrees.");
    tuneHint->setStyleSheet("color: #aabbcc; font-size: 11px;");
    tuneHint->setWordWrap(true);
    tuneOuter->addWidget(tuneHint);

    QString spinStyle =
        "QDoubleSpinBox { background-color: #1a1f2e; color: #ffffff; "
        "border: 1px solid #4d6e9f; border-radius: 3px; "
        "padding: 3px 6px; font-size: 12px; min-width: 120px; }"
        "QDoubleSpinBox:focus { border: 1px solid #5dade2; }";
    QString tuneLblStyle = "color: #d0d0d0; font-size: 12px;";

    auto makeSpin = [spinStyle](const QString& suffix) {
        QDoubleSpinBox* sp = new QDoubleSpinBox();
        sp->setRange(-1.0, 1.0);        // ±1 mm / ±1 deg
        sp->setDecimals(3);             // 0.001 resolution (1 μm / 1 m°)
        sp->setSingleStep(0.001);       // arrow step = 1 μm or 1 m°
        sp->setSuffix(suffix);
        sp->setAlignment(Qt::AlignRight);
        sp->setStyleSheet(spinStyle);
        sp->setKeyboardTracking(false);
        sp->setFixedWidth(140);
        return sp;
    };

    QGridLayout* tuneGrid = new QGridLayout();
    tuneGrid->setHorizontalSpacing(10);
    tuneGrid->setVerticalSpacing(6);

    const QString mmSuffix  = QString(" mm");
    const QString degSuffix = QString::fromUtf8(" \xC2\xB0"); // " °"

    m_tuneOffsetXSpin = makeSpin(mmSuffix);
    m_tuneOffsetYSpin = makeSpin(mmSuffix);
    m_tuneOffsetZSpin = makeSpin(mmSuffix);
    m_tuneOffsetUSpin = makeSpin(degSuffix);
    m_tuneOffsetVSpin = makeSpin(degSuffix);
    m_tuneOffsetWSpin = makeSpin(degSuffix);

    // Two columns: X|U, Y|V, Z|W
    QLabel* xLbl = new QLabel("X:"); xLbl->setStyleSheet(tuneLblStyle);
    QLabel* yLbl = new QLabel("Y:"); yLbl->setStyleSheet(tuneLblStyle);
    QLabel* zLbl = new QLabel("Z:"); zLbl->setStyleSheet(tuneLblStyle);
    QLabel* uLbl = new QLabel("U:"); uLbl->setStyleSheet(tuneLblStyle);
    QLabel* vLbl = new QLabel("V:"); vLbl->setStyleSheet(tuneLblStyle);
    QLabel* wLbl = new QLabel("W:"); wLbl->setStyleSheet(tuneLblStyle);

    tuneGrid->addWidget(xLbl, 0, 0);
    tuneGrid->addWidget(m_tuneOffsetXSpin, 0, 1);
    tuneGrid->addWidget(uLbl, 0, 2);
    tuneGrid->addWidget(m_tuneOffsetUSpin, 0, 3);

    tuneGrid->addWidget(yLbl, 1, 0);
    tuneGrid->addWidget(m_tuneOffsetYSpin, 1, 1);
    tuneGrid->addWidget(vLbl, 1, 2);
    tuneGrid->addWidget(m_tuneOffsetVSpin, 1, 3);

    tuneGrid->addWidget(zLbl, 2, 0);
    tuneGrid->addWidget(m_tuneOffsetZSpin, 2, 1);
    tuneGrid->addWidget(wLbl, 2, 2);
    tuneGrid->addWidget(m_tuneOffsetWSpin, 2, 3);

    // Fixed widths — don't stretch to fill
    QHBoxLayout* tuneGridRow = new QHBoxLayout();
    tuneGridRow->setContentsMargins(0, 0, 0, 0);
    tuneGridRow->addLayout(tuneGrid);
    tuneGridRow->addStretch();
    tuneOuter->addLayout(tuneGridRow);

    m_tunePreviewLabel = new QLabel("(Preview available after retrace)");
    m_tunePreviewLabel->setWordWrap(true);
    m_tunePreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_tunePreviewLabel->setStyleSheet(
        "font-family: monospace; font-size: 11px; color: #d0e0f0; "
        "background-color: #0f1520; border: 1px solid #2d3e5f; "
        "border-radius: 4px; padding: 6px;");
    tuneOuter->addWidget(m_tunePreviewLabel);

    m_tuneResetBtn = new QPushButton("Clear Offsets");
    m_tuneResetBtn->setFixedHeight(28);
    m_tuneResetBtn->setStyleSheet(
        "QPushButton { background-color: #34495e; color: white; font-size: 11px; "
        "border-radius: 3px; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #4a6785; }");
    connect(m_tuneResetBtn, &QPushButton::clicked, this, [this]() {
        if (!m_tuneOffsetXSpin) return;
        QSignalBlocker bX(m_tuneOffsetXSpin);
        QSignalBlocker bY(m_tuneOffsetYSpin);
        QSignalBlocker bZ(m_tuneOffsetZSpin);
        QSignalBlocker bU(m_tuneOffsetUSpin);
        QSignalBlocker bV(m_tuneOffsetVSpin);
        QSignalBlocker bW(m_tuneOffsetWSpin);
        m_tuneOffsetXSpin->setValue(0.0);
        m_tuneOffsetYSpin->setValue(0.0);
        m_tuneOffsetZSpin->setValue(0.0);
        m_tuneOffsetUSpin->setValue(0.0);
        m_tuneOffsetVSpin->setValue(0.0);
        m_tuneOffsetWSpin->setValue(0.0);
        onTuneOffsetChanged();
    });
    QHBoxLayout* tuneBtnRow = new QHBoxLayout();
    tuneBtnRow->addStretch();
    tuneBtnRow->addWidget(m_tuneResetBtn);
    tuneOuter->addLayout(tuneBtnRow);

    for (QDoubleSpinBox* sp : {m_tuneOffsetXSpin, m_tuneOffsetYSpin, m_tuneOffsetZSpin,
                               m_tuneOffsetUSpin, m_tuneOffsetVSpin, m_tuneOffsetWSpin}) {
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &PlacementRetraceWidget::onTuneOffsetChanged);
    }

    // Left-top aligned + fixed width (UI convention)
    QHBoxLayout* tuneGroupRow = new QHBoxLayout();
    tuneGroupRow->setContentsMargins(0, 0, 0, 0);
    tuneGroupRow->addWidget(tuneGroup, 0, Qt::AlignLeft | Qt::AlignTop);
    tuneGroupRow->addStretch();
    reviewLayout->addLayout(tuneGroupRow);

    reviewLayout->addStretch();

    QHBoxLayout* reviewBtnRow = new QHBoxLayout();
    m_saveBtn = new QPushButton("Save to Target Position");  // updated dynamically
    m_saveBtn->setFixedHeight(44);
    m_saveBtn->setStyleSheet(greenBtnStyle);
    connect(m_saveBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onSaveToTarget);
    reviewBtnRow->addWidget(m_saveBtn);

    m_resetBtn = new QPushButton("Reset");
    m_resetBtn->setFixedHeight(44);
    m_resetBtn->setStyleSheet(orangeBtnStyle);
    connect(m_resetBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onReset);
    reviewBtnRow->addWidget(m_resetBtn);

    reviewLayout->addLayout(reviewBtnRow);

    // Hex velocity preset buttons
    QString velBtnStyle =
        "QPushButton { background-color: #34495e; color: white; font-weight: bold; "
        "font-size: 12px; border-radius: 4px; padding: 6px 12px; }"
        "QPushButton:hover { background-color: #4a6785; }"
        "QPushButton:checked { background-color: #2980b9; border: 2px solid #5dade2; }";

    QHBoxLayout* velRow = new QHBoxLayout();
    QLabel* velLabel = new QLabel("Hex Velocity:");
    velLabel->setStyleSheet("color: #aabbcc; font-size: 12px; font-weight: bold;");
    velRow->addWidget(velLabel);

    m_vel01Btn = new QPushButton("0.1 mm/s");
    m_vel01Btn->setFixedHeight(34);
    m_vel01Btn->setCheckable(true);
    m_vel01Btn->setChecked(true);
    m_vel01Btn->setStyleSheet(velBtnStyle);
    connect(m_vel01Btn, &QPushButton::clicked, this, [this]() {
        m_vel01Btn->setChecked(true);
        m_vel005Btn->setChecked(false);
        setHexVelocity(0.1);
    });
    velRow->addWidget(m_vel01Btn);

    m_vel005Btn = new QPushButton("0.05 mm/s");
    m_vel005Btn->setFixedHeight(34);
    m_vel005Btn->setCheckable(true);
    m_vel005Btn->setStyleSheet(velBtnStyle);
    connect(m_vel005Btn, &QPushButton::clicked, this, [this]() {
        m_vel005Btn->setChecked(true);
        m_vel01Btn->setChecked(false);
        setHexVelocity(0.05);
    });
    velRow->addWidget(m_vel005Btn);

    velRow->addStretch();
    reviewLayout->addLayout(velRow);

    m_moveToTargetBtn = new QPushButton("Move to Target Position");
    m_moveToTargetBtn->setFixedHeight(44);
    m_moveToTargetBtn->setStyleSheet(
        "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #9b59b6; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }");
    m_moveToTargetBtn->setEnabled(false);
    connect(m_moveToTargetBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onMoveToTarget);
    reviewLayout->addWidget(m_moveToTargetBtn);

    m_skipBtn = new QPushButton("Continue Without Saving");
    m_skipBtn->setFixedHeight(44);
    m_skipBtn->setStyleSheet(
        "QPushButton { background-color: #7f8c8d; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #95a5a6; }");
    connect(m_skipBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onSkip);
    reviewLayout->addWidget(m_skipBtn);

    QString redBtnStyle =
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }";

    m_endBtn = new QPushButton("End Retrace");
    m_endBtn->setFixedHeight(44);
    m_endBtn->setStyleSheet(redBtnStyle);
    connect(m_endBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onEnd);
    reviewLayout->addWidget(m_endBtn);

    reviewScroll->setWidget(reviewContent);
    reviewOuterLayout->addWidget(reviewScroll);

    m_wizardStack->addWidget(reviewPage);  // index 5

    mainLayout->addWidget(m_wizardStack, 1);
}

void PlacementRetraceWidget::setState(WizardState state)
{
    m_state = state;

    switch (state) {
    case Idle: {
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        m_autoRunAll = false;
        // Ready requires all three lines valid:
        //   - referenceLine: establishes the coordinate frame (re-measured)
        //   - normalPlacementLine: physically re-measured at steps 3/4
        //   - targetLine: cached only, used to compute designOffset
        bool ready = m_set && m_set->referenceLine.valid
                     && m_set->targetLine.valid
                     && m_set->normalPlacementLine.valid
                     && m_set->designOffsetValid
                     && !m_set->config.targetDevice.empty()
                     && !m_set->config.masterPositionName.empty()
                     && !m_set->config.targetPositionName.empty();
        m_startBtn->setEnabled(ready);
        bool refP1Set = m_set && !m_set->referenceLine.p1VisionProfilePath.empty();
        bool refP2Set = m_set && !m_set->referenceLine.p2VisionProfilePath.empty();
        bool tgtP1Set = m_set && !m_set->normalPlacementLine.p1VisionProfilePath.empty();
        bool tgtP2Set = m_set && !m_set->normalPlacementLine.p2VisionProfilePath.empty();
        bool allProfilesSet = refP1Set && refP2Set && tgtP1Set && tgtP2Set;
        if (m_autoRunAllBtn) {
            // Auto-Run All 4 is currently disabled (hidden via setVisible(false)
            // in setupUI). Keep the slot wired but do not enable.
            m_autoRunAllBtn->setEnabled(false);
            QString tip = "Run the retrace, auto-detecting where profiles are set.\n"
                          "Steps without a profile will wait for manual Confirm.\n"
                          "Profile status:\n";
            tip += QString("  RefP1: %1\n").arg(refP1Set ? "set" : "MISSING");
            tip += QString("  RefP2: %1\n").arg(refP2Set ? "set" : "MISSING");
            tip += QString("  NrmP1: %1\n").arg(tgtP1Set ? "set" : "MISSING");
            tip += QString("  NrmP2: %1").arg(tgtP2Set ? "set" : "MISSING");
            m_autoRunAllBtn->setToolTip(tip);
            (void)allProfilesSet;  // kept for summary below
        }

        auto pStatus = [](bool b) { return b ? QString("set") : QString("MISSING"); };

        if (m_set && ready) {
            m_idleSummaryLabel->setText(QString(
                "Ready to retrace (v3 Normal-Placement model).\n\n"
                "Device: %1\n"
                "Master Position: %2\n"
                "Target Position: %3\n\n"
                "Design Offset (target − normal): dX=%4  dY=%5\n"
                "Taught Ref→Tgt Angle Δ: %6%7\n\n"
                "Vision profiles:\n"
                "  RefP1: %8    RefP2: %9\n"
                "  NrmP1: %10   NrmP2: %11\n\n"
                "Press 'Start Retrace' to begin.")
                .arg(QString::fromStdString(m_set->config.targetDevice))
                .arg(QString::fromStdString(m_set->config.masterPositionName))
                .arg(QString::fromStdString(m_set->config.targetPositionName))
                .arg(m_set->designOffsetX, 0, 'f', 4).arg(m_set->designOffsetY, 0, 'f', 4)
                .arg(m_set->deltaAngleDeg, 0, 'f', 3).arg(QChar(0x00B0))
                .arg(pStatus(refP1Set)).arg(pStatus(refP2Set))
                .arg(pStatus(tgtP1Set)).arg(pStatus(tgtP2Set)));
        } else {
            QString missing;
            if (!m_set) {
                missing = "No set loaded.";
            } else {
                missing = "Cannot retrace. Please ensure:\n";
                if (!m_set->referenceLine.valid)        missing += "  - Reference line is taught\n";
                if (!m_set->targetLine.valid)           missing += "  - Target line is taught\n";
                if (!m_set->normalPlacementLine.valid)  missing += "  - Normal Placement line is taught\n";
                if (!m_set->designOffsetValid)          missing += "  - designOffset unavailable (need both target and normal lines)\n";
                if (m_set->config.targetDevice.empty()) missing += "  - Target device is selected\n";
                if (m_set->config.masterPositionName.empty()) missing += "  - Master position selected\n";
                if (m_set->config.targetPositionName.empty()) missing += "  - Target position selected\n";
            }
            m_idleSummaryLabel->setText(missing);
        }
        break;
    }
    case WaitRefP1:
        m_wizardStack->setCurrentIndex(1);
        m_confirmRefP1Btn->setEnabled(false);
        if (m_autoDetectRefP1Btn) m_autoDetectRefP1Btn->setEnabled(false);
        m_refP1InstructionLabel->setText("Step 1/4: Navigating to Reference Point 1...");
        if (m_set) loadReferenceImage(m_refP1ImageLabel, m_set->referenceLine.p1ImageFile);
        updateAutoDetectVisibility();
        m_positionTimer->start(250);
        QTimer::singleShot(0, this, [this]() { autoNavigateToRefP1(); });
        break;

    case WaitRefP2:
        m_wizardStack->setCurrentIndex(2);
        if (m_autoDetectRefP2Btn) m_autoDetectRefP2Btn->setEnabled(true);
        m_refP2InstructionLabel->setText("Step 2/4: Moved to Reference P2 area. Verify and press Confirm.");
        if (m_set) loadReferenceImage(m_refP2ImageLabel, m_set->referenceLine.p2ImageFile);
        updateAutoDetectVisibility();
        m_positionTimer->start(250);
        break;

    case WaitNrmP1:
        m_wizardStack->setCurrentIndex(3);
        m_confirmNrmP1Btn->setEnabled(false);
        if (m_autoDetectNrmP1Btn) m_autoDetectNrmP1Btn->setEnabled(false);
        m_nrmP1InstructionLabel->setText("Step 3/4: Navigating to Normal Point 1...");
        if (m_set) loadReferenceImage(m_nrmP1ImageLabel, m_set->normalPlacementLine.p1ImageFile);
        updateAutoDetectVisibility();
        m_positionTimer->start(250);
        QTimer::singleShot(0, this, [this]() { autoNavigateToNrmP1(); });
        break;

    case WaitNrmP2:
        m_wizardStack->setCurrentIndex(4);
        if (m_autoDetectNrmP2Btn) m_autoDetectNrmP2Btn->setEnabled(true);
        m_nrmP2InstructionLabel->setText("Step 4/4: Moved to Normal P2 area. Verify and press Confirm.");
        if (m_set) loadReferenceImage(m_nrmP2ImageLabel, m_set->normalPlacementLine.p2ImageFile);
        updateAutoDetectVisibility();
        m_positionTimer->start(250);
        break;

    case Review: {
        m_wizardStack->setCurrentIndex(5);
        m_positionTimer->stop();
        m_autoRunAll = false;
        m_saveBtn->setEnabled(true);
        QString targetName = m_set ? QString::fromStdString(m_set->config.targetPositionName) : "Target";
        m_saveBtn->setText(QString("Save to \"%1\"").arg(targetName));
        // Sync checkbox from loaded set
        if (m_set) {
            m_applyUCheckbox->setChecked(m_set->config.applyUCorrection);
        }
        // Reset velocity buttons to default 0.1 mm/s
        m_vel01Btn->setChecked(true);
        m_vel005Btn->setChecked(false);
        setHexVelocity(0.1);
        computeRetraceResults();
        if (m_autoSaveContinue) {
            QTimer::singleShot(0, this, &PlacementRetraceWidget::performAutoSaveContinue);
        }
        break;
    }
    }
}

void PlacementRetraceWidget::onStartRetrace()
{
    if (!m_set) return;
    if (!m_set->designOffsetValid) {
        std::cout << "[PlacementRetrace] Cannot start: designOffset not valid "
                  << "(need both target line and normal placement line taught)"
                  << std::endl;
        return;
    }

    // v3 model: the "master vector" concept is replaced by a cached design
    // offset (taught_target_center − taught_normal_center). At retrace we
    // re-measure the reference frame AND the normal placement line; the
    // correction is the design offset rotated by the frame's Δθ. Kept
    // m_masterVecX/Y/m_masterDeltaAngle populated for legacy UI display.
    m_masterVecX = m_set->designOffsetX;
    m_masterVecY = m_set->designOffsetY;
    m_masterDeltaAngle = m_set->deltaAngleDeg;

    // Save taught P1->P2 deltas for relative navigation between the two
    // corner captures at each line. Steps 3/4 navigate along the normal
    // placement line (what the part physically presents), not the target.
    m_refDeltaX = m_set->referenceLine.p2x - m_set->referenceLine.p1x;
    m_refDeltaY = m_set->referenceLine.p2y - m_set->referenceLine.p1y;
    m_refDeltaZ = m_set->referenceLine.p2z - m_set->referenceLine.p1z;
    m_nrmDeltaX = m_set->normalPlacementLine.p2x - m_set->normalPlacementLine.p1x;
    m_nrmDeltaY = m_set->normalPlacementLine.p2y - m_set->normalPlacementLine.p1y;
    m_nrmDeltaZ = m_set->normalPlacementLine.p2z - m_set->normalPlacementLine.p1z;

    // Clear retraced data
    m_retracedRef = PlacementLinePoints();
    m_retracedRef.name = "Reference";
    m_retracedNrm = PlacementLinePoints();
    m_retracedNrm.name = "Normal";

    // Reset per-step meta and log gate for the new run
    for (int i = 0; i < 4; ++i) m_stepMeta[i] = RetracePointMeta();
    m_loggedThisRun = false;

    std::cout << "[PlacementRetrace] Start retrace. DesignOffset: dX="
              << m_masterVecX << " dY=" << m_masterVecY
              << " taughtRefAngle=" << m_set->referenceLine.angleDeg
              << std::endl;

    // Apply Ref P1 camera exposure before first motion
    applyCameraExposureAsync(m_set->referenceLine.p1Exposure);

    setState(WaitRefP1);
}

void PlacementRetraceWidget::onAutoRunAll()
{
    if (!m_set) return;
    std::cout << "[PlacementRetrace] Auto-Run started" << std::endl;
    m_autoRunAll = true;
    if (m_logMode != "recipe") m_logMode = "autoRunAll";
    onStartRetrace();   // setState(WaitRefP1) does not touch m_autoRunAll
}

void PlacementRetraceWidget::applyCameraExposureAsync(double exposureUs)
{
    if (!m_context || !m_context->cameraManager || !m_set) return;
    if (m_set->config.cameraId.empty()) return;

    auto* camMgr = m_context->cameraManager;
    std::string cameraId = m_set->config.cameraId;

    if (m_exposureFuture.valid()) {
        m_exposureFuture.wait();
    }
    m_exposureFuture = std::async(std::launch::async, [camMgr, cameraId, exposureUs]() {
        ICameraHardware::ExposureSettings expSettings;
        expSettings.auto_exposure = false;
        expSettings.auto_gain = false;
        expSettings.gain = 0.0;
        expSettings.exposure_time = exposureUs;

        bool ok = camMgr->ApplyExposureSettings(cameraId, expSettings);

        std::cout << "[PlacementRetrace] Camera \"" << cameraId
                  << "\" exposure=" << exposureUs << "us "
                  << (ok ? "OK" : "FAILED") << std::endl;
    });
}

std::string PlacementRetraceWidget::getImageDirectory() const
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        std::filesystem::path productPath(
            m_context->productPositionManager->GetCurrentProductPath());
        return (productPath.parent_path() / "images" / "trainer").string();
    }
    return "config/images/trainer";
}

void PlacementRetraceWidget::loadReferenceImage(QLabel* label, const std::string& imageFile)
{
    if (!label) return;
    if (imageFile.empty()) {
        label->setText("No reference image");
        label->setPixmap(QPixmap());
        return;
    }
    std::string fullPath = getImageDirectory() + "/" + imageFile;
    QPixmap pixmap(QString::fromStdString(fullPath));
    if (pixmap.isNull()) {
        label->setText("Image not found");
        return;
    }

    // Crop center 25% of the image
    int cropW = pixmap.width() / 2;
    int cropH = pixmap.height() / 2;
    int cropX = (pixmap.width() - cropW) / 2;
    int cropY = (pixmap.height() - cropH) / 2;
    QPixmap cropped = pixmap.copy(cropX, cropY, cropW, cropH);

    // Scale to label size
    QPixmap scaled = cropped.scaled(label->size(), Qt::KeepAspectRatio, Qt::FastTransformation);

    // Draw crosshair overlay
    QPainter painter(&scaled);
    painter.setPen(QPen(QColor(255, 0, 0), 1));
    int cx = scaled.width() / 2;
    int cy = scaled.height() / 2;
    painter.drawLine(cx, 0, cx, scaled.height());
    painter.drawLine(0, cy, scaled.width(), cy);
    painter.end();

    label->setText("");
    label->setPixmap(scaled);
}

void PlacementRetraceWidget::runMotionAsync(std::function<bool()> motionFunc,
                                             std::function<void(bool)> onComplete)
{
    m_motionCallback = onComplete;
    m_motionFuture = std::async(std::launch::async, motionFunc);
    m_motionPollTimer->start(50);
}

void PlacementRetraceWidget::autoNavigateToRefP1()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        m_refP1InstructionLabel->setText("Step 1/4: Motion manager not available. Manually jog to Reference P1.");
        m_confirmRefP1Btn->setEnabled(true);
        return;
    }

    auto* mm = m_context->motionManager;
    std::string dev = m_set->measureDevice;
    double tx = m_set->referenceLine.p1x;
    double ty = m_set->referenceLine.p1y;
    double tz = m_set->referenceLine.p1z;

    runMotionAsync(
        [mm, dev, tx, ty, tz]() {
            return mm->MoveToAbs(dev, tx, ty, tz);
        },
        [this](bool ok) {
            if (ok) {
                m_refP1InstructionLabel->setText(
                    "Step 1/4: Arrived at Reference P1.\nVerify position and press Confirm.");
            } else {
                m_refP1InstructionLabel->setText(
                    "Step 1/4: Auto-navigate FAILED.\nManually jog to Reference P1, then press Confirm.");
            }
            m_confirmRefP1Btn->setEnabled(true);
            if (m_autoDetectRefP1Btn) m_autoDetectRefP1Btn->setEnabled(true);
            if (ok && m_autoRunAll) {
                QTimer::singleShot(0, this, &PlacementRetraceWidget::onAutoDetectRefP1);
            } else if (!ok) {
                m_autoRunAll = false;
            }
        });
}

void PlacementRetraceWidget::autoNavigateToNrmP1()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        m_nrmP1InstructionLabel->setText("Step 3/4: Motion manager not available. Manually jog to Normal P1.");
        m_confirmNrmP1Btn->setEnabled(true);
        return;
    }

    auto* mm = m_context->motionManager;
    std::string dev = m_set->measureDevice;
    double tx = m_set->normalPlacementLine.p1x;
    double ty = m_set->normalPlacementLine.p1y;
    double tz = m_set->normalPlacementLine.p1z;

    runMotionAsync(
        [mm, dev, tx, ty, tz]() {
            return mm->MoveToAbs(dev, tx, ty, tz);
        },
        [this](bool ok) {
            if (ok) {
                m_nrmP1InstructionLabel->setText(
                    "Step 3/4: Arrived at Normal P1.\nVerify position and press Confirm.");
            } else {
                m_nrmP1InstructionLabel->setText(
                    "Step 3/4: Auto-navigate FAILED.\nManually jog to Normal P1, then press Confirm.");
            }
            m_confirmNrmP1Btn->setEnabled(true);
            if (m_autoDetectNrmP1Btn) m_autoDetectNrmP1Btn->setEnabled(true);
            if (ok && m_autoRunAll) {
                QTimer::singleShot(0, this, &PlacementRetraceWidget::onAutoDetectNrmP1);
            } else if (!ok) {
                m_autoRunAll = false;
            }
        });
}

void PlacementRetraceWidget::onConfirmRefP1()
{
    if (!m_context || !m_context->motionManager) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    m_retracedRef.p1x = pos.x;
    m_retracedRef.p1y = pos.y;
    m_retracedRef.p1z = pos.z;
    if (m_stepMeta[0].reason.empty()) m_stepMeta[0].reason = "manual";

    // Re-enable auto-detect chain after manual fallback
    if (m_autoDetectFromRecipe && !m_autoRunAll) {
        m_autoRunAll = true;
    }

    std::cout << "[PlacementRetrace] Ref P1: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    // Apply Ref P2 exposure before moving
    applyCameraExposureAsync(m_set->referenceLine.p2Exposure);

    auto* mm = m_context->motionManager;
    std::string dev = m_set->measureDevice;
    double targetX = pos.x + m_refDeltaX;
    double targetY = pos.y + m_refDeltaY;
    double targetZ = pos.z + m_refDeltaZ;

    m_confirmRefP1Btn->setEnabled(false);
    setState(WaitRefP2);

    runMotionAsync(
        [mm, dev, targetX, targetY, targetZ]() {
            return mm->MoveToAbs(dev, targetX, targetY, targetZ);
        },
        [this](bool ok) {
            if (!ok) {
                m_refP2InstructionLabel->setText("Step 2/4: Auto-move FAILED. Manually jog to Ref P2, then Confirm.");
                m_autoRunAll = false;
            } else if (m_autoRunAll) {
                QTimer::singleShot(0, this, &PlacementRetraceWidget::onAutoDetectRefP2);
            }
        });
}

void PlacementRetraceWidget::onConfirmRefP2()
{
    if (!m_context || !m_context->motionManager) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    m_retracedRef.p2x = pos.x;
    m_retracedRef.p2y = pos.y;
    m_retracedRef.p2z = pos.z;
    m_retracedRef.valid = true;
    m_retracedRef.angleDeg = PlacementCorrectionMath::ComputeLineAngle(
        m_retracedRef.p1x, m_retracedRef.p1y, m_retracedRef.p2x, m_retracedRef.p2y);
    if (m_stepMeta[1].reason.empty()) m_stepMeta[1].reason = "manual";

    // Re-enable auto-detect chain after manual fallback
    if (m_autoDetectFromRecipe && !m_autoRunAll) {
        m_autoRunAll = true;
    }

    std::cout << "[PlacementRetrace] Ref P2: X=" << pos.x << " Y=" << pos.y
              << " Angle=" << m_retracedRef.angleDeg << std::endl;

    // Apply Nrm P1 exposure before navigating to normal-placement line
    applyCameraExposureAsync(m_set->normalPlacementLine.p1Exposure);

    setState(WaitNrmP1);
}

void PlacementRetraceWidget::onConfirmNrmP1()
{
    if (!m_context || !m_context->motionManager) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    m_retracedNrm.p1x = pos.x;
    m_retracedNrm.p1y = pos.y;
    m_retracedNrm.p1z = pos.z;
    if (m_stepMeta[2].reason.empty()) m_stepMeta[2].reason = "manual";

    // Re-enable auto-detect chain after manual fallback
    if (m_autoDetectFromRecipe && !m_autoRunAll) {
        m_autoRunAll = true;
    }

    std::cout << "[PlacementRetrace] Nrm P1: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    // Apply Nrm P2 exposure before moving
    applyCameraExposureAsync(m_set->normalPlacementLine.p2Exposure);

    auto* mm = m_context->motionManager;
    std::string dev = m_set->measureDevice;
    double targetX = pos.x + m_nrmDeltaX;
    double targetY = pos.y + m_nrmDeltaY;
    double targetZ = pos.z + m_nrmDeltaZ;

    m_confirmNrmP1Btn->setEnabled(false);
    setState(WaitNrmP2);

    runMotionAsync(
        [mm, dev, targetX, targetY, targetZ]() {
            return mm->MoveToAbs(dev, targetX, targetY, targetZ);
        },
        [this](bool ok) {
            if (!ok) {
                m_nrmP2InstructionLabel->setText("Step 4/4: Auto-move FAILED. Manually jog to Normal P2, then Confirm.");
                m_autoRunAll = false;
            } else if (m_autoRunAll) {
                QTimer::singleShot(0, this, &PlacementRetraceWidget::onAutoDetectNrmP2);
            }
        });
}

void PlacementRetraceWidget::onConfirmNrmP2()
{
    if (!m_context || !m_context->motionManager) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    m_retracedNrm.p2x = pos.x;
    m_retracedNrm.p2y = pos.y;
    m_retracedNrm.p2z = pos.z;
    m_retracedNrm.valid = true;
    m_retracedNrm.angleDeg = PlacementCorrectionMath::ComputeLineAngle(
        m_retracedNrm.p1x, m_retracedNrm.p1y, m_retracedNrm.p2x, m_retracedNrm.p2y);
    if (m_stepMeta[3].reason.empty()) m_stepMeta[3].reason = "manual";

    std::cout << "[PlacementRetrace] Nrm P2: X=" << pos.x << " Y=" << pos.y
              << " Angle=" << m_retracedNrm.angleDeg << std::endl;

    setState(Review);
}

// Core correction computation. Called when all 4 points (RefP1, RefP2, NrmP1, NrmP2) are confirmed.
//
// v3 algorithm (normal-placement-based):
//   1. Read current hex device position (live XYZUVW).
//   2. Compute retraced reference angle and Δθ vs taught reference angle
//      (this is the new coordinate frame's rotation wrt teach time).
//   3. Compute retraced normal-placement midpoint NC_retraced (where the
//      lens physically landed).
//   4. Rotate the cached designOffset (taught_target_center − taught_normal_center)
//      by Δθ → this is where the lens *should* be relative to its current
//      landing position.
//   5. correctionGlobal = rotatedDesignOffset  (hex must move lens by this)
//   6. Transform correctionGlobal → device-local via transformation matrix.
//   7. correctedXYZ = liveHex + localCorrection
//      correctedU   = liveHex.u + (Am − At)   (if applyUCorrection)
//
// U-ANGLE CORRECTION (master-vs-target model):
//   The U correction rotates the placed part so its measured line becomes
//   parallel to the master line. Both lines are measured in the SAME retrace
//   frame, so the correction is just the angular difference between them:
//       Am = retraced reference (master) line angle   [m_retracedRef.angleDeg]
//       At = retraced normal-placement (target) angle  [m_retracedNrm.angleDeg]
//       ΔU = Am − At      → applied to hex U so that At' = Am (parallel)
//   The reverseSign flag flips ΔU if the hex U axis rotates opposite to the
//   gantry-measured angle convention. (Frame Δθ is still computed, but only for
//   the XY design-offset rotation in steps 2–5 — it no longer drives U.)
//
// The corrected position is stored in m_correctedX..W for use by onSaveToTarget().
void PlacementRetraceWidget::computeRetraceResults()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        m_reviewLabel->setText("Error: motion manager or set not available.");
        return;
    }
    if (!m_set->designOffsetValid) {
        m_reviewLabel->setText(
            "Error: designOffset not valid. Re-teach target and normal-placement lines.");
        return;
    }

    // Read live hex position
    auto hexPos = m_context->motionManager->GetCurrentPosition(m_set->config.targetDevice);
    m_liveHexX = hexPos.x;
    m_liveHexY = hexPos.y;
    m_liveHexZ = hexPos.z;
    m_liveHexU = hexPos.u;
    m_liveHexV = hexPos.v;
    m_liveHexW = hexPos.w;

    // Midpoints
    double retRefCx = 0.0, retRefCy = 0.0, retNrmCx = 0.0, retNrmCy = 0.0;
    PlacementCorrectionMath::Midpoint(
        m_retracedRef.p1x, m_retracedRef.p1y,
        m_retracedRef.p2x, m_retracedRef.p2y, retRefCx, retRefCy);
    PlacementCorrectionMath::Midpoint(
        m_retracedNrm.p1x, m_retracedNrm.p1y,
        m_retracedNrm.p2x, m_retracedNrm.p2y, retNrmCx, retNrmCy);

    // Frame rotation: new reference angle − taught reference angle.
    // Still used for the XY design-offset rotation below — NOT for U anymore.
    const double taughtRefAngle = m_set->referenceLine.angleDeg;
    const double retracedRefAngle = m_retracedRef.angleDeg;
    const double frameDeltaTheta = PlacementCorrectionMath::ComputeDeltaAngle(
        taughtRefAngle, retracedRefAngle);

    // U-angle correction (master-vs-target model): rotate the placed part so its
    // measured line becomes parallel to the master line. Both lines are measured
    // in THIS retrace, so the correction is simply the angular difference:
    //   Am = retraced reference (master) line angle
    //   At = retraced normal-placement (target) line angle
    //   ΔU = Am − At   → applied to hex U so that At' = Am
    const double masterAngle = m_retracedRef.angleDeg;   // Am
    const double targetAngle = m_retracedNrm.angleDeg;   // At
    const double uDeltaDeg = PlacementCorrectionMath::ComputeDeltaAngle(
        targetAngle, masterAngle);                       // Am − At, normalized
    const double uCorrection = PlacementCorrectionMath::GetCorrectionValue(
        uDeltaDeg, m_set->config.reverseSign);
    m_logUCorrection = uCorrection;

    // Safety guard: a U correction beyond this threshold is almost always a bad
    // line measurement (wrong point, vision misdetect, etc.). Auto-disable U
    // correction so the hex doesn't perform a dangerous large rotation. Operator
    // can re-enable manually after verifying the retraced points.
    constexpr double kUAutoDisableThresholdDeg = 3.0;
    bool uAutoDisabled = false;
    if (std::abs(uCorrection) > kUAutoDisableThresholdDeg
        && m_set->config.applyUCorrection) {
        m_set->config.applyUCorrection = false;
        if (m_applyUCheckbox) {
            QSignalBlocker b(m_applyUCheckbox);
            m_applyUCheckbox->setChecked(false);
        }
        uAutoDisabled = true;
        std::cout << "[PlacementRetrace] |ΔU|=" << std::abs(uCorrection)
                  << "° exceeds " << kUAutoDisableThresholdDeg
                  << "° — auto-disabled applyUCorrection" << std::endl;
    }

    // Rotate the cached design offset through the frame delta
    double rotatedOffX = 0.0, rotatedOffY = 0.0;
    PlacementCorrectionMath::Rotate2D(
        m_set->designOffsetX, m_set->designOffsetY, frameDeltaTheta,
        rotatedOffX, rotatedOffY);

    // Ideal target location in gantry global = retraced normal midpoint + rotated offset
    double idealTargetX = retNrmCx + rotatedOffX;
    double idealTargetY = retNrmCy + rotatedOffY;

    // The lens currently sits at NC_retraced; to land at idealTarget, the hex
    // must translate by (idealTarget − NC_retraced) = rotatedOffset in gantry global.
    double corrGlobalX = rotatedOffX;
    double corrGlobalY = rotatedOffY;

    // Stash intermediates for the retrace log
    m_logFrameDeltaTheta = frameDeltaTheta;
    m_logRotatedOffsetX  = rotatedOffX;
    m_logRotatedOffsetY  = rotatedOffY;
    m_logIdealTargetX    = idealTargetX;
    m_logIdealTargetY    = idealTargetY;
    m_logCorrGlobalX     = corrGlobalX;
    m_logCorrGlobalY     = corrGlobalY;

    // Transform correction to local device coords
    const auto* matrix = m_matrixLoaded
        ? m_matrixLoader.GetMatrix(m_set->config.targetDevice) : nullptr;

    double corrLocalX = 0.0, corrLocalY = 0.0, corrLocalZ = 0.0;
    if (matrix) {
        CoordinateTransformer::Vector6D globalCorr(corrGlobalX, corrGlobalY, 0, 0, 0, 0);
        auto localCorr = CoordinateTransformer::TransformGlobalToLocal(globalCorr, *matrix);
        corrLocalX = localCorr.x;
        corrLocalY = localCorr.y;
        corrLocalZ = localCorr.z;
    }
    m_logCorrLocalX = corrLocalX;
    m_logCorrLocalY = corrLocalY;
    m_logCorrLocalZ = corrLocalZ;

    // PLACE_ADJ = live hex position + corrections
    m_correctedX = m_liveHexX + corrLocalX;
    m_correctedY = m_liveHexY + corrLocalY;
    m_correctedZ = m_liveHexZ + corrLocalZ;
    m_correctedU = m_set->config.applyUCorrection
        ? (m_liveHexU + uCorrection)
        : m_liveHexU;
    m_correctedV = m_liveHexV;
    m_correctedW = m_liveHexW;

    // Deltas (corrected - master)
    double deltaX = m_correctedX - m_set->config.masterX;
    double deltaY = m_correctedY - m_set->config.masterY;
    double deltaZ = m_correctedZ - m_set->config.masterZ;
    double deltaU = m_correctedU - m_set->config.masterU;
    double deltaV = m_correctedV - m_set->config.masterV;
    double deltaW = m_correctedW - m_set->config.masterW;

    // Build review text
    QString deg = QString(QChar(0x00B0));
    QString text;
    text += QString("=== RETRACE RESULTS (v3: Normal-Placement model) ===\n\n");

    text += QString("--- Retraced Points ---\n");
    text += QString("Ref P1: X=%1  Y=%2\n").arg(m_retracedRef.p1x, 0, 'f', 4).arg(m_retracedRef.p1y, 0, 'f', 4);
    text += QString("Ref P2: X=%1  Y=%2\n").arg(m_retracedRef.p2x, 0, 'f', 4).arg(m_retracedRef.p2y, 0, 'f', 4);
    text += QString("Nrm P1: X=%1  Y=%2\n").arg(m_retracedNrm.p1x, 0, 'f', 4).arg(m_retracedNrm.p1y, 0, 'f', 4);
    text += QString("Nrm P2: X=%1  Y=%2\n\n").arg(m_retracedNrm.p2x, 0, 'f', 4).arg(m_retracedNrm.p2y, 0, 'f', 4);

    text += QString("--- U Correction (master vs target) ---\n");
    text += QString("Master angle Am (ref):     %1%2\n").arg(masterAngle, 0, 'f', 3).arg(deg);
    text += QString("Target angle At (normal):  %1%2\n").arg(targetAngle, 0, 'f', 3).arg(deg);
    text += QString("U correction ΔU = Am−At:   %1%2%3\n")
        .arg(uCorrection, 0, 'f', 3).arg(deg)
        .arg(m_set->config.applyUCorrection ? "" : "  (U correction DISABLED)");
    if (uAutoDisabled) {
        text += QString("⚠ AUTO-DISABLED U correction: |ΔU| > %1%2 threshold.\n"
                        "   Verify retraced points; re-enable the "
                        "checkbox manually if correct.\n")
                    .arg(kUAutoDisableThresholdDeg, 0, 'f', 1).arg(deg);
    }
    text += QString("Frame Δθ (XY only):        %1%2\n\n")
        .arg(frameDeltaTheta, 0, 'f', 3).arg(deg);

    text += QString("--- Design Offset (cached at teach) ---\n");
    text += QString("designOffset: dX=%1  dY=%2\n").arg(m_set->designOffsetX, 0, 'f', 4).arg(m_set->designOffsetY, 0, 'f', 4);
    text += QString("Rotated by Δθ: dX=%1  dY=%2\n\n").arg(rotatedOffX, 0, 'f', 4).arg(rotatedOffY, 0, 'f', 4);

    text += QString("--- Correction (gantry global) ---\n");
    text += QString("Ideal target @ (%1, %2)\n").arg(idealTargetX, 0, 'f', 4).arg(idealTargetY, 0, 'f', 4);
    text += QString("Hex correction: dX=%1  dY=%2\n\n").arg(corrGlobalX, 0, 'f', 4).arg(corrGlobalY, 0, 'f', 4);

    if (matrix) {
        text += QString("--- Correction (local %1) ---\n").arg(QString::fromStdString(m_set->config.targetDevice));
        text += QString("dX=%1  dY=%2  dZ=%3\n\n").arg(corrLocalX, 0, 'f', 4).arg(corrLocalY, 0, 'f', 4).arg(corrLocalZ, 0, 'f', 4);
    }

    // Comparison table: Master vs Corrected Target
    QString masterName = QString::fromStdString(m_set->config.masterPositionName);
    QString targetName = QString::fromStdString(m_set->config.targetPositionName);

    text += QString("============================================================\n");
    text += QString("  AXIS    Master \"%1\"    Corrected \"%2\"    Delta\n")
        .arg(masterName).arg(targetName);
    text += QString("============================================================\n");
    text += QString("  X     %1          %2          %3\n")
        .arg(m_set->config.masterX, 10, 'f', 4)
        .arg(m_correctedX, 10, 'f', 4)
        .arg(deltaX, 10, 'f', 4);
    text += QString("  Y     %1          %2          %3\n")
        .arg(m_set->config.masterY, 10, 'f', 4)
        .arg(m_correctedY, 10, 'f', 4)
        .arg(deltaY, 10, 'f', 4);
    text += QString("  Z     %1          %2          %3\n")
        .arg(m_set->config.masterZ, 10, 'f', 4)
        .arg(m_correctedZ, 10, 'f', 4)
        .arg(deltaZ, 10, 'f', 4);
    text += QString("  U     %1%4       %2%4       %3%4\n")
        .arg(m_set->config.masterU, 10, 'f', 4)
        .arg(m_correctedU, 10, 'f', 4)
        .arg(deltaU, 10, 'f', 4)
        .arg(deg);
    text += QString("  V     %1%4       %2%4       %3%4\n")
        .arg(m_set->config.masterV, 10, 'f', 4)
        .arg(m_correctedV, 10, 'f', 4)
        .arg(deltaV, 10, 'f', 4)
        .arg(deg);
    text += QString("  W     %1%4       %2%4       %3%4\n")
        .arg(m_set->config.masterW, 10, 'f', 4)
        .arg(m_correctedW, 10, 'f', 4)
        .arg(deltaW, 10, 'f', 4)
        .arg(deg);
    text += QString("============================================================\n");

    m_reviewLabel->setText(text);

    // Refresh the tune-offset live preview now that m_correctedX..W are valid
    updateTunePreview();

    std::cout << "[PlacementRetrace] Corrected " << m_set->config.targetPositionName
              << ": X=" << m_correctedX << " Y=" << m_correctedY << " Z=" << m_correctedZ
              << " U=" << m_correctedU << " V=" << m_correctedV << " W=" << m_correctedW << std::endl;
}

void PlacementRetraceWidget::onSaveToTarget()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    std::string device = m_set->config.targetDevice;
    std::string posName = m_set->config.targetPositionName;

    // Apply manual tune offsets — stored in mm / deg directly (native units).
    // Sign is direct-add (positive adds, negative subtracts) per user spec.
    PositionStruct corrected;
    corrected.x = m_correctedX + m_set->config.tuneOffsetX;
    corrected.y = m_correctedY + m_set->config.tuneOffsetY;
    corrected.z = m_correctedZ + m_set->config.tuneOffsetZ;
    corrected.u = m_correctedU + m_set->config.tuneOffsetU;
    corrected.v = m_correctedV + m_set->config.tuneOffsetV;
    corrected.w = m_correctedW + m_set->config.tuneOffsetW;

    m_context->motionManager->AddNamedPosition(device, posName, corrected);

    // Save to config file
    std::string localPath = "config/motor_devices.local.json";
    std::string basePath = "config/motor_devices.json";
    std::string errMsg;
    bool saved = MotionJsonLoader::SavePositionToFile(
        localPath, basePath, device, posName, corrected, true, errMsg);

    if (saved) {
        // Update set target position data (include manual tune offset)
        m_set->config.targetX = corrected.x;
        m_set->config.targetY = corrected.y;
        m_set->config.targetZ = corrected.z;
        m_set->config.targetU = corrected.u;
        m_set->config.targetV = corrected.v;
        m_set->config.targetW = corrected.w;

        QString deg = QString(QChar(0x00B0));
        QString savedMsg = m_reviewLabel->text();
        savedMsg += QString("\n\nSAVED to \"%1\" on %2\n")
            .arg(QString::fromStdString(posName))
            .arg(QString::fromStdString(device));
        m_reviewLabel->setText(savedMsg);

        m_saveBtn->setEnabled(false);
        m_saveBtn->setText(QString("Saved to \"%1\"").arg(QString::fromStdString(posName)));
        m_moveToTargetBtn->setEnabled(true);
        m_moveToTargetBtn->setText(QString("Move %1 to \"%2\"")
            .arg(QString::fromStdString(device))
            .arg(QString::fromStdString(posName)));

        // Update skip button to "Continue" now that position is saved
        m_skipBtn->setText("Continue");
        m_skipBtn->setStyleSheet(
            "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
            "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
            "QPushButton:hover { background-color: #2ecc71; }");

        writeLogEntry("saved");
        emit retraceCompleted(*m_set);

        std::cout << "[PlacementRetrace] Saved corrected position to " << device << "/" << posName << std::endl;
    } else {
        QMessageBox::warning(this, "Save Failed",
            "Could not save position: " + QString::fromStdString(errMsg));
    }
}

void PlacementRetraceWidget::onMoveToTarget()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    std::string device = m_set->config.targetDevice;
    std::string posName = m_set->config.targetPositionName;

    m_moveToTargetBtn->setEnabled(false);
    m_moveToTargetBtn->setText("Moving...");

    auto* mm = m_context->motionManager;
    runMotionAsync(
        [mm, device, posName]() {
            return mm->MoveToNamedPosition(device, posName, true);
        },
        [this, device, posName](bool ok) {
            if (ok) {
                m_moveToTargetBtn->setText(QString("Moved %1 to \"%2\"")
                    .arg(QString::fromStdString(device))
                    .arg(QString::fromStdString(posName)));
                std::cout << "[PlacementRetrace] Moved " << device << " to " << posName << std::endl;
            } else {
                m_moveToTargetBtn->setText("Move FAILED");
                m_moveToTargetBtn->setEnabled(true);
                QMessageBox::warning(this, "Move Failed",
                    QString("Failed to move %1 to \"%2\".")
                        .arg(QString::fromStdString(device))
                        .arg(QString::fromStdString(posName)));
            }
        });
}

void PlacementRetraceWidget::setHexVelocity(double velocityMmPerSec)
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    std::string device = m_set->config.targetDevice;
    bool ok = m_context->motionManager->SetSystemVelocity(device, velocityMmPerSec);

    std::cout << "[PlacementRetrace] Set " << device
              << " system velocity to " << velocityMmPerSec << " mm/s "
              << (ok ? "OK" : "FAILED") << std::endl;
}

void PlacementRetraceWidget::onReset()
{
    m_moveToTargetBtn->setEnabled(false);
    m_moveToTargetBtn->setText("Move to Target Position");
    m_saveBtn->setEnabled(true);
    QString targetName = m_set ? QString::fromStdString(m_set->config.targetPositionName) : "Target";
    m_saveBtn->setText(QString("Save to \"%1\"").arg(targetName));
    // Reset skip button back to original style
    m_skipBtn->setText("Continue Without Saving");
    m_skipBtn->setStyleSheet(
        "QPushButton { background-color: #7f8c8d; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #95a5a6; }");
    setState(Idle);
}

void PlacementRetraceWidget::onSkip()
{
    std::cout << "[PlacementRetrace] User chose to continue" << std::endl;
    m_positionTimer->stop();
    // If the user didn't save, record the skipped outcome. If a save already
    // logged this run, m_loggedThisRun guards against a duplicate entry.
    writeLogEntry("skipped");
    setState(Idle);
    emit retraceSkipped();
}

void PlacementRetraceWidget::onEnd()
{
    m_positionTimer->stop();
    writeLogEntry("ended");
    setState(Idle);
    emit retraceEnded();
}

void PlacementRetraceWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    // If a previous async query completed, update the display
    if (m_positionReady.exchange(false)) {
        QString text = QString("X: %1  Y: %2  Z: %3")
            .arg(m_cachedPosX, 10, 'f', 4)
            .arg(m_cachedPosY, 10, 'f', 4)
            .arg(m_cachedPosZ, 10, 'f', 4);

        switch (m_state) {
        case WaitRefP1: m_refP1PositionLabel->setText(text); break;
        case WaitRefP2: m_refP2PositionLabel->setText(text); break;
        case WaitNrmP1: m_nrmP1PositionLabel->setText(text); break;
        case WaitNrmP2: m_nrmP2PositionLabel->setText(text); break;
        default: break;
        }
    }

    // Launch async position query for next timer tick (skip if previous query still running)
    if (!m_positionFuture.valid() ||
        m_positionFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        auto* mm = m_context->motionManager;
        std::string dev = m_set->measureDevice;
        m_positionFuture = std::async(std::launch::async, [this, mm, dev]() {
            auto pos = mm->GetCurrentPosition(dev);
            m_cachedPosX = pos.x;
            m_cachedPosY = pos.y;
            m_cachedPosZ = pos.z;
            m_positionReady.store(true);
        });
    }
}

// ============================================================================
// Manual Offset Tuner — slots and helpers
// ============================================================================

void PlacementRetraceWidget::loadTuneOffsetsFromConfig()
{
    if (!m_set || !m_tuneOffsetXSpin) return;

    // Block valueChanged signals so we don't fire onTuneOffsetChanged during
    // programmatic population.
    QSignalBlocker bX(m_tuneOffsetXSpin);
    QSignalBlocker bY(m_tuneOffsetYSpin);
    QSignalBlocker bZ(m_tuneOffsetZSpin);
    QSignalBlocker bU(m_tuneOffsetUSpin);
    QSignalBlocker bV(m_tuneOffsetVSpin);
    QSignalBlocker bW(m_tuneOffsetWSpin);

    m_tuneOffsetXSpin->setValue(m_set->config.tuneOffsetX);
    m_tuneOffsetYSpin->setValue(m_set->config.tuneOffsetY);
    m_tuneOffsetZSpin->setValue(m_set->config.tuneOffsetZ);
    m_tuneOffsetUSpin->setValue(m_set->config.tuneOffsetU);
    m_tuneOffsetVSpin->setValue(m_set->config.tuneOffsetV);
    m_tuneOffsetWSpin->setValue(m_set->config.tuneOffsetW);

    updateTunePreview();
}

void PlacementRetraceWidget::updateTunePreview()
{
    if (!m_tunePreviewLabel) return;
    if (!m_set) {
        m_tunePreviewLabel->setText("(No training set loaded)");
        return;
    }

    const double offX = m_set->config.tuneOffsetX;
    const double offY = m_set->config.tuneOffsetY;
    const double offZ = m_set->config.tuneOffsetZ;
    const double offU = m_set->config.tuneOffsetU;
    const double offV = m_set->config.tuneOffsetV;
    const double offW = m_set->config.tuneOffsetW;

    // Only show a meaningful preview once the retrace has computed a result.
    // Before Review is reached, m_correctedX..W are still zero-initialized.
    if (m_state != Review) {
        QString text = QString("Offsets staged (applied at Save):\n"
                               "  dX=%1 mm  dY=%2 mm  dZ=%3 mm\n"
                               "  dU=%4 deg  dV=%5 deg  dW=%6 deg")
            .arg(offX, 0, 'f', 4)
            .arg(offY, 0, 'f', 4)
            .arg(offZ, 0, 'f', 4)
            .arg(offU, 0, 'f', 4)
            .arg(offV, 0, 'f', 4)
            .arg(offW, 0, 'f', 4);
        m_tunePreviewLabel->setText(text);
        return;
    }

    const double finalX = m_correctedX + offX;
    const double finalY = m_correctedY + offY;
    const double finalZ = m_correctedZ + offZ;
    const double finalU = m_correctedU + offU;
    const double finalV = m_correctedV + offV;
    const double finalW = m_correctedW + offW;

    QString text;
    text += QString("Corrected (pre-offset):\n");
    text += QString("  X=%1  Y=%2  Z=%3\n")
        .arg(m_correctedX, 10, 'f', 4)
        .arg(m_correctedY, 10, 'f', 4)
        .arg(m_correctedZ, 10, 'f', 4);
    text += QString("  U=%1  V=%2  W=%3\n")
        .arg(m_correctedU, 10, 'f', 4)
        .arg(m_correctedV, 10, 'f', 4)
        .arg(m_correctedW, 10, 'f', 4);
    text += QString("Final to SAVE (with offset):\n");
    text += QString("  X=%1  Y=%2  Z=%3\n")
        .arg(finalX, 10, 'f', 4)
        .arg(finalY, 10, 'f', 4)
        .arg(finalZ, 10, 'f', 4);
    text += QString("  U=%1  V=%2  W=%3")
        .arg(finalU, 10, 'f', 4)
        .arg(finalV, 10, 'f', 4)
        .arg(finalW, 10, 'f', 4);

    m_tunePreviewLabel->setText(text);
}

void PlacementRetraceWidget::onTuneOffsetChanged()
{
    if (!m_set || !m_tuneOffsetXSpin) return;

    m_set->config.tuneOffsetX = m_tuneOffsetXSpin->value();
    m_set->config.tuneOffsetY = m_tuneOffsetYSpin->value();
    m_set->config.tuneOffsetZ = m_tuneOffsetZSpin->value();
    m_set->config.tuneOffsetU = m_tuneOffsetUSpin->value();
    m_set->config.tuneOffsetV = m_tuneOffsetVSpin->value();
    m_set->config.tuneOffsetW = m_tuneOffsetWSpin->value();

    updateTunePreview();

    // Re-enable Save button if it was disabled after a previous save — the
    // user has changed the offset, so the pending "save" now needs to apply
    // the new offset.
    if (m_state == Review && m_saveBtn && !m_saveBtn->isEnabled()) {
        m_saveBtn->setEnabled(true);
        QString targetName = QString::fromStdString(m_set->config.targetPositionName);
        m_saveBtn->setText(QString("Save to \"%1\"").arg(targetName));
    }

    emit offsetsPersistRequested();
}

// ============================================================================
// Vision auto-detect
// ============================================================================

void PlacementRetraceWidget::updateAutoDetectVisibility()
{
    if (!m_set) {
        if (m_autoDetectRefP1Btn) m_autoDetectRefP1Btn->setVisible(false);
        if (m_autoDetectRefP2Btn) m_autoDetectRefP2Btn->setVisible(false);
        if (m_autoDetectNrmP1Btn) m_autoDetectNrmP1Btn->setVisible(false);
        if (m_autoDetectNrmP2Btn) m_autoDetectNrmP2Btn->setVisible(false);
        return;
    }
    if (m_autoDetectRefP1Btn) m_autoDetectRefP1Btn->setVisible(!m_set->referenceLine.p1VisionProfilePath.empty());
    if (m_autoDetectRefP2Btn) m_autoDetectRefP2Btn->setVisible(!m_set->referenceLine.p2VisionProfilePath.empty());
    if (m_autoDetectNrmP1Btn) m_autoDetectNrmP1Btn->setVisible(!m_set->normalPlacementLine.p1VisionProfilePath.empty());
    if (m_autoDetectNrmP2Btn) m_autoDetectNrmP2Btn->setVisible(!m_set->normalPlacementLine.p2VisionProfilePath.empty());
}

void PlacementRetraceWidget::onAutoDetectRefP1()
{
    if (m_set) runAutoDetect(1, m_set->referenceLine.p1VisionProfilePath);
}
void PlacementRetraceWidget::onAutoDetectRefP2()
{
    if (m_set) runAutoDetect(2, m_set->referenceLine.p2VisionProfilePath);
}
void PlacementRetraceWidget::onAutoDetectNrmP1()
{
    if (m_set) runAutoDetect(3, m_set->normalPlacementLine.p1VisionProfilePath);
}
void PlacementRetraceWidget::onAutoDetectNrmP2()
{
    if (m_set) runAutoDetect(4, m_set->normalPlacementLine.p2VisionProfilePath);
}

// Iterate detect→move→detect until residual <5um or 5 iterations.
// Z is preserved across moves. On success, triggers the same path as a manual
// Confirm click for `step`. On failure, leaves state and re-enables manual buttons.
void PlacementRetraceWidget::runAutoDetect(int step, const std::string& profilePath)
{
    if (!m_context || !m_context->motionManager || !m_set) return;
    if (profilePath.empty()) return;
    if (m_set->config.cameraId.empty()) {
        QMessageBox::warning(this, "Auto-Detect", "Camera ID not configured for this set.");
        return;
    }

    // Disable both buttons for this step while detecting
    auto setStepEnabled = [this, step](bool en) {
        switch (step) {
            case 1:
                m_confirmRefP1Btn->setEnabled(en);
                if (m_autoDetectRefP1Btn) m_autoDetectRefP1Btn->setEnabled(en);
                m_refP1InstructionLabel->setText(en
                    ? QString("Step 1/4: Verify position and press Confirm.")
                    : QString("Step 1/4: Executing vision positioning..."));
                break;
            case 2:
                m_confirmRefP2Btn->setEnabled(en);
                if (m_autoDetectRefP2Btn) m_autoDetectRefP2Btn->setEnabled(en);
                m_refP2InstructionLabel->setText(en
                    ? QString("Step 2/4: Verify position and press Confirm.")
                    : QString("Step 2/4: Executing vision positioning..."));
                break;
            case 3:
                m_confirmNrmP1Btn->setEnabled(en);
                if (m_autoDetectNrmP1Btn) m_autoDetectNrmP1Btn->setEnabled(en);
                m_nrmP1InstructionLabel->setText(en
                    ? QString("Step 3/4: Verify position and press Confirm.")
                    : QString("Step 3/4: Executing vision positioning..."));
                break;
            case 4:
                m_confirmNrmP2Btn->setEnabled(en);
                if (m_autoDetectNrmP2Btn) m_autoDetectNrmP2Btn->setEnabled(en);
                m_nrmP2InstructionLabel->setText(en
                    ? QString("Step 4/4: Verify position and press Confirm.")
                    : QString("Step 4/4: Executing vision positioning..."));
                break;
            default: break;
        }
    };
    setStepEnabled(false);

    constexpr double CONVERGE_MM = 0.005;
    constexpr int MAX_ITERS = 5;

    std::string device = m_set->measureDevice;
    std::string camera = m_set->config.cameraId;
    auto* mm = m_context->motionManager;
    auto* appCtx = m_context;

    m_visionFuture = std::async(std::launch::async,
        [this, step, profilePath, device, camera, mm, appCtx, setStepEnabled]() {
        bool success = false;
        std::string lastError;
        int iters = 0;
        double lastResidual = 0.0;

        for (int i = 0; i < MAX_ITERS; ++i) {
            iters = i + 1;
            VisionCommandHandler h(appCtx);
            SequenceContext sctx;
            std::map<std::string, std::string> params;
            params["profile_path"]             = profilePath;
            params["camera"]                   = camera;
            params["motion_device"]            = device;
            params["calculate_world_position"] = "true";
            std::string err;

            bool callOk = h.ExecuteCommand(nullptr, "vision", "execute_profile",
                                           params, err, &sctx);
            bool vOk = sctx.GetBoolean("vision.result.success", false);
            if (!callOk || !vOk) {
                lastError = !err.empty() ? err
                          : std::string("vision profile execution failed");
                break;
            }

            // VisionCommandHandler reports success=true even when the pattern
            // match found nothing (it just skips writing world_x/y_mm). Guard
            // against trusting default-zero coords by requiring the keys to
            // actually exist in the context.
            if (!sctx.HasValue("vision.detected.world_x_mm")
                || !sctx.HasValue("vision.detected.world_y_mm")) {
                lastError = "vision profile ran but found no feature "
                            "(no anchor/corner/center match)";
                break;
            }

            double wx  = sctx.GetNumeric("vision.detected.world_x_mm", 0.0f);
            double wy  = sctx.GetNumeric("vision.detected.world_y_mm", 0.0f);
            double dvx = sctx.GetNumeric("vision.detected.device_x_mm", 0.0f);
            double dvy = sctx.GetNumeric("vision.detected.device_y_mm", 0.0f);
            lastResidual = std::hypot(wx - dvx, wy - dvy);

            std::cout << "[PlacementRetrace] AutoDetect step=" << step
                      << " iter=" << iters
                      << " world=(" << wx << ", " << wy << ")"
                      << " residual=" << lastResidual << " mm" << std::endl;

            if (lastResidual < CONVERGE_MM) {
                success = true;
                break;
            }

            // Move to detected world XY, keep Z
            auto cur = mm->GetCurrentPosition(device);
            if (!mm->MoveToAbs(device, wx, wy, cur.z)) {
                lastError = "MoveToAbs failed during auto-detect iteration";
                break;
            }
        }

        // Marshal back to UI thread for state update
        QMetaObject::invokeMethod(this,
            [this, step, success, lastError, iters, lastResidual, setStepEnabled]() {
            if (success) {
                std::cout << "[PlacementRetrace] AutoDetect step=" << step
                          << " converged in " << iters << " iter(s), residual="
                          << lastResidual << " mm" << std::endl;
                // Stamp per-step meta so the log records this was auto-detected
                if (step >= 1 && step <= 4) {
                    m_stepMeta[step - 1] = RetracePointMeta{
                        /*autoDetected*/ true,
                        /*visionIters*/  iters,
                        /*finalResidualMm*/ lastResidual,
                        /*reason*/ "auto"
                    };
                }
                // Re-enable so the slot can drive UI; then dispatch as if Confirm was pressed
                setStepEnabled(true);
                switch (step) {
                    case 1: onConfirmRefP1(); break;
                    case 2: onConfirmRefP2(); break;
                    case 3: onConfirmNrmP1(); break;
                    case 4: onConfirmNrmP2(); break;
                    default: break;
                }
            } else {
                m_autoRunAll = false;  // stop chain on any failure
                QMessageBox::warning(this, "Auto-Detect Failed",
                    QString("Auto-detect failed after %1 iter(s):\n%2\n\n"
                            "Verify position manually and press Confirm.")
                        .arg(iters)
                        .arg(QString::fromStdString(lastError)));
                setStepEnabled(true);
            }
        }, Qt::QueuedConnection);
    });
}

void PlacementRetraceWidget::performAutoSaveContinue()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        std::cerr << "[PlacementRetrace] Auto-save-continue aborted: missing context/set" << std::endl;
        return;
    }

    std::cout << "[PlacementRetrace] Auto-save-continue: saving corrected position" << std::endl;

    onSaveToTarget();

    // onSaveToTarget disables the save button on success
    if (m_saveBtn && m_saveBtn->isEnabled()) {
        std::cerr << "[PlacementRetrace] Auto-save failed — falling back to manual review" << std::endl;
        return;
    }

    std::cout << "[PlacementRetrace] Auto-save-continue: moving to target" << std::endl;

    std::string device = m_set->config.targetDevice;
    std::string posName = m_set->config.targetPositionName;

    if (m_moveToTargetBtn) {
        m_moveToTargetBtn->setEnabled(false);
        m_moveToTargetBtn->setText("Moving...");
    }

    auto* mm = m_context->motionManager;
    runMotionAsync(
        [mm, device, posName]() {
            return mm->MoveToNamedPosition(device, posName, true);
        },
        [this, device, posName](bool ok) {
            if (ok) {
                if (m_moveToTargetBtn) {
                    m_moveToTargetBtn->setText(QString("Moved %1 to \"%2\"")
                        .arg(QString::fromStdString(device))
                        .arg(QString::fromStdString(posName)));
                }
                std::cout << "[PlacementRetrace] Auto-save-continue: move complete, continuing recipe"
                          << std::endl;
                QTimer::singleShot(0, this, [this]() { onSkip(); });
            } else {
                if (m_moveToTargetBtn) {
                    m_moveToTargetBtn->setText("Move FAILED");
                    m_moveToTargetBtn->setEnabled(true);
                }
                std::cerr << "[PlacementRetrace] Auto-save-continue: move failed — "
                          << "falling back to manual review" << std::endl;
            }
        });
}

void PlacementRetraceWidget::writeLogEntry(const std::string& outcome,
                                             const std::string& failureStep,
                                             const std::string& failureDetail)
{
    if (!m_set) return;
    if (m_loggedThisRun && outcome != "failed") {
        // Already logged a primary outcome for this run (e.g. "saved"); don't
        // append a second entry when the user hits Continue/End afterward.
        return;
    }

    RetraceLogEntry e;
    e.timestamp           = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    e.setName             = m_set->name;
    e.setUid              = m_set->uid;
    e.mode                = m_logMode;
    e.outcome             = outcome;
    e.measureDevice       = m_set->measureDevice;
    e.targetDevice        = m_set->config.targetDevice;
    e.masterPositionName  = m_set->config.masterPositionName;
    e.targetPositionName  = m_set->config.targetPositionName;

    e.retracedRefP1x = m_retracedRef.p1x; e.retracedRefP1y = m_retracedRef.p1y; e.retracedRefP1z = m_retracedRef.p1z;
    e.retracedRefP2x = m_retracedRef.p2x; e.retracedRefP2y = m_retracedRef.p2y; e.retracedRefP2z = m_retracedRef.p2z;
    e.retracedNrmP1x = m_retracedNrm.p1x; e.retracedNrmP1y = m_retracedNrm.p1y; e.retracedNrmP1z = m_retracedNrm.p1z;
    e.retracedNrmP2x = m_retracedNrm.p2x; e.retracedNrmP2y = m_retracedNrm.p2y; e.retracedNrmP2z = m_retracedNrm.p2z;
    e.retracedRefAngleDeg = m_retracedRef.angleDeg;
    e.retracedNrmAngleDeg = m_retracedNrm.angleDeg;

    e.taughtRefAngleDeg   = m_set->referenceLine.angleDeg;
    e.frameDeltaThetaDeg  = m_logFrameDeltaTheta;
    e.uCorrectionDeg      = m_logUCorrection;

    e.designOffsetX = m_set->designOffsetX;
    e.designOffsetY = m_set->designOffsetY;
    e.rotatedOffsetX = m_logRotatedOffsetX;
    e.rotatedOffsetY = m_logRotatedOffsetY;

    e.idealTargetX = m_logIdealTargetX;
    e.idealTargetY = m_logIdealTargetY;
    e.correctionGlobalX = m_logCorrGlobalX;
    e.correctionGlobalY = m_logCorrGlobalY;
    e.correctionLocalX  = m_logCorrLocalX;
    e.correctionLocalY  = m_logCorrLocalY;
    e.correctionLocalZ  = m_logCorrLocalZ;

    e.liveHexX = m_liveHexX; e.liveHexY = m_liveHexY; e.liveHexZ = m_liveHexZ;
    e.liveHexU = m_liveHexU; e.liveHexV = m_liveHexV; e.liveHexW = m_liveHexW;

    e.tuneOffsetX = m_set->config.tuneOffsetX;
    e.tuneOffsetY = m_set->config.tuneOffsetY;
    e.tuneOffsetZ = m_set->config.tuneOffsetZ;
    e.tuneOffsetU = m_set->config.tuneOffsetU;
    e.tuneOffsetV = m_set->config.tuneOffsetV;
    e.tuneOffsetW = m_set->config.tuneOffsetW;
    e.applyUCorrection = m_set->config.applyUCorrection;

    e.correctedX = m_correctedX; e.correctedY = m_correctedY; e.correctedZ = m_correctedZ;
    e.correctedU = m_correctedU; e.correctedV = m_correctedV; e.correctedW = m_correctedW;

    for (int i = 0; i < 4; ++i) e.steps[i] = m_stepMeta[i];

    e.failureStep   = failureStep;
    e.failureDetail = failureDetail;

    void* ppm = (m_context && m_context->productPositionManager)
        ? static_cast<void*>(m_context->productPositionManager)
        : nullptr;
    std::string logPath = PlacementRetraceLogger::ResolvePathForSet(*m_set, ppm);
    if (PlacementRetraceLogger::Append(logPath, e)) {
        m_loggedThisRun = true;
    }
}

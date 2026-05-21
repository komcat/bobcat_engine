// MultiDeviceRunAlignmentWidget.cpp - Multi-device run alignment mode
#include "MultiDeviceRunAlignmentWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <iostream>
#include <cmath>

#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include <nlohmann/json.hpp>
#include <fstream>
#endif

MultiDeviceRunAlignmentWidget::MultiDeviceRunAlignmentWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
{
    setupUI();

    // Load transformation matrices for hex devices
    m_matrixLoader.LoadFromFile("config/transformation_matrix.json");

    connect(m_positionTimer, &QTimer::timeout, this, &MultiDeviceRunAlignmentWidget::updatePositionDisplay);
}

void MultiDeviceRunAlignmentWidget::SetTrainingSet(TrainingSet* ts)
{
    m_trainingSet = ts;
    setState(Ready);
}

void MultiDeviceRunAlignmentWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Run Multi-Device Alignment");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    // Main content area
    QHBoxLayout* contentLayout = new QHBoxLayout();

    // Left: wizard stack
    m_wizardStack = new QStackedWidget();

    // ========================================
    // Page 0: Ready
    // ========================================
    QWidget* readyPage = new QWidget();
    QVBoxLayout* readyLayout = new QVBoxLayout(readyPage);

    m_readyLabel = new QLabel();
    m_readyLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_readyLabel->setWordWrap(true);
    readyLayout->addWidget(m_readyLabel);

    // Hex device checkboxes
    QGroupBox* hexGroup = new QGroupBox("Hex Devices to Register");
    hexGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* hexCheckLayout = new QVBoxLayout(hexGroup);

    m_hexLeftCheck = new QCheckBox("hex-left");
    m_hexLeftCheck->setStyleSheet("color: #ccc; font-size: 13px; padding: 4px;");
    m_hexRightCheck = new QCheckBox("hex-right");
    m_hexRightCheck->setStyleSheet("color: #ccc; font-size: 13px; padding: 4px;");

    hexCheckLayout->addWidget(m_hexLeftCheck);
    hexCheckLayout->addWidget(m_hexRightCheck);

    QLabel* hexHint = new QLabel(
        "Check devices whose end-effector positions should be\n"
        "registered via 3-corner capture with the gantry camera.");
    hexHint->setStyleSheet("color: #777; font-size: 11px; padding: 4px;");
    hexHint->setWordWrap(true);
    hexCheckLayout->addWidget(hexHint);

    readyLayout->addWidget(hexGroup);
    readyLayout->addStretch();

    m_startBtn = new QPushButton("Start Multi-Device Alignment");
    m_startBtn->setFixedHeight(45);
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 16px; border-radius: 4px; padding: 10px 30px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #555; color: #888; }");
    connect(m_startBtn, &QPushButton::clicked, this, &MultiDeviceRunAlignmentWidget::onStartAlignment);
    readyLayout->addWidget(m_startBtn);

    m_wizardStack->addWidget(readyPage);  // index 0

    // ========================================
    // Page 1: Ref adjustment
    // ========================================
    QWidget* refPage = new QWidget();
    QVBoxLayout* refLayout = new QVBoxLayout(refPage);

    m_refInstructionLabel = new QLabel();
    m_refInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_refInstructionLabel->setWordWrap(true);
    m_refInstructionLabel->setAlignment(Qt::AlignCenter);
    refLayout->addWidget(m_refInstructionLabel);

    QGroupBox* refPosGroup = new QGroupBox("Current Position");
    refPosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* refPosLayout = new QVBoxLayout(refPosGroup);
    m_refPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_refPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_refPositionLabel->setAlignment(Qt::AlignCenter);
    refPosLayout->addWidget(m_refPositionLabel);
    refLayout->addWidget(refPosGroup);

    refLayout->addStretch();

    QHBoxLayout* refBtnRow = new QHBoxLayout();
    m_captureRefBtn = new QPushButton("Capture Adjusted Position");
    m_captureRefBtn->setFixedHeight(40);
    m_captureRefBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }");
    m_cancelRefBtn = new QPushButton("Cancel");
    m_cancelRefBtn->setFixedHeight(40);
    m_cancelRefBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }");
    refBtnRow->addWidget(m_captureRefBtn);
    refBtnRow->addWidget(m_cancelRefBtn);
    refLayout->addLayout(refBtnRow);

    connect(m_captureRefBtn, &QPushButton::clicked, this, &MultiDeviceRunAlignmentWidget::onCaptureActualRef);
    connect(m_cancelRefBtn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(Ready);
    });

    m_wizardStack->addWidget(refPage);  // index 1

    // ========================================
    // Page 2: Hex registration corners
    // ========================================
    QWidget* hexRegPage = new QWidget();
    QVBoxLayout* hexRegLayout = new QVBoxLayout(hexRegPage);

    m_hexInstructionLabel = new QLabel();
    m_hexInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #e0a0ff; font-weight: bold; padding: 15px; "
        "background-color: #2d2e5f; border-radius: 6px;");
    m_hexInstructionLabel->setWordWrap(true);
    m_hexInstructionLabel->setAlignment(Qt::AlignCenter);
    hexRegLayout->addWidget(m_hexInstructionLabel);

    QGroupBox* hexPosGroup = new QGroupBox("Gantry Position");
    hexPosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* hexPosLayout = new QVBoxLayout(hexPosGroup);
    m_hexPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_hexPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_hexPositionLabel->setAlignment(Qt::AlignCenter);
    hexPosLayout->addWidget(m_hexPositionLabel);
    hexRegLayout->addWidget(hexPosGroup);

    hexRegLayout->addStretch();

    QHBoxLayout* hexRegBtnRow = new QHBoxLayout();
    m_captureHexCornerBtn = new QPushButton("Capture Corner");
    m_captureHexCornerBtn->setFixedHeight(40);
    m_captureHexCornerBtn->setStyleSheet(
        "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #9b59b6; }");
    m_skipHexCornerBtn = new QPushButton("Skip (2 corners only)");
    m_skipHexCornerBtn->setFixedHeight(40);
    m_skipHexCornerBtn->setStyleSheet(
        "QPushButton { background-color: #7f8c8d; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #95a5a6; }");
    m_skipHexCornerBtn->setVisible(false);
    m_cancelHexBtn = new QPushButton("Cancel");
    m_cancelHexBtn->setFixedHeight(40);
    m_cancelHexBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }");

    hexRegBtnRow->addWidget(m_captureHexCornerBtn);
    hexRegBtnRow->addWidget(m_skipHexCornerBtn);
    hexRegBtnRow->addWidget(m_cancelHexBtn);
    hexRegLayout->addLayout(hexRegBtnRow);

    connect(m_captureHexCornerBtn, &QPushButton::clicked, this, &MultiDeviceRunAlignmentWidget::onCaptureHexCorner);
    connect(m_skipHexCornerBtn, &QPushButton::clicked, this, &MultiDeviceRunAlignmentWidget::onSkipHexCorner3);
    connect(m_cancelHexBtn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(Ready);
    });

    m_wizardStack->addWidget(hexRegPage);  // index 2

    // ========================================
    // Page 3: Hex registration summary
    // ========================================
    QWidget* hexSummaryPage = new QWidget();
    QVBoxLayout* hexSummaryLayout = new QVBoxLayout(hexSummaryPage);

    m_hexSummaryLabel = new QLabel();
    m_hexSummaryLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #8e44ad; border-radius: 6px;");
    m_hexSummaryLabel->setWordWrap(true);
    hexSummaryLayout->addWidget(m_hexSummaryLabel);

    hexSummaryLayout->addStretch();

    QHBoxLayout* hexSummaryBtnRow = new QHBoxLayout();
    m_confirmHexBtn = new QPushButton("Confirm Registration");
    m_confirmHexBtn->setFixedHeight(40);
    m_confirmHexBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }");
    m_redoHexBtn = new QPushButton("Redo");
    m_redoHexBtn->setFixedHeight(40);
    m_redoHexBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #f39c12; }");

    hexSummaryBtnRow->addWidget(m_confirmHexBtn);
    hexSummaryBtnRow->addWidget(m_redoHexBtn);
    hexSummaryLayout->addLayout(hexSummaryBtnRow);

    connect(m_confirmHexBtn, &QPushButton::clicked, this, &MultiDeviceRunAlignmentWidget::onConfirmHexRegistration);
    connect(m_redoHexBtn, &QPushButton::clicked, this, &MultiDeviceRunAlignmentWidget::onRedoHexRegistration);

    m_wizardStack->addWidget(hexSummaryPage);  // index 3

    // ========================================
    // Page 4: Bonding (moves with pre-assigned device)
    // ========================================
    QWidget* bondPage = new QWidget();
    QVBoxLayout* bondLayout = new QVBoxLayout(bondPage);

    m_bondInstructionLabel = new QLabel();
    m_bondInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #00ff88; font-weight: bold; padding: 15px; "
        "background-color: #1a3a2f; border-radius: 6px;");
    m_bondInstructionLabel->setWordWrap(true);
    m_bondInstructionLabel->setAlignment(Qt::AlignCenter);
    bondLayout->addWidget(m_bondInstructionLabel);

    QGroupBox* bondPosGroup = new QGroupBox("Current Position");
    bondPosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* bondPosLayout = new QVBoxLayout(bondPosGroup);
    m_bondPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_bondPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_bondPositionLabel->setAlignment(Qt::AlignCenter);
    bondPosLayout->addWidget(m_bondPositionLabel);
    bondLayout->addWidget(bondPosGroup);

    bondLayout->addStretch();

    QHBoxLayout* bondBtnRow = new QHBoxLayout();
    m_prevBondBtn = new QPushButton("Back");
    m_prevBondBtn->setFixedHeight(40);
    m_prevBondBtn->setStyleSheet(
        "QPushButton { background-color: #7f8c8d; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #95a5a6; }");
    m_nextBondBtn = new QPushButton("Next Bonding Point");
    m_nextBondBtn->setFixedHeight(40);
    m_nextBondBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }");

    bondBtnRow->addWidget(m_prevBondBtn);
    bondBtnRow->addWidget(m_nextBondBtn);
    bondLayout->addLayout(bondBtnRow);

    connect(m_prevBondBtn, &QPushButton::clicked, this, &MultiDeviceRunAlignmentWidget::onMoveToPrevBonding);
    connect(m_nextBondBtn, &QPushButton::clicked, this, &MultiDeviceRunAlignmentWidget::onMoveToNextBonding);

    m_wizardStack->addWidget(bondPage);  // index 4

    // ========================================
    // Page 5: Done
    // ========================================
    QWidget* donePage = new QWidget();
    QVBoxLayout* doneLayout = new QVBoxLayout(donePage);

    m_doneLabel = new QLabel();
    m_doneLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #27ae60; border-radius: 6px;");
    m_doneLabel->setWordWrap(true);
    doneLayout->addWidget(m_doneLabel);

    doneLayout->addStretch();

    m_finishBtn = new QPushButton("Finish");
    m_finishBtn->setFixedHeight(40);
    m_finishBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }");
    connect(m_finishBtn, &QPushButton::clicked, this, &MultiDeviceRunAlignmentWidget::onFinish);
    doneLayout->addWidget(m_finishBtn);

    m_wizardStack->addWidget(donePage);  // index 5

    contentLayout->addWidget(m_wizardStack, 6);

    // Right: log view
    m_logView = new QTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(
        "QTextEdit { background-color: #0a0f1a; color: #aaa; border: 1px solid #3d5a8c; "
        "font-family: monospace; font-size: 11px; padding: 5px; }");
    contentLayout->addWidget(m_logView, 4);

    mainLayout->addLayout(contentLayout, 1);
}

void MultiDeviceRunAlignmentWidget::setState(AlignState state)
{
    m_state = state;

    switch (state) {
    case Ready: {
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        m_logView->clear();

        if (!m_trainingSet || !m_trainingSet->valid) {
            m_readyLabel->setText(
                "Training set is not ready.\n\n"
                "Please complete:\n"
                "1. Teach at least one bonding position\n"
                "2. Teach reference points and compute the frame");
            m_startBtn->setEnabled(false);
            m_hexLeftCheck->setEnabled(false);
            m_hexRightCheck->setEnabled(false);
        } else {
            int refCount = static_cast<int>(m_trainingSet->referencePoints.size());
            int bondCount = static_cast<int>(m_trainingSet->bondingPoints.size());
            int visionCount = 0;
            for (const auto& rp : m_trainingSet->referencePoints) {
                if (!rp.visionProfilePath.empty()) visionCount++;
            }
            QString visionNote = visionCount > 0
                ? QString("\nVision-enabled refs: %1 (auto-detect & center)").arg(visionCount)
                : QString("\nNo vision profiles (manual adjustment only)");
            m_readyLabel->setText(QString(
                "Training Set: %1\n"
                "Device: %2\n\n"
                "Reference Points: %3\n"
                "Bonding Points: %4%5\n\n"
                "The multi-device alignment process will:\n"
                "1. Move to each taught reference point\n"
                "2. Auto-detect via vision (or manual adjust if no profile)\n"
                "3. Compute the correction transform\n"
                "4. Register hex device end-effectors (3-corner capture)\n"
                "5. Move to corrected bonding positions\n"
                "    (using device assignments from Teach step)")
                .arg(QString::fromStdString(m_trainingSet->name))
                .arg(QString::fromStdString(m_trainingSet->deviceName))
                .arg(refCount)
                .arg(bondCount)
                .arg(visionNote));
            m_startBtn->setEnabled(true);

            // Enable hex checkboxes only if transformation matrices exist
            m_hexLeftCheck->setEnabled(m_matrixLoader.HasDevice("hex-left"));
            m_hexRightCheck->setEnabled(m_matrixLoader.HasDevice("hex-right"));

            if (!m_matrixLoader.HasDevice("hex-left")) {
                m_hexLeftCheck->setToolTip("No transformation matrix found in config/transformation_matrix.json");
            }
            if (!m_matrixLoader.HasDevice("hex-right")) {
                m_hexRightCheck->setToolTip("No transformation matrix found in config/transformation_matrix.json");
            }

            // Append "(taught)" and "(vision)" to labels if teach data exists
            bool hasLeftTeach = false, hasRightTeach = false;
            bool hasLeftVision = false, hasRightVision = false;
            if (m_trainingSet) {
                for (const auto& hd : m_trainingSet->hexDevicePositions) {
                    if (hd.deviceName == "hex-left" && hd.valid) {
                        hasLeftTeach = true;
                        hasLeftVision = !hd.visionProfilePath.empty();
                    }
                    if (hd.deviceName == "hex-right" && hd.valid) {
                        hasRightTeach = true;
                        hasRightVision = !hd.visionProfilePath.empty();
                    }
                }
            }
            QString leftLabel = "hex-left";
            if (hasLeftTeach) leftLabel += hasLeftVision ? " (taught+vision)" : " (taught)";
            QString rightLabel = "hex-right";
            if (hasRightTeach) rightLabel += hasRightVision ? " (taught+vision)" : " (taught)";
            m_hexLeftCheck->setText(leftLabel);
            m_hexRightCheck->setText(rightLabel);
        }
        break;
    }

    case MoveToRef:
    case AdjustRef:
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer->start(100);
        break;

    case Computing:
        m_positionTimer->stop();
        break;

    case RegisterHex_Corner1:
        m_wizardStack->setCurrentIndex(2);
        m_hexInstructionLabel->setText(QString(
            "Hex Registration: %1\n\n"
            "Step 1/3: Jog GANTRY camera to the TOP-LEFT corner\n"
            "of the %1 end-effector tip,\nthen click 'Capture Corner'")
            .arg(QString::fromStdString(m_pendingHexReg.deviceName)));
        m_skipHexCornerBtn->setVisible(false);
        m_positionTimer->start(100);
        break;

    case RegisterHex_Corner2:
        m_wizardStack->setCurrentIndex(2);
        m_hexInstructionLabel->setText(QString(
            "Hex Registration: %1\n\n"
            "Step 2/3: Jog GANTRY camera to the BOTTOM-RIGHT corner\n"
            "of the %1 end-effector tip,\nthen click 'Capture Corner'")
            .arg(QString::fromStdString(m_pendingHexReg.deviceName)));
        m_skipHexCornerBtn->setVisible(false);
        m_positionTimer->start(100);
        break;

    case RegisterHex_Corner3:
        m_wizardStack->setCurrentIndex(2);
        m_hexInstructionLabel->setText(QString(
            "Hex Registration: %1\n\n"
            "Step 3/3 (Optional): Jog GANTRY camera to the TOP-RIGHT corner\n"
            "of the %1 end-effector tip.\n\nClick 'Capture Corner' or 'Skip'")
            .arg(QString::fromStdString(m_pendingHexReg.deviceName)));
        m_skipHexCornerBtn->setVisible(true);
        m_positionTimer->start(100);
        break;

    case RegisterHex_Summary:
        m_wizardStack->setCurrentIndex(3);
        m_positionTimer->stop();
        break;

    case MoveToBonding:
        m_wizardStack->setCurrentIndex(4);
        m_positionTimer->start(100);
        break;

    case Done:
        m_wizardStack->setCurrentIndex(5);
        m_positionTimer->stop();
        break;
    }
}

void MultiDeviceRunAlignmentWidget::onStartAlignment()
{
    if (!m_trainingSet || !m_trainingSet->valid) return;

    m_actualRefs.clear();
    m_correctedBonds.clear();
    m_hexRegistrations.clear();
    m_hexDevicesToRegister.clear();
    m_currentRefIndex = 0;
    m_currentBondIndex = 0;
    m_currentHexRegIndex = 0;
    m_logView->clear();

    // Collect checked hex devices
    if (m_hexLeftCheck->isChecked()) {
        m_hexDevicesToRegister.push_back("hex-left");
    }
    if (m_hexRightCheck->isChecked()) {
        m_hexDevicesToRegister.push_back("hex-right");
    }

    appendLog("=== Multi-Device Alignment Started ===");
    appendLog(QString("Training Set: %1").arg(QString::fromStdString(m_trainingSet->name)));
    appendLog(QString("Gantry device: %1").arg(QString::fromStdString(m_trainingSet->deviceName)));

    if (!m_hexDevicesToRegister.empty()) {
        QString hexList;
        for (const auto& dev : m_hexDevicesToRegister) {
            if (!hexList.isEmpty()) hexList += ", ";
            hexList += QString::fromStdString(dev);
        }
        appendLog(QString("Hex devices to register: %1").arg(hexList));
    } else {
        appendLog("No hex devices selected (gantry-only mode).");
    }

    moveToCurrentRef();
}

void MultiDeviceRunAlignmentWidget::moveToCurrentRef()
{
    if (!m_trainingSet || m_currentRefIndex >= static_cast<int>(m_trainingSet->referencePoints.size())) {
        // All refs captured, compute transform
        computeTransform();
        return;
    }

    const auto& ref = m_trainingSet->referencePoints[m_currentRefIndex];
    appendLog(QString("\nMoving to %1 (taught: X=%2, Y=%3, Z=%4)")
        .arg(QString::fromStdString(ref.name))
        .arg(ref.globalX, 0, 'f', 4)
        .arg(ref.globalY, 0, 'f', 4)
        .arg(ref.globalZ, 0, 'f', 4));

    if (!m_context || !m_context->motionManager) return;

    std::string device = m_trainingSet->deviceName;
    double gx = ref.globalX, gy = ref.globalY, gz = ref.globalZ;
    int refIdx = m_currentRefIndex;
    size_t refCount = m_trainingSet->referencePoints.size();
    std::string refName = ref.name;
    std::string visionProfile = ref.visionProfilePath;

    if (m_captureRefBtn) m_captureRefBtn->setEnabled(false);
    setState(MoveToRef);

    m_moveFuture = std::async(std::launch::async,
        [this, device, gx, gy, gz, refIdx, refCount, refName, visionProfile]() {
        bool moveOk = m_context->motionManager->MoveToAbs(device, gx, gy, gz, 100.0, true);

        if (!moveOk) {
            QMetaObject::invokeMethod(this, [this, refIdx, refCount, refName]() {
                appendLog("WARNING: Move failed! Please jog manually.");
                showManualAdjust(refIdx, refCount, refName);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [this]() {
            appendLog("Move complete.");
        }, Qt::QueuedConnection);

        // If vision profile exists, try auto detect & center
        if (!visionProfile.empty()) {
            QMetaObject::invokeMethod(this, [this, refName]() {
                appendLog(QString("Running vision profile for %1...")
                    .arg(QString::fromStdString(refName)));
            }, Qt::QueuedConnection);

            bool visionOk = runVisionDetectCenter(visionProfile, device);

            if (visionOk) {
                QMetaObject::invokeMethod(this, [this, refIdx, refCount, refName, device]() {
                    appendLog(QString("Vision centered on %1 - auto-capturing position")
                        .arg(QString::fromStdString(refName)));
                    autoCaptureRef(refIdx, device);
                }, Qt::QueuedConnection);
                return;
            }

            QMetaObject::invokeMethod(this, [this, refIdx, refCount, refName]() {
                appendLog("Vision failed - falling back to manual adjustment.");
                showManualAdjust(refIdx, refCount, refName);
            }, Qt::QueuedConnection);
            return;
        }

        // No vision profile - manual adjustment
        QMetaObject::invokeMethod(this, [this, refIdx, refCount, refName]() {
            showManualAdjust(refIdx, refCount, refName);
        }, Qt::QueuedConnection);
    });
}

void MultiDeviceRunAlignmentWidget::showManualAdjust(
    int refIdx, size_t refCount, const std::string& refName)
{
    m_refInstructionLabel->setText(QString(
        "Reference %1 of %2: %3\n\n"
        "Gantry moved to taught position.\n"
        "Jog to align with the ACTUAL reference feature,\n"
        "then click 'Capture Adjusted Position'")
        .arg(refIdx + 1)
        .arg(refCount)
        .arg(QString::fromStdString(refName)));

    setState(AdjustRef);
    if (m_captureRefBtn) m_captureRefBtn->setEnabled(true);
}

void MultiDeviceRunAlignmentWidget::autoCaptureRef(int refIdx, const std::string& device)
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) return;

    auto pos = m_context->motionManager->GetCurrentPosition(device);
    const auto& taughtRef = m_trainingSet->referencePoints[refIdx];

    ReferencePoint actual;
    actual.name = taughtRef.name;
    actual.globalX = pos.x;
    actual.globalY = pos.y;
    actual.globalZ = pos.z;
    actual.valid = true;
    m_actualRefs.push_back(actual);

    double dx = pos.x - taughtRef.globalX;
    double dy = pos.y - taughtRef.globalY;

    appendLog(QString("Auto-captured %1: X=%2, Y=%3, Z=%4  (delta: dX=%5, dY=%6)")
        .arg(QString::fromStdString(actual.name))
        .arg(pos.x, 0, 'f', 4)
        .arg(pos.y, 0, 'f', 4)
        .arg(pos.z, 0, 'f', 4)
        .arg(dx, 0, 'f', 4)
        .arg(dy, 0, 'f', 4));

    m_currentRefIndex++;
    moveToCurrentRef();
}

bool MultiDeviceRunAlignmentWidget::runVisionDetectCenter(
    const std::string& profilePath, const std::string& device)
{
#ifdef HAS_OPENCV
    std::string cameraName;
    {
        std::ifstream file(profilePath);
        if (!file.is_open()) {
            std::cout << "[MultiDeviceAlignment] Cannot open vision profile: " << profilePath << std::endl;
            return false;
        }
        nlohmann::json j;
        file >> j;
        file.close();
        if (j.contains("camera") && j["camera"].contains("cameraId")) {
            cameraName = j["camera"].value("cameraId", "");
        }
    }
    if (cameraName.empty()) {
        std::cout << "[MultiDeviceAlignment] Vision profile has no camera ID" << std::endl;
        return false;
    }

    VisionCommandHandler visionHandler(m_context);
    SequenceContext ctx;
    std::string error;

    std::map<std::string, std::string> params;
    params["profile_path"] = profilePath;
    params["camera"] = cameraName;
    params["motion_device"] = device;
    params["calculate_world_position"] = "true";

    bool ok = visionHandler.ExecuteCommand(
        nullptr, "vision", "execute_profile", params, error, &ctx);

    if (!ok) {
        std::cout << "[MultiDeviceAlignment] Vision execution error: " << error << std::endl;
        return false;
    }

    bool visionSuccess = ctx.GetBoolean("vision.result.success", false);
    if (!visionSuccess) {
        std::string visionError = ctx.GetString("vision.result.error", "Unknown error");
        std::cout << "[MultiDeviceAlignment] Vision failed: " << visionError << std::endl;
        return false;
    }

    if (!ctx.HasValue("vision.detected.world_x_mm") ||
        !ctx.HasValue("vision.detected.world_y_mm")) {
        std::cout << "[MultiDeviceAlignment] No feature detected (no world position)" << std::endl;
        return false;
    }

    double worldX = static_cast<double>(ctx.GetNumeric("vision.detected.world_x_mm", 0.0f));
    double worldY = static_cast<double>(ctx.GetNumeric("vision.detected.world_y_mm", 0.0f));

    std::cout << "[MultiDeviceAlignment] Detected feature at world: X=" << worldX
              << " Y=" << worldY << std::endl;

    auto currentPos = m_context->motionManager->GetCurrentPosition(device);
    bool moveOk = m_context->motionManager->MoveToAbs(
        device, worldX, worldY, currentPos.z, 100.0, true);

    if (!moveOk) {
        std::cout << "[MultiDeviceAlignment] Move to detected position failed" << std::endl;
        return false;
    }

    std::cout << "[MultiDeviceAlignment] Centered on feature at X=" << worldX
              << " Y=" << worldY << std::endl;
    return true;
#else
    (void)profilePath;
    (void)device;
    return false;
#endif
}

void MultiDeviceRunAlignmentWidget::onCaptureActualRef()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) return;

    std::string device = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(device);

    const auto& taughtRef = m_trainingSet->referencePoints[m_currentRefIndex];

    ReferencePoint actual;
    actual.name = taughtRef.name;
    actual.globalX = pos.x;
    actual.globalY = pos.y;
    actual.globalZ = pos.z;
    actual.valid = true;
    m_actualRefs.push_back(actual);

    double dx = pos.x - taughtRef.globalX;
    double dy = pos.y - taughtRef.globalY;

    appendLog(QString("Captured %1: X=%2, Y=%3, Z=%4  (delta: dX=%5, dY=%6)")
        .arg(QString::fromStdString(actual.name))
        .arg(pos.x, 0, 'f', 4)
        .arg(pos.y, 0, 'f', 4)
        .arg(pos.z, 0, 'f', 4)
        .arg(dx, 0, 'f', 4)
        .arg(dy, 0, 'f', 4));

    m_currentRefIndex++;
    moveToCurrentRef();
}

void MultiDeviceRunAlignmentWidget::computeTransform()
{
    appendLog("\n=== Computing Transform ===");

    if (m_actualRefs.size() < 2) {
        appendLog("ERROR: Need at least 2 reference points!");
        setState(Ready);
        return;
    }

    // Compute live frame from actual refs
    AccuracyTrainerMath::ComputeCoordinateAxes(
        m_actualRefs[0].globalX, m_actualRefs[0].globalY,
        m_actualRefs[1].globalX, m_actualRefs[1].globalY,
        m_liveOriginX, m_liveOriginY,
        m_liveXAxisX, m_liveXAxisY,
        m_liveYAxisX, m_liveYAxisY);

    appendLog(QString("Live Origin: (%1, %2)")
        .arg(m_liveOriginX, 0, 'f', 4)
        .arg(m_liveOriginY, 0, 'f', 4));
    appendLog(QString("Live X-axis: (%1, %2)")
        .arg(m_liveXAxisX, 0, 'f', 6)
        .arg(m_liveXAxisY, 0, 'f', 6));

    // Optional: if 3 refs, compute scale correction
    double scale = 1.0;
    if (m_actualRefs.size() >= 3 && m_trainingSet->referencePoints.size() >= 3) {
        double taughtDist = AccuracyTrainerMath::Distance(
            m_trainingSet->referencePoints[0].globalX, m_trainingSet->referencePoints[0].globalY,
            m_trainingSet->referencePoints[1].globalX, m_trainingSet->referencePoints[1].globalY);
        double liveDist = AccuracyTrainerMath::Distance(
            m_actualRefs[0].globalX, m_actualRefs[0].globalY,
            m_actualRefs[1].globalX, m_actualRefs[1].globalY);

        if (taughtDist > 1e-6) {
            scale = liveDist / taughtDist;
            appendLog(QString("Scale factor: %1 (taught=%2mm, live=%3mm)")
                .arg(scale, 0, 'f', 6)
                .arg(taughtDist, 0, 'f', 4)
                .arg(liveDist, 0, 'f', 4));
        }
    }

    // Transform each bonding point: local coords -> live world coords
    m_correctedBonds.clear();
    appendLog("\n=== Corrected Bonding Positions ===");
    for (const auto& bp : m_trainingSet->bondingPoints) {
        double scaledLocalX = bp.localX * scale;
        double scaledLocalY = bp.localY * scale;

        double corrX = 0.0, corrY = 0.0;
        AccuracyTrainerMath::LocalToWorld(
            scaledLocalX, scaledLocalY,
            m_liveOriginX, m_liveOriginY,
            m_liveXAxisX, m_liveXAxisY,
            m_liveYAxisX, m_liveYAxisY,
            corrX, corrY);

        CorrectedBond cb;
        cb.name = bp.name;
        cb.correctedX = corrX;
        cb.correctedY = corrY;
        cb.correctedZ = bp.globalZ;  // Keep original Z
        cb.deltaX = corrX - bp.globalX;
        cb.deltaY = corrY - bp.globalY;
        m_correctedBonds.push_back(cb);

        appendLog(QString("  %1: corrected(%2, %3)  delta(%4, %5)")
            .arg(QString::fromStdString(bp.name))
            .arg(corrX, 0, 'f', 4)
            .arg(corrY, 0, 'f', 4)
            .arg(cb.deltaX, 0, 'f', 4)
            .arg(cb.deltaY, 0, 'f', 4));
    }

    // If hex devices were selected, start hex registration; otherwise go to bonding
    if (!m_hexDevicesToRegister.empty()) {
        startHexRegistration();
    } else if (!m_correctedBonds.empty()) {
        applyDeviceAssignments();
        startBondingSequence();
    } else {
        appendLog("No bonding points to move to.");
        setState(Done);
    }
}

void MultiDeviceRunAlignmentWidget::startHexRegistration()
{
    m_currentHexRegIndex = 0;
    appendLog("\n=== Hex Device Registration ===");
    beginNextHexCornerCapture();
}

void MultiDeviceRunAlignmentWidget::beginNextHexCornerCapture()
{
    if (m_currentHexRegIndex >= static_cast<int>(m_hexDevicesToRegister.size())) {
        // All hex devices registered, proceed to bonding
        appendLog("\n=== All Hex Devices Registered ===");
        if (!m_correctedBonds.empty()) {
            applyDeviceAssignments();
            startBondingSequence();
        } else {
            appendLog("No bonding points to move to.");
            setState(Done);
        }
        return;
    }

    // Start capture for the next hex device
    m_pendingHexReg = HexDeviceRegistration();
    m_pendingHexReg.deviceName = m_hexDevicesToRegister[m_currentHexRegIndex];

    appendLog(QString("\nRegistering hex device: %1 (%2 of %3)")
        .arg(QString::fromStdString(m_pendingHexReg.deviceName))
        .arg(m_currentHexRegIndex + 1)
        .arg(m_hexDevicesToRegister.size()));

    // Check for taught positions for this hex device
    const HexDeviceTeachData* teachData = nullptr;
    if (m_trainingSet) {
        for (const auto& hd : m_trainingSet->hexDevicePositions) {
            if (hd.deviceName == m_pendingHexReg.deviceName && hd.valid) {
                teachData = &hd;
                break;
            }
        }
    }

    if (teachData && m_context && m_context->motionManager) {
        // Auto-navigate: move hex to taught position, then gantry to viewing position
        appendLog(QString("  Auto-navigating to taught positions for %1")
            .arg(QString::fromStdString(m_pendingHexReg.deviceName)));

        std::string hexDevice = m_pendingHexReg.deviceName;
        std::string gantryDevice = m_trainingSet->deviceName;
        PositionStruct hexTarget;
        hexTarget.x = teachData->hexX;
        hexTarget.y = teachData->hexY;
        hexTarget.z = teachData->hexZ;
        hexTarget.u = teachData->hexU;
        hexTarget.v = teachData->hexV;
        hexTarget.w = teachData->hexW;
        double gantryX = teachData->gantryViewX;
        double gantryY = teachData->gantryViewY;
        double gantryZ = teachData->gantryViewZ;
        std::string visionProfile = teachData->visionProfilePath;
        int teachMode = teachData->gantryTeachMode;

        m_moveFuture = std::async(std::launch::async,
            [this, hexDevice, gantryDevice, hexTarget, gantryX, gantryY, gantryZ, visionProfile, teachMode, teachData]() {
            // First move hex to working position
            bool hexOk = m_context->motionManager->MoveToAbs6(hexDevice, hexTarget, true);
            // Then move gantry to viewing position
            bool gantryOk = m_context->motionManager->MoveToAbs(gantryDevice, gantryX, gantryY, gantryZ, 100.0, true);

            QMetaObject::invokeMethod(this, [this, hexDevice, hexOk, gantryOk]() {
                if (!hexOk) {
                    appendLog("  WARNING: Hex auto-move failed! Please jog manually.");
                }
                if (!gantryOk) {
                    appendLog("  WARNING: Gantry auto-move failed! Please jog manually.");
                }
                if (hexOk && gantryOk) {
                    appendLog("  Auto-navigate complete.");
                }

                // Read hex device position after move
                auto hexPos = m_context->motionManager->GetCurrentPosition(hexDevice);
                m_pendingHexReg.hexPosX = hexPos.x;
                m_pendingHexReg.hexPosY = hexPos.y;
                m_pendingHexReg.hexPosZ = hexPos.z;
            }, Qt::QueuedConnection);

            // Try vision auto-detect if profile exists
            if (!visionProfile.empty() && gantryOk) {
                QMetaObject::invokeMethod(this, [this]() {
                    appendLog("  Running vision profile to detect hex tip...");
                }, Qt::QueuedConnection);

                bool visionOk = runVisionDetectCenter(visionProfile, gantryDevice);

                if (visionOk) {
                    // Vision succeeded - capture center position directly
                    auto centerPos = m_context->motionManager->GetCurrentPosition(gantryDevice);

                    QMetaObject::invokeMethod(this, [this, centerPos]() {
                        // Use detected center as hex registration
                        m_pendingHexReg.centerGantryX = centerPos.x;
                        m_pendingHexReg.centerGantryY = centerPos.y;
                        m_pendingHexReg.centerGantryZ = centerPos.z;
                        // Set corners from center (no real corner data, but mark valid)
                        m_pendingHexReg.cornerTL_X = centerPos.x;
                        m_pendingHexReg.cornerTL_Y = centerPos.y;
                        m_pendingHexReg.cornerBR_X = centerPos.x;
                        m_pendingHexReg.cornerBR_Y = centerPos.y;
                        m_pendingHexReg.hasThirdCorner = false;
                        m_pendingHexReg.valid = true;

                        appendLog(QString("  Vision detected hex tip center: X=%1, Y=%2, Z=%3")
                            .arg(centerPos.x, 0, 'f', 4)
                            .arg(centerPos.y, 0, 'f', 4)
                            .arg(centerPos.z, 0, 'f', 4));
                        appendLog(QString("  Hex device pos: X=%1, Y=%2, Z=%3")
                            .arg(m_pendingHexReg.hexPosX, 0, 'f', 4)
                            .arg(m_pendingHexReg.hexPosY, 0, 'f', 4)
                            .arg(m_pendingHexReg.hexPosZ, 0, 'f', 4));
                        appendLog("  Auto-registering (skipping manual corners).");

                        onConfirmHexRegistration();
                    }, Qt::QueuedConnection);
                    return;
                }

                // Vision failed - fall back based on teach mode
                QMetaObject::invokeMethod(this, [this, teachMode, teachData]() {
                    appendLog("  Vision failed - falling back to manual registration.");
                    if (teachMode == 0) {
                        // Single point: use current gantry position as center
                        autoRegisterSinglePoint();
                    } else {
                        m_currentHexTeachData = teachData;
                        navigateToCornerAndSetState(RegisterHex_Corner1);
                    }
                }, Qt::QueuedConnection);
                return;
            }

            // No vision profile - register based on teach mode
            QMetaObject::invokeMethod(this, [this, teachMode, teachData]() {
                if (teachMode == 0) {
                    // Single point: use current gantry position as center (already at taught pos)
                    appendLog("  Single-point mode: using gantry position as center.");
                    autoRegisterSinglePoint();
                } else {
                    m_currentHexTeachData = teachData;
                    appendLog("  Corner capture with auto-navigation to saved positions.");
                    navigateToCornerAndSetState(RegisterHex_Corner1);
                }
            }, Qt::QueuedConnection);
        });
    } else {
        // No teach data: manual jog flow (original behavior)
        m_currentHexTeachData = nullptr;
        // Read hex device's current position (it should be in its working position)
        if (m_context && m_context->motionManager) {
            auto hexPos = m_context->motionManager->GetCurrentPosition(m_pendingHexReg.deviceName);
            m_pendingHexReg.hexPosX = hexPos.x;
            m_pendingHexReg.hexPosY = hexPos.y;
            m_pendingHexReg.hexPosZ = hexPos.z;

            appendLog(QString("  Hex device current position: X=%1, Y=%2, Z=%3")
                .arg(hexPos.x, 0, 'f', 4)
                .arg(hexPos.y, 0, 'f', 4)
                .arg(hexPos.z, 0, 'f', 4));
        }

        setState(RegisterHex_Corner1);
    }
}

void MultiDeviceRunAlignmentWidget::navigateToCornerAndSetState(AlignState cornerState)
{
    if (!m_currentHexTeachData || !m_context || !m_context->motionManager || !m_trainingSet) {
        setState(cornerState);
        return;
    }

    double targetX = 0.0, targetY = 0.0, targetZ = m_currentHexTeachData->cornerTL_Z;
    QString cornerName;

    switch (cornerState) {
    case RegisterHex_Corner1:
        targetX = m_currentHexTeachData->cornerTL_X;
        targetY = m_currentHexTeachData->cornerTL_Y;
        targetZ = m_currentHexTeachData->cornerTL_Z;
        cornerName = "TL";
        break;
    case RegisterHex_Corner2:
        targetX = m_currentHexTeachData->cornerBR_X;
        targetY = m_currentHexTeachData->cornerBR_Y;
        cornerName = "BR";
        break;
    case RegisterHex_Corner3:
        targetX = m_currentHexTeachData->cornerTR_X;
        targetY = m_currentHexTeachData->cornerTR_Y;
        cornerName = "TR";
        break;
    default:
        setState(cornerState);
        return;
    }

    appendLog(QString("  Auto-navigating gantry to saved %1 corner: X=%2, Y=%3")
        .arg(cornerName).arg(targetX, 0, 'f', 4).arg(targetY, 0, 'f', 4));

    std::string gantryDevice = m_trainingSet->deviceName;
    m_cornerNavFuture = std::async(std::launch::async,
        [this, gantryDevice, targetX, targetY, targetZ, cornerState]() {
        bool ok = m_context->motionManager->MoveToAbs(gantryDevice, targetX, targetY, targetZ, 100.0, true);
        QMetaObject::invokeMethod(this, [this, cornerState, ok]() {
            if (!ok) {
                appendLog("  Warning: auto-navigation failed, jog manually.");
            }
            setState(cornerState);
        }, Qt::QueuedConnection);
    });
}

void MultiDeviceRunAlignmentWidget::onCaptureHexCorner()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) return;

    // Read gantry position (capturing hex end-effector position via gantry camera)
    std::string gantryDevice = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(gantryDevice);

    switch (m_state) {
    case RegisterHex_Corner1:
        m_pendingHexReg.cornerTL_X = pos.x;
        m_pendingHexReg.cornerTL_Y = pos.y;
        m_pendingHexReg.centerGantryZ = pos.z;  // Z from first corner
        appendLog(QString("  Corner 1 (TL): X=%1, Y=%2, Z=%3")
            .arg(pos.x, 0, 'f', 4).arg(pos.y, 0, 'f', 4).arg(pos.z, 0, 'f', 4));
        navigateToCornerAndSetState(RegisterHex_Corner2);
        break;

    case RegisterHex_Corner2:
        m_pendingHexReg.cornerBR_X = pos.x;
        m_pendingHexReg.cornerBR_Y = pos.y;
        appendLog(QString("  Corner 2 (BR): X=%1, Y=%2")
            .arg(pos.x, 0, 'f', 4).arg(pos.y, 0, 'f', 4));
        // Auto-skip corner 3 if teach data only has 2 corners
        if (m_currentHexTeachData && !m_currentHexTeachData->hasThirdCorner) {
            appendLog("  Taught data has 2 corners - skipping corner 3.");
            m_pendingHexReg.hasThirdCorner = false;
            computeHexCenter();
        } else {
            navigateToCornerAndSetState(RegisterHex_Corner3);
        }
        break;

    case RegisterHex_Corner3:
        m_pendingHexReg.cornerTR_X = pos.x;
        m_pendingHexReg.cornerTR_Y = pos.y;
        m_pendingHexReg.hasThirdCorner = true;
        appendLog(QString("  Corner 3 (TR): X=%1, Y=%2")
            .arg(pos.x, 0, 'f', 4).arg(pos.y, 0, 'f', 4));
        computeHexCenter();
        break;

    default:
        break;
    }
}

void MultiDeviceRunAlignmentWidget::onSkipHexCorner3()
{
    m_pendingHexReg.hasThirdCorner = false;
    appendLog("  Skipped corner 3 (using 2-corner midpoint)");
    computeHexCenter();
}

void MultiDeviceRunAlignmentWidget::computeHexCenter()
{
    // Compute center from corners (same pattern as TeachBondingWidget)
    if (m_pendingHexReg.hasThirdCorner) {
        // 3-corner: centroid of TL, BR, TR, and implied BL
        double blX = m_pendingHexReg.cornerTL_X + m_pendingHexReg.cornerBR_X - m_pendingHexReg.cornerTR_X;
        double blY = m_pendingHexReg.cornerTL_Y + m_pendingHexReg.cornerBR_Y - m_pendingHexReg.cornerTR_Y;
        m_pendingHexReg.centerGantryX = (m_pendingHexReg.cornerTL_X + m_pendingHexReg.cornerBR_X
                                          + m_pendingHexReg.cornerTR_X + blX) / 4.0;
        m_pendingHexReg.centerGantryY = (m_pendingHexReg.cornerTL_Y + m_pendingHexReg.cornerBR_Y
                                          + m_pendingHexReg.cornerTR_Y + blY) / 4.0;
    } else {
        // 2-corner: midpoint of TL and BR
        m_pendingHexReg.centerGantryX = (m_pendingHexReg.cornerTL_X + m_pendingHexReg.cornerBR_X) / 2.0;
        m_pendingHexReg.centerGantryY = (m_pendingHexReg.cornerTL_Y + m_pendingHexReg.cornerBR_Y) / 2.0;
    }

    m_pendingHexReg.valid = true;

    double dist = AccuracyTrainerMath::Distance(
        m_pendingHexReg.cornerTL_X, m_pendingHexReg.cornerTL_Y,
        m_pendingHexReg.cornerBR_X, m_pendingHexReg.cornerBR_Y);

    appendLog(QString("  Computed center (gantry coords): X=%1, Y=%2, Z=%3")
        .arg(m_pendingHexReg.centerGantryX, 0, 'f', 4)
        .arg(m_pendingHexReg.centerGantryY, 0, 'f', 4)
        .arg(m_pendingHexReg.centerGantryZ, 0, 'f', 4));
    appendLog(QString("  Diagonal distance: %1 mm").arg(dist, 0, 'f', 4));

    appendLog(QString("  Hex device pos: X=%1, Y=%2, Z=%3")
        .arg(m_pendingHexReg.hexPosX, 0, 'f', 4)
        .arg(m_pendingHexReg.hexPosY, 0, 'f', 4)
        .arg(m_pendingHexReg.hexPosZ, 0, 'f', 4));

    // Auto-confirm registration (no manual summary step)
    onConfirmHexRegistration();
}

void MultiDeviceRunAlignmentWidget::autoRegisterSinglePoint()
{
    // Use current gantry position as the registration center (single-point mode)
    if (!m_context || !m_context->motionManager || !m_trainingSet) return;

    std::string gantryDevice = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(gantryDevice);

    m_pendingHexReg.centerGantryX = pos.x;
    m_pendingHexReg.centerGantryY = pos.y;
    m_pendingHexReg.centerGantryZ = pos.z;
    m_pendingHexReg.cornerTL_X = pos.x;
    m_pendingHexReg.cornerTL_Y = pos.y;
    m_pendingHexReg.cornerBR_X = pos.x;
    m_pendingHexReg.cornerBR_Y = pos.y;
    m_pendingHexReg.hasThirdCorner = false;
    m_pendingHexReg.valid = true;

    appendLog(QString("  Single-point registration: center=(%1, %2, %3)")
        .arg(pos.x, 0, 'f', 4)
        .arg(pos.y, 0, 'f', 4)
        .arg(pos.z, 0, 'f', 4));
    appendLog(QString("  Hex device pos: X=%1, Y=%2, Z=%3")
        .arg(m_pendingHexReg.hexPosX, 0, 'f', 4)
        .arg(m_pendingHexReg.hexPosY, 0, 'f', 4)
        .arg(m_pendingHexReg.hexPosZ, 0, 'f', 4));

    // Show summary for user confirmation instead of auto-confirming
    m_hexSummaryLabel->setText(QString(
        "Hex Registration: %1 (Single Point)\n\n"
        "Gantry Center: X=%2  Y=%3  Z=%4\n\n"
        "Hex Device Position:\n"
        "  X=%5  Y=%6  Z=%7\n\n"
        "Click 'Confirm Registration' to accept.")
        .arg(QString::fromStdString(m_pendingHexReg.deviceName))
        .arg(pos.x, 0, 'f', 4)
        .arg(pos.y, 0, 'f', 4)
        .arg(pos.z, 0, 'f', 4)
        .arg(m_pendingHexReg.hexPosX, 0, 'f', 4)
        .arg(m_pendingHexReg.hexPosY, 0, 'f', 4)
        .arg(m_pendingHexReg.hexPosZ, 0, 'f', 4));
    setState(RegisterHex_Summary);
}

void MultiDeviceRunAlignmentWidget::onConfirmHexRegistration()
{
    m_hexRegistrations.push_back(m_pendingHexReg);
    appendLog(QString("  Confirmed registration for %1")
        .arg(QString::fromStdString(m_pendingHexReg.deviceName)));

    m_currentHexRegIndex++;
    beginNextHexCornerCapture();
}

void MultiDeviceRunAlignmentWidget::onRedoHexRegistration()
{
    appendLog(QString("  Redoing registration for %1")
        .arg(QString::fromStdString(m_pendingHexReg.deviceName)));

    // Re-read hex position in case user repositioned
    if (m_context && m_context->motionManager) {
        auto hexPos = m_context->motionManager->GetCurrentPosition(m_pendingHexReg.deviceName);
        m_pendingHexReg.hexPosX = hexPos.x;
        m_pendingHexReg.hexPosY = hexPos.y;
        m_pendingHexReg.hexPosZ = hexPos.z;
    }

    m_pendingHexReg.cornerTL_X = 0.0;
    m_pendingHexReg.cornerTL_Y = 0.0;
    m_pendingHexReg.cornerBR_X = 0.0;
    m_pendingHexReg.cornerBR_Y = 0.0;
    m_pendingHexReg.cornerTR_X = 0.0;
    m_pendingHexReg.cornerTR_Y = 0.0;
    m_pendingHexReg.hasThirdCorner = false;
    m_pendingHexReg.valid = false;

    setState(RegisterHex_Corner1);
}

void MultiDeviceRunAlignmentWidget::applyDeviceAssignments()
{
    appendLog("\n=== Device Assignments ===");

    // Read device assignments from training set (configured during Teach step)
    for (int i = 0; i < static_cast<int>(m_correctedBonds.size()); ++i) {
        auto& cb = m_correctedBonds[i];

        // Check if this bonding point has a device assignment from teach step
        auto it = m_trainingSet->bondingDeviceAssignments.find(cb.name);
        if (it != m_trainingSet->bondingDeviceAssignments.end() && it->second != "gantry-main") {
            cb.assignedDevice = it->second;
            // Find matching hex registration index
            cb.assignedHexIndex = -1;
            for (int h = 0; h < static_cast<int>(m_hexRegistrations.size()); ++h) {
                if (m_hexRegistrations[h].deviceName == it->second) {
                    cb.assignedHexIndex = h;
                    break;
                }
            }
            if (cb.assignedHexIndex < 0) {
                // Hex device was assigned but not registered in this run - fall back to gantry
                appendLog(QString("  WARNING: %1 assigned to %2 but device not registered, using gantry-main")
                    .arg(QString::fromStdString(cb.name))
                    .arg(QString::fromStdString(it->second)));
                cb.assignedDevice = "gantry-main";
            }
        } else {
            // Default: gantry-main
            cb.assignedDevice = "gantry-main";
            cb.assignedHexIndex = -1;
        }

        appendLog(QString("  %1 -> %2")
            .arg(QString::fromStdString(cb.name))
            .arg(QString::fromStdString(cb.assignedDevice)));
    }

    // Warn if a hex device is assigned to multiple bonding points
    for (int h = 0; h < static_cast<int>(m_hexRegistrations.size()); ++h) {
        int count = 0;
        for (const auto& cb : m_correctedBonds) {
            if (cb.assignedHexIndex == h) ++count;
        }
        if (count > 1) {
            appendLog(QString("WARNING: %1 is assigned to %2 bonding points (expected 1)")
                .arg(QString::fromStdString(m_hexRegistrations[h].deviceName))
                .arg(count));
        }
    }
}

void MultiDeviceRunAlignmentWidget::startBondingSequence()
{
    if (m_correctedBonds.empty()) {
        appendLog("No bonding points to move to.");
        setState(Done);
        return;
    }

    m_currentBondIndex = 0;
    setState(MoveToBonding);
    showBondingPointAndAutoMove();
}

void MultiDeviceRunAlignmentWidget::showBondingPointAndAutoMove()
{
    if (m_currentBondIndex < 0 || m_currentBondIndex >= static_cast<int>(m_correctedBonds.size())) {
        return;
    }

    const auto& cb = m_correctedBonds[m_currentBondIndex];

    // Update instruction label
    m_bondInstructionLabel->setText(QString(
        "Bonding Point %1 of %2: %3\n\n"
        "Device: %4\n"
        "Corrected Position: X=%5  Y=%6  Z=%7\n"
        "Correction Delta: dX=%8  dY=%9\n\n"
        "Moving to position...")
        .arg(m_currentBondIndex + 1)
        .arg(m_correctedBonds.size())
        .arg(QString::fromStdString(cb.name))
        .arg(QString::fromStdString(cb.assignedDevice))
        .arg(cb.correctedX, 0, 'f', 4)
        .arg(cb.correctedY, 0, 'f', 4)
        .arg(cb.correctedZ, 0, 'f', 4)
        .arg(cb.deltaX, 0, 'f', 4)
        .arg(cb.deltaY, 0, 'f', 4));

    m_bondPositionLabel->setText(QString("X: %1  Y: %2  Z: %3")
        .arg(cb.correctedX, 10, 'f', 4)
        .arg(cb.correctedY, 10, 'f', 4)
        .arg(cb.correctedZ, 10, 'f', 4));

    // Append Z adjustment info if applicable
    if (cb.assignedHexIndex >= 0 && m_trainingSet) {
        for (const auto& hd : m_trainingSet->hexDevicePositions) {
            if (hd.deviceName == cb.assignedDevice && hd.valid && std::abs(hd.hexZAdjustment) > 1e-6) {
                m_bondInstructionLabel->setText(m_bondInstructionLabel->text()
                    + QString("\nZ Adjustment: %1 mm").arg(hd.hexZAdjustment, 0, 'f', 4));
                break;
            }
        }
    }

    // Show/hide Back button: hidden for first point
    m_prevBondBtn->setVisible(m_currentBondIndex > 0);

    // Disable buttons during move
    m_prevBondBtn->setEnabled(false);
    m_nextBondBtn->setEnabled(false);

    // Auto-move
    moveToCurrentBonding();
}

void MultiDeviceRunAlignmentWidget::moveToCurrentBonding()
{
    if (m_currentBondIndex >= static_cast<int>(m_correctedBonds.size())) {
        return;
    }
    if (!m_context || !m_context->motionManager || !m_trainingSet) return;

    const auto& cb = m_correctedBonds[m_currentBondIndex];
    std::string gantryDevice = m_trainingSet->deviceName;
    double cx = cb.correctedX, cy = cb.correctedY, cz = cb.correctedZ;

    // Always move gantry-main to the corrected position
    appendLog(QString("\nMoving gantry-main to %1: X=%2, Y=%3, Z=%4")
        .arg(QString::fromStdString(cb.name))
        .arg(cx, 0, 'f', 4).arg(cy, 0, 'f', 4).arg(cz, 0, 'f', 4));

    if (cb.assignedHexIndex < 0) {
        // Gantry-only: move gantry to corrected position
        m_moveFuture = std::async(std::launch::async, [this, gantryDevice, cx, cy, cz]() {
            bool moveOk = m_context->motionManager->MoveToAbs(gantryDevice, cx, cy, cz, 100.0, true);
            QMetaObject::invokeMethod(this, [this, moveOk]() {
                if (!moveOk) {
                    appendLog("WARNING: Gantry move failed!");
                } else {
                    appendLog("Gantry move complete.");
                }
                m_bondInstructionLabel->setText(
                    m_bondInstructionLabel->text().replace("Moving to position...", "Position reached."));
                m_prevBondBtn->setEnabled(m_currentBondIndex > 0);
                m_nextBondBtn->setEnabled(true);
            }, Qt::QueuedConnection);
        });
    } else {
        // Hex device: 2-step move — XY first, then Z adjustment
        int hexIndex = cb.assignedHexIndex;
        if (hexIndex >= static_cast<int>(m_hexRegistrations.size())) {
            appendLog("ERROR: Invalid hex device selection.");
            m_prevBondBtn->setEnabled(m_currentBondIndex > 0);
            m_nextBondBtn->setEnabled(true);
            return;
        }

        const auto& hexReg = m_hexRegistrations[hexIndex];
        const auto* matrix = m_matrixLoader.GetMatrix(hexReg.deviceName);

        if (!matrix) {
            appendLog(QString("ERROR: No transformation matrix for %1")
                .arg(QString::fromStdString(hexReg.deviceName)));
            m_prevBondBtn->setEnabled(m_currentBondIndex > 0);
            m_nextBondBtn->setEnabled(true);
            return;
        }

        // Delta in gantry-main XY plane
        double gantryDeltaX = cb.correctedX - hexReg.centerGantryX;
        double gantryDeltaY = cb.correctedY - hexReg.centerGantryY;

        // Look up hexZAdjustment from training set teach data
        double hexZAdjustment = 0.0;
        if (m_trainingSet) {
            for (const auto& hd : m_trainingSet->hexDevicePositions) {
                if (hd.deviceName == hexReg.deviceName && hd.valid) {
                    hexZAdjustment = hd.hexZAdjustment;
                    break;
                }
            }
        }

        appendLog(QString("Also moving %1 (delta: dX=%2, dY=%3, Zadj=%4)")
            .arg(QString::fromStdString(hexReg.deviceName))
            .arg(gantryDeltaX, 0, 'f', 4)
            .arg(gantryDeltaY, 0, 'f', 4)
            .arg(hexZAdjustment, 0, 'f', 4));

        // Two separate transforms to keep lateral (XY) and height (Z) moves independent.
        // This prevents the hex from lowering while still moving laterally, avoiding crashes.
        // Step 1: Lateral repositioning — transform gantry XY delta only
        CoordinateTransformer::Vector6D xyGlobalDelta(gantryDeltaX, gantryDeltaY, 0.0, 0.0, 0.0, 0.0);
        auto xyLocalDelta = CoordinateTransformer::TransformGlobalToLocal(xyGlobalDelta, *matrix);

        // Step 2: Height adjustment — transform Z adjustment only
        CoordinateTransformer::Vector6D zGlobalDelta(0.0, 0.0, hexZAdjustment, 0.0, 0.0, 0.0);
        auto zLocalDelta = CoordinateTransformer::TransformGlobalToLocal(zGlobalDelta, *matrix);

        appendLog(QString("  Lateral local delta: dX=%1, dY=%2, dZ=%3")
            .arg(xyLocalDelta.x, 0, 'f', 4).arg(xyLocalDelta.y, 0, 'f', 4)
            .arg(xyLocalDelta.z, 0, 'f', 4));
        appendLog(QString("  Height local delta:  dX=%1, dY=%2, dZ=%3")
            .arg(zLocalDelta.x, 0, 'f', 4).arg(zLocalDelta.y, 0, 'f', 4)
            .arg(zLocalDelta.z, 0, 'f', 4));

        std::string hexDevice = hexReg.deviceName;
        auto curPos = m_context->motionManager->GetCurrentPosition(hexDevice);

        // Step 1 target: lateral repositioning only (gantry XY delta transformed)
        PositionStruct xyTargetPos;
        xyTargetPos.x = curPos.x + xyLocalDelta.x;
        xyTargetPos.y = curPos.y + xyLocalDelta.y;
        xyTargetPos.z = curPos.z + xyLocalDelta.z;
        xyTargetPos.u = curPos.u + xyLocalDelta.u;
        xyTargetPos.v = curPos.v + xyLocalDelta.v;
        xyTargetPos.w = curPos.w + xyLocalDelta.w;

        appendLog(QString("  Step 1 - Hex lateral target: X=%1, Y=%2, Z=%3")
            .arg(xyTargetPos.x, 0, 'f', 4).arg(xyTargetPos.y, 0, 'f', 4)
            .arg(xyTargetPos.z, 0, 'f', 4));

        // Capture Z height delta for Step 2 (all 3 axes — matrix may map gantry Z to any hex axis)
        double capturedZdX = zLocalDelta.x;
        double capturedZdY = zLocalDelta.y;
        double capturedZdZ = zLocalDelta.z;
        bool hasZAdjustment = (std::abs(capturedZdX) > 1e-6 ||
                               std::abs(capturedZdY) > 1e-6 ||
                               std::abs(capturedZdZ) > 1e-6);

        m_moveFuture = std::async(std::launch::async,
            [this, gantryDevice, cx, cy, cz, hexDevice, xyTargetPos,
             capturedZdX, capturedZdY, capturedZdZ, hasZAdjustment]() {
            // Step 1: Move gantry + hex lateral simultaneously
            bool gantryOk = m_context->motionManager->MoveToAbs(gantryDevice, cx, cy, cz, 100.0, true);
            bool hexOk = m_context->motionManager->MoveToAbs6(hexDevice, xyTargetPos, true);

            if (!gantryOk) {
                QMetaObject::invokeMethod(this, [this]() {
                    appendLog("WARNING: Gantry move failed!");
                }, Qt::QueuedConnection);
            }
            if (!hexOk) {
                QMetaObject::invokeMethod(this, [this]() {
                    appendLog("WARNING: Hex lateral move failed!");
                }, Qt::QueuedConnection);
            }

            // Step 2: Height adjustment (separate move to avoid crash)
            bool zOk = true;
            if (hexOk && hasZAdjustment) {
                auto afterXYPos = m_context->motionManager->GetCurrentPosition(hexDevice);
                PositionStruct zTargetPos;
                zTargetPos.x = afterXYPos.x + capturedZdX;
                zTargetPos.y = afterXYPos.y + capturedZdY;
                zTargetPos.z = afterXYPos.z + capturedZdZ;
                zTargetPos.u = afterXYPos.u;
                zTargetPos.v = afterXYPos.v;
                zTargetPos.w = afterXYPos.w;

                QMetaObject::invokeMethod(this, [this, zTargetPos]() {
                    appendLog(QString("  Step 2 - Hex height target: X=%1, Y=%2, Z=%3")
                        .arg(zTargetPos.x, 0, 'f', 4)
                        .arg(zTargetPos.y, 0, 'f', 4)
                        .arg(zTargetPos.z, 0, 'f', 4));
                }, Qt::QueuedConnection);

                zOk = m_context->motionManager->MoveToAbs6(hexDevice, zTargetPos, true);
            }

            QMetaObject::invokeMethod(this, [this, gantryOk, hexOk, zOk, hasZAdjustment]() {
                if (!zOk) appendLog("WARNING: Hex height move failed!");
                bool allOk = gantryOk && hexOk && (zOk || !hasZAdjustment);
                if (allOk) appendLog("All moves complete.");
                m_bondInstructionLabel->setText(
                    m_bondInstructionLabel->text().replace("Moving to position...", "Position reached."));
                m_prevBondBtn->setEnabled(m_currentBondIndex > 0);
                m_nextBondBtn->setEnabled(true);
            }, Qt::QueuedConnection);
        });
    }
}

void MultiDeviceRunAlignmentWidget::onMoveToNextBonding()
{
    m_currentBondIndex++;

    if (m_currentBondIndex >= static_cast<int>(m_correctedBonds.size())) {
        // All done
        appendLog("\n=== Multi-Device Alignment Complete ===");

        QString summary = "Multi-Device Alignment Complete\n\n";
        summary += "Corrected Bonding Positions:\n";
        for (const auto& cb : m_correctedBonds) {
            summary += QString("  %1 [%2]: dX=%3  dY=%4\n")
                .arg(QString::fromStdString(cb.name))
                .arg(QString::fromStdString(cb.assignedDevice))
                .arg(cb.deltaX, 0, 'f', 4)
                .arg(cb.deltaY, 0, 'f', 4);
        }
        if (!m_hexRegistrations.empty()) {
            summary += "\nRegistered Hex Devices:\n";
            for (const auto& reg : m_hexRegistrations) {
                summary += QString("  %1: center=(%2, %3) hexPos=(%4, %5)\n")
                    .arg(QString::fromStdString(reg.deviceName))
                    .arg(reg.centerGantryX, 0, 'f', 4)
                    .arg(reg.centerGantryY, 0, 'f', 4)
                    .arg(reg.hexPosX, 0, 'f', 4)
                    .arg(reg.hexPosY, 0, 'f', 4);
            }
        }
        m_doneLabel->setText(summary);
        setState(Done);
        return;
    }

    showBondingPointAndAutoMove();
}

void MultiDeviceRunAlignmentWidget::onMoveToPrevBonding()
{
    if (m_currentBondIndex <= 0) return;

    m_currentBondIndex--;
    showBondingPointAndAutoMove();
}

void MultiDeviceRunAlignmentWidget::onFinish()
{
    setState(Ready);
}

void MultiDeviceRunAlignmentWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) return;

    std::string device = m_trainingSet->deviceName;
    // Non-blocking cached read fed by UnifiedMotionManager's background poll —
    // GetCurrentPosition() is a synchronous controller round-trip and must not
    // run on the UI thread on a fast cadence (see unified_motion_manager.h:175).
    PositionStruct pos;
    if (!m_context->motionManager->GetCachedPosition(device, pos)) {
        return; // No cached sample yet
    }

    QString posText = QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4);

    switch (m_state) {
    case MoveToRef:
    case AdjustRef:
        m_refPositionLabel->setText(posText);
        break;
    case RegisterHex_Corner1:
    case RegisterHex_Corner2:
    case RegisterHex_Corner3:
        m_hexPositionLabel->setText(posText);
        break;
    case MoveToBonding:
        // Show position of the device assigned to the current bonding point
        if (m_currentBondIndex >= 0 && m_currentBondIndex < static_cast<int>(m_correctedBonds.size())) {
            const auto& cb = m_correctedBonds[m_currentBondIndex];
            if (cb.assignedHexIndex >= 0 && cb.assignedHexIndex < static_cast<int>(m_hexRegistrations.size())) {
                PositionStruct hexPos;
                if (m_context->motionManager->GetCachedPosition(
                        m_hexRegistrations[cb.assignedHexIndex].deviceName, hexPos)) {
                    posText = QString("X: %1  Y: %2  Z: %3  [%4]")
                        .arg(hexPos.x, 10, 'f', 4)
                        .arg(hexPos.y, 10, 'f', 4)
                        .arg(hexPos.z, 10, 'f', 4)
                        .arg(QString::fromStdString(cb.assignedDevice));
                }
            }
        }
        m_bondPositionLabel->setText(posText);
        break;
    default:
        break;
    }
}

void MultiDeviceRunAlignmentWidget::appendLog(const QString& text)
{
    m_logView->append(text);
    std::cout << "[MultiDeviceAlignment] " << text.toStdString() << std::endl;
}

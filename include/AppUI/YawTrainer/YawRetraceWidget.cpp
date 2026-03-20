// YawRetraceWidget.cpp - 7-state wizard for 4-point retrace
#include "YawRetraceWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>

YawRetraceWidget::YawRetraceWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
    , m_hexTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &YawRetraceWidget::updatePositionDisplay);
    connect(m_hexTimer, &QTimer::timeout, this, &YawRetraceWidget::updateHexPositionDisplay);
}

void YawRetraceWidget::SetData(YawTrainerSet* set)
{
    m_set = set;
    setState(Idle);
}

void YawRetraceWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Retrace & Apply Yaw Correction");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    m_wizardStack = new QStackedWidget();

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    QString greenBtnStyle =
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a5c35; color: #888; }";

    QString blueBtnStyle =
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }";

    QString cancelBtnStyle =
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }";

    // Helper lambda to create a position capture page
    auto createCapturePage = [&](const QString& instruction, QLabel*& instrLabel,
                                  QLabel*& posLabel, QPushButton*& confirmBtn,
                                  QPushButton*& cancelBtn2, const char* confirmSlot) -> QWidget*
    {
        QWidget* page = new QWidget();
        QVBoxLayout* layout = new QVBoxLayout(page);

        instrLabel = new QLabel(instruction);
        instrLabel->setStyleSheet(
            "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
            "background-color: #2d3e5f; border-radius: 6px;");
        instrLabel->setWordWrap(true);
        instrLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(instrLabel);

        QGroupBox* posGroup = new QGroupBox("Current Gantry Position");
        posGroup->setStyleSheet(groupStyle);
        QVBoxLayout* posLayout = new QVBoxLayout(posGroup);
        posLabel = new QLabel("X: ---  Y: ---  Z: ---");
        posLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
        posLabel->setAlignment(Qt::AlignCenter);
        posLayout->addWidget(posLabel);
        layout->addWidget(posGroup);

        layout->addStretch();

        QHBoxLayout* btnRow = new QHBoxLayout();
        confirmBtn = new QPushButton("Confirm");
        confirmBtn->setFixedHeight(44);
        confirmBtn->setStyleSheet(greenBtnStyle);
        btnRow->addWidget(confirmBtn);

        cancelBtn2 = new QPushButton("Cancel");
        cancelBtn2->setFixedHeight(44);
        cancelBtn2->setStyleSheet(cancelBtnStyle);
        connect(cancelBtn2, &QPushButton::clicked, this, [this]() {
            m_positionTimer->stop();
            setState(Idle);
        });
        btnRow->addWidget(cancelBtn2);
        layout->addLayout(btnRow);

        return page;
    };

    // ========================================
    // Page 0: Idle
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_idleSummaryLabel = new QLabel("No training set loaded.");
    m_idleSummaryLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_idleSummaryLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleSummaryLabel);

    idleLayout->addStretch();

    m_startBtn = new QPushButton("Start Retrace && Apply");
    m_startBtn->setFixedHeight(44);
    m_startBtn->setStyleSheet(blueBtnStyle);
    connect(m_startBtn, &QPushButton::clicked, this, &YawRetraceWidget::onStartRetrace);
    idleLayout->addWidget(m_startBtn);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Confirm Reference P1
    // ========================================
    QWidget* refP1Page = createCapturePage(
        "Step 1/4: Jog to Reference Line Point 1",
        m_refP1InstructionLabel, m_refP1PositionLabel,
        m_confirmRefP1Btn, m_cancelBtn, nullptr);
    connect(m_confirmRefP1Btn, &QPushButton::clicked, this, &YawRetraceWidget::onConfirmRefP1);
    m_wizardStack->addWidget(refP1Page);  // index 1

    // ========================================
    // Page 2: Confirm Reference P2
    // ========================================
    QWidget* refP2Page = createCapturePage(
        "Step 2/4: Verify Reference Line Point 2",
        m_refP2InstructionLabel, m_refP2PositionLabel,
        m_confirmRefP2Btn, m_cancelRefP2Btn, nullptr);
    connect(m_confirmRefP2Btn, &QPushButton::clicked, this, &YawRetraceWidget::onConfirmRefP2);
    m_wizardStack->addWidget(refP2Page);  // index 2

    // ========================================
    // Page 3: Confirm Target P1
    // ========================================
    QWidget* tgtP1Page = createCapturePage(
        "Step 3/4: Jog to Target Line Point 1",
        m_tgtP1InstructionLabel, m_tgtP1PositionLabel,
        m_confirmTgtP1Btn, m_cancelTgtP1Btn, nullptr);
    connect(m_confirmTgtP1Btn, &QPushButton::clicked, this, &YawRetraceWidget::onConfirmTgtP1);
    m_wizardStack->addWidget(tgtP1Page);  // index 3

    // ========================================
    // Page 4: Confirm Target P2
    // ========================================
    QWidget* tgtP2Page = createCapturePage(
        "Step 4/4: Verify Target Line Point 2",
        m_tgtP2InstructionLabel, m_tgtP2PositionLabel,
        m_confirmTgtP2Btn, m_cancelTgtP2Btn, nullptr);
    connect(m_confirmTgtP2Btn, &QPushButton::clicked, this, &YawRetraceWidget::onConfirmTgtP2);
    m_wizardStack->addWidget(tgtP2Page);  // index 4

    // ========================================
    // Page 5: Review + Apply
    // ========================================
    QWidget* reviewPage = new QWidget();
    QVBoxLayout* reviewLayout = new QVBoxLayout(reviewPage);

    m_reviewLabel = new QLabel();
    m_reviewLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;"
        "font-family: monospace;");
    m_reviewLabel->setWordWrap(true);
    reviewLayout->addWidget(m_reviewLabel);

    QGroupBox* hexGroup = new QGroupBox("Live Hex Position");
    hexGroup->setStyleSheet(groupStyle);
    QVBoxLayout* hexLayout = new QVBoxLayout(hexGroup);
    m_hexPositionLabel = new QLabel("X: ---  Y: ---  Z: ---\nU: ---  V: ---  W: ---");
    m_hexPositionLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-family: monospace; padding: 10px;");
    hexLayout->addWidget(m_hexPositionLabel);
    reviewLayout->addWidget(hexGroup);

    m_applyBtn = new QPushButton("Apply Correction");
    m_applyBtn->setFixedHeight(48);
    m_applyBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 16px; border-radius: 4px; padding: 10px 30px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a5c35; color: #888; }");
    connect(m_applyBtn, &QPushButton::clicked, this, &YawRetraceWidget::onApplyCorrection);
    reviewLayout->addWidget(m_applyBtn);

    m_resultLabel = new QLabel();
    m_resultLabel->setStyleSheet("font-size: 13px; color: #aaa; padding: 10px;");
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    reviewLayout->addWidget(m_resultLabel);

    reviewLayout->addStretch();

    m_wizardStack->addWidget(reviewPage);  // index 5

    // ========================================
    // Page 6: Done
    // ========================================
    QWidget* donePage = new QWidget();
    QVBoxLayout* doneLayout = new QVBoxLayout(donePage);

    m_doneLabel = new QLabel();
    m_doneLabel->setStyleSheet(
        "font-size: 14px; color: #27ae60; padding: 20px; "
        "background-color: #1a2a3f; border: 1px solid #27ae60; border-radius: 6px;"
        "font-family: monospace;");
    m_doneLabel->setWordWrap(true);
    m_doneLabel->setAlignment(Qt::AlignCenter);
    doneLayout->addWidget(m_doneLabel);

    doneLayout->addStretch();

    m_resetBtn = new QPushButton("Run Again");
    m_resetBtn->setFixedHeight(44);
    m_resetBtn->setStyleSheet(blueBtnStyle);
    connect(m_resetBtn, &QPushButton::clicked, this, &YawRetraceWidget::onReset);
    doneLayout->addWidget(m_resetBtn);

    m_wizardStack->addWidget(donePage);  // index 6

    mainLayout->addWidget(m_wizardStack, 1);
}

void YawRetraceWidget::setState(WizardState state)
{
    m_state = state;
    m_positionTimer->stop();
    m_hexTimer->stop();

    switch (state) {
    case Idle: {
        m_wizardStack->setCurrentIndex(0);
        if (!m_set) {
            m_idleSummaryLabel->setText("No training set loaded.");
            m_startBtn->setEnabled(false);
            return;
        }

        bool ready = m_set->referenceLine.valid && m_set->targetLine.valid
                     && !m_set->correction.targetDevice.empty()
                     && !m_set->correction.targetAxis.empty();

        m_startBtn->setEnabled(ready);

        if (!ready) {
            m_idleSummaryLabel->setText(
                "Training set not ready for retrace.\n\n"
                "Requirements:\n"
                "  - Both Reference and Target lines must be taught\n"
                "  - Correction device/axis must be configured (Yaw Result step)\n\n"
                "Complete the previous steps first.");
        } else {
            m_set->RecalculateDelta();
            double correction = YawTrainerMath::GetCorrectionValue(
                m_set->deltaAngleDeg, m_set->correction.reverseSign);

            m_idleSummaryLabel->setText(QString(
                "Ready to retrace.\n\n"
                "Reference Line: %1%2\n"
                "Target Line:    %3%4\n"
                "Delta:          %5%6\n\n"
                "Correction Target: %7 / %8 = %9%10%11\n\n"
                "Press Start to retrace all 4 points.")
                .arg(m_set->referenceLine.angleDeg, 0, 'f', 3).arg(QChar(0x00B0))
                .arg(m_set->targetLine.angleDeg, 0, 'f', 3).arg(QChar(0x00B0))
                .arg(m_set->deltaAngleDeg, 0, 'f', 3).arg(QChar(0x00B0))
                .arg(QString::fromStdString(m_set->correction.targetDevice))
                .arg(QString::fromStdString(m_set->correction.targetAxis))
                .arg(correction, 0, 'f', 3)
                .arg(QChar(0x00B0))
                .arg(m_set->correction.reverseSign ? " (reversed)" : ""));
        }
        break;
    }

    case WaitRefP1:
        m_wizardStack->setCurrentIndex(1);
        m_refP1InstructionLabel->setText(QString(
            "Step 1/4: Jog gantry to Reference Line Point 1\n\n"
            "Taught Ref P1: X=%1  Y=%2  Z=%3\n\n"
            "Move to Reference P1, then press Confirm.")
            .arg(m_set->referenceLine.p1x, 0, 'f', 4)
            .arg(m_set->referenceLine.p1y, 0, 'f', 4)
            .arg(m_set->referenceLine.p1z, 0, 'f', 4));
        m_positionTimer->start(100);
        break;

    case WaitRefP2:
        m_wizardStack->setCurrentIndex(2);
        m_positionTimer->start(100);
        break;

    case WaitTgtP1:
        m_wizardStack->setCurrentIndex(3);
        m_tgtP1InstructionLabel->setText(QString(
            "Step 3/4: Jog gantry to Target Line Point 1\n\n"
            "Taught Tgt P1: X=%1  Y=%2  Z=%3\n\n"
            "Move to Target P1, then press Confirm.")
            .arg(m_set->targetLine.p1x, 0, 'f', 4)
            .arg(m_set->targetLine.p1y, 0, 'f', 4)
            .arg(m_set->targetLine.p1z, 0, 'f', 4));
        m_positionTimer->start(100);
        break;

    case WaitTgtP2:
        m_wizardStack->setCurrentIndex(4);
        m_positionTimer->start(100);
        break;

    case ReviewApply:
        m_wizardStack->setCurrentIndex(5);
        m_applyBtn->setEnabled(true);
        m_resultLabel->setText("");
        m_hexTimer->start(100);
        break;

    case Done:
        m_wizardStack->setCurrentIndex(6);
        break;
    }
}

void YawRetraceWidget::onStartRetrace()
{
    if (!m_set || !m_set->referenceLine.valid || !m_set->targetLine.valid) {
        QMessageBox::warning(this, "Error", "Training set lines not valid.");
        return;
    }

    // Save taught deltas for both lines BEFORE any overwrite
    m_refDeltaX = m_set->referenceLine.p2x - m_set->referenceLine.p1x;
    m_refDeltaY = m_set->referenceLine.p2y - m_set->referenceLine.p1y;
    m_refDeltaZ = m_set->referenceLine.p2z - m_set->referenceLine.p1z;

    m_tgtDeltaX = m_set->targetLine.p2x - m_set->targetLine.p1x;
    m_tgtDeltaY = m_set->targetLine.p2y - m_set->targetLine.p1y;
    m_tgtDeltaZ = m_set->targetLine.p2z - m_set->targetLine.p1z;

    std::cout << "[YawRetrace] Start. Ref delta: dX=" << m_refDeltaX
              << " dY=" << m_refDeltaY << " dZ=" << m_refDeltaZ
              << " | Tgt delta: dX=" << m_tgtDeltaX
              << " dY=" << m_tgtDeltaY << " dZ=" << m_tgtDeltaZ << std::endl;

    m_set->correction.applied = false;

    setState(WaitRefP1);
}

void YawRetraceWidget::onConfirmRefP1()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_set->referenceLine.p1x = pos.x;
    m_set->referenceLine.p1y = pos.y;
    m_set->referenceLine.p1z = pos.z;

    std::cout << "[YawRetrace] Ref P1 confirmed: X=" << pos.x
              << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    // MoveRelative by taught ref delta to P2 area
    m_confirmRefP1Btn->setEnabled(false);
    m_confirmRefP1Btn->setText("Moving to Ref P2...");

    bool moveOk = true;
    if (std::abs(m_refDeltaX) > 1e-6)
        moveOk = m_context->motionManager->MoveRelative(m_set->deviceName, "X", m_refDeltaX, true);
    if (moveOk && std::abs(m_refDeltaY) > 1e-6)
        moveOk = m_context->motionManager->MoveRelative(m_set->deviceName, "Y", m_refDeltaY, true);
    if (moveOk && std::abs(m_refDeltaZ) > 1e-6)
        moveOk = m_context->motionManager->MoveRelative(m_set->deviceName, "Z", m_refDeltaZ, true);

    m_confirmRefP1Btn->setEnabled(true);
    m_confirmRefP1Btn->setText("Confirm");

    if (!moveOk) {
        QMessageBox::warning(this, "Move Failed",
            "Failed to move gantry by taught delta to Ref P2 area.");
        return;
    }

    auto p2pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_refP2InstructionLabel->setText(QString(
        "Step 2/4: Verify Reference Line Point 2\n\n"
        "Gantry moved by taught delta to Ref P2 area.\n"
        "Current: X=%1  Y=%2  Z=%3\n\n"
        "Adjust if needed, then press Confirm.")
        .arg(p2pos.x, 0, 'f', 4)
        .arg(p2pos.y, 0, 'f', 4)
        .arg(p2pos.z, 0, 'f', 4));

    setState(WaitRefP2);
}

void YawRetraceWidget::onConfirmRefP2()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_set->referenceLine.p2x = pos.x;
    m_set->referenceLine.p2y = pos.y;
    m_set->referenceLine.p2z = pos.z;
    m_set->referenceLine.valid = true;
    m_set->referenceLine.angleDeg = YawTrainerMath::ComputeLineAngle(
        m_set->referenceLine.p1x, m_set->referenceLine.p1y,
        m_set->referenceLine.p2x, m_set->referenceLine.p2y);

    std::cout << "[YawRetrace] Ref P2 confirmed: X=" << pos.x
              << " Y=" << pos.y << " Z=" << pos.z
              << " RefAngle=" << m_set->referenceLine.angleDeg << std::endl;

    setState(WaitTgtP1);
}

void YawRetraceWidget::onConfirmTgtP1()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_set->targetLine.p1x = pos.x;
    m_set->targetLine.p1y = pos.y;
    m_set->targetLine.p1z = pos.z;

    std::cout << "[YawRetrace] Tgt P1 confirmed: X=" << pos.x
              << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    // MoveRelative by taught target delta to P2 area
    m_confirmTgtP1Btn->setEnabled(false);
    m_confirmTgtP1Btn->setText("Moving to Tgt P2...");

    bool moveOk = true;
    if (std::abs(m_tgtDeltaX) > 1e-6)
        moveOk = m_context->motionManager->MoveRelative(m_set->deviceName, "X", m_tgtDeltaX, true);
    if (moveOk && std::abs(m_tgtDeltaY) > 1e-6)
        moveOk = m_context->motionManager->MoveRelative(m_set->deviceName, "Y", m_tgtDeltaY, true);
    if (moveOk && std::abs(m_tgtDeltaZ) > 1e-6)
        moveOk = m_context->motionManager->MoveRelative(m_set->deviceName, "Z", m_tgtDeltaZ, true);

    m_confirmTgtP1Btn->setEnabled(true);
    m_confirmTgtP1Btn->setText("Confirm");

    if (!moveOk) {
        QMessageBox::warning(this, "Move Failed",
            "Failed to move gantry by taught delta to Tgt P2 area.");
        return;
    }

    auto p2pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_tgtP2InstructionLabel->setText(QString(
        "Step 4/4: Verify Target Line Point 2\n\n"
        "Gantry moved by taught delta to Tgt P2 area.\n"
        "Current: X=%1  Y=%2  Z=%3\n\n"
        "Adjust if needed, then press Confirm.")
        .arg(p2pos.x, 0, 'f', 4)
        .arg(p2pos.y, 0, 'f', 4)
        .arg(p2pos.z, 0, 'f', 4));

    setState(WaitTgtP2);
}

void YawRetraceWidget::onConfirmTgtP2()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_set->targetLine.p2x = pos.x;
    m_set->targetLine.p2y = pos.y;
    m_set->targetLine.p2z = pos.z;
    m_set->targetLine.valid = true;
    m_set->targetLine.angleDeg = YawTrainerMath::ComputeLineAngle(
        m_set->targetLine.p1x, m_set->targetLine.p1y,
        m_set->targetLine.p2x, m_set->targetLine.p2y);

    std::cout << "[YawRetrace] Tgt P2 confirmed: X=" << pos.x
              << " Y=" << pos.y << " Z=" << pos.z
              << " TgtAngle=" << m_set->targetLine.angleDeg << std::endl;

    // Recompute delta
    m_set->RecalculateDelta();
    m_set->valid = true;

    double correction = YawTrainerMath::GetCorrectionValue(
        m_set->deltaAngleDeg, m_set->correction.reverseSign);
    m_set->correction.appliedValue = correction;

    // Build review text
    QString review = QString(
        "All 4 Points Retraced\n\n"
        "Reference Line:\n"
        "  P1(%1, %2) -> P2(%3, %4)\n"
        "  Angle: %5%6\n\n"
        "Target Line:\n"
        "  P1(%7, %8) -> P2(%9, %10)\n"
        "  Angle: %11%12\n\n"
        "Delta: %13%14\n"
        "Correction: %15 %16 = %17%18%19")
        .arg(m_set->referenceLine.p1x, 0, 'f', 4).arg(m_set->referenceLine.p1y, 0, 'f', 4)
        .arg(m_set->referenceLine.p2x, 0, 'f', 4).arg(m_set->referenceLine.p2y, 0, 'f', 4)
        .arg(m_set->referenceLine.angleDeg, 0, 'f', 3).arg(QChar(0x00B0))
        .arg(m_set->targetLine.p1x, 0, 'f', 4).arg(m_set->targetLine.p1y, 0, 'f', 4)
        .arg(m_set->targetLine.p2x, 0, 'f', 4).arg(m_set->targetLine.p2y, 0, 'f', 4)
        .arg(m_set->targetLine.angleDeg, 0, 'f', 3).arg(QChar(0x00B0))
        .arg(m_set->deltaAngleDeg, 0, 'f', 3).arg(QChar(0x00B0))
        .arg(QString::fromStdString(m_set->correction.targetDevice))
        .arg(QString::fromStdString(m_set->correction.targetAxis))
        .arg(correction, 0, 'f', 3)
        .arg(QChar(0x00B0))
        .arg(m_set->correction.reverseSign ? " (reversed)" : "");

    m_reviewLabel->setText(review);

    emit retraceCompleted(*m_set);

    setState(ReviewApply);
}

void YawRetraceWidget::onApplyCorrection()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    if (m_set->correction.targetDevice.empty() || m_set->correction.targetAxis.empty()) {
        QMessageBox::warning(this, "Error", "No correction target configured.");
        return;
    }

    auto beforePos = m_context->motionManager->GetCurrentPosition(m_set->correction.targetDevice);

    std::cout << "[YawRetrace] Apply: MoveRelative " << m_set->correction.targetDevice
              << " " << m_set->correction.targetAxis
              << " by " << m_set->correction.appliedValue << " deg" << std::endl;

    bool ok = m_context->motionManager->MoveRelative(
        m_set->correction.targetDevice,
        m_set->correction.targetAxis,
        m_set->correction.appliedValue,
        true);

    if (ok) {
        auto afterPos = m_context->motionManager->GetCurrentPosition(m_set->correction.targetDevice);
        m_set->correction.applied = true;

        m_doneLabel->setText(QString(
            "Yaw Correction Applied Successfully\n\n"
            "Applied %1%2 to %3 %4\n\n"
            "Before: U=%5  V=%6  W=%7\n"
            "After:  U=%8  V=%9  W=%10\n\n"
            "Training set updated and ready to save.")
            .arg(m_set->correction.appliedValue, 0, 'f', 3)
            .arg(QChar(0x00B0))
            .arg(QString::fromStdString(m_set->correction.targetDevice))
            .arg(QString::fromStdString(m_set->correction.targetAxis))
            .arg(beforePos.u, 0, 'f', 3).arg(beforePos.v, 0, 'f', 3).arg(beforePos.w, 0, 'f', 3)
            .arg(afterPos.u, 0, 'f', 3).arg(afterPos.v, 0, 'f', 3).arg(afterPos.w, 0, 'f', 3));

        emit correctionApplied(m_set->correction.appliedValue);
        setState(Done);
    } else {
        m_resultLabel->setText("Failed to apply correction. Check device connection.");
        m_resultLabel->setStyleSheet("font-size: 13px; color: #e74c3c; padding: 10px;");
    }
}

void YawRetraceWidget::onReset()
{
    setState(Idle);
}

void YawRetraceWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    QString text = QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4);

    switch (m_state) {
    case WaitRefP1: m_refP1PositionLabel->setText(text); break;
    case WaitRefP2: m_refP2PositionLabel->setText(text); break;
    case WaitTgtP1: m_tgtP1PositionLabel->setText(text); break;
    case WaitTgtP2: m_tgtP2PositionLabel->setText(text); break;
    default: break;
    }
}

void YawRetraceWidget::updateHexPositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_set ||
        m_set->correction.targetDevice.empty()) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->correction.targetDevice);
    m_hexPositionLabel->setText(QString(
        "Current %1 position:\n"
        "  X: %2   Y: %3   Z: %4\n"
        "  U: %5   V: %6   W: %7")
        .arg(QString::fromStdString(m_set->correction.targetDevice))
        .arg(pos.x, 8, 'f', 3).arg(pos.y, 8, 'f', 3).arg(pos.z, 8, 'f', 3)
        .arg(pos.u, 8, 'f', 3).arg(pos.v, 8, 'f', 3).arg(pos.w, 8, 'f', 3));
}

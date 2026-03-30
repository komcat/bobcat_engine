// RetraceAndApplyWidget.cpp - Combined retrace P1 -> P2 -> recompute -> apply correction
#include "RetraceAndApplyWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>

RetraceAndApplyWidget::RetraceAndApplyWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
    , m_hexTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &RetraceAndApplyWidget::updatePositionDisplay);
    connect(m_hexTimer, &QTimer::timeout, this, &RetraceAndApplyWidget::updateHexPositionDisplay);
}

void RetraceAndApplyWidget::SetData(PlanarAdjustmentSet* set)
{
    m_set = set;
    setState(Idle);
}

void RetraceAndApplyWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Retrace & Apply Correction");
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

    // ========================================
    // Page 0: Idle - show summary, start button
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
    connect(m_startBtn, &QPushButton::clicked, this, &RetraceAndApplyWidget::onStartRetrace);
    idleLayout->addWidget(m_startBtn);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Confirm P1
    // ========================================
    QWidget* p1Page = new QWidget();
    QVBoxLayout* p1Layout = new QVBoxLayout(p1Page);

    m_p1InstructionLabel = new QLabel(
        "Step 1/3: Jog gantry to Point 1 position\n\n"
        "Move gantry near the Point 1 location, then press Confirm.\n"
        "The current position will overwrite the saved Point 1.");
    m_p1InstructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_p1InstructionLabel->setWordWrap(true);
    m_p1InstructionLabel->setAlignment(Qt::AlignCenter);
    p1Layout->addWidget(m_p1InstructionLabel);

    QGroupBox* p1PosGroup = new QGroupBox("Current Gantry Position");
    p1PosGroup->setStyleSheet(groupStyle);
    QVBoxLayout* p1PosLayout = new QVBoxLayout(p1PosGroup);
    m_p1PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_p1PositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_p1PositionLabel->setAlignment(Qt::AlignCenter);
    p1PosLayout->addWidget(m_p1PositionLabel);
    p1Layout->addWidget(p1PosGroup);

    p1Layout->addStretch();

    QHBoxLayout* p1BtnRow = new QHBoxLayout();
    m_confirmP1Btn = new QPushButton("Confirm Point 1");
    m_confirmP1Btn->setFixedHeight(44);
    m_confirmP1Btn->setStyleSheet(greenBtnStyle);
    connect(m_confirmP1Btn, &QPushButton::clicked, this, &RetraceAndApplyWidget::onConfirmP1);
    p1BtnRow->addWidget(m_confirmP1Btn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(44);
    m_cancelBtn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(Idle);
    });
    p1BtnRow->addWidget(m_cancelBtn);
    p1Layout->addLayout(p1BtnRow);

    m_wizardStack->addWidget(p1Page);  // index 1

    // ========================================
    // Page 2: Confirm P2 (after MoveRelative)
    // ========================================
    QWidget* p2Page = new QWidget();
    QVBoxLayout* p2Layout = new QVBoxLayout(p2Page);

    m_p2InstructionLabel = new QLabel(
        "Step 2/3: Verify Point 2\n\n"
        "Gantry moved by taught delta to Point 2 area.\n"
        "Adjust manually if needed, then press Confirm.\n"
        "The current position will overwrite the saved Point 2.");
    m_p2InstructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_p2InstructionLabel->setWordWrap(true);
    m_p2InstructionLabel->setAlignment(Qt::AlignCenter);
    p2Layout->addWidget(m_p2InstructionLabel);

    QGroupBox* p2PosGroup = new QGroupBox("Current Gantry Position");
    p2PosGroup->setStyleSheet(groupStyle);
    QVBoxLayout* p2PosLayout = new QVBoxLayout(p2PosGroup);
    m_p2PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_p2PositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_p2PositionLabel->setAlignment(Qt::AlignCenter);
    p2PosLayout->addWidget(m_p2PositionLabel);
    p2Layout->addWidget(p2PosGroup);

    p2Layout->addStretch();

    QHBoxLayout* p2BtnRow = new QHBoxLayout();
    m_confirmP2Btn = new QPushButton("Confirm Point 2");
    m_confirmP2Btn->setFixedHeight(44);
    m_confirmP2Btn->setStyleSheet(greenBtnStyle);
    connect(m_confirmP2Btn, &QPushButton::clicked, this, &RetraceAndApplyWidget::onConfirmP2);
    p2BtnRow->addWidget(m_confirmP2Btn);

    m_cancelP2Btn = new QPushButton("Cancel");
    m_cancelP2Btn->setFixedHeight(44);
    m_cancelP2Btn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelP2Btn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(Idle);
    });
    p2BtnRow->addWidget(m_cancelP2Btn);
    p2Layout->addLayout(p2BtnRow);

    m_wizardStack->addWidget(p2Page);  // index 2

    // ========================================
    // Page 3: Review recomputed angles + Apply
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
    connect(m_applyBtn, &QPushButton::clicked, this, &RetraceAndApplyWidget::onApplyCorrection);
    reviewLayout->addWidget(m_applyBtn);

    m_resultLabel = new QLabel();
    m_resultLabel->setStyleSheet("font-size: 13px; color: #aaa; padding: 10px;");
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    reviewLayout->addWidget(m_resultLabel);

    reviewLayout->addStretch();

    m_wizardStack->addWidget(reviewPage);  // index 3

    // ========================================
    // Page 4: Done
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
    connect(m_resetBtn, &QPushButton::clicked, this, &RetraceAndApplyWidget::onReset);
    doneLayout->addWidget(m_resetBtn);

    m_wizardStack->addWidget(donePage);  // index 4

    mainLayout->addWidget(m_wizardStack, 1);
}

void RetraceAndApplyWidget::setState(WizardState state)
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

        bool ready = m_set->point1.valid && m_set->point2.valid
                     && !m_set->correction.targetDevice.empty()
                     && !m_set->correction.targetAxis.empty();

        m_startBtn->setEnabled(ready);

        if (!ready) {
            m_idleSummaryLabel->setText(
                "Training set not ready for retrace.\n\n"
                "Requirements:\n"
                "  - Both Point 1 and Point 2 must be taught\n"
                "  - Correction device/axis must be configured (Tilt & Select step)\n\n"
                "Complete the previous steps first.");
        } else {
            m_set->RecalculateAngles();
            double sep = PlanarAdjustmentMath::Separation(m_set->point1, m_set->point2);
            QString angleInfo;
            if (m_set->angleXValid)
                angleInfo += QString("  Angle X: %1%2\n").arg(m_set->angleX_deg, 0, 'f', 3).arg(QChar(0x00B0));
            if (m_set->angleYValid)
                angleInfo += QString("  Angle Y: %1%2\n").arg(m_set->angleY_deg, 0, 'f', 3).arg(QChar(0x00B0));

            double corrVal = 0.0;
            if (m_set->correction.sourceAngle == "angle_x" && m_set->angleXValid)
                corrVal = PlanarAdjustmentMath::GetCorrectionValue(m_set->angleX_deg, m_set->correction.reverseSign);
            else if (m_set->correction.sourceAngle == "angle_y" && m_set->angleYValid)
                corrVal = PlanarAdjustmentMath::GetCorrectionValue(m_set->angleY_deg, m_set->correction.reverseSign);

            m_idleSummaryLabel->setText(QString(
                "Ready to retrace.\n\n"
                "Taught Points:\n"
                "  P1: X=%1  Y=%2  Z=%3\n"
                "  P2: X=%4  Y=%5  Z=%6\n"
                "  Separation: %7 mm\n\n"
                "Current Angles:\n%8\n"
                "Correction Target:\n"
                "  %9 / %10 axis = %11%12%13\n\n"
                "Press Start to begin retrace.")
                .arg(m_set->point1.x, 0, 'f', 4).arg(m_set->point1.y, 0, 'f', 4).arg(m_set->point1.z, 0, 'f', 4)
                .arg(m_set->point2.x, 0, 'f', 4).arg(m_set->point2.y, 0, 'f', 4).arg(m_set->point2.z, 0, 'f', 4)
                .arg(sep, 0, 'f', 3)
                .arg(angleInfo)
                .arg(QString::fromStdString(m_set->correction.targetDevice))
                .arg(QString::fromStdString(m_set->correction.targetAxis))
                .arg(corrVal, 0, 'f', 3)
                .arg(QChar(0x00B0))
                .arg(m_set->correction.reverseSign ? " (reversed)" : ""));
        }
        break;
    }

    case WaitP1:
        m_wizardStack->setCurrentIndex(1);
        m_confirmP1Btn->setEnabled(false);
        m_p1InstructionLabel->setText(QString(
            "Step 1/3: Navigating to Point 1...\n\n"
            "Taught P1: X=%1  Y=%2  Z=%3\n\n"
            "Moving gantry to Point 1, please wait...")
            .arg(m_set->point1.x, 0, 'f', 4)
            .arg(m_set->point1.y, 0, 'f', 4)
            .arg(m_set->point1.z, 0, 'f', 4));
        m_positionTimer->start(100);
        QTimer::singleShot(0, this, [this]() { autoNavigateToP1(); });
        break;

    case WaitP2:
        m_wizardStack->setCurrentIndex(2);
        m_confirmP2Btn->setEnabled(false);
        m_p2InstructionLabel->setText(QString(
            "Step 2/3: Navigating to Point 2...\n\n"
            "Taught P2: X=%1  Y=%2  Z=%3\n\n"
            "Moving gantry to Point 2, please wait...")
            .arg(m_taughtP2X, 0, 'f', 4)
            .arg(m_taughtP2Y, 0, 'f', 4)
            .arg(m_taughtP2Z, 0, 'f', 4));
        m_positionTimer->start(100);
        QTimer::singleShot(0, this, [this]() { autoNavigateToP2(); });
        break;

    case ReviewApply:
        m_wizardStack->setCurrentIndex(3);
        m_applyBtn->setEnabled(true);
        m_resultLabel->setText("");
        m_hexTimer->start(100);
        break;

    case Done:
        m_wizardStack->setCurrentIndex(4);
        break;
    }
}

void RetraceAndApplyWidget::onStartRetrace()
{
    if (!m_set || !m_set->point1.valid || !m_set->point2.valid) {
        QMessageBox::warning(this, "Error", "Training set points not valid.");
        return;
    }

    // Save taught P2 absolute position BEFORE any overwrite
    m_taughtP2X = m_set->point2.x;
    m_taughtP2Y = m_set->point2.y;
    m_taughtP2Z = m_set->point2.z;

    std::cout << "[Retrace] Start. Taught P2: X=" << m_taughtP2X
              << " Y=" << m_taughtP2Y << " Z=" << m_taughtP2Z << std::endl;

    // Reset applied flag for this run
    m_set->correction.applied = false;

    setState(WaitP1);
}

void RetraceAndApplyWidget::onConfirmP1()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    // Capture current position as new P1
    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_set->point1.x = pos.x;
    m_set->point1.y = pos.y;
    m_set->point1.z = pos.z;
    m_set->point1.valid = true;

    std::cout << "[Retrace] P1 confirmed: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    setState(WaitP2);
}

void RetraceAndApplyWidget::onConfirmP2()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    // Capture current position as new P2
    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_set->point2.x = pos.x;
    m_set->point2.y = pos.y;
    m_set->point2.z = pos.z;
    m_set->point2.valid = true;

    std::cout << "[Retrace] P2 confirmed: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    // Recompute angles from new P1/P2
    m_set->RecalculateAngles();
    m_set->valid = true;

    std::cout << "[Retrace] Recomputed: AngleX=" << m_set->angleX_deg
              << " AngleY=" << m_set->angleY_deg << std::endl;

    // Compute correction value
    double angleDeg = 0.0;
    bool angleValid = false;
    if (m_set->correction.sourceAngle == "angle_x") {
        angleDeg = m_set->angleX_deg;
        angleValid = m_set->angleXValid;
    } else if (m_set->correction.sourceAngle == "angle_y") {
        angleDeg = m_set->angleY_deg;
        angleValid = m_set->angleYValid;
    }

    double corrVal = PlanarAdjustmentMath::GetCorrectionValue(angleDeg, m_set->correction.reverseSign);
    m_set->correction.appliedValue = corrVal;

    // Build review text
    double sep = PlanarAdjustmentMath::Separation(m_set->point1, m_set->point2);

    QString review = QString(
        "Step 3/3: Review & Apply Correction\n\n"
        "New Point 1:  X=%1  Y=%2  Z=%3\n"
        "New Point 2:  X=%4  Y=%5  Z=%6\n"
        "dX=%7  dY=%8  dZ=%9\n"
        "Separation: %10 mm\n\n")
        .arg(m_set->point1.x, 0, 'f', 4).arg(m_set->point1.y, 0, 'f', 4).arg(m_set->point1.z, 0, 'f', 4)
        .arg(m_set->point2.x, 0, 'f', 4).arg(m_set->point2.y, 0, 'f', 4).arg(m_set->point2.z, 0, 'f', 4)
        .arg(m_set->deltaX, 0, 'f', 4).arg(m_set->deltaY, 0, 'f', 4).arg(m_set->deltaZ, 0, 'f', 4)
        .arg(sep, 0, 'f', 3);

    if (m_set->angleXValid)
        review += QString("Angle X (YZ tilt): %1%2\n").arg(m_set->angleX_deg, 0, 'f', 3).arg(QChar(0x00B0));
    if (m_set->angleYValid)
        review += QString("Angle Y (XZ tilt): %1%2\n").arg(m_set->angleY_deg, 0, 'f', 3).arg(QChar(0x00B0));

    review += QString("\nCorrection: %1 %2 = %3%4%5")
        .arg(QString::fromStdString(m_set->correction.targetDevice))
        .arg(QString::fromStdString(m_set->correction.targetAxis))
        .arg(corrVal, 0, 'f', 3)
        .arg(QChar(0x00B0))
        .arg(m_set->correction.reverseSign ? " (reversed)" : "");

    if (!angleValid) {
        review += "\n\nWARNING: Selected source angle is not valid!";
        m_applyBtn->setEnabled(false);
    }

    m_reviewLabel->setText(review);

    // Emit updated set so page can save
    emit retraceCompleted(*m_set);

    setState(ReviewApply);
}

void RetraceAndApplyWidget::autoNavigateToP1()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    std::cout << "[Retrace] Auto-navigating to P1: X=" << m_set->point1.x
              << " Y=" << m_set->point1.y << " Z=" << m_set->point1.z << std::endl;

    bool moveOk = true;
    moveOk = m_context->motionManager->MoveToPosition(m_set->deviceName, "X", m_set->point1.x, true);
    if (moveOk)
        moveOk = m_context->motionManager->MoveToPosition(m_set->deviceName, "Y", m_set->point1.y, true);
    if (moveOk)
        moveOk = m_context->motionManager->MoveToPosition(m_set->deviceName, "Z", m_set->point1.z, true);

    if (!moveOk) {
        m_p1InstructionLabel->setText(QString(
            "Step 1/3: Auto-navigate to Point 1 FAILED\n\n"
            "Taught P1: X=%1  Y=%2  Z=%3\n\n"
            "Manually jog to Point 1, then press Confirm.")
            .arg(m_set->point1.x, 0, 'f', 4)
            .arg(m_set->point1.y, 0, 'f', 4)
            .arg(m_set->point1.z, 0, 'f', 4));
    } else {
        m_p1InstructionLabel->setText(QString(
            "Step 1/3: Arrived at Point 1\n\n"
            "Taught P1: X=%1  Y=%2  Z=%3\n\n"
            "Verify position and press Confirm.")
            .arg(m_set->point1.x, 0, 'f', 4)
            .arg(m_set->point1.y, 0, 'f', 4)
            .arg(m_set->point1.z, 0, 'f', 4));
    }
    m_confirmP1Btn->setEnabled(true);
}

void RetraceAndApplyWidget::autoNavigateToP2()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    std::cout << "[Retrace] Auto-navigating to P2: X=" << m_taughtP2X
              << " Y=" << m_taughtP2Y << " Z=" << m_taughtP2Z << std::endl;

    bool moveOk = true;
    moveOk = m_context->motionManager->MoveToPosition(m_set->deviceName, "X", m_taughtP2X, true);
    if (moveOk)
        moveOk = m_context->motionManager->MoveToPosition(m_set->deviceName, "Y", m_taughtP2Y, true);
    if (moveOk)
        moveOk = m_context->motionManager->MoveToPosition(m_set->deviceName, "Z", m_taughtP2Z, true);

    if (!moveOk) {
        m_p2InstructionLabel->setText(QString(
            "Step 2/3: Auto-navigate to Point 2 FAILED\n\n"
            "Taught P2: X=%1  Y=%2  Z=%3\n\n"
            "Manually jog to Point 2, then press Confirm.")
            .arg(m_taughtP2X, 0, 'f', 4)
            .arg(m_taughtP2Y, 0, 'f', 4)
            .arg(m_taughtP2Z, 0, 'f', 4));
    } else {
        m_p2InstructionLabel->setText(QString(
            "Step 2/3: Arrived at Point 2\n\n"
            "Taught P2: X=%1  Y=%2  Z=%3\n\n"
            "Verify position and press Confirm.")
            .arg(m_taughtP2X, 0, 'f', 4)
            .arg(m_taughtP2Y, 0, 'f', 4)
            .arg(m_taughtP2Z, 0, 'f', 4));
    }
    m_confirmP2Btn->setEnabled(true);
}

void RetraceAndApplyWidget::onApplyCorrection()
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

    std::cout << "[Retrace] Apply: MoveRelative " << m_set->correction.targetDevice
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
            "Correction Applied Successfully\n\n"
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

        std::cout << "[Retrace] Applied. Before U=" << beforePos.u
                  << " After U=" << afterPos.u << std::endl;

        emit correctionApplied(m_set->correction.appliedValue);
        setState(Done);
    } else {
        m_resultLabel->setText("Failed to apply correction. Check device connection.");
        m_resultLabel->setStyleSheet("font-size: 13px; color: #e74c3c; padding: 10px;");
        std::cout << "[Retrace] MoveRelative failed!" << std::endl;
    }
}

void RetraceAndApplyWidget::onReset()
{
    setState(Idle);
}

void RetraceAndApplyWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    QString text = QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4);

    if (m_state == WaitP1)
        m_p1PositionLabel->setText(text);
    else if (m_state == WaitP2)
        m_p2PositionLabel->setText(text);
}

void RetraceAndApplyWidget::updateHexPositionDisplay()
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

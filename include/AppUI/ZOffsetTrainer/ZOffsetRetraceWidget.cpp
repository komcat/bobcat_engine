// ZOffsetRetraceWidget.cpp - Combined retrace: recapture ref+target -> recompute -> apply Z correction
#include "ZOffsetRetraceWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateTransformer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>
#include <cmath>

ZOffsetRetraceWidget::ZOffsetRetraceWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
    , m_hexTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &ZOffsetRetraceWidget::updatePositionDisplay);
    connect(m_hexTimer, &QTimer::timeout, this, &ZOffsetRetraceWidget::updateHexPositionDisplay);
}

void ZOffsetRetraceWidget::SetData(ZOffsetSet* set)
{
    m_set = set;
    setState(Idle);
}

void ZOffsetRetraceWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Retrace & Apply Z Correction");
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
    connect(m_startBtn, &QPushButton::clicked, this, &ZOffsetRetraceWidget::onStartRetrace);
    idleLayout->addWidget(m_startBtn);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Confirm Reference Surface
    // ========================================
    QWidget* refPage = new QWidget();
    QVBoxLayout* refLayout = new QVBoxLayout(refPage);

    m_refInstructionLabel = new QLabel(
        "Step 1/3: Jog gantry to Reference Surface\n\n"
        "Move gantry to the reference surface position, then press Confirm.\n"
        "Current position will overwrite saved Reference point.");
    m_refInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_refInstructionLabel->setWordWrap(true);
    m_refInstructionLabel->setAlignment(Qt::AlignCenter);
    refLayout->addWidget(m_refInstructionLabel);

    QGroupBox* refPosGroup = new QGroupBox("Current Gantry Position");
    refPosGroup->setStyleSheet(groupStyle);
    QVBoxLayout* refPosLayout = new QVBoxLayout(refPosGroup);
    m_refPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_refPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_refPositionLabel->setAlignment(Qt::AlignCenter);
    refPosLayout->addWidget(m_refPositionLabel);
    refLayout->addWidget(refPosGroup);

    refLayout->addStretch();

    QHBoxLayout* refBtnRow = new QHBoxLayout();
    m_confirmRefBtn = new QPushButton("Confirm Reference");
    m_confirmRefBtn->setFixedHeight(44);
    m_confirmRefBtn->setStyleSheet(greenBtnStyle);
    connect(m_confirmRefBtn, &QPushButton::clicked, this, &ZOffsetRetraceWidget::onConfirmReference);
    refBtnRow->addWidget(m_confirmRefBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(44);
    m_cancelBtn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(Idle);
    });
    refBtnRow->addWidget(m_cancelBtn);
    refLayout->addLayout(refBtnRow);

    m_wizardStack->addWidget(refPage);  // index 1

    // ========================================
    // Page 2: Confirm Target Surface
    // ========================================
    QWidget* tgtPage = new QWidget();
    QVBoxLayout* tgtLayout = new QVBoxLayout(tgtPage);

    m_tgtInstructionLabel = new QLabel(
        "Step 2/3: Jog gantry to Target Surface\n\n"
        "Move gantry to the target surface position, then press Confirm.\n"
        "Current position will overwrite saved Target point.");
    m_tgtInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_tgtInstructionLabel->setWordWrap(true);
    m_tgtInstructionLabel->setAlignment(Qt::AlignCenter);
    tgtLayout->addWidget(m_tgtInstructionLabel);

    QGroupBox* tgtPosGroup = new QGroupBox("Current Gantry Position");
    tgtPosGroup->setStyleSheet(groupStyle);
    QVBoxLayout* tgtPosLayout = new QVBoxLayout(tgtPosGroup);
    m_tgtPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_tgtPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_tgtPositionLabel->setAlignment(Qt::AlignCenter);
    tgtPosLayout->addWidget(m_tgtPositionLabel);
    tgtLayout->addWidget(tgtPosGroup);

    tgtLayout->addStretch();

    QHBoxLayout* tgtBtnRow = new QHBoxLayout();
    m_confirmTgtBtn = new QPushButton("Confirm Target");
    m_confirmTgtBtn->setFixedHeight(44);
    m_confirmTgtBtn->setStyleSheet(greenBtnStyle);
    connect(m_confirmTgtBtn, &QPushButton::clicked, this, &ZOffsetRetraceWidget::onConfirmTarget);
    tgtBtnRow->addWidget(m_confirmTgtBtn);

    m_cancelTgtBtn = new QPushButton("Cancel");
    m_cancelTgtBtn->setFixedHeight(44);
    m_cancelTgtBtn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelTgtBtn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(Idle);
    });
    tgtBtnRow->addWidget(m_cancelTgtBtn);
    tgtLayout->addLayout(tgtBtnRow);

    m_wizardStack->addWidget(tgtPage);  // index 2

    // ========================================
    // Page 3: Review recomputed Z diff + Apply
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

    m_applyBtn = new QPushButton("Apply Z Correction");
    m_applyBtn->setFixedHeight(48);
    m_applyBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 16px; border-radius: 4px; padding: 10px 30px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a5c35; color: #888; }");
    connect(m_applyBtn, &QPushButton::clicked, this, &ZOffsetRetraceWidget::onApplyCorrection);
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
    connect(m_resetBtn, &QPushButton::clicked, this, &ZOffsetRetraceWidget::onReset);
    doneLayout->addWidget(m_resetBtn);

    m_wizardStack->addWidget(donePage);  // index 4

    mainLayout->addWidget(m_wizardStack, 1);
}

void ZOffsetRetraceWidget::setState(WizardState state)
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

        bool ready = m_set->reference.valid && m_set->target.valid
                     && !m_set->correction.targetDevice.empty();

        m_startBtn->setEnabled(ready);

        if (!ready) {
            m_idleSummaryLabel->setText(
                "Training set not ready for retrace.\n\n"
                "Requirements:\n"
                "  - Both Reference and Target surfaces must be taught\n"
                "  - Target device must be configured (Z Offset & Config step)\n\n"
                "Complete the previous steps first.");
        } else {
            m_set->RecalculateZDiff();
            double measuredUm = m_set->measuredZDiffMm * 1000.0;
            double corrMm = ZOffsetMath::ComputeCorrection(
                m_set->measuredZDiffMm, m_set->correction.targetZDiffUm, m_set->correction.reverseSign);
            double corrUm = corrMm * 1000.0;

            m_idleSummaryLabel->setText(QString(
                "Ready to retrace.\n\n"
                "Current Points:\n"
                "  Reference: Z=%1 mm  (X=%2, Y=%3)\n"
                "  Target:    Z=%4 mm  (X=%5, Y=%6)\n\n"
                "Measured Z Diff: %7 mm (%8 %9m)\n"
                "Target Z Diff:   %10 %11m\n\n"
                "Correction: %12 Z = %13 mm (%14 %15m)%16\n\n"
                "Press Start to begin retrace.")
                .arg(m_set->reference.z, 0, 'f', 4).arg(m_set->reference.x, 0, 'f', 4).arg(m_set->reference.y, 0, 'f', 4)
                .arg(m_set->target.z, 0, 'f', 4).arg(m_set->target.x, 0, 'f', 4).arg(m_set->target.y, 0, 'f', 4)
                .arg(m_set->measuredZDiffMm, 0, 'f', 4)
                .arg(measuredUm, 0, 'f', 1)
                .arg(QChar(0x00B5))
                .arg(m_set->correction.targetZDiffUm, 0, 'f', 1)
                .arg(QChar(0x00B5))
                .arg(QString::fromStdString(m_set->correction.targetDevice))
                .arg(corrMm, 0, 'f', 4)
                .arg(corrUm, 0, 'f', 1)
                .arg(QChar(0x00B5))
                .arg(m_set->correction.reverseSign ? " (reversed)" : ""));
        }
        break;
    }

    case WaitReference:
        m_wizardStack->setCurrentIndex(1);
        m_refInstructionLabel->setText(QString(
            "Step 1/3: Jog gantry to Reference Surface\n\n"
            "Saved Reference: Z=%1 mm  (X=%2, Y=%3)\n\n"
            "Move gantry to reference surface, then press Confirm.\n"
            "Current position will overwrite saved Reference.")
            .arg(m_set->reference.z, 0, 'f', 4)
            .arg(m_set->reference.x, 0, 'f', 4)
            .arg(m_set->reference.y, 0, 'f', 4));
        m_positionTimer->start(100);
        break;

    case WaitTarget:
        m_wizardStack->setCurrentIndex(2);
        m_tgtInstructionLabel->setText(QString(
            "Step 2/3: Jog gantry to Target Surface\n\n"
            "Saved Target: Z=%1 mm  (X=%2, Y=%3)\n\n"
            "Move gantry to target surface, then press Confirm.\n"
            "Current position will overwrite saved Target.")
            .arg(m_set->target.z, 0, 'f', 4)
            .arg(m_set->target.x, 0, 'f', 4)
            .arg(m_set->target.y, 0, 'f', 4));
        m_positionTimer->start(100);
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

void ZOffsetRetraceWidget::onStartRetrace()
{
    if (!m_set || !m_set->reference.valid || !m_set->target.valid) {
        QMessageBox::warning(this, "Error", "Training set points not valid.");
        return;
    }

    std::cout << "[ZOffsetRetrace] Start retrace." << std::endl;

    // Reset applied flag for this run
    m_set->correction.applied = false;

    setState(WaitReference);
}

void ZOffsetRetraceWidget::onConfirmReference()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    // Capture current position as new reference
    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_set->reference.x = pos.x;
    m_set->reference.y = pos.y;
    m_set->reference.z = pos.z;
    m_set->reference.valid = true;

    std::cout << "[ZOffsetRetrace] Reference confirmed: Z=" << pos.z
              << " (X=" << pos.x << " Y=" << pos.y << ")" << std::endl;

    setState(WaitTarget);
}

void ZOffsetRetraceWidget::onConfirmTarget()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    // Capture current position as new target
    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    m_set->target.x = pos.x;
    m_set->target.y = pos.y;
    m_set->target.z = pos.z;
    m_set->target.valid = true;

    std::cout << "[ZOffsetRetrace] Target confirmed: Z=" << pos.z
              << " (X=" << pos.x << " Y=" << pos.y << ")" << std::endl;

    // Recompute Z difference
    m_set->RecalculateZDiff();
    m_set->valid = true;

    std::cout << "[ZOffsetRetrace] Recomputed Z diff: " << m_set->measuredZDiffMm << " mm" << std::endl;

    // Compute correction value
    double corrMm = ZOffsetMath::ComputeCorrection(
        m_set->measuredZDiffMm, m_set->correction.targetZDiffUm, m_set->correction.reverseSign);
    m_set->correction.appliedValueMm = corrMm;

    double measuredUm = m_set->measuredZDiffMm * 1000.0;
    double corrUm = corrMm * 1000.0;

    // Build review text
    QString review = QString(
        "Step 3/3: Review & Apply Z Correction\n\n"
        "New Reference: Z=%1 mm  (X=%2, Y=%3)\n"
        "New Target:    Z=%4 mm  (X=%5, Y=%6)\n\n"
        "Measured Z Diff: %7 mm (%8 %9m)\n"
        "Target Z Diff:   %10 %11m\n\n"
        "Correction: %12 Z = %13 mm (%14 %15m)%16")
        .arg(m_set->reference.z, 0, 'f', 4).arg(m_set->reference.x, 0, 'f', 4).arg(m_set->reference.y, 0, 'f', 4)
        .arg(m_set->target.z, 0, 'f', 4).arg(m_set->target.x, 0, 'f', 4).arg(m_set->target.y, 0, 'f', 4)
        .arg(m_set->measuredZDiffMm, 0, 'f', 4)
        .arg(measuredUm, 0, 'f', 1)
        .arg(QChar(0x00B5))
        .arg(m_set->correction.targetZDiffUm, 0, 'f', 1)
        .arg(QChar(0x00B5))
        .arg(QString::fromStdString(m_set->correction.targetDevice))
        .arg(corrMm, 0, 'f', 4)
        .arg(corrUm, 0, 'f', 1)
        .arg(QChar(0x00B5))
        .arg(m_set->correction.reverseSign ? " (reversed)" : "");

    m_reviewLabel->setText(review);

    // Emit updated set so page can save
    emit retraceCompleted(*m_set);

    setState(ReviewApply);
}

void ZOffsetRetraceWidget::onApplyCorrection()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    if (m_set->correction.targetDevice.empty()) {
        QMessageBox::warning(this, "Error", "No target device configured.");
        return;
    }

    auto beforePos = m_context->motionManager->GetCurrentPosition(m_set->correction.targetDevice);

    std::cout << "[ZOffsetRetrace] Global Z correction: " << m_set->correction.appliedValueMm
              << " mm -> " << m_set->correction.targetDevice << std::endl;

    // Transform global Z correction to local hex axes
    TransformationMatrixLoader matrixLoader;
    bool matrixLoaded = matrixLoader.LoadFromFile("config/transformation_matrix.json");
    if (!matrixLoaded) {
        QMessageBox::warning(this, "Error", "Could not load transformation_matrix.json");
        return;
    }

    const auto* matrix = matrixLoader.GetMatrix(m_set->correction.targetDevice);
    if (!matrix) {
        QMessageBox::warning(this, "Error",
            QString("No transformation matrix found for device: %1")
                .arg(QString::fromStdString(m_set->correction.targetDevice)));
        return;
    }

    CoordinateTransformer::Vector6D zGlobalDelta(0.0, 0.0, m_set->correction.appliedValueMm, 0.0, 0.0, 0.0);
    auto zLocalDelta = CoordinateTransformer::TransformGlobalToLocal(zGlobalDelta, *matrix);

    std::cout << "[ZOffsetRetrace] Local delta: dX=" << zLocalDelta.x
              << " dY=" << zLocalDelta.y << " dZ=" << zLocalDelta.z << std::endl;

    // Apply via MoveToAbs6 (adds local deltas to current position)
    auto curPos = m_context->motionManager->GetCurrentPosition(m_set->correction.targetDevice);
    PositionStruct targetPos;
    targetPos.x = curPos.x + zLocalDelta.x;
    targetPos.y = curPos.y + zLocalDelta.y;
    targetPos.z = curPos.z + zLocalDelta.z;
    targetPos.u = curPos.u;
    targetPos.v = curPos.v;
    targetPos.w = curPos.w;

    bool ok = m_context->motionManager->MoveToAbs6(m_set->correction.targetDevice, targetPos, true);

    if (ok) {
        auto afterPos = m_context->motionManager->GetCurrentPosition(m_set->correction.targetDevice);
        m_set->correction.applied = true;

        double corrUm = m_set->correction.appliedValueMm * 1000.0;
        m_doneLabel->setText(QString(
            "Z Correction Applied Successfully\n\n"
            "Applied %1 mm (%2 %3m) to %4 Z\n\n"
            "Before: Z=%5\n"
            "After:  Z=%6\n\n"
            "Training set updated and ready to save.")
            .arg(m_set->correction.appliedValueMm, 0, 'f', 4)
            .arg(corrUm, 0, 'f', 1)
            .arg(QChar(0x00B5))
            .arg(QString::fromStdString(m_set->correction.targetDevice))
            .arg(beforePos.z, 0, 'f', 4)
            .arg(afterPos.z, 0, 'f', 4));

        std::cout << "[ZOffsetRetrace] Applied. Before Z=" << beforePos.z
                  << " After Z=" << afterPos.z << std::endl;

        emit correctionApplied(m_set->correction.appliedValueMm);
        setState(Done);
    } else {
        m_resultLabel->setText("Failed to apply correction. Check device connection.");
        m_resultLabel->setStyleSheet("font-size: 13px; color: #e74c3c; padding: 10px;");
        std::cout << "[ZOffsetRetrace] MoveToAbs6 failed!" << std::endl;
    }
}

void ZOffsetRetraceWidget::onReset()
{
    setState(Idle);
}

void ZOffsetRetraceWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->deviceName);
    QString text = QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4);

    if (m_state == WaitReference)
        m_refPositionLabel->setText(text);
    else if (m_state == WaitTarget)
        m_tgtPositionLabel->setText(text);
}

void ZOffsetRetraceWidget::updateHexPositionDisplay()
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

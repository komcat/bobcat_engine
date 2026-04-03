// ExposureAdjustWidget.cpp - Per-point camera exposure adjustment wizard
#include "ExposureAdjustWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "CameraManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <iostream>
#include <chrono>

ExposureAdjustWidget::ExposureAdjustWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
    , m_motionPollTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &ExposureAdjustWidget::updatePositionDisplay);
    connect(m_motionPollTimer, &QTimer::timeout, this, [this]() {
        if (m_motionFuture.valid() &&
            m_motionFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            m_motionPollTimer->stop();
            bool result = m_motionFuture.get();
            if (m_motionCallback) m_motionCallback(result);
        }
    });
}

void ExposureAdjustWidget::SetData(PlacementCorrectionSet* set)
{
    m_set = set;
    setStep(Idle);
}

void ExposureAdjustWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Adjust Exposure");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    m_stack = new QStackedWidget();

    QString greenBtnStyle =
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    QString blueBtnStyle =
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    QString orangeBtnStyle =
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "font-size: 13px; border-radius: 4px; padding: 6px 16px; }"
        "QPushButton:hover { background-color: #f39c12; }";

    QString redBtnStyle =
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }";

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    QString instructionStyle =
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;";

    // ========================================
    // Page 0: Idle
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_idleLabel = new QLabel(
        "Navigate to each taught point and adjust camera exposure.\n\n"
        "Only exposure values are saved — positions are not modified.\n\n"
        "Requires taught reference and target lines.");
    m_idleLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_idleLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleLabel);

    idleLayout->addStretch();

    m_startBtn = new QPushButton("Start Exposure Adjust");
    m_startBtn->setFixedHeight(44);
    m_startBtn->setStyleSheet(greenBtnStyle);
    connect(m_startBtn, &QPushButton::clicked, this, &ExposureAdjustWidget::onStart);
    idleLayout->addWidget(m_startBtn);

    m_stack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Active point (reused for all 4 points)
    // ========================================
    QWidget* activePage = new QWidget();
    QVBoxLayout* activeLayout = new QVBoxLayout(activePage);

    m_pointTitleLabel = new QLabel("Point 1/4: Reference P1");
    m_pointTitleLabel->setStyleSheet(instructionStyle);
    m_pointTitleLabel->setWordWrap(true);
    m_pointTitleLabel->setAlignment(Qt::AlignCenter);
    activeLayout->addWidget(m_pointTitleLabel);

    // Target position info
    m_targetPosLabel = new QLabel("Target: ---");
    m_targetPosLabel->setStyleSheet(
        "font-size: 12px; color: #aaa; font-family: monospace; padding: 5px;");
    activeLayout->addWidget(m_targetPosLabel);

    // Live position
    QGroupBox* posGroup = new QGroupBox("Current Gantry Position");
    posGroup->setStyleSheet(groupStyle);
    QVBoxLayout* posLayout = new QVBoxLayout(posGroup);
    m_positionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_positionLabel->setStyleSheet(
        "font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_positionLabel->setAlignment(Qt::AlignCenter);
    posLayout->addWidget(m_positionLabel);
    activeLayout->addWidget(posGroup);

    // Exposure control
    QGroupBox* expGroup = new QGroupBox("Exposure Setting");
    expGroup->setStyleSheet(groupStyle);
    QVBoxLayout* expLayout = new QVBoxLayout(expGroup);

    QHBoxLayout* expRow = new QHBoxLayout();
    m_exposureSpin = new QSpinBox();
    m_exposureSpin->setRange(1, 100000);
    m_exposureSpin->setValue(800);
    m_exposureSpin->setSuffix(" us");
    m_exposureSpin->setStyleSheet(
        "QSpinBox { background-color: #1a1f2e; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 6px; font-size: 14px; }"
        "QSpinBox::up-button, QSpinBox::down-button { background-color: #2d3e5f; border: none; width: 20px; }");
    expRow->addWidget(m_exposureSpin, 1);

    m_applyBtn = new QPushButton("Apply");
    m_applyBtn->setFixedHeight(36);
    m_applyBtn->setStyleSheet(orangeBtnStyle);
    connect(m_applyBtn, &QPushButton::clicked, this, &ExposureAdjustWidget::onApplyExposure);
    expRow->addWidget(m_applyBtn);

    expLayout->addLayout(expRow);

    m_applyStatusLabel = new QLabel("");
    m_applyStatusLabel->setStyleSheet("font-size: 11px; color: #888; padding: 2px;");
    expLayout->addWidget(m_applyStatusLabel);

    activeLayout->addWidget(expGroup);

    activeLayout->addStretch();

    // Buttons
    QHBoxLayout* btnRow = new QHBoxLayout();

    m_confirmBtn = new QPushButton("Confirm && Next");
    m_confirmBtn->setFixedHeight(44);
    m_confirmBtn->setStyleSheet(blueBtnStyle);
    connect(m_confirmBtn, &QPushButton::clicked, this, &ExposureAdjustWidget::onConfirmPoint);
    btnRow->addWidget(m_confirmBtn);

    m_skipBtn = new QPushButton("Skip");
    m_skipBtn->setFixedHeight(44);
    m_skipBtn->setStyleSheet(orangeBtnStyle);
    connect(m_skipBtn, &QPushButton::clicked, this, &ExposureAdjustWidget::onSkipPoint);
    btnRow->addWidget(m_skipBtn);

    activeLayout->addLayout(btnRow);

    m_endBtn = new QPushButton("End Adjust");
    m_endBtn->setFixedHeight(44);
    m_endBtn->setStyleSheet(redBtnStyle);
    connect(m_endBtn, &QPushButton::clicked, this, &ExposureAdjustWidget::onEnd);
    activeLayout->addWidget(m_endBtn);

    m_stack->addWidget(activePage);  // index 1

    mainLayout->addWidget(m_stack, 1);
}

void ExposureAdjustWidget::setStep(PointStep step)
{
    m_step = step;

    if (step == Idle) {
        m_stack->setCurrentIndex(0);
        m_positionTimer->stop();
        bool ready = m_set && m_set->referenceLine.valid && m_set->targetLine.valid
                     && !m_set->config.cameraId.empty();
        m_startBtn->setEnabled(ready);

        if (!ready && m_set) {
            QString reason;
            if (!m_set->referenceLine.valid || !m_set->targetLine.valid)
                reason = "Teach reference and target lines first.";
            else if (m_set->config.cameraId.empty())
                reason = "Select a camera in the Camera Exposure group.";
            m_idleLabel->setText(
                "Navigate to each taught point and adjust camera exposure.\n\n"
                "Only exposure values are saved — positions are not modified.\n\n"
                + reason);
        }
        return;
    }

    // Active point page
    m_stack->setCurrentIndex(1);
    m_positionTimer->start(100);
    m_applyStatusLabel->clear();

    // Load current exposure value into spinbox
    m_exposureSpin->setValue(static_cast<int>(currentExposureRef()));

    // Update title
    m_pointTitleLabel->setText(QString("Point %1/4: %2")
        .arg(stepNumber()).arg(stepLabel()));

    // Show target position
    double tx = 0, ty = 0, tz = 0;
    switch (step) {
    case RefP1: tx = m_set->referenceLine.p1x; ty = m_set->referenceLine.p1y; tz = m_set->referenceLine.p1z; break;
    case RefP2: tx = m_set->referenceLine.p2x; ty = m_set->referenceLine.p2y; tz = m_set->referenceLine.p2z; break;
    case TgtP1: tx = m_set->targetLine.p1x;    ty = m_set->targetLine.p1y;    tz = m_set->targetLine.p1z;    break;
    case TgtP2: tx = m_set->targetLine.p2x;    ty = m_set->targetLine.p2y;    tz = m_set->targetLine.p2z;    break;
    default: break;
    }
    m_targetPosLabel->setText(QString("Taught position: X=%1  Y=%2  Z=%3")
        .arg(tx, 0, 'f', 4).arg(ty, 0, 'f', 4).arg(tz, 0, 'f', 4));

    // Update confirm button text
    if (step == TgtP2) {
        m_confirmBtn->setText("Confirm && Finish");
    } else {
        m_confirmBtn->setText("Confirm && Next");
    }

    // Navigate to point
    navigateToPoint();
}

void ExposureAdjustWidget::runMotionAsync(std::function<bool()> motionFunc,
                                           std::function<void(bool)> onComplete)
{
    m_motionCallback = onComplete;
    m_motionFuture = std::async(std::launch::async, motionFunc);
    m_motionPollTimer->start(50);
}

void ExposureAdjustWidget::navigateToPoint()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    double tx = 0, ty = 0, tz = 0;
    switch (m_step) {
    case RefP1: tx = m_set->referenceLine.p1x; ty = m_set->referenceLine.p1y; tz = m_set->referenceLine.p1z; break;
    case RefP2: tx = m_set->referenceLine.p2x; ty = m_set->referenceLine.p2y; tz = m_set->referenceLine.p2z; break;
    case TgtP1: tx = m_set->targetLine.p1x;    ty = m_set->targetLine.p1y;    tz = m_set->targetLine.p1z;    break;
    case TgtP2: tx = m_set->targetLine.p2x;    ty = m_set->targetLine.p2y;    tz = m_set->targetLine.p2z;    break;
    default: return;
    }

    // Apply current exposure before moving
    applyExposureToCamera(currentExposureRef());

    m_confirmBtn->setEnabled(false);
    m_skipBtn->setEnabled(false);

    auto* mm = m_context->motionManager;
    std::string dev = m_set->measureDevice;
    int stepNum = stepNumber();
    QString stepLbl = stepLabel();

    runMotionAsync(
        [mm, dev, tx, ty, tz]() {
            bool ok = mm->MoveToPosition(dev, "X", tx, true);
            if (ok) ok = mm->MoveToPosition(dev, "Y", ty, true);
            if (ok) ok = mm->MoveToPosition(dev, "Z", tz, true);
            return ok;
        },
        [this, stepNum, stepLbl](bool ok) {
            m_confirmBtn->setEnabled(true);
            m_skipBtn->setEnabled(true);
            if (ok) {
                m_pointTitleLabel->setText(QString("Point %1/4: %2\nArrived. Adjust exposure and confirm.")
                    .arg(stepNum).arg(stepLbl));
            } else {
                m_pointTitleLabel->setText(QString("Point %1/4: %2\nAuto-navigate FAILED. Jog manually.")
                    .arg(stepNum).arg(stepLbl));
            }
            std::cout << "[ExposureAdjust] Navigated to " << stepLbl.toStdString() << std::endl;
        });
}

void ExposureAdjustWidget::applyExposureToCamera(double exposureUs)
{
    if (!m_context || !m_context->cameraManager || !m_set) return;
    if (m_set->config.cameraId.empty()) return;

    ICameraHardware::ExposureSettings expSettings;
    expSettings.auto_exposure = false;
    expSettings.auto_gain = false;
    expSettings.gain = 0.0;
    expSettings.exposure_time = exposureUs;

    bool ok = m_context->cameraManager->ApplyExposureSettings(
        m_set->config.cameraId, expSettings);

    std::cout << "[ExposureAdjust] Camera \"" << m_set->config.cameraId
              << "\" exposure=" << exposureUs << "us "
              << (ok ? "OK" : "FAILED") << std::endl;
}

double& ExposureAdjustWidget::currentExposureRef()
{
    // Return reference to the exposure field for the current step
    static double dummy = 800.0;
    if (!m_set) return dummy;

    switch (m_step) {
    case RefP1: return m_set->referenceLine.p1Exposure;
    case RefP2: return m_set->referenceLine.p2Exposure;
    case TgtP1: return m_set->targetLine.p1Exposure;
    case TgtP2: return m_set->targetLine.p2Exposure;
    default:    return dummy;
    }
}

QString ExposureAdjustWidget::stepLabel() const
{
    switch (m_step) {
    case RefP1: return "Reference P1";
    case RefP2: return "Reference P2";
    case TgtP1: return "Target P1";
    case TgtP2: return "Target P2";
    default:    return "---";
    }
}

int ExposureAdjustWidget::stepNumber() const
{
    switch (m_step) {
    case RefP1: return 1;
    case RefP2: return 2;
    case TgtP1: return 3;
    case TgtP2: return 4;
    default:    return 0;
    }
}

void ExposureAdjustWidget::onStart()
{
    if (!m_set) return;
    setStep(RefP1);
}

void ExposureAdjustWidget::onApplyExposure()
{
    double val = static_cast<double>(m_exposureSpin->value());
    applyExposureToCamera(val);
    m_applyStatusLabel->setText(QString("Applied %1 us to \"%2\"")
        .arg(m_exposureSpin->value())
        .arg(QString::fromStdString(m_set ? m_set->config.cameraId : "")));
    m_applyStatusLabel->setStyleSheet("font-size: 11px; color: #00ff88; padding: 2px;");
}

void ExposureAdjustWidget::onConfirmPoint()
{
    if (!m_set) return;

    // Save spinbox value to the current point's exposure
    currentExposureRef() = static_cast<double>(m_exposureSpin->value());

    std::cout << "[ExposureAdjust] Confirmed " << stepLabel().toStdString()
              << " exposure=" << m_exposureSpin->value() << "us" << std::endl;

    // Advance to next point or finish
    switch (m_step) {
    case RefP1: setStep(RefP2); break;
    case RefP2: setStep(TgtP1); break;
    case TgtP1: setStep(TgtP2); break;
    case TgtP2:
        m_positionTimer->stop();
        emit exposureSaved(*m_set);
        setStep(Idle);
        break;
    default: break;
    }
}

void ExposureAdjustWidget::onSkipPoint()
{
    // Advance without saving exposure change
    switch (m_step) {
    case RefP1: setStep(RefP2); break;
    case RefP2: setStep(TgtP1); break;
    case TgtP1: setStep(TgtP2); break;
    case TgtP2:
        m_positionTimer->stop();
        setStep(Idle);
        break;
    default: break;
    }
}

void ExposureAdjustWidget::onEnd()
{
    m_positionTimer->stop();
    setStep(Idle);
    emit adjustEnded();
}

void ExposureAdjustWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    m_positionLabel->setText(QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4));
}

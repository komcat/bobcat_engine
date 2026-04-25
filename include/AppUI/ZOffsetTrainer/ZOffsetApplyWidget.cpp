// ZOffsetApplyWidget.cpp - Apply Z offset correction button + live hex readout
#include "ZOffsetApplyWidget.h"
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

ZOffsetApplyWidget::ZOffsetApplyWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &ZOffsetApplyWidget::updateHexPosition);
}

void ZOffsetApplyWidget::SetCorrection(const ZOffsetCorrectionConfig& cfg)
{
    m_correction = cfg;

    if (cfg.targetDevice.empty()) {
        m_summaryLabel->setText("No correction configured.\nPlease select target device on the previous page.");
        m_applyBtn->setEnabled(false);
        m_positionTimer->stop();
        return;
    }

    double corrUm = cfg.appliedValueMm * 1000.0;
    m_summaryLabel->setText(QString(
        "Target:     %1 / Z axis\n"
        "Correction: %2 mm (%3 %4m)%5")
        .arg(QString::fromStdString(cfg.targetDevice))
        .arg(cfg.appliedValueMm, 0, 'f', 4)
        .arg(corrUm, 0, 'f', 1)
        .arg(QChar(0x00B5))
        .arg(cfg.reverseSign ? "  (sign reversed)" : ""));

    m_applyBtn->setEnabled(!cfg.applied);
    if (cfg.applied) {
        double appliedUm = cfg.appliedValueMm * 1000.0;
        m_resultLabel->setText(QString("Already applied: %1 mm (%2 %3m) to %4 Z")
            .arg(cfg.appliedValueMm, 0, 'f', 4)
            .arg(appliedUm, 0, 'f', 1)
            .arg(QChar(0x00B5))
            .arg(QString::fromStdString(cfg.targetDevice)));
        m_resultLabel->setStyleSheet("font-size: 13px; color: #f0c040; padding: 10px;");
    } else {
        m_resultLabel->setText("");
    }

    m_positionTimer->start(100);
}

void ZOffsetApplyWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Apply Z Offset Correction");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    // Summary section
    QGroupBox* summaryGroup = new QGroupBox("Correction Summary");
    summaryGroup->setStyleSheet(groupStyle);
    QVBoxLayout* summaryLayout = new QVBoxLayout(summaryGroup);

    m_summaryLabel = new QLabel("No correction configured.");
    m_summaryLabel->setStyleSheet("font-size: 14px; color: #ffffff; font-family: monospace; padding: 10px;");
    m_summaryLabel->setWordWrap(true);
    summaryLayout->addWidget(m_summaryLabel);
    mainLayout->addWidget(summaryGroup);

    // Live hex position display
    QGroupBox* posGroup = new QGroupBox("Live Hex Position");
    posGroup->setStyleSheet(groupStyle);
    QVBoxLayout* posLayout = new QVBoxLayout(posGroup);

    m_hexPositionLabel = new QLabel("X: ---  Y: ---  Z: ---\nU: ---  V: ---  W: ---");
    m_hexPositionLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-family: monospace; padding: 10px;");
    posLayout->addWidget(m_hexPositionLabel);
    mainLayout->addWidget(posGroup);

    // Apply button
    m_applyBtn = new QPushButton("Apply Z Correction");
    m_applyBtn->setFixedHeight(48);
    m_applyBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 16px; border-radius: 4px; padding: 10px 30px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a5c35; color: #888; }"
    );
    m_applyBtn->setEnabled(false);
    connect(m_applyBtn, &QPushButton::clicked, this, &ZOffsetApplyWidget::onApply);
    mainLayout->addWidget(m_applyBtn);

    // Result label
    m_resultLabel = new QLabel();
    m_resultLabel->setStyleSheet("font-size: 13px; color: #aaa; padding: 10px;");
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_resultLabel);

    mainLayout->addStretch();
}

void ZOffsetApplyWidget::onApply()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    if (m_correction.targetDevice.empty()) {
        QMessageBox::warning(this, "Error", "No correction configured.");
        return;
    }

    // Capture position before
    auto beforePos = m_context->motionManager->GetCurrentPosition(m_correction.targetDevice);

    std::cout << "[ZOffsetApply] Global Z correction: " << m_correction.appliedValueMm
              << " mm -> " << m_correction.targetDevice << std::endl;

    // Transform global Z correction to local hex axes
    TransformationMatrixLoader matrixLoader;
    bool matrixLoaded = matrixLoader.LoadFromFile("config/transformation_matrix.json");
    if (!matrixLoaded) {
        QMessageBox::warning(this, "Error", "Could not load transformation_matrix.json");
        return;
    }

    const auto* matrix = matrixLoader.GetMatrix(m_correction.targetDevice);
    if (!matrix) {
        QMessageBox::warning(this, "Error",
            QString("No transformation matrix found for device: %1")
                .arg(QString::fromStdString(m_correction.targetDevice)));
        return;
    }

    CoordinateTransformer::Vector6D zGlobalDelta(0.0, 0.0, m_correction.appliedValueMm, 0.0, 0.0, 0.0);
    auto zLocalDelta = CoordinateTransformer::TransformGlobalToLocal(zGlobalDelta, *matrix);

    std::cout << "[ZOffsetApply] Local delta: dX=" << zLocalDelta.x
              << " dY=" << zLocalDelta.y << " dZ=" << zLocalDelta.z << std::endl;

    // Apply via MoveToAbs6 (adds local deltas to current position)
    auto curPos = m_context->motionManager->GetCurrentPosition(m_correction.targetDevice);
    PositionStruct targetPos;
    targetPos.x = curPos.x + zLocalDelta.x;
    targetPos.y = curPos.y + zLocalDelta.y;
    targetPos.z = curPos.z + zLocalDelta.z;
    targetPos.u = curPos.u;
    targetPos.v = curPos.v;
    targetPos.w = curPos.w;

    bool ok = m_context->motionManager->MoveToAbs6(m_correction.targetDevice, targetPos, true);

    if (ok) {
        auto afterPos = m_context->motionManager->GetCurrentPosition(m_correction.targetDevice);

        m_correction.applied = true;
        m_applyBtn->setEnabled(false);

        double corrUm = m_correction.appliedValueMm * 1000.0;
        m_resultLabel->setText(QString(
            "Applied %1 mm (%2 %3m) to %4 Z\n\n"
            "Before: Z=%5\n"
            "After:  Z=%6")
            .arg(m_correction.appliedValueMm, 0, 'f', 4)
            .arg(corrUm, 0, 'f', 1)
            .arg(QChar(0x00B5))
            .arg(QString::fromStdString(m_correction.targetDevice))
            .arg(beforePos.z, 0, 'f', 4)
            .arg(afterPos.z, 0, 'f', 4));
        m_resultLabel->setStyleSheet("font-size: 13px; color: #27ae60; padding: 10px; font-family: monospace;");

        std::cout << "[ZOffsetApply] Success. Before Z=" << beforePos.z
                  << " After Z=" << afterPos.z << std::endl;

        emit correctionApplied(m_correction.appliedValueMm);
    } else {
        m_resultLabel->setText("Failed to apply correction. Check device connection.");
        m_resultLabel->setStyleSheet("font-size: 13px; color: #e74c3c; padding: 10px;");
        std::cout << "[ZOffsetApply] MoveToAbs6 failed!" << std::endl;
    }
}

void ZOffsetApplyWidget::updateHexPosition()
{
    if (!m_context || !m_context->motionManager || m_correction.targetDevice.empty()) {
        m_hexPositionLabel->setText("X: ---  Y: ---  Z: ---\nU: ---  V: ---  W: ---");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_correction.targetDevice);

    m_hexPositionLabel->setText(QString(
        "Current %1 position:\n"
        "  X: %2   Y: %3   Z: %4\n"
        "  U: %5   V: %6   W: %7")
        .arg(QString::fromStdString(m_correction.targetDevice))
        .arg(pos.x, 9, 'f', 4).arg(pos.y, 9, 'f', 4).arg(pos.z, 9, 'f', 4)
        .arg(pos.u, 9, 'f', 4).arg(pos.v, 9, 'f', 4).arg(pos.w, 9, 'f', 4));
}

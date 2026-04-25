// YawApplyCorrectionWidget.cpp - Apply yaw correction button + live hex readout
#include "YawApplyCorrectionWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>

YawApplyCorrectionWidget::YawApplyCorrectionWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &YawApplyCorrectionWidget::updateHexPosition);
}

void YawApplyCorrectionWidget::SetCorrection(const YawCorrectionConfig& cfg)
{
    m_correction = cfg;

    if (cfg.targetDevice.empty() || cfg.targetAxis.empty()) {
        m_summaryLabel->setText("No correction configured.\nPlease select device/axis on the previous page.");
        m_applyBtn->setEnabled(false);
        m_positionTimer->stop();
        return;
    }

    m_summaryLabel->setText(QString(
        "Target:     %1 / %2 axis\n"
        "Correction: %3%4%5")
        .arg(QString::fromStdString(cfg.targetDevice))
        .arg(QString::fromStdString(cfg.targetAxis))
        .arg(cfg.appliedValue, 0, 'f', 3)
        .arg(QChar(0x00B0))
        .arg(cfg.reverseSign ? "  (sign reversed)" : ""));

    m_applyBtn->setEnabled(!cfg.applied);
    if (cfg.applied) {
        m_resultLabel->setText(QString("Already applied: %1%2 to %3 %4")
            .arg(cfg.appliedValue, 0, 'f', 3)
            .arg(QChar(0x00B0))
            .arg(QString::fromStdString(cfg.targetDevice))
            .arg(QString::fromStdString(cfg.targetAxis)));
        m_resultLabel->setStyleSheet("font-size: 13px; color: #f0c040; padding: 10px;");
    } else {
        m_resultLabel->setText("");
    }

    m_positionTimer->start(100);
}

void YawApplyCorrectionWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Apply Yaw Correction");
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
    m_applyBtn = new QPushButton("Apply Correction");
    m_applyBtn->setFixedHeight(48);
    m_applyBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 16px; border-radius: 4px; padding: 10px 30px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a5c35; color: #888; }");
    m_applyBtn->setEnabled(false);
    connect(m_applyBtn, &QPushButton::clicked, this, &YawApplyCorrectionWidget::onApply);
    mainLayout->addWidget(m_applyBtn);

    // Result label
    m_resultLabel = new QLabel();
    m_resultLabel->setStyleSheet("font-size: 13px; color: #aaa; padding: 10px;");
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_resultLabel);

    mainLayout->addStretch();
}

void YawApplyCorrectionWidget::onApply()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    if (m_correction.targetDevice.empty() || m_correction.targetAxis.empty()) {
        QMessageBox::warning(this, "Error", "No correction configured.");
        return;
    }

    auto beforePos = m_context->motionManager->GetCurrentPosition(m_correction.targetDevice);

    std::cout << "[YawApplyCorrection] MoveRelative: " << m_correction.targetDevice
              << " axis=" << m_correction.targetAxis
              << " value=" << m_correction.appliedValue << " deg" << std::endl;

    bool ok = m_context->motionManager->MoveRelative(
        m_correction.targetDevice,
        m_correction.targetAxis,
        m_correction.appliedValue,
        true);  // blocking

    if (ok) {
        auto afterPos = m_context->motionManager->GetCurrentPosition(m_correction.targetDevice);

        m_correction.applied = true;
        m_applyBtn->setEnabled(false);

        m_resultLabel->setText(QString(
            "Applied %1%2 to %3 %4\n\n"
            "Before: U=%5  V=%6  W=%7\n"
            "After:  U=%8  V=%9  W=%10")
            .arg(m_correction.appliedValue, 0, 'f', 3)
            .arg(QChar(0x00B0))
            .arg(QString::fromStdString(m_correction.targetDevice))
            .arg(QString::fromStdString(m_correction.targetAxis))
            .arg(beforePos.u, 0, 'f', 4).arg(beforePos.v, 0, 'f', 4).arg(beforePos.w, 0, 'f', 4)
            .arg(afterPos.u, 0, 'f', 4).arg(afterPos.v, 0, 'f', 4).arg(afterPos.w, 0, 'f', 4));
        m_resultLabel->setStyleSheet("font-size: 13px; color: #27ae60; padding: 10px; font-family: monospace;");

        std::cout << "[YawApplyCorrection] Success." << std::endl;

        emit correctionApplied(m_correction.appliedValue);
    } else {
        m_resultLabel->setText("Failed to apply correction. Check device connection and axis.");
        m_resultLabel->setStyleSheet("font-size: 13px; color: #e74c3c; padding: 10px;");
        std::cout << "[YawApplyCorrection] MoveRelative failed!" << std::endl;
    }
}

void YawApplyCorrectionWidget::updateHexPosition()
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

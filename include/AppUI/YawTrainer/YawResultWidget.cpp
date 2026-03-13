// YawResultWidget.cpp - Display ref/target angles, delta, and device/axis selection
#include "YawResultWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <iostream>

YawResultWidget::YawResultWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    setupUI();
}

void YawResultWidget::SetData(const YawTrainerSet& set)
{
    m_set = set;

    // Update line summary
    if (set.referenceLine.valid && set.targetLine.valid) {
        double refLen = YawTrainerMath::LineLength(
            set.referenceLine.p1x, set.referenceLine.p1y,
            set.referenceLine.p2x, set.referenceLine.p2y);
        double tgtLen = YawTrainerMath::LineLength(
            set.targetLine.p1x, set.targetLine.p1y,
            set.targetLine.p2x, set.targetLine.p2y);
        m_lineSummaryLabel->setText(QString(
            "Reference:  P1(%1, %2) -> P2(%3, %4)  len=%5 mm\n"
            "Target:     P1(%6, %7) -> P2(%8, %9)  len=%10 mm")
            .arg(set.referenceLine.p1x, 0, 'f', 4).arg(set.referenceLine.p1y, 0, 'f', 4)
            .arg(set.referenceLine.p2x, 0, 'f', 4).arg(set.referenceLine.p2y, 0, 'f', 4)
            .arg(refLen, 0, 'f', 3)
            .arg(set.targetLine.p1x, 0, 'f', 4).arg(set.targetLine.p1y, 0, 'f', 4)
            .arg(set.targetLine.p2x, 0, 'f', 4).arg(set.targetLine.p2y, 0, 'f', 4)
            .arg(tgtLen, 0, 'f', 3));
    } else {
        m_lineSummaryLabel->setText("Lines not yet captured.");
    }

    // Update angle labels
    if (set.referenceLine.valid) {
        m_refAngleLabel->setText(QString("Reference Angle: %1%2")
            .arg(set.referenceLine.angleDeg, 0, 'f', 3)
            .arg(QChar(0x00B0)));
        m_refAngleLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-weight: bold; padding: 5px;");
    } else {
        m_refAngleLabel->setText("Reference Angle: ---");
        m_refAngleLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    }

    if (set.targetLine.valid) {
        m_tgtAngleLabel->setText(QString("Target Angle: %1%2")
            .arg(set.targetLine.angleDeg, 0, 'f', 3)
            .arg(QChar(0x00B0)));
        m_tgtAngleLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-weight: bold; padding: 5px;");
    } else {
        m_tgtAngleLabel->setText("Target Angle: ---");
        m_tgtAngleLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    }

    if (set.deltaValid) {
        m_deltaAngleLabel->setText(QString("Delta (Target - Reference): %1%2")
            .arg(set.deltaAngleDeg, 0, 'f', 3)
            .arg(QChar(0x00B0)));
        m_deltaAngleLabel->setStyleSheet(
            "font-size: 16px; color: #f0c040; font-weight: bold; padding: 10px; "
            "background-color: #2d3e5f; border-radius: 6px;");
    } else {
        m_deltaAngleLabel->setText("Delta: --- (both lines required)");
        m_deltaAngleLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    }

    // Restore target device/axis/sign
    if (!set.correction.targetDevice.empty()) {
        int idx = m_targetDeviceCombo->findText(QString::fromStdString(set.correction.targetDevice));
        if (idx >= 0) m_targetDeviceCombo->setCurrentIndex(idx);
    }
    if (!set.correction.targetAxis.empty()) {
        int idx = m_targetAxisCombo->findText(QString::fromStdString(set.correction.targetAxis));
        if (idx >= 0) m_targetAxisCombo->setCurrentIndex(idx);
    }
    m_reverseSignCheck->setChecked(set.correction.reverseSign);

    // Auto-suggest W axis for yaw if not already configured
    if (set.correction.targetAxis.empty()) {
        m_targetAxisCombo->setCurrentText("W");
    }

    updatePreview();
}

void YawResultWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Yaw Result & Correction Selection");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    // ========================================
    // Section A: Computed Angles
    // ========================================
    QGroupBox* anglesGroup = new QGroupBox("Computed Angles");
    anglesGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* anglesLayout = new QVBoxLayout(anglesGroup);

    m_lineSummaryLabel = new QLabel("Lines not yet captured.");
    m_lineSummaryLabel->setStyleSheet("font-size: 12px; color: #ccc; font-family: monospace; padding: 5px;");
    anglesLayout->addWidget(m_lineSummaryLabel);

    m_refAngleLabel = new QLabel("Reference Angle: ---");
    m_refAngleLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    anglesLayout->addWidget(m_refAngleLabel);

    m_tgtAngleLabel = new QLabel("Target Angle: ---");
    m_tgtAngleLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    anglesLayout->addWidget(m_tgtAngleLabel);

    m_deltaAngleLabel = new QLabel("Delta: ---");
    m_deltaAngleLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    anglesLayout->addWidget(m_deltaAngleLabel);

    mainLayout->addWidget(anglesGroup);

    // ========================================
    // Section B: Correction Selection
    // ========================================
    QGroupBox* selGroup = new QGroupBox("Correction Selection");
    selGroup->setStyleSheet(anglesGroup->styleSheet());
    QFormLayout* selLayout = new QFormLayout(selGroup);

    QString comboStyle =
        "QComboBox { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #2d3e5f; color: white; "
        "selection-background-color: #3d5a8c; }";

    m_targetDeviceCombo = new QComboBox();
    m_targetDeviceCombo->setStyleSheet(comboStyle);
    populateHexDevices();
    selLayout->addRow("Target device:", m_targetDeviceCombo);

    m_targetAxisCombo = new QComboBox();
    m_targetAxisCombo->setStyleSheet(comboStyle);
    m_targetAxisCombo->addItems({"U", "V", "W"});
    m_targetAxisCombo->setCurrentText("W");
    selLayout->addRow("Target axis:", m_targetAxisCombo);

    m_reverseSignCheck = new QCheckBox("Reverse sign");
    m_reverseSignCheck->setStyleSheet("color: #ccc; font-size: 12px;");
    selLayout->addRow("", m_reverseSignCheck);

    mainLayout->addWidget(selGroup);

    // Preview line
    m_previewLabel = new QLabel("Correction: ---");
    m_previewLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 10px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_previewLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_previewLabel);

    mainLayout->addStretch();

    // Connect signals for live preview update
    connect(m_targetDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &YawResultWidget::updatePreview);
    connect(m_targetAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &YawResultWidget::updatePreview);
    connect(m_reverseSignCheck, &QCheckBox::toggled,
            this, &YawResultWidget::updatePreview);
}

void YawResultWidget::populateHexDevices()
{
    m_targetDeviceCombo->clear();

    if (m_context && m_context->motionManager) {
        auto devices = m_context->motionManager->GetAllDeviceNames();
        for (const auto& dev : devices) {
            if (dev.find("hex") != std::string::npos) {
                m_targetDeviceCombo->addItem(QString::fromStdString(dev));
            }
        }
    }

    // Fallback if none found
    if (m_targetDeviceCombo->count() == 0) {
        m_targetDeviceCombo->addItem("hex-left");
        m_targetDeviceCombo->addItem("hex-right");
    }
}

void YawResultWidget::updatePreview()
{
    QString targetDevice = m_targetDeviceCombo->currentText();
    QString targetAxis = m_targetAxisCombo->currentText();
    bool reverseSign = m_reverseSignCheck->isChecked();

    if (!m_set.deltaValid) {
        m_previewLabel->setText("Correction: --- (both lines required)");
        return;
    }

    double correction = YawTrainerMath::GetCorrectionValue(m_set.deltaAngleDeg, reverseSign);

    m_previewLabel->setText(QString("Correction: %1 %2 = %3%4")
        .arg(targetDevice)
        .arg(targetAxis)
        .arg(correction, 0, 'f', 3)
        .arg(QChar(0x00B0)));

    // Build and emit YawCorrectionConfig
    YawCorrectionConfig cfg;
    cfg.targetDevice = targetDevice.toStdString();
    cfg.targetAxis = targetAxis.toStdString();
    cfg.reverseSign = reverseSign;
    cfg.applied = false;
    cfg.appliedValue = correction;

    emit correctionConfigured(cfg);
}

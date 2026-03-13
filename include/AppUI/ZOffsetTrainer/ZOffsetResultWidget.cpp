// ZOffsetResultWidget.cpp - Z difference display + target device/Z diff selection
#include "ZOffsetResultWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <iostream>

ZOffsetResultWidget::ZOffsetResultWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    setupUI();
}

void ZOffsetResultWidget::SetData(const ZOffsetSet& set)
{
    m_set = set;

    // Update point summary
    if (set.reference.valid && set.target.valid) {
        m_pointSummaryLabel->setText(QString(
            "Reference Surface (P1):  Z = %1 mm  (X=%2, Y=%3)\n"
            "Target Surface (P2):     Z = %4 mm  (X=%5, Y=%6)")
            .arg(set.reference.z, 0, 'f', 4).arg(set.reference.x, 0, 'f', 4).arg(set.reference.y, 0, 'f', 4)
            .arg(set.target.z, 0, 'f', 4).arg(set.target.x, 0, 'f', 4).arg(set.target.y, 0, 'f', 4));
    } else {
        m_pointSummaryLabel->setText("Points not yet captured.");
    }

    // Update Z difference display
    if (set.zDiffValid) {
        double zDiffUm = set.measuredZDiffMm * 1000.0;
        m_zDiffLabel->setText(QString("Measured Z Difference: %1 mm")
            .arg(set.measuredZDiffMm, 0, 'f', 4));
        m_zDiffLabel->setStyleSheet("font-size: 16px; color: #00ff88; font-weight: bold; padding: 5px;");
        m_zDiffUmLabel->setText(QString("= %1 %2m")
            .arg(zDiffUm, 0, 'f', 1)
            .arg(QChar(0x00B5)));  // mu sign
        m_zDiffUmLabel->setStyleSheet("font-size: 14px; color: #00cc66; padding: 5px;");
    } else {
        m_zDiffLabel->setText("Measured Z Difference: ---");
        m_zDiffLabel->setStyleSheet("font-size: 16px; color: #888; padding: 5px;");
        m_zDiffUmLabel->setText("");
    }

    // Restore target device
    if (!set.correction.targetDevice.empty()) {
        int idx = m_targetDeviceCombo->findText(QString::fromStdString(set.correction.targetDevice));
        if (idx >= 0) m_targetDeviceCombo->setCurrentIndex(idx);
    }

    // Restore target Z diff
    m_targetZDiffSpinBox->blockSignals(true);
    m_targetZDiffSpinBox->setValue(set.correction.targetZDiffUm);
    m_targetZDiffSpinBox->blockSignals(false);

    // Restore reverse sign
    m_reverseSignCheck->setChecked(set.correction.reverseSign);

    updatePreview();
}

void ZOffsetResultWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Z Difference & Correction Config");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    // ========================================
    // Section A: Z Difference
    // ========================================
    QGroupBox* zDiffGroup = new QGroupBox("Measured Z Difference");
    zDiffGroup->setStyleSheet(groupStyle);
    QVBoxLayout* zDiffLayout = new QVBoxLayout(zDiffGroup);

    m_pointSummaryLabel = new QLabel("Points not yet captured.");
    m_pointSummaryLabel->setStyleSheet("font-size: 12px; color: #ccc; font-family: monospace; padding: 5px;");
    zDiffLayout->addWidget(m_pointSummaryLabel);

    m_zDiffLabel = new QLabel("Measured Z Difference: ---");
    m_zDiffLabel->setStyleSheet("font-size: 16px; color: #888; padding: 5px;");
    zDiffLayout->addWidget(m_zDiffLabel);

    m_zDiffUmLabel = new QLabel("");
    m_zDiffUmLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    zDiffLayout->addWidget(m_zDiffUmLabel);

    mainLayout->addWidget(zDiffGroup);

    // ========================================
    // Section B: Correction Selection
    // ========================================
    QGroupBox* selGroup = new QGroupBox("Correction Config");
    selGroup->setStyleSheet(groupStyle);
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

    m_targetZDiffSpinBox = new QDoubleSpinBox();
    m_targetZDiffSpinBox->setRange(-10000.0, 10000.0);
    m_targetZDiffSpinBox->setDecimals(1);
    m_targetZDiffSpinBox->setValue(150.0);
    m_targetZDiffSpinBox->setSuffix(QString(" %1m").arg(QChar(0x00B5)));
    m_targetZDiffSpinBox->setStyleSheet(
        "QDoubleSpinBox { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px 8px; font-size: 13px; }");
    selLayout->addRow("Target Z Difference:", m_targetZDiffSpinBox);

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
            this, &ZOffsetResultWidget::updatePreview);
    connect(m_targetZDiffSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ZOffsetResultWidget::updatePreview);
    connect(m_reverseSignCheck, &QCheckBox::toggled,
            this, &ZOffsetResultWidget::updatePreview);
}

void ZOffsetResultWidget::populateHexDevices()
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

void ZOffsetResultWidget::updatePreview()
{
    QString targetDevice = m_targetDeviceCombo->currentText();
    double targetZDiffUm = m_targetZDiffSpinBox->value();
    bool reverseSign = m_reverseSignCheck->isChecked();

    if (!m_set.zDiffValid) {
        m_previewLabel->setText("Correction: --- (capture both points first)");
        return;
    }

    double correctionMm = ZOffsetMath::ComputeCorrection(
        m_set.measuredZDiffMm, targetZDiffUm, reverseSign);
    double correctionUm = correctionMm * 1000.0;

    m_previewLabel->setText(QString("Correction: %1 Z = %2 mm (%3 %4m)")
        .arg(targetDevice)
        .arg(correctionMm, 0, 'f', 4)
        .arg(correctionUm, 0, 'f', 1)
        .arg(QChar(0x00B5)));

    // Build and emit config
    ZOffsetCorrectionConfig cfg;
    cfg.targetDevice = targetDevice.toStdString();
    cfg.targetZDiffUm = targetZDiffUm;
    cfg.reverseSign = reverseSign;
    cfg.applied = false;
    cfg.appliedValueMm = correctionMm;

    emit correctionConfigured(cfg);
}

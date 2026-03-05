// TiltResultWidget.cpp - Angle display + device/axis selection for correction
#include "TiltResultWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <iostream>

TiltResultWidget::TiltResultWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    setupUI();
}

void TiltResultWidget::SetData(const PlanarAdjustmentSet& set)
{
    m_set = set;

    // Update point summary
    if (set.point1.valid && set.point2.valid) {
        m_pointSummaryLabel->setText(QString(
            "Point 1:  X=%1  Y=%2  Z=%3\n"
            "Point 2:  X=%4  Y=%5  Z=%6")
            .arg(set.point1.x, 0, 'f', 4).arg(set.point1.y, 0, 'f', 4).arg(set.point1.z, 0, 'f', 4)
            .arg(set.point2.x, 0, 'f', 4).arg(set.point2.y, 0, 'f', 4).arg(set.point2.z, 0, 'f', 4));

        double sep = PlanarAdjustmentMath::Separation(set.point1, set.point2);
        m_deltaLabel->setText(QString(
            "dX=%1  dY=%2  dZ=%3\n"
            "Separation: %4 mm")
            .arg(set.deltaX, 0, 'f', 4).arg(set.deltaY, 0, 'f', 4).arg(set.deltaZ, 0, 'f', 4)
            .arg(sep, 0, 'f', 3));
    } else {
        m_pointSummaryLabel->setText("Points not yet captured.");
        m_deltaLabel->setText("");
    }

    // Update angle labels
    if (set.angleXValid) {
        m_angleXLabel->setText(QString("Angle X (YZ tilt): %1%2")
            .arg(set.angleX_deg, 0, 'f', 3)
            .arg(QChar(0x00B0)));  // degree sign
        m_angleXLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-weight: bold; padding: 5px;");
    } else {
        m_angleXLabel->setText(QString("Angle X (YZ tilt): %1  [N/A: dY %2 0]")
            .arg(QChar(0x2014))  // em dash
            .arg(QChar(0x2248)));  // approx equal
        m_angleXLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    }

    if (set.angleYValid) {
        m_angleYLabel->setText(QString("Angle Y (XZ tilt): %1%2")
            .arg(set.angleY_deg, 0, 'f', 3)
            .arg(QChar(0x00B0)));
        m_angleYLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-weight: bold; padding: 5px;");
    } else {
        m_angleYLabel->setText(QString("Angle Y (XZ tilt): %1  [N/A: dX %2 0]")
            .arg(QChar(0x2014))
            .arg(QChar(0x2248)));
        m_angleYLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    }

    // Update source angle combo
    m_sourceAngleCombo->blockSignals(true);
    m_sourceAngleCombo->clear();
    if (set.angleXValid) {
        m_sourceAngleCombo->addItem(
            QString("Angle X (%1%2)").arg(set.angleX_deg, 0, 'f', 3).arg(QChar(0x00B0)),
            "angle_x");
    }
    if (set.angleYValid) {
        m_sourceAngleCombo->addItem(
            QString("Angle Y (%1%2)").arg(set.angleY_deg, 0, 'f', 3).arg(QChar(0x00B0)),
            "angle_y");
    }
    if (m_sourceAngleCombo->count() == 0) {
        m_sourceAngleCombo->addItem("No valid angles", "");
    }

    // Restore previous selection if possible
    if (!set.correction.sourceAngle.empty()) {
        for (int i = 0; i < m_sourceAngleCombo->count(); ++i) {
            if (m_sourceAngleCombo->itemData(i).toString().toStdString() == set.correction.sourceAngle) {
                m_sourceAngleCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    m_sourceAngleCombo->blockSignals(false);

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

    // Auto-suggest axis: Angle X -> U, Angle Y -> V
    if (set.correction.targetAxis.empty()) {
        QString sourceAngle = m_sourceAngleCombo->currentData().toString();
        if (sourceAngle == "angle_x") {
            m_targetAxisCombo->setCurrentText("U");
        } else if (sourceAngle == "angle_y") {
            m_targetAxisCombo->setCurrentText("V");
        }
    }

    updatePreview();
}

void TiltResultWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Tilt Result & Correction Selection");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    // ========================================
    // Section A: Computed Angles
    // ========================================
    QGroupBox* anglesGroup = new QGroupBox("Computed Angles");
    anglesGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QVBoxLayout* anglesLayout = new QVBoxLayout(anglesGroup);

    m_pointSummaryLabel = new QLabel("Points not yet captured.");
    m_pointSummaryLabel->setStyleSheet("font-size: 12px; color: #ccc; font-family: monospace; padding: 5px;");
    anglesLayout->addWidget(m_pointSummaryLabel);

    m_deltaLabel = new QLabel();
    m_deltaLabel->setStyleSheet("font-size: 12px; color: #aaa; font-family: monospace; padding: 5px;");
    anglesLayout->addWidget(m_deltaLabel);

    m_angleXLabel = new QLabel("Angle X (YZ tilt): ---");
    m_angleXLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    anglesLayout->addWidget(m_angleXLabel);

    m_angleYLabel = new QLabel("Angle Y (XZ tilt): ---");
    m_angleYLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    anglesLayout->addWidget(m_angleYLabel);

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

    m_sourceAngleCombo = new QComboBox();
    m_sourceAngleCombo->setStyleSheet(comboStyle);
    selLayout->addRow("Source angle:", m_sourceAngleCombo);

    m_targetDeviceCombo = new QComboBox();
    m_targetDeviceCombo->setStyleSheet(comboStyle);
    populateHexDevices();
    selLayout->addRow("Target device:", m_targetDeviceCombo);

    m_targetAxisCombo = new QComboBox();
    m_targetAxisCombo->setStyleSheet(comboStyle);
    m_targetAxisCombo->addItems({"U", "V", "W"});
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
    connect(m_sourceAngleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TiltResultWidget::updatePreview);
    connect(m_targetDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TiltResultWidget::updatePreview);
    connect(m_targetAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TiltResultWidget::updatePreview);
    connect(m_reverseSignCheck, &QCheckBox::toggled,
            this, &TiltResultWidget::updatePreview);
}

void TiltResultWidget::populateHexDevices()
{
    m_targetDeviceCombo->clear();

    if (m_context && m_context->motionManager) {
        auto devices = m_context->motionManager->GetAllDeviceNames();
        for (const auto& dev : devices) {
            // Filter to hex devices
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

void TiltResultWidget::updatePreview()
{
    QString sourceAngle = m_sourceAngleCombo->currentData().toString();
    QString targetDevice = m_targetDeviceCombo->currentText();
    QString targetAxis = m_targetAxisCombo->currentText();
    bool reverseSign = m_reverseSignCheck->isChecked();

    double angleDeg = 0.0;
    if (sourceAngle == "angle_x" && m_set.angleXValid) {
        angleDeg = m_set.angleX_deg;
    } else if (sourceAngle == "angle_y" && m_set.angleYValid) {
        angleDeg = m_set.angleY_deg;
    } else {
        m_previewLabel->setText("Correction: --- (no valid angle selected)");
        return;
    }

    double correction = PlanarAdjustmentMath::GetCorrectionValue(angleDeg, reverseSign);

    m_previewLabel->setText(QString("Correction: %1 %2 = %3%4")
        .arg(targetDevice)
        .arg(targetAxis)
        .arg(correction, 0, 'f', 3)
        .arg(QChar(0x00B0)));

    // Build and emit CorrectionConfig
    CorrectionConfig cfg;
    cfg.sourceAngle = sourceAngle.toStdString();
    cfg.targetDevice = targetDevice.toStdString();
    cfg.targetAxis = targetAxis.toStdString();
    cfg.reverseSign = reverseSign;
    cfg.applied = false;
    cfg.appliedValue = correction;

    emit correctionConfigured(cfg);
}

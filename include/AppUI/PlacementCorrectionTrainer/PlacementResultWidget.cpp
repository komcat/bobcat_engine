// PlacementResultWidget.cpp - Display master vector teach results (no correction)
#include "PlacementResultWidget.h"
#include "ApplicationContext.h"
#include "CoordinateTransformer.h"
#include "unified_motion_manager.h"
#include "MotionJsonLoader.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QScrollArea>
#include <QSignalBlocker>
#include <iostream>

PlacementResultWidget::PlacementResultWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    setupUI();

    m_matrixLoaded = m_matrixLoader.LoadFromFile("config/transformation_matrix.json");
    if (!m_matrixLoaded) {
        m_matrixLoaded = m_matrixLoader.LoadFromFile("config/transformation_matrix.local.json");
    }
}

void PlacementResultWidget::SetData(const PlacementCorrectionSet& set)
{
    m_set = set;

    if (!set.config.targetDevice.empty()) {
        loadTransformationMatrix(set.config.targetDevice);
    }

    // ========================================
    // Section A: Line Summary & Angles
    // ========================================
    if (set.referenceLine.valid && set.targetLine.valid) {
        double refLen = PlacementCorrectionMath::LineLength(
            set.referenceLine.p1x, set.referenceLine.p1y,
            set.referenceLine.p2x, set.referenceLine.p2y);
        double tgtLen = PlacementCorrectionMath::LineLength(
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

    if (set.referenceLine.valid) {
        m_refAngleLabel->setText(QString("Reference Angle (theta_ref): %1%2")
            .arg(set.refAngleDeg, 0, 'f', 3).arg(QChar(0x00B0)));
        m_refAngleLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-weight: bold; padding: 5px;");
    } else {
        m_refAngleLabel->setText("Reference Angle: ---");
        m_refAngleLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    }

    if (set.targetLine.valid) {
        m_tgtAngleLabel->setText(QString("Target Angle (theta_tgt): %1%2")
            .arg(set.targetAngleDeg, 0, 'f', 3).arg(QChar(0x00B0)));
        m_tgtAngleLabel->setStyleSheet("font-size: 14px; color: #00ff88; font-weight: bold; padding: 5px;");
    } else {
        m_tgtAngleLabel->setText("Target Angle: ---");
        m_tgtAngleLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    }

    if (set.deltaValid) {
        m_deltaAngleLabel->setText(QString("Delta theta_Y (target - ref): %1%2")
            .arg(set.deltaAngleDeg, 0, 'f', 3).arg(QChar(0x00B0)));
        m_deltaAngleLabel->setStyleSheet(
            "font-size: 16px; color: #f0c040; font-weight: bold; padding: 10px; "
            "background-color: #2d3e5f; border-radius: 6px;");
    } else {
        m_deltaAngleLabel->setText("Delta: --- (both lines required)");
        m_deltaAngleLabel->setStyleSheet("font-size: 14px; color: #888; padding: 5px;");
    }

    // ========================================
    // Section B: Center Points & Master Vector
    // ========================================
    if (set.referenceLine.valid) {
        double refCx = 0.0, refCy = 0.0;
        PlacementCorrectionMath::Midpoint(
            set.referenceLine.p1x, set.referenceLine.p1y,
            set.referenceLine.p2x, set.referenceLine.p2y,
            refCx, refCy);
        m_refCenterLabel->setText(QString(
            "Reference Origin (midpoint): X=%1  Y=%2")
            .arg(refCx, 0, 'f', 4).arg(refCy, 0, 'f', 4));
        m_refCenterLabel->setStyleSheet("font-size: 13px; color: #00ff88; font-family: monospace; padding: 5px;");

        if (set.targetLine.valid) {
            double tgtCx = 0.0, tgtCy = 0.0;
            PlacementCorrectionMath::Midpoint(
                set.targetLine.p1x, set.targetLine.p1y,
                set.targetLine.p2x, set.targetLine.p2y,
                tgtCx, tgtCy);
            m_tgtCenterLabel->setText(QString(
                "Target Center (midpoint):    X=%1  Y=%2")
                .arg(tgtCx, 0, 'f', 4).arg(tgtCy, 0, 'f', 4));
            m_tgtCenterLabel->setStyleSheet("font-size: 13px; color: #00ff88; font-family: monospace; padding: 5px;");

            double dist = PlacementCorrectionMath::Distance2D(refCx, refCy, tgtCx, tgtCy);
            double dx = tgtCx - refCx;
            double dy = tgtCy - refCy;
            m_distanceLabel->setText(QString(
                "Master Vector: %1 mm  (dX=%2  dY=%3)")
                .arg(dist, 0, 'f', 4).arg(dx, 0, 'f', 4).arg(dy, 0, 'f', 4));
            m_distanceLabel->setStyleSheet(
                "font-size: 13px; color: #f0c040; font-weight: bold; font-family: monospace; padding: 5px;");
        } else {
            m_tgtCenterLabel->setText("Target Center: ---");
            m_tgtCenterLabel->setStyleSheet("font-size: 13px; color: #888; padding: 5px;");
            m_distanceLabel->setText("Master Vector: ---");
            m_distanceLabel->setStyleSheet("font-size: 13px; color: #888; padding: 5px;");
        }
    } else {
        m_refCenterLabel->setText("Reference Origin: ---");
        m_refCenterLabel->setStyleSheet("font-size: 13px; color: #888; padding: 5px;");
        m_tgtCenterLabel->setText("Target Center: ---");
        m_tgtCenterLabel->setStyleSheet("font-size: 13px; color: #888; padding: 5px;");
        m_distanceLabel->setText("Master Vector: ---");
        m_distanceLabel->setStyleSheet("font-size: 13px; color: #888; padding: 5px;");
    }

    // ========================================
    // Section C: Master Position Data (read-only)
    // ========================================
    m_masterPosLabel->setText(QString(
        "Master \"%1\" position (%2):\n"
        "  X: %3    Y: %4    Z: %5\n"
        "  U: %6%9    V: %7%9    W: %8%9")
        .arg(QString::fromStdString(set.config.masterPositionName))
        .arg(QString::fromStdString(set.config.targetDevice))
        .arg(set.config.masterX, 0, 'f', 4).arg(set.config.masterY, 0, 'f', 4)
        .arg(set.config.masterZ, 0, 'f', 4)
        .arg(set.config.masterU, 0, 'f', 4).arg(set.config.masterV, 0, 'f', 4)
        .arg(set.config.masterW, 0, 'f', 4)
        .arg(QChar(0x00B0)));

    // ========================================
    // Section D: Transform Details (collapsible)
    // ========================================
    const auto* matrix = m_matrixLoaded
        ? m_matrixLoader.GetMatrix(set.config.targetDevice)
        : nullptr;

    if (matrix) {
        m_matrixLabel->setText(QString(
            "Transformation Matrix (%1):\n"
            "  [%2  %3  %4]\n"
            "  [%5  %6  %7]\n"
            "  [%8  %9  %10]")
            .arg(QString::fromStdString(set.config.targetDevice))
            .arg((*matrix)[0], 6, 'f', 2).arg((*matrix)[1], 6, 'f', 2).arg((*matrix)[2], 6, 'f', 2)
            .arg((*matrix)[3], 6, 'f', 2).arg((*matrix)[4], 6, 'f', 2).arg((*matrix)[5], 6, 'f', 2)
            .arg((*matrix)[6], 6, 'f', 2).arg((*matrix)[7], 6, 'f', 2).arg((*matrix)[8], 6, 'f', 2));
        m_matrixLabel->setStyleSheet(
            "font-size: 12px; color: #ccc; font-family: monospace; padding: 5px; "
            "background-color: #1a1f2e; border: 1px solid #3d5a8c; border-radius: 4px;");
    } else {
        m_matrixLabel->setText(QString("Transformation Matrix: not found for \"%1\"")
            .arg(QString::fromStdString(set.config.targetDevice)));
        m_matrixLabel->setStyleSheet("font-size: 12px; color: #888; padding: 5px;");
    }

    if (set.referenceLine.valid && set.targetLine.valid && matrix) {
        double refCx = 0.0, refCy = 0.0, tgtCx = 0.0, tgtCy = 0.0;
        PlacementCorrectionMath::Midpoint(
            set.referenceLine.p1x, set.referenceLine.p1y,
            set.referenceLine.p2x, set.referenceLine.p2y, refCx, refCy);
        PlacementCorrectionMath::Midpoint(
            set.targetLine.p1x, set.targetLine.p1y,
            set.targetLine.p2x, set.targetLine.p2y, tgtCx, tgtCy);
        double gDx = tgtCx - refCx;
        double gDy = tgtCy - refCy;

        m_targetGlobalDeltaLabel->setText(QString(
            "Master vector (gantry global):  dX=%1  dY=%2")
            .arg(gDx, 0, 'f', 4).arg(gDy, 0, 'f', 4));

        CoordinateTransformer::Vector6D globalDelta(gDx, gDy, 0, 0, 0, 0);
        auto local = CoordinateTransformer::TransformGlobalToLocal(globalDelta, *matrix);
        m_targetLocalDeltaLabel->setText(QString(
            "Master vector (local %1):  dX=%2  dY=%3  dZ=%4")
            .arg(QString::fromStdString(set.config.targetDevice))
            .arg(local.x, 0, 'f', 4).arg(local.y, 0, 'f', 4).arg(local.z, 0, 'f', 4));
    } else {
        m_targetGlobalDeltaLabel->setText("Master vector (global): ---");
        m_targetLocalDeltaLabel->setText("Master vector (local): ---");
    }

    // ========================================
    // Section E: Manual Offset Tuner — refresh current-target display,
    // reset offset spinboxes, clear any stale status message.
    // ========================================
    if (m_currentTargetLabel) {
        if (!set.config.targetPositionName.empty()) {
            m_currentTargetLabel->setText(QString(
                "Current \"%1\" position (%2):\n"
                "  X: %3    Y: %4    Z: %5\n"
                "  U: %6%9    V: %7%9    W: %8%9")
                .arg(QString::fromStdString(set.config.targetPositionName))
                .arg(QString::fromStdString(set.config.targetDevice))
                .arg(set.config.targetX, 0, 'f', 4).arg(set.config.targetY, 0, 'f', 4)
                .arg(set.config.targetZ, 0, 'f', 4)
                .arg(set.config.targetU, 0, 'f', 4).arg(set.config.targetV, 0, 'f', 4)
                .arg(set.config.targetW, 0, 'f', 4)
                .arg(QChar(0x00B0)));
        } else {
            m_currentTargetLabel->setText("No target position selected.");
        }
    }
    if (m_offsetXSpin) {
        QSignalBlocker bX(m_offsetXSpin);
        QSignalBlocker bY(m_offsetYSpin);
        QSignalBlocker bZ(m_offsetZSpin);
        QSignalBlocker bU(m_offsetUSpin);
        QSignalBlocker bV(m_offsetVSpin);
        QSignalBlocker bW(m_offsetWSpin);
        m_offsetXSpin->setValue(0.0);
        m_offsetYSpin->setValue(0.0);
        m_offsetZSpin->setValue(0.0);
        m_offsetUSpin->setValue(0.0);
        m_offsetVSpin->setValue(0.0);
        m_offsetWSpin->setValue(0.0);
    }
    onManualOffsetChanged();
    if (m_resaveStatusLabel) {
        m_resaveStatusLabel->clear();
    }
}

void PlacementResultWidget::setupUI()
{
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    QWidget* scrollContent = new QWidget();
    QVBoxLayout* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QHBoxLayout* titleRow = new QHBoxLayout();
    QLabel* title = new QLabel("Master Vector Results");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    titleRow->addWidget(title);
    titleRow->addStretch();

    QPushButton* recomputeBtn = new QPushButton("Recompute");
    recomputeBtn->setFixedHeight(30);
    recomputeBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 4px 16px; font-size: 12px; }"
        "QPushButton:hover { background-color: #3498db; }");
    connect(recomputeBtn, &QPushButton::clicked, this, [this]() {
        emit recomputeRequested();
    });
    titleRow->addWidget(recomputeBtn);
    mainLayout->addLayout(titleRow);

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    // Section A: Computed Angles
    QGroupBox* anglesGroup = new QGroupBox("Computed Angles");
    anglesGroup->setStyleSheet(groupStyle);
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

    // Section B: Center Points & Master Vector
    QGroupBox* centerGroup = new QGroupBox("Center Points & Master Vector");
    centerGroup->setStyleSheet(groupStyle);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerGroup);

    m_refCenterLabel = new QLabel("Reference Origin: ---");
    m_refCenterLabel->setStyleSheet("font-size: 13px; color: #888; padding: 5px;");
    centerLayout->addWidget(m_refCenterLabel);

    m_tgtCenterLabel = new QLabel("Target Center: ---");
    m_tgtCenterLabel->setStyleSheet("font-size: 13px; color: #888; padding: 5px;");
    centerLayout->addWidget(m_tgtCenterLabel);

    m_distanceLabel = new QLabel("Master Vector: ---");
    m_distanceLabel->setStyleSheet("font-size: 13px; color: #888; padding: 5px;");
    centerLayout->addWidget(m_distanceLabel);

    mainLayout->addWidget(centerGroup);

    // Section C: Master Position (read-only)
    QGroupBox* masterGroup = new QGroupBox("Master Position Data");
    masterGroup->setStyleSheet(groupStyle);
    QVBoxLayout* masterLayout = new QVBoxLayout(masterGroup);

    m_masterPosLabel = new QLabel("Master position: ---");
    m_masterPosLabel->setStyleSheet(
        "font-size: 12px; color: #ccc; font-family: monospace; padding: 8px; "
        "background-color: #1a1f2e; border: 1px solid #3d5a8c; border-radius: 4px;");
    m_masterPosLabel->setWordWrap(true);
    masterLayout->addWidget(m_masterPosLabel);

    mainLayout->addWidget(masterGroup);

    // Section D: Transform Details (collapsible, hidden by default)
    m_toggleTransformBtn = new QPushButton("Show Transform Details");
    m_toggleTransformBtn->setFixedHeight(28);
    m_toggleTransformBtn->setStyleSheet(
        "QPushButton { background-color: #1a1f2e; color: #888; border: 1px solid #3d5a8c; "
        "border-radius: 3px; padding: 3px 10px; font-size: 11px; }"
        "QPushButton:hover { background-color: #2d3e5f; color: #ccc; }");
    connect(m_toggleTransformBtn, &QPushButton::clicked,
            this, &PlacementResultWidget::onToggleTransformDetails);
    mainLayout->addWidget(m_toggleTransformBtn);

    m_transformGroup = new QGroupBox("Transform Details");
    m_transformGroup->setStyleSheet(groupStyle);
    QVBoxLayout* transformLayout = new QVBoxLayout(m_transformGroup);

    m_matrixLabel = new QLabel("Transformation Matrix: ---");
    m_matrixLabel->setStyleSheet("font-size: 12px; color: #888; padding: 5px;");
    m_matrixLabel->setWordWrap(true);
    transformLayout->addWidget(m_matrixLabel);

    m_targetGlobalDeltaLabel = new QLabel("Master vector (global): ---");
    m_targetGlobalDeltaLabel->setStyleSheet("font-size: 12px; color: #888; padding: 5px;");
    transformLayout->addWidget(m_targetGlobalDeltaLabel);

    m_targetLocalDeltaLabel = new QLabel("Master vector (local): ---");
    m_targetLocalDeltaLabel->setStyleSheet("font-size: 12px; color: #888; padding: 5px;");
    transformLayout->addWidget(m_targetLocalDeltaLabel);

    m_transformGroup->setVisible(false);
    mainLayout->addWidget(m_transformGroup);

    // ========================================
    // Section E: Manual Offset Tuner (re-save target position)
    // Fixed width + top-left alignment (UI convention going forward).
    // ========================================
    QGroupBox* offsetGroup = new QGroupBox("Manual Offset Tuner (Re-save Target)");
    offsetGroup->setStyleSheet(groupStyle);
    offsetGroup->setFixedWidth(440);
    offsetGroup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QVBoxLayout* offsetLayout = new QVBoxLayout(offsetGroup);
    offsetLayout->setContentsMargins(10, 10, 10, 10);
    offsetLayout->setSpacing(8);

    QLabel* offsetHint = new QLabel(
        "Nudge the currently-saved target position by a manual offset and\n"
        "write it back to motor_devices.local.json. XYZ in mm, UVW in\n"
        "degrees. Positive values add, negative subtract.");
    offsetHint->setStyleSheet("color: #aabbcc; font-size: 11px;");
    offsetHint->setWordWrap(true);
    offsetLayout->addWidget(offsetHint);

    m_currentTargetLabel = new QLabel("No target position selected.");
    m_currentTargetLabel->setStyleSheet(
        "font-size: 12px; color: #ccc; font-family: monospace; padding: 8px; "
        "background-color: #1a1f2e; border: 1px solid #3d5a8c; border-radius: 4px;");
    m_currentTargetLabel->setWordWrap(true);
    offsetLayout->addWidget(m_currentTargetLabel);

    QString spinStyle =
        "QDoubleSpinBox { background-color: #1a1f2e; color: #ffffff; "
        "border: 1px solid #4d6e9f; border-radius: 3px; "
        "padding: 3px 6px; font-size: 12px; min-width: 120px; }"
        "QDoubleSpinBox:focus { border: 1px solid #5dade2; }";
    QString axisLblStyle = "color: #d0d0d0; font-size: 12px;";

    auto makeSpin = [spinStyle](const QString& suffix) {
        QDoubleSpinBox* sp = new QDoubleSpinBox();
        sp->setRange(-1.0, 1.0);        // ±1 mm / ±1 deg
        sp->setDecimals(3);             // 1 μm / 1 m° resolution
        sp->setSingleStep(0.001);
        sp->setSuffix(suffix);
        sp->setAlignment(Qt::AlignRight);
        sp->setStyleSheet(spinStyle);
        sp->setKeyboardTracking(false);
        sp->setFixedWidth(140);
        return sp;
    };

    const QString mmSuffix  = QString(" mm");
    const QString degSuffix = QString::fromUtf8(" \xC2\xB0"); // " °"

    m_offsetXSpin = makeSpin(mmSuffix);
    m_offsetYSpin = makeSpin(mmSuffix);
    m_offsetZSpin = makeSpin(mmSuffix);
    m_offsetUSpin = makeSpin(degSuffix);
    m_offsetVSpin = makeSpin(degSuffix);
    m_offsetWSpin = makeSpin(degSuffix);

    QGridLayout* offsetGrid = new QGridLayout();
    offsetGrid->setHorizontalSpacing(10);
    offsetGrid->setVerticalSpacing(6);

    auto makeAxisLabel = [axisLblStyle](const QString& t) {
        QLabel* lbl = new QLabel(t);
        lbl->setStyleSheet(axisLblStyle);
        return lbl;
    };

    offsetGrid->addWidget(makeAxisLabel("X:"), 0, 0);
    offsetGrid->addWidget(m_offsetXSpin,       0, 1);
    offsetGrid->addWidget(makeAxisLabel("U:"), 0, 2);
    offsetGrid->addWidget(m_offsetUSpin,       0, 3);

    offsetGrid->addWidget(makeAxisLabel("Y:"), 1, 0);
    offsetGrid->addWidget(m_offsetYSpin,       1, 1);
    offsetGrid->addWidget(makeAxisLabel("V:"), 1, 2);
    offsetGrid->addWidget(m_offsetVSpin,       1, 3);

    offsetGrid->addWidget(makeAxisLabel("Z:"), 2, 0);
    offsetGrid->addWidget(m_offsetZSpin,       2, 1);
    offsetGrid->addWidget(makeAxisLabel("W:"), 2, 2);
    offsetGrid->addWidget(m_offsetWSpin,       2, 3);

    // Don't stretch — fixed widths throughout
    QHBoxLayout* offsetGridRow = new QHBoxLayout();
    offsetGridRow->setContentsMargins(0, 0, 0, 0);
    offsetGridRow->addLayout(offsetGrid);
    offsetGridRow->addStretch();
    offsetLayout->addLayout(offsetGridRow);

    m_offsetPreviewLabel = new QLabel("Preview: (enter an offset)");
    m_offsetPreviewLabel->setWordWrap(true);
    m_offsetPreviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_offsetPreviewLabel->setStyleSheet(
        "font-family: monospace; font-size: 11px; color: #d0e0f0; "
        "background-color: #0f1520; border: 1px solid #2d3e5f; "
        "border-radius: 4px; padding: 6px;");
    offsetLayout->addWidget(m_offsetPreviewLabel);

    QHBoxLayout* offsetBtnRow = new QHBoxLayout();

    m_offsetClearBtn = new QPushButton("Clear Offsets");
    m_offsetClearBtn->setFixedHeight(30);
    m_offsetClearBtn->setStyleSheet(
        "QPushButton { background-color: #34495e; color: white; font-size: 11px; "
        "border-radius: 3px; padding: 4px 12px; }"
        "QPushButton:hover { background-color: #4a6785; }");
    connect(m_offsetClearBtn, &QPushButton::clicked, this, [this]() {
        if (!m_offsetXSpin) return;
        QSignalBlocker bX(m_offsetXSpin);
        QSignalBlocker bY(m_offsetYSpin);
        QSignalBlocker bZ(m_offsetZSpin);
        QSignalBlocker bU(m_offsetUSpin);
        QSignalBlocker bV(m_offsetVSpin);
        QSignalBlocker bW(m_offsetWSpin);
        m_offsetXSpin->setValue(0.0);
        m_offsetYSpin->setValue(0.0);
        m_offsetZSpin->setValue(0.0);
        m_offsetUSpin->setValue(0.0);
        m_offsetVSpin->setValue(0.0);
        m_offsetWSpin->setValue(0.0);
        onManualOffsetChanged();
    });
    offsetBtnRow->addWidget(m_offsetClearBtn);

    offsetBtnRow->addStretch();

    m_resaveTargetBtn = new QPushButton("Re-save Target Position");
    m_resaveTargetBtn->setFixedHeight(34);
    m_resaveTargetBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 13px; border-radius: 4px; padding: 6px 18px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }");
    connect(m_resaveTargetBtn, &QPushButton::clicked,
            this, &PlacementResultWidget::onResaveTargetPosition);
    offsetBtnRow->addWidget(m_resaveTargetBtn);

    offsetLayout->addLayout(offsetBtnRow);

    m_resaveStatusLabel = new QLabel();
    m_resaveStatusLabel->setWordWrap(true);
    m_resaveStatusLabel->setStyleSheet(
        "font-size: 11px; color: #aabbcc; padding: 4px;");
    offsetLayout->addWidget(m_resaveStatusLabel);

    for (QDoubleSpinBox* sp : {m_offsetXSpin, m_offsetYSpin, m_offsetZSpin,
                               m_offsetUSpin, m_offsetVSpin, m_offsetWSpin}) {
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &PlacementResultWidget::onManualOffsetChanged);
    }

    // Left-top aligned + fixed width (UI convention)
    QHBoxLayout* offsetGroupRow = new QHBoxLayout();
    offsetGroupRow->setContentsMargins(0, 0, 0, 0);
    offsetGroupRow->addWidget(offsetGroup, 0, Qt::AlignLeft | Qt::AlignTop);
    offsetGroupRow->addStretch();
    mainLayout->addLayout(offsetGroupRow);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea);
}

void PlacementResultWidget::loadTransformationMatrix(const std::string& deviceName)
{
    if (m_matrixLoaded && m_matrixLoader.HasDevice(deviceName)) {
        return;
    }
    m_matrixLoaded = m_matrixLoader.LoadFromFile("config/transformation_matrix.json");
    if (!m_matrixLoaded) {
        m_matrixLoaded = m_matrixLoader.LoadFromFile("config/transformation_matrix.local.json");
    }
}

void PlacementResultWidget::onToggleTransformDetails()
{
    bool visible = !m_transformGroup->isVisible();
    m_transformGroup->setVisible(visible);
    m_toggleTransformBtn->setText(visible ? "Hide Transform Details" : "Show Transform Details");
}

// ============================================================================
// Manual Offset Tuner — live preview + re-save handler
// ============================================================================

void PlacementResultWidget::onManualOffsetChanged()
{
    if (!m_offsetPreviewLabel || !m_offsetXSpin) return;

    if (m_set.config.targetPositionName.empty()) {
        m_offsetPreviewLabel->setText("Preview: (no target position selected)");
        if (m_resaveTargetBtn) m_resaveTargetBtn->setEnabled(false);
        return;
    }

    const double offX = m_offsetXSpin->value();  // mm
    const double offY = m_offsetYSpin->value();  // mm
    const double offZ = m_offsetZSpin->value();  // mm
    const double offU = m_offsetUSpin->value();  // deg
    const double offV = m_offsetVSpin->value();  // deg
    const double offW = m_offsetWSpin->value();  // deg

    const double newX = m_set.config.targetX + offX;
    const double newY = m_set.config.targetY + offY;
    const double newZ = m_set.config.targetZ + offZ;
    const double newU = m_set.config.targetU + offU;
    const double newV = m_set.config.targetV + offV;
    const double newW = m_set.config.targetW + offW;

    QString deg = QString(QChar(0x00B0));
    QString text;
    text += QString("New \"%1\" on Re-save:\n")
        .arg(QString::fromStdString(m_set.config.targetPositionName));
    text += QString("  X: %1    Y: %2    Z: %3\n")
        .arg(newX, 10, 'f', 4).arg(newY, 10, 'f', 4).arg(newZ, 10, 'f', 4);
    text += QString("  U: %1%4   V: %2%4   W: %3%4")
        .arg(newU, 10, 'f', 4).arg(newV, 10, 'f', 4).arg(newW, 10, 'f', 4).arg(deg);
    m_offsetPreviewLabel->setText(text);

    if (m_resaveTargetBtn) {
        // Disable if nothing to apply (all zero)
        bool anyNonZero =
            m_offsetXSpin->value() != 0.0 || m_offsetYSpin->value() != 0.0 ||
            m_offsetZSpin->value() != 0.0 || m_offsetUSpin->value() != 0.0 ||
            m_offsetVSpin->value() != 0.0 || m_offsetWSpin->value() != 0.0;
        m_resaveTargetBtn->setEnabled(anyNonZero);
    }
}

void PlacementResultWidget::onResaveTargetPosition()
{
    if (!m_context || !m_context->motionManager) {
        if (m_resaveStatusLabel) {
            m_resaveStatusLabel->setText("Error: motion manager unavailable.");
            m_resaveStatusLabel->setStyleSheet(
                "font-size: 11px; color: #e74c3c; padding: 4px;");
        }
        return;
    }

    const std::string device  = m_set.config.targetDevice;
    const std::string posName = m_set.config.targetPositionName;

    if (device.empty() || posName.empty()) {
        if (m_resaveStatusLabel) {
            m_resaveStatusLabel->setText("Error: target device or position name is empty.");
            m_resaveStatusLabel->setStyleSheet(
                "font-size: 11px; color: #e74c3c; padding: 4px;");
        }
        return;
    }

    // Compute new target = current target + offset (already in mm / deg)
    PositionStruct corrected;
    corrected.x = m_set.config.targetX + m_offsetXSpin->value();
    corrected.y = m_set.config.targetY + m_offsetYSpin->value();
    corrected.z = m_set.config.targetZ + m_offsetZSpin->value();
    corrected.u = m_set.config.targetU + m_offsetUSpin->value();
    corrected.v = m_set.config.targetV + m_offsetVSpin->value();
    corrected.w = m_set.config.targetW + m_offsetWSpin->value();

    // Update in-memory named position
    m_context->motionManager->AddNamedPosition(device, posName, corrected);

    // Write back to motor_devices.local.json (falls back to base file)
    const std::string localPath = "config/motor_devices.local.json";
    const std::string basePath  = "config/motor_devices.json";
    std::string errMsg;
    bool saved = MotionJsonLoader::SavePositionToFile(
        localPath, basePath, device, posName, corrected, true, errMsg);

    if (!saved) {
        if (m_resaveStatusLabel) {
            m_resaveStatusLabel->setText(
                QString("Save FAILED: %1").arg(QString::fromStdString(errMsg)));
            m_resaveStatusLabel->setStyleSheet(
                "font-size: 11px; color: #e74c3c; font-weight: bold; padding: 4px;");
        }
        return;
    }

    // Mirror the new values into our local set copy, then notify the host page
    // so it can persist the placement_correction training set JSON too.
    m_set.config.targetX = corrected.x;
    m_set.config.targetY = corrected.y;
    m_set.config.targetZ = corrected.z;
    m_set.config.targetU = corrected.u;
    m_set.config.targetV = corrected.v;
    m_set.config.targetW = corrected.w;

    emit targetResaved(m_set);

    // Refresh the "Current target" readout and reset spinboxes so the next
    // nudge starts from zero (the new baseline is the just-saved value).
    if (m_currentTargetLabel) {
        QString deg = QString(QChar(0x00B0));
        m_currentTargetLabel->setText(QString(
            "Current \"%1\" position (%2):\n"
            "  X: %3    Y: %4    Z: %5\n"
            "  U: %6%9    V: %7%9    W: %8%9")
            .arg(QString::fromStdString(posName))
            .arg(QString::fromStdString(device))
            .arg(m_set.config.targetX, 0, 'f', 4)
            .arg(m_set.config.targetY, 0, 'f', 4)
            .arg(m_set.config.targetZ, 0, 'f', 4)
            .arg(m_set.config.targetU, 0, 'f', 4)
            .arg(m_set.config.targetV, 0, 'f', 4)
            .arg(m_set.config.targetW, 0, 'f', 4)
            .arg(deg));
    }

    {
        QSignalBlocker bX(m_offsetXSpin);
        QSignalBlocker bY(m_offsetYSpin);
        QSignalBlocker bZ(m_offsetZSpin);
        QSignalBlocker bU(m_offsetUSpin);
        QSignalBlocker bV(m_offsetVSpin);
        QSignalBlocker bW(m_offsetWSpin);
        m_offsetXSpin->setValue(0.0);
        m_offsetYSpin->setValue(0.0);
        m_offsetZSpin->setValue(0.0);
        m_offsetUSpin->setValue(0.0);
        m_offsetVSpin->setValue(0.0);
        m_offsetWSpin->setValue(0.0);
    }
    onManualOffsetChanged();

    if (m_resaveStatusLabel) {
        m_resaveStatusLabel->setText(QString(
            "Saved \"%1\" to %2 and motor_devices.local.json.")
            .arg(QString::fromStdString(posName))
            .arg(QString::fromStdString(device)));
        m_resaveStatusLabel->setStyleSheet(
            "font-size: 11px; color: #2ecc71; font-weight: bold; padding: 4px;");
    }

    std::cout << "[PlacementResult] Re-saved " << device << "/" << posName
              << " with offset: X=" << corrected.x << " Y=" << corrected.y
              << " Z=" << corrected.z << " U=" << corrected.u
              << " V=" << corrected.v << " W=" << corrected.w << std::endl;
}

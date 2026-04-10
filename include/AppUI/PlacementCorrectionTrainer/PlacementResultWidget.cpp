// PlacementResultWidget.cpp - Display master vector teach results (no correction)
#include "PlacementResultWidget.h"
#include "ApplicationContext.h"
#include "CoordinateTransformer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFrame>
#include <QScrollArea>
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

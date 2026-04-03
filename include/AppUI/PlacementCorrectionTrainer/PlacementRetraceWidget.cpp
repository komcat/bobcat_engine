// PlacementRetraceWidget.cpp - Retrace wizard for placement correction
#include "PlacementRetraceWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "CoordinateTransformer.h"
#include "MotionJsonLoader.h"
#include "CameraManager.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QScrollArea>
#include <QPixmap>
#include <QPainter>
#include <iostream>
#include <cmath>
#include <filesystem>

PlacementRetraceWidget::PlacementRetraceWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
    , m_motionPollTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &PlacementRetraceWidget::updatePositionDisplay);
    connect(m_motionPollTimer, &QTimer::timeout, this, [this]() {
        if (m_motionFuture.valid() &&
            m_motionFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            m_motionPollTimer->stop();
            bool result = m_motionFuture.get();
            if (m_motionCallback) m_motionCallback(result);
        }
    });

    m_matrixLoaded = m_matrixLoader.LoadFromFile("config/transformation_matrix.json");
    if (!m_matrixLoaded) {
        m_matrixLoaded = m_matrixLoader.LoadFromFile("config/transformation_matrix.local.json");
    }
}

void PlacementRetraceWidget::SetData(PlacementCorrectionSet* set)
{
    m_set = set;
    setState(Idle);
}

void PlacementRetraceWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Retrace & Review");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    m_wizardStack = new QStackedWidget();

    QString blueBtnStyle =
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    QString greenBtnStyle =
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    QString orangeBtnStyle =
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #f39c12; }";

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    QString posLabelStyle = "font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;";
    QString instructionStyle =
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;";

    // ========================================
    // Page 0: Idle
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_idleSummaryLabel = new QLabel("Load a training set with taught ref/target lines to begin retrace.");
    m_idleSummaryLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_idleSummaryLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleSummaryLabel);

    idleLayout->addStretch();

    m_startBtn = new QPushButton("Start Retrace");
    m_startBtn->setFixedHeight(44);
    m_startBtn->setStyleSheet(greenBtnStyle);
    connect(m_startBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onStartRetrace);
    idleLayout->addWidget(m_startBtn);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: WaitRefP1
    // ========================================
    QWidget* refP1Page = new QWidget();
    QVBoxLayout* refP1Layout = new QVBoxLayout(refP1Page);

    m_refP1InstructionLabel = new QLabel("Step 1/4: Navigating to Reference Point 1...");
    m_refP1InstructionLabel->setStyleSheet(instructionStyle);
    m_refP1InstructionLabel->setWordWrap(true);
    m_refP1InstructionLabel->setAlignment(Qt::AlignCenter);
    refP1Layout->addWidget(m_refP1InstructionLabel);

    QGroupBox* refP1Group = new QGroupBox("Current Gantry Position");
    refP1Group->setStyleSheet(groupStyle);
    QVBoxLayout* refP1PosLayout = new QVBoxLayout(refP1Group);
    m_refP1PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_refP1PositionLabel->setStyleSheet(posLabelStyle);
    m_refP1PositionLabel->setAlignment(Qt::AlignCenter);
    refP1PosLayout->addWidget(m_refP1PositionLabel);
    refP1Layout->addWidget(refP1Group);

    m_refP1ImageLabel = new QLabel("No reference image");
    m_refP1ImageLabel->setFixedSize(320, 240);
    m_refP1ImageLabel->setAlignment(Qt::AlignCenter);
    m_refP1ImageLabel->setStyleSheet(
        "QLabel { color: #555; font-size: 11px; border: 1px solid #3d5a8c; "
        "border-radius: 4px; background-color: #0a0f1e; }");
    refP1Layout->addWidget(m_refP1ImageLabel, 0, Qt::AlignCenter);

    refP1Layout->addStretch();

    m_confirmRefP1Btn = new QPushButton("Confirm Reference P1");
    m_confirmRefP1Btn->setFixedHeight(44);
    m_confirmRefP1Btn->setStyleSheet(blueBtnStyle);
    m_confirmRefP1Btn->setEnabled(false);
    connect(m_confirmRefP1Btn, &QPushButton::clicked, this, &PlacementRetraceWidget::onConfirmRefP1);
    refP1Layout->addWidget(m_confirmRefP1Btn);

    m_wizardStack->addWidget(refP1Page);  // index 1

    // ========================================
    // Page 2: WaitRefP2
    // ========================================
    QWidget* refP2Page = new QWidget();
    QVBoxLayout* refP2Layout = new QVBoxLayout(refP2Page);

    m_refP2InstructionLabel = new QLabel("Step 2/4: Verify Reference Point 2...");
    m_refP2InstructionLabel->setStyleSheet(instructionStyle);
    m_refP2InstructionLabel->setWordWrap(true);
    m_refP2InstructionLabel->setAlignment(Qt::AlignCenter);
    refP2Layout->addWidget(m_refP2InstructionLabel);

    QGroupBox* refP2Group = new QGroupBox("Current Gantry Position");
    refP2Group->setStyleSheet(groupStyle);
    QVBoxLayout* refP2PosLayout = new QVBoxLayout(refP2Group);
    m_refP2PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_refP2PositionLabel->setStyleSheet(posLabelStyle);
    m_refP2PositionLabel->setAlignment(Qt::AlignCenter);
    refP2PosLayout->addWidget(m_refP2PositionLabel);
    refP2Layout->addWidget(refP2Group);

    m_refP2ImageLabel = new QLabel("No reference image");
    m_refP2ImageLabel->setFixedSize(320, 240);
    m_refP2ImageLabel->setAlignment(Qt::AlignCenter);
    m_refP2ImageLabel->setStyleSheet(
        "QLabel { color: #555; font-size: 11px; border: 1px solid #3d5a8c; "
        "border-radius: 4px; background-color: #0a0f1e; }");
    refP2Layout->addWidget(m_refP2ImageLabel, 0, Qt::AlignCenter);

    refP2Layout->addStretch();

    m_confirmRefP2Btn = new QPushButton("Confirm Reference P2");
    m_confirmRefP2Btn->setFixedHeight(44);
    m_confirmRefP2Btn->setStyleSheet(blueBtnStyle);
    connect(m_confirmRefP2Btn, &QPushButton::clicked, this, &PlacementRetraceWidget::onConfirmRefP2);
    refP2Layout->addWidget(m_confirmRefP2Btn);

    m_wizardStack->addWidget(refP2Page);  // index 2

    // ========================================
    // Page 3: WaitTgtP1
    // ========================================
    QWidget* tgtP1Page = new QWidget();
    QVBoxLayout* tgtP1Layout = new QVBoxLayout(tgtP1Page);

    m_tgtP1InstructionLabel = new QLabel("Step 3/4: Navigating to Target Point 1...");
    m_tgtP1InstructionLabel->setStyleSheet(instructionStyle);
    m_tgtP1InstructionLabel->setWordWrap(true);
    m_tgtP1InstructionLabel->setAlignment(Qt::AlignCenter);
    tgtP1Layout->addWidget(m_tgtP1InstructionLabel);

    QGroupBox* tgtP1Group = new QGroupBox("Current Gantry Position");
    tgtP1Group->setStyleSheet(groupStyle);
    QVBoxLayout* tgtP1PosLayout = new QVBoxLayout(tgtP1Group);
    m_tgtP1PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_tgtP1PositionLabel->setStyleSheet(posLabelStyle);
    m_tgtP1PositionLabel->setAlignment(Qt::AlignCenter);
    tgtP1PosLayout->addWidget(m_tgtP1PositionLabel);
    tgtP1Layout->addWidget(tgtP1Group);

    m_tgtP1ImageLabel = new QLabel("No reference image");
    m_tgtP1ImageLabel->setFixedSize(320, 240);
    m_tgtP1ImageLabel->setAlignment(Qt::AlignCenter);
    m_tgtP1ImageLabel->setStyleSheet(
        "QLabel { color: #555; font-size: 11px; border: 1px solid #3d5a8c; "
        "border-radius: 4px; background-color: #0a0f1e; }");
    tgtP1Layout->addWidget(m_tgtP1ImageLabel, 0, Qt::AlignCenter);

    tgtP1Layout->addStretch();

    m_confirmTgtP1Btn = new QPushButton("Confirm Target P1");
    m_confirmTgtP1Btn->setFixedHeight(44);
    m_confirmTgtP1Btn->setStyleSheet(blueBtnStyle);
    m_confirmTgtP1Btn->setEnabled(false);
    connect(m_confirmTgtP1Btn, &QPushButton::clicked, this, &PlacementRetraceWidget::onConfirmTgtP1);
    tgtP1Layout->addWidget(m_confirmTgtP1Btn);

    m_wizardStack->addWidget(tgtP1Page);  // index 3

    // ========================================
    // Page 4: WaitTgtP2
    // ========================================
    QWidget* tgtP2Page = new QWidget();
    QVBoxLayout* tgtP2Layout = new QVBoxLayout(tgtP2Page);

    m_tgtP2InstructionLabel = new QLabel("Step 4/4: Verify Target Point 2...");
    m_tgtP2InstructionLabel->setStyleSheet(instructionStyle);
    m_tgtP2InstructionLabel->setWordWrap(true);
    m_tgtP2InstructionLabel->setAlignment(Qt::AlignCenter);
    tgtP2Layout->addWidget(m_tgtP2InstructionLabel);

    QGroupBox* tgtP2Group = new QGroupBox("Current Gantry Position");
    tgtP2Group->setStyleSheet(groupStyle);
    QVBoxLayout* tgtP2PosLayout = new QVBoxLayout(tgtP2Group);
    m_tgtP2PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_tgtP2PositionLabel->setStyleSheet(posLabelStyle);
    m_tgtP2PositionLabel->setAlignment(Qt::AlignCenter);
    tgtP2PosLayout->addWidget(m_tgtP2PositionLabel);
    tgtP2Layout->addWidget(tgtP2Group);

    m_tgtP2ImageLabel = new QLabel("No reference image");
    m_tgtP2ImageLabel->setFixedSize(320, 240);
    m_tgtP2ImageLabel->setAlignment(Qt::AlignCenter);
    m_tgtP2ImageLabel->setStyleSheet(
        "QLabel { color: #555; font-size: 11px; border: 1px solid #3d5a8c; "
        "border-radius: 4px; background-color: #0a0f1e; }");
    tgtP2Layout->addWidget(m_tgtP2ImageLabel, 0, Qt::AlignCenter);

    tgtP2Layout->addStretch();

    m_confirmTgtP2Btn = new QPushButton("Confirm Target P2");
    m_confirmTgtP2Btn->setFixedHeight(44);
    m_confirmTgtP2Btn->setStyleSheet(blueBtnStyle);
    connect(m_confirmTgtP2Btn, &QPushButton::clicked, this, &PlacementRetraceWidget::onConfirmTgtP2);
    tgtP2Layout->addWidget(m_confirmTgtP2Btn);

    m_wizardStack->addWidget(tgtP2Page);  // index 4

    // ========================================
    // Page 5: Review
    // ========================================
    QWidget* reviewPage = new QWidget();
    QVBoxLayout* reviewOuterLayout = new QVBoxLayout(reviewPage);
    reviewOuterLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* reviewScroll = new QScrollArea();
    reviewScroll->setWidgetResizable(true);
    reviewScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    QWidget* reviewContent = new QWidget();
    QVBoxLayout* reviewLayout = new QVBoxLayout(reviewContent);

    m_reviewLabel = new QLabel("Calculating...");
    m_reviewLabel->setStyleSheet(
        "font-size: 12px; color: #ccc; font-family: monospace; padding: 10px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_reviewLabel->setWordWrap(true);
    m_reviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    reviewLayout->addWidget(m_reviewLabel);

    // Apply U correction checkbox
    m_applyUCheckbox = new QCheckBox("Apply U (rotation) correction");
    m_applyUCheckbox->setChecked(true);
    m_applyUCheckbox->setStyleSheet(
        "QCheckBox { color: #f0c040; font-size: 13px; font-weight: bold; padding: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; }"
        "QCheckBox::indicator:unchecked { border: 1px solid #4d6e9f; background: #1a1f2e; border-radius: 3px; }"
        "QCheckBox::indicator:checked { border: 1px solid #4d6e9f; background: #2980b9; border-radius: 3px; }");
    connect(m_applyUCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_set) {
            m_set->config.applyUCorrection = checked;
            if (m_state == Review) {
                m_saveBtn->setEnabled(true);
                QString targetName = QString::fromStdString(m_set->config.targetPositionName);
                m_saveBtn->setText(QString("Save to \"%1\"").arg(targetName));
                computeRetraceResults();
            }
        }
    });
    reviewLayout->addWidget(m_applyUCheckbox);

    reviewLayout->addStretch();

    QHBoxLayout* reviewBtnRow = new QHBoxLayout();
    m_saveBtn = new QPushButton("Save to Target Position");  // updated dynamically
    m_saveBtn->setFixedHeight(44);
    m_saveBtn->setStyleSheet(greenBtnStyle);
    connect(m_saveBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onSaveToTarget);
    reviewBtnRow->addWidget(m_saveBtn);

    m_resetBtn = new QPushButton("Reset");
    m_resetBtn->setFixedHeight(44);
    m_resetBtn->setStyleSheet(orangeBtnStyle);
    connect(m_resetBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onReset);
    reviewBtnRow->addWidget(m_resetBtn);

    reviewLayout->addLayout(reviewBtnRow);

    m_moveToTargetBtn = new QPushButton("Move to Target Position");
    m_moveToTargetBtn->setFixedHeight(44);
    m_moveToTargetBtn->setStyleSheet(
        "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #9b59b6; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }");
    m_moveToTargetBtn->setEnabled(false);
    connect(m_moveToTargetBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onMoveToTarget);
    reviewLayout->addWidget(m_moveToTargetBtn);

    QString redBtnStyle =
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }";

    m_endBtn = new QPushButton("End Retrace");
    m_endBtn->setFixedHeight(44);
    m_endBtn->setStyleSheet(redBtnStyle);
    connect(m_endBtn, &QPushButton::clicked, this, &PlacementRetraceWidget::onEnd);
    reviewLayout->addWidget(m_endBtn);

    reviewScroll->setWidget(reviewContent);
    reviewOuterLayout->addWidget(reviewScroll);

    m_wizardStack->addWidget(reviewPage);  // index 5

    mainLayout->addWidget(m_wizardStack, 1);
}

void PlacementRetraceWidget::setState(WizardState state)
{
    m_state = state;

    switch (state) {
    case Idle: {
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        bool ready = m_set && m_set->referenceLine.valid && m_set->targetLine.valid
                     && !m_set->config.targetDevice.empty()
                     && !m_set->config.masterPositionName.empty()
                     && !m_set->config.targetPositionName.empty();
        m_startBtn->setEnabled(ready);

        if (m_set && ready) {
            m_idleSummaryLabel->setText(QString(
                "Ready to retrace.\n\n"
                "Device: %1\n"
                "Master Position: %2\n"
                "Target Position: %3\n\n"
                "Master Vector: dX=%4  dY=%5\n"
                "Master Delta Angle: %6%7\n\n"
                "Press 'Start Retrace' to begin.")
                .arg(QString::fromStdString(m_set->config.targetDevice))
                .arg(QString::fromStdString(m_set->config.masterPositionName))
                .arg(QString::fromStdString(m_set->config.targetPositionName))
                .arg(m_masterVecX, 0, 'f', 4).arg(m_masterVecY, 0, 'f', 4)
                .arg(m_set->deltaAngleDeg, 0, 'f', 3).arg(QChar(0x00B0)));
        } else {
            m_idleSummaryLabel->setText(
                "Cannot retrace. Please ensure:\n"
                "  - Reference line is taught\n"
                "  - Target line is taught\n"
                "  - Target device is selected\n"
                "  - Master and target positions are selected");
        }
        break;
    }
    case WaitRefP1:
        m_wizardStack->setCurrentIndex(1);
        m_confirmRefP1Btn->setEnabled(false);
        m_refP1InstructionLabel->setText("Step 1/4: Navigating to Reference Point 1...");
        if (m_set) loadReferenceImage(m_refP1ImageLabel, m_set->referenceLine.p1ImageFile);
        m_positionTimer->start(100);
        QTimer::singleShot(0, this, [this]() { autoNavigateToRefP1(); });
        break;

    case WaitRefP2:
        m_wizardStack->setCurrentIndex(2);
        m_refP2InstructionLabel->setText("Step 2/4: Moved to Reference P2 area. Verify and press Confirm.");
        if (m_set) loadReferenceImage(m_refP2ImageLabel, m_set->referenceLine.p2ImageFile);
        m_positionTimer->start(100);
        break;

    case WaitTgtP1:
        m_wizardStack->setCurrentIndex(3);
        m_confirmTgtP1Btn->setEnabled(false);
        m_tgtP1InstructionLabel->setText("Step 3/4: Navigating to Target Point 1...");
        if (m_set) loadReferenceImage(m_tgtP1ImageLabel, m_set->targetLine.p1ImageFile);
        m_positionTimer->start(100);
        QTimer::singleShot(0, this, [this]() { autoNavigateToTgtP1(); });
        break;

    case WaitTgtP2:
        m_wizardStack->setCurrentIndex(4);
        m_tgtP2InstructionLabel->setText("Step 4/4: Moved to Target P2 area. Verify and press Confirm.");
        if (m_set) loadReferenceImage(m_tgtP2ImageLabel, m_set->targetLine.p2ImageFile);
        m_positionTimer->start(100);
        break;

    case Review: {
        m_wizardStack->setCurrentIndex(5);
        m_positionTimer->stop();
        m_saveBtn->setEnabled(true);
        QString targetName = m_set ? QString::fromStdString(m_set->config.targetPositionName) : "Target";
        m_saveBtn->setText(QString("Save to \"%1\"").arg(targetName));
        // Sync checkbox from loaded set
        if (m_set) {
            m_applyUCheckbox->setChecked(m_set->config.applyUCorrection);
        }
        computeRetraceResults();
        break;
    }
    }
}

void PlacementRetraceWidget::onStartRetrace()
{
    if (!m_set) return;

    // Compute master vector from taught data
    double refCx = 0.0, refCy = 0.0, tgtCx = 0.0, tgtCy = 0.0;
    PlacementCorrectionMath::Midpoint(
        m_set->referenceLine.p1x, m_set->referenceLine.p1y,
        m_set->referenceLine.p2x, m_set->referenceLine.p2y, refCx, refCy);
    PlacementCorrectionMath::Midpoint(
        m_set->targetLine.p1x, m_set->targetLine.p1y,
        m_set->targetLine.p2x, m_set->targetLine.p2y, tgtCx, tgtCy);
    m_masterVecX = tgtCx - refCx;
    m_masterVecY = tgtCy - refCy;
    m_masterDeltaAngle = m_set->deltaAngleDeg;

    // Save taught P1->P2 deltas for relative navigation
    m_refDeltaX = m_set->referenceLine.p2x - m_set->referenceLine.p1x;
    m_refDeltaY = m_set->referenceLine.p2y - m_set->referenceLine.p1y;
    m_refDeltaZ = m_set->referenceLine.p2z - m_set->referenceLine.p1z;
    m_tgtDeltaX = m_set->targetLine.p2x - m_set->targetLine.p1x;
    m_tgtDeltaY = m_set->targetLine.p2y - m_set->targetLine.p1y;
    m_tgtDeltaZ = m_set->targetLine.p2z - m_set->targetLine.p1z;

    // Clear retraced data
    m_retracedRef = PlacementLinePoints();
    m_retracedRef.name = "Reference";
    m_retracedTgt = PlacementLinePoints();
    m_retracedTgt.name = "Target";

    std::cout << "[PlacementRetrace] Start retrace. Master vector: dX=" << m_masterVecX
              << " dY=" << m_masterVecY << " dAngle=" << m_masterDeltaAngle << std::endl;

    // Apply Ref P1 camera exposure before first motion
    applyCameraExposure(m_set->referenceLine.p1Exposure);

    setState(WaitRefP1);
}

void PlacementRetraceWidget::applyCameraExposure(double exposureUs)
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

    std::cout << "[PlacementRetrace] Camera \"" << m_set->config.cameraId
              << "\" exposure=" << exposureUs << "us "
              << (ok ? "OK" : "FAILED") << std::endl;
}

std::string PlacementRetraceWidget::getImageDirectory() const
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        std::filesystem::path productPath(
            m_context->productPositionManager->GetCurrentProductPath());
        return (productPath.parent_path() / "images" / "trainer").string();
    }
    return "config/images/trainer";
}

void PlacementRetraceWidget::loadReferenceImage(QLabel* label, const std::string& imageFile)
{
    if (!label) return;
    if (imageFile.empty()) {
        label->setText("No reference image");
        label->setPixmap(QPixmap());
        return;
    }
    std::string fullPath = getImageDirectory() + "/" + imageFile;
    QPixmap pixmap(QString::fromStdString(fullPath));
    if (pixmap.isNull()) {
        label->setText("Image not found");
        return;
    }

    // Crop center 25% of the image
    int cropW = pixmap.width() / 2;
    int cropH = pixmap.height() / 2;
    int cropX = (pixmap.width() - cropW) / 2;
    int cropY = (pixmap.height() - cropH) / 2;
    QPixmap cropped = pixmap.copy(cropX, cropY, cropW, cropH);

    // Scale to label size
    QPixmap scaled = cropped.scaled(label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);

    // Draw crosshair overlay
    QPainter painter(&scaled);
    painter.setPen(QPen(QColor(255, 0, 0), 1));
    int cx = scaled.width() / 2;
    int cy = scaled.height() / 2;
    painter.drawLine(cx, 0, cx, scaled.height());
    painter.drawLine(0, cy, scaled.width(), cy);
    painter.end();

    label->setText("");
    label->setPixmap(scaled);
}

void PlacementRetraceWidget::runMotionAsync(std::function<bool()> motionFunc,
                                             std::function<void(bool)> onComplete)
{
    m_motionCallback = onComplete;
    m_motionFuture = std::async(std::launch::async, motionFunc);
    m_motionPollTimer->start(50);
}

void PlacementRetraceWidget::autoNavigateToRefP1()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        m_refP1InstructionLabel->setText("Step 1/4: Motion manager not available. Manually jog to Reference P1.");
        m_confirmRefP1Btn->setEnabled(true);
        return;
    }

    auto* mm = m_context->motionManager;
    std::string dev = m_set->measureDevice;
    double tx = m_set->referenceLine.p1x;
    double ty = m_set->referenceLine.p1y;
    double tz = m_set->referenceLine.p1z;

    runMotionAsync(
        [mm, dev, tx, ty, tz]() {
            bool ok = mm->MoveToPosition(dev, "X", tx, true);
            if (ok) ok = mm->MoveToPosition(dev, "Y", ty, true);
            if (ok) ok = mm->MoveToPosition(dev, "Z", tz, true);
            return ok;
        },
        [this](bool ok) {
            if (ok) {
                m_refP1InstructionLabel->setText(
                    "Step 1/4: Arrived at Reference P1.\nVerify position and press Confirm.");
            } else {
                m_refP1InstructionLabel->setText(
                    "Step 1/4: Auto-navigate FAILED.\nManually jog to Reference P1, then press Confirm.");
            }
            m_confirmRefP1Btn->setEnabled(true);
        });
}

void PlacementRetraceWidget::autoNavigateToTgtP1()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        m_tgtP1InstructionLabel->setText("Step 3/4: Motion manager not available. Manually jog to Target P1.");
        m_confirmTgtP1Btn->setEnabled(true);
        return;
    }

    auto* mm = m_context->motionManager;
    std::string dev = m_set->measureDevice;
    double tx = m_set->targetLine.p1x;
    double ty = m_set->targetLine.p1y;
    double tz = m_set->targetLine.p1z;

    runMotionAsync(
        [mm, dev, tx, ty, tz]() {
            bool ok = mm->MoveToPosition(dev, "X", tx, true);
            if (ok) ok = mm->MoveToPosition(dev, "Y", ty, true);
            if (ok) ok = mm->MoveToPosition(dev, "Z", tz, true);
            return ok;
        },
        [this](bool ok) {
            if (ok) {
                m_tgtP1InstructionLabel->setText(
                    "Step 3/4: Arrived at Target P1.\nVerify position and press Confirm.");
            } else {
                m_tgtP1InstructionLabel->setText(
                    "Step 3/4: Auto-navigate FAILED.\nManually jog to Target P1, then press Confirm.");
            }
            m_confirmTgtP1Btn->setEnabled(true);
        });
}

void PlacementRetraceWidget::onConfirmRefP1()
{
    if (!m_context || !m_context->motionManager) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    m_retracedRef.p1x = pos.x;
    m_retracedRef.p1y = pos.y;
    m_retracedRef.p1z = pos.z;

    std::cout << "[PlacementRetrace] Ref P1: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    // Apply Ref P2 exposure before moving
    applyCameraExposure(m_set->referenceLine.p2Exposure);

    auto* mm = m_context->motionManager;
    std::string dev = m_set->measureDevice;
    double dx = m_refDeltaX, dy = m_refDeltaY, dz = m_refDeltaZ;

    m_confirmRefP1Btn->setEnabled(false);
    setState(WaitRefP2);

    runMotionAsync(
        [mm, dev, dx, dy, dz]() {
            bool ok = true;
            if (std::abs(dx) > 1e-6) ok = mm->MoveRelative(dev, "X", dx, true);
            if (ok && std::abs(dy) > 1e-6) ok = mm->MoveRelative(dev, "Y", dy, true);
            if (ok && std::abs(dz) > 1e-6) ok = mm->MoveRelative(dev, "Z", dz, true);
            return ok;
        },
        [this](bool ok) {
            if (!ok) {
                m_refP2InstructionLabel->setText("Step 2/4: Auto-move FAILED. Manually jog to Ref P2, then Confirm.");
            }
        });
}

void PlacementRetraceWidget::onConfirmRefP2()
{
    if (!m_context || !m_context->motionManager) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    m_retracedRef.p2x = pos.x;
    m_retracedRef.p2y = pos.y;
    m_retracedRef.p2z = pos.z;
    m_retracedRef.valid = true;
    m_retracedRef.angleDeg = PlacementCorrectionMath::ComputeLineAngle(
        m_retracedRef.p1x, m_retracedRef.p1y, m_retracedRef.p2x, m_retracedRef.p2y);

    std::cout << "[PlacementRetrace] Ref P2: X=" << pos.x << " Y=" << pos.y
              << " Angle=" << m_retracedRef.angleDeg << std::endl;

    // Apply Tgt P1 exposure before navigating to target
    applyCameraExposure(m_set->targetLine.p1Exposure);

    setState(WaitTgtP1);
}

void PlacementRetraceWidget::onConfirmTgtP1()
{
    if (!m_context || !m_context->motionManager) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    m_retracedTgt.p1x = pos.x;
    m_retracedTgt.p1y = pos.y;
    m_retracedTgt.p1z = pos.z;

    std::cout << "[PlacementRetrace] Tgt P1: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    // Apply Tgt P2 exposure before moving
    applyCameraExposure(m_set->targetLine.p2Exposure);

    auto* mm = m_context->motionManager;
    std::string dev = m_set->measureDevice;
    double dx = m_tgtDeltaX, dy = m_tgtDeltaY, dz = m_tgtDeltaZ;

    m_confirmTgtP1Btn->setEnabled(false);
    setState(WaitTgtP2);

    runMotionAsync(
        [mm, dev, dx, dy, dz]() {
            bool ok = true;
            if (std::abs(dx) > 1e-6) ok = mm->MoveRelative(dev, "X", dx, true);
            if (ok && std::abs(dy) > 1e-6) ok = mm->MoveRelative(dev, "Y", dy, true);
            if (ok && std::abs(dz) > 1e-6) ok = mm->MoveRelative(dev, "Z", dz, true);
            return ok;
        },
        [this](bool ok) {
            if (!ok) {
                m_tgtP2InstructionLabel->setText("Step 4/4: Auto-move FAILED. Manually jog to Target P2, then Confirm.");
            }
        });
}

void PlacementRetraceWidget::onConfirmTgtP2()
{
    if (!m_context || !m_context->motionManager) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    m_retracedTgt.p2x = pos.x;
    m_retracedTgt.p2y = pos.y;
    m_retracedTgt.p2z = pos.z;
    m_retracedTgt.valid = true;
    m_retracedTgt.angleDeg = PlacementCorrectionMath::ComputeLineAngle(
        m_retracedTgt.p1x, m_retracedTgt.p1y, m_retracedTgt.p2x, m_retracedTgt.p2y);

    std::cout << "[PlacementRetrace] Tgt P2: X=" << pos.x << " Y=" << pos.y
              << " Angle=" << m_retracedTgt.angleDeg << std::endl;

    setState(Review);
}

void PlacementRetraceWidget::computeRetraceResults()
{
    if (!m_context || !m_context->motionManager || !m_set) {
        m_reviewLabel->setText("Error: motion manager or set not available.");
        return;
    }

    // Read live hex position
    auto hexPos = m_context->motionManager->GetCurrentPosition(m_set->config.targetDevice);
    m_liveHexX = hexPos.x;
    m_liveHexY = hexPos.y;
    m_liveHexZ = hexPos.z;
    m_liveHexU = hexPos.u;
    m_liveHexV = hexPos.v;
    m_liveHexW = hexPos.w;

    // Compute retraced centers
    double retRefCx = 0.0, retRefCy = 0.0, retTgtCx = 0.0, retTgtCy = 0.0;
    PlacementCorrectionMath::Midpoint(
        m_retracedRef.p1x, m_retracedRef.p1y,
        m_retracedRef.p2x, m_retracedRef.p2y, retRefCx, retRefCy);
    PlacementCorrectionMath::Midpoint(
        m_retracedTgt.p1x, m_retracedTgt.p1y,
        m_retracedTgt.p2x, m_retracedTgt.p2y, retTgtCx, retTgtCy);

    // Expected point = new ref origin + master vector
    double expectedX = retRefCx + m_masterVecX;
    double expectedY = retRefCy + m_masterVecY;

    // Actual point = retraced target center
    double actualX = retTgtCx;
    double actualY = retTgtCy;

    // Correction in gantry global = expected - actual
    double corrGlobalX = expectedX - actualX;
    double corrGlobalY = expectedY - actualY;

    // Transform correction to local device coords
    const auto* matrix = m_matrixLoaded
        ? m_matrixLoader.GetMatrix(m_set->config.targetDevice) : nullptr;

    double corrLocalX = 0.0, corrLocalY = 0.0, corrLocalZ = 0.0;
    if (matrix) {
        CoordinateTransformer::Vector6D globalCorr(corrGlobalX, corrGlobalY, 0, 0, 0, 0);
        auto localCorr = CoordinateTransformer::TransformGlobalToLocal(globalCorr, *matrix);
        corrLocalX = localCorr.x;
        corrLocalY = localCorr.y;
        corrLocalZ = localCorr.z;
    }

    // Angle correction: actual delta angle applied as offset from live hex U
    double actualDeltaAngle = PlacementCorrectionMath::ComputeDeltaAngle(
        m_retracedRef.angleDeg, m_retracedTgt.angleDeg);

    // PLACE_ADJ = live hex position + corrections
    m_correctedX = m_liveHexX + corrLocalX;
    m_correctedY = m_liveHexY + corrLocalY;
    m_correctedZ = m_liveHexZ + corrLocalZ;
    m_correctedU = m_set->config.applyUCorrection
        ? (m_liveHexU + actualDeltaAngle)
        : m_liveHexU;
    m_correctedV = m_liveHexV;
    m_correctedW = m_liveHexW;

    // Actual vector
    double actualVecX = retTgtCx - retRefCx;
    double actualVecY = retTgtCy - retRefCy;

    // Deltas (corrected - master)
    double deltaX = m_correctedX - m_set->config.masterX;
    double deltaY = m_correctedY - m_set->config.masterY;
    double deltaZ = m_correctedZ - m_set->config.masterZ;
    double deltaU = m_correctedU - m_set->config.masterU;
    double deltaV = m_correctedV - m_set->config.masterV;
    double deltaW = m_correctedW - m_set->config.masterW;

    // Build review text
    QString deg = QString(QChar(0x00B0));
    QString text;
    text += QString("=== RETRACE RESULTS ===\n\n");

    text += QString("--- Retraced Points ---\n");
    text += QString("Ref P1: X=%1  Y=%2\n").arg(m_retracedRef.p1x, 0, 'f', 4).arg(m_retracedRef.p1y, 0, 'f', 4);
    text += QString("Ref P2: X=%1  Y=%2\n").arg(m_retracedRef.p2x, 0, 'f', 4).arg(m_retracedRef.p2y, 0, 'f', 4);
    text += QString("Tgt P1: X=%1  Y=%2\n").arg(m_retracedTgt.p1x, 0, 'f', 4).arg(m_retracedTgt.p1y, 0, 'f', 4);
    text += QString("Tgt P2: X=%1  Y=%2\n\n").arg(m_retracedTgt.p2x, 0, 'f', 4).arg(m_retracedTgt.p2y, 0, 'f', 4);

    text += QString("--- Angles ---\n");
    text += QString("Ref Angle:  %1%2\n").arg(m_retracedRef.angleDeg, 0, 'f', 3).arg(deg);
    text += QString("Tgt Angle:  %1%2\n").arg(m_retracedTgt.angleDeg, 0, 'f', 3).arg(deg);
    text += QString("Actual Delta: %1%2%3\n\n")
        .arg(actualDeltaAngle, 0, 'f', 3).arg(deg)
        .arg(m_set->config.applyUCorrection ? "" : "  (U correction DISABLED)");

    text += QString("--- Vectors (gantry global) ---\n");
    text += QString("Master Vector:  dX=%1  dY=%2\n").arg(m_masterVecX, 0, 'f', 4).arg(m_masterVecY, 0, 'f', 4);
    text += QString("Actual Vector:  dX=%1  dY=%2\n").arg(actualVecX, 0, 'f', 4).arg(actualVecY, 0, 'f', 4);
    text += QString("Correction (global): dX=%1  dY=%2\n\n").arg(corrGlobalX, 0, 'f', 4).arg(corrGlobalY, 0, 'f', 4);

    if (matrix) {
        text += QString("--- Correction (local %1) ---\n").arg(QString::fromStdString(m_set->config.targetDevice));
        text += QString("dX=%1  dY=%2  dZ=%3\n\n").arg(corrLocalX, 0, 'f', 4).arg(corrLocalY, 0, 'f', 4).arg(corrLocalZ, 0, 'f', 4);
    }

    // Comparison table: Master vs Corrected Target
    QString masterName = QString::fromStdString(m_set->config.masterPositionName);
    QString targetName = QString::fromStdString(m_set->config.targetPositionName);

    text += QString("============================================================\n");
    text += QString("  AXIS    Master \"%1\"    Corrected \"%2\"    Delta\n")
        .arg(masterName).arg(targetName);
    text += QString("============================================================\n");
    text += QString("  X     %1          %2          %3\n")
        .arg(m_set->config.masterX, 10, 'f', 4)
        .arg(m_correctedX, 10, 'f', 4)
        .arg(deltaX, 10, 'f', 4);
    text += QString("  Y     %1          %2          %3\n")
        .arg(m_set->config.masterY, 10, 'f', 4)
        .arg(m_correctedY, 10, 'f', 4)
        .arg(deltaY, 10, 'f', 4);
    text += QString("  Z     %1          %2          %3\n")
        .arg(m_set->config.masterZ, 10, 'f', 4)
        .arg(m_correctedZ, 10, 'f', 4)
        .arg(deltaZ, 10, 'f', 4);
    text += QString("  U     %1%4       %2%4       %3%4\n")
        .arg(m_set->config.masterU, 10, 'f', 4)
        .arg(m_correctedU, 10, 'f', 4)
        .arg(deltaU, 10, 'f', 4)
        .arg(deg);
    text += QString("  V     %1%4       %2%4       %3%4\n")
        .arg(m_set->config.masterV, 10, 'f', 4)
        .arg(m_correctedV, 10, 'f', 4)
        .arg(deltaV, 10, 'f', 4)
        .arg(deg);
    text += QString("  W     %1%4       %2%4       %3%4\n")
        .arg(m_set->config.masterW, 10, 'f', 4)
        .arg(m_correctedW, 10, 'f', 4)
        .arg(deltaW, 10, 'f', 4)
        .arg(deg);
    text += QString("============================================================\n");

    m_reviewLabel->setText(text);

    std::cout << "[PlacementRetrace] Corrected " << m_set->config.targetPositionName
              << ": X=" << m_correctedX << " Y=" << m_correctedY << " Z=" << m_correctedZ
              << " U=" << m_correctedU << " V=" << m_correctedV << " W=" << m_correctedW << std::endl;
}

void PlacementRetraceWidget::onSaveToTarget()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    std::string device = m_set->config.targetDevice;
    std::string posName = m_set->config.targetPositionName;

    // Update in-memory position
    PositionStruct corrected;
    corrected.x = m_correctedX;
    corrected.y = m_correctedY;
    corrected.z = m_correctedZ;
    corrected.u = m_correctedU;
    corrected.v = m_correctedV;
    corrected.w = m_correctedW;

    m_context->motionManager->AddNamedPosition(device, posName, corrected);

    // Save to config file
    std::string localPath = "config/motor_devices.local.json";
    std::string basePath = "config/motor_devices.json";
    std::string errMsg;
    bool saved = MotionJsonLoader::SavePositionToFile(
        localPath, basePath, device, posName, corrected, true, errMsg);

    if (saved) {
        // Update set target position data
        m_set->config.targetX = m_correctedX;
        m_set->config.targetY = m_correctedY;
        m_set->config.targetZ = m_correctedZ;
        m_set->config.targetU = m_correctedU;
        m_set->config.targetV = m_correctedV;
        m_set->config.targetW = m_correctedW;

        QString deg = QString(QChar(0x00B0));
        QString savedMsg = m_reviewLabel->text();
        savedMsg += QString("\n\nSAVED to \"%1\" on %2\n")
            .arg(QString::fromStdString(posName))
            .arg(QString::fromStdString(device));
        m_reviewLabel->setText(savedMsg);

        m_saveBtn->setEnabled(false);
        m_saveBtn->setText(QString("Saved to \"%1\"").arg(QString::fromStdString(posName)));
        m_moveToTargetBtn->setEnabled(true);
        m_moveToTargetBtn->setText(QString("Move %1 to \"%2\"")
            .arg(QString::fromStdString(device))
            .arg(QString::fromStdString(posName)));

        emit retraceCompleted(*m_set);

        std::cout << "[PlacementRetrace] Saved corrected position to " << device << "/" << posName << std::endl;
    } else {
        QMessageBox::warning(this, "Save Failed",
            "Could not save position: " + QString::fromStdString(errMsg));
    }
}

void PlacementRetraceWidget::onMoveToTarget()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    std::string device = m_set->config.targetDevice;
    std::string posName = m_set->config.targetPositionName;

    m_moveToTargetBtn->setEnabled(false);
    m_moveToTargetBtn->setText("Moving...");

    auto* mm = m_context->motionManager;
    runMotionAsync(
        [mm, device, posName]() {
            return mm->MoveToNamedPosition(device, posName, true);
        },
        [this, device, posName](bool ok) {
            if (ok) {
                m_moveToTargetBtn->setText(QString("Moved %1 to \"%2\"")
                    .arg(QString::fromStdString(device))
                    .arg(QString::fromStdString(posName)));
                std::cout << "[PlacementRetrace] Moved " << device << " to " << posName << std::endl;
            } else {
                m_moveToTargetBtn->setText("Move FAILED");
                m_moveToTargetBtn->setEnabled(true);
                QMessageBox::warning(this, "Move Failed",
                    QString("Failed to move %1 to \"%2\".")
                        .arg(QString::fromStdString(device))
                        .arg(QString::fromStdString(posName)));
            }
        });
}

void PlacementRetraceWidget::onReset()
{
    m_moveToTargetBtn->setEnabled(false);
    m_moveToTargetBtn->setText("Move to Target Position");
    m_saveBtn->setEnabled(true);
    QString targetName = m_set ? QString::fromStdString(m_set->config.targetPositionName) : "Target";
    m_saveBtn->setText(QString("Save to \"%1\"").arg(targetName));
    setState(Idle);
}

void PlacementRetraceWidget::onEnd()
{
    m_positionTimer->stop();
    setState(Idle);
    emit retraceEnded();
}

void PlacementRetraceWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_set) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_set->measureDevice);
    QString text = QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4);

    switch (m_state) {
    case WaitRefP1: m_refP1PositionLabel->setText(text); break;
    case WaitRefP2: m_refP2PositionLabel->setText(text); break;
    case WaitTgtP1: m_tgtP1PositionLabel->setText(text); break;
    case WaitTgtP2: m_tgtP2PositionLabel->setText(text); break;
    default: break;
    }
}

// TeachPlacementLineWidget.cpp - Two-point capture widget for placement correction
#include "TeachPlacementLineWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "CameraManager.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>
#include <filesystem>
#include <chrono>

TeachPlacementLineWidget::TeachPlacementLineWidget(const QString& lineName,
                                                   ApplicationContext* context,
                                                   QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_deviceName("gantry-main")
    , m_lineName(lineName)
    , m_positionTimer(new QTimer(this))
{
    m_line.name = lineName.toStdString();
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &TeachPlacementLineWidget::updatePositionDisplay);
}

void TeachPlacementLineWidget::SetDeviceName(const std::string& deviceName)
{
    m_deviceName = deviceName;
}

void TeachPlacementLineWidget::SetCameraId(const std::string& cameraId)
{
    m_cameraId = cameraId;
}

void TeachPlacementLineWidget::SetLine(const PlacementLinePoints& lp)
{
    m_line = lp;
    m_p1ExposureSpin->setValue(static_cast<int>(lp.p1Exposure));
    m_p2ExposureSpin->setValue(static_cast<int>(lp.p2Exposure));
    if (lp.valid) {
        setState(Captured);
    } else {
        setState(Idle);
    }
}

void TeachPlacementLineWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel(QString("Teach %1 Line").arg(m_lineName));
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        QString("Capture two XY points using %1 crosshair to define the %2 edge line.\n"
                "The angle of this line will be computed in the XY plane.")
            .arg("gantry-main").arg(m_lineName));
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    m_wizardStack = new QStackedWidget();

    QString blueBtnStyle =
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }";

    QString cancelBtnStyle =
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }";

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    QString spinStyle =
        "QSpinBox { background-color: #1a1f2e; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px; font-size: 13px; }"
        "QSpinBox::up-button, QSpinBox::down-button { background-color: #2d3e5f; border: none; }";

    // ========================================
    // Page 0: Idle
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_idleLabel = new QLabel(QString("No line captured for %1.").arg(m_lineName));
    m_idleLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_idleLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleLabel);

    idleLayout->addStretch();

    m_startTeachBtn = new QPushButton(QString("Teach %1 Line").arg(m_lineName));
    m_startTeachBtn->setFixedHeight(40);
    m_startTeachBtn->setStyleSheet(blueBtnStyle);
    connect(m_startTeachBtn, &QPushButton::clicked, this, [this]() {
        setState(TeachingP1);
    });
    idleLayout->addWidget(m_startTeachBtn);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Teaching P1
    // ========================================
    QWidget* p1Page = new QWidget();
    QVBoxLayout* p1Layout = new QVBoxLayout(p1Page);

    m_p1InstructionLabel = new QLabel(
        QString("Step 1/2: Jog gantry to %1 edge Point 1, then press Capture").arg(m_lineName));
    m_p1InstructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_p1InstructionLabel->setWordWrap(true);
    m_p1InstructionLabel->setAlignment(Qt::AlignCenter);
    p1Layout->addWidget(m_p1InstructionLabel);

    QGroupBox* p1PosGroup = new QGroupBox("Current Gantry Position");
    p1PosGroup->setStyleSheet(groupStyle);
    QVBoxLayout* p1PosLayout = new QVBoxLayout(p1PosGroup);
    m_p1PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_p1PositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_p1PositionLabel->setAlignment(Qt::AlignCenter);
    p1PosLayout->addWidget(m_p1PositionLabel);
    p1Layout->addWidget(p1PosGroup);

    QHBoxLayout* p1ExpRow = new QHBoxLayout();
    QLabel* p1ExpLabel = new QLabel("Exposure:");
    p1ExpLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    p1ExpRow->addWidget(p1ExpLabel);
    m_p1ExposureSpin = new QSpinBox();
    m_p1ExposureSpin->setRange(1, 100000);
    m_p1ExposureSpin->setValue(800);
    m_p1ExposureSpin->setSuffix(" us");
    m_p1ExposureSpin->setStyleSheet(spinStyle);
    p1ExpRow->addWidget(m_p1ExposureSpin);
    m_applyP1ExposureBtn = new QPushButton("Apply");
    m_applyP1ExposureBtn->setFixedSize(60, 28);
    m_applyP1ExposureBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "font-size: 11px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #f39c12; }");
    connect(m_applyP1ExposureBtn, &QPushButton::clicked, this, [this]() {
        applyCameraExposure(static_cast<double>(m_p1ExposureSpin->value()));
    });
    p1ExpRow->addWidget(m_applyP1ExposureBtn);
    p1Layout->addLayout(p1ExpRow);

    p1Layout->addStretch();

    QHBoxLayout* p1BtnRow = new QHBoxLayout();
    m_captureP1Btn = new QPushButton("Capture Point 1");
    m_captureP1Btn->setFixedHeight(40);
    m_captureP1Btn->setStyleSheet(blueBtnStyle);
    connect(m_captureP1Btn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onCaptureP1);
    p1BtnRow->addWidget(m_captureP1Btn);

    m_cancelP1Btn = new QPushButton("Cancel");
    m_cancelP1Btn->setFixedHeight(40);
    m_cancelP1Btn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelP1Btn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(m_line.valid ? Captured : Idle);
    });
    p1BtnRow->addWidget(m_cancelP1Btn);
    p1Layout->addLayout(p1BtnRow);

    m_wizardStack->addWidget(p1Page);  // index 1

    // ========================================
    // Page 2: Teaching P2
    // ========================================
    QWidget* p2Page = new QWidget();
    QVBoxLayout* p2Layout = new QVBoxLayout(p2Page);

    m_p2InstructionLabel = new QLabel(
        QString("Step 2/2: Jog gantry to %1 edge Point 2, then press Capture").arg(m_lineName));
    m_p2InstructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_p2InstructionLabel->setWordWrap(true);
    m_p2InstructionLabel->setAlignment(Qt::AlignCenter);
    p2Layout->addWidget(m_p2InstructionLabel);

    QGroupBox* p2PosGroup = new QGroupBox("Current Gantry Position");
    p2PosGroup->setStyleSheet(groupStyle);
    QVBoxLayout* p2PosLayout = new QVBoxLayout(p2PosGroup);
    m_p2PositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_p2PositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_p2PositionLabel->setAlignment(Qt::AlignCenter);
    p2PosLayout->addWidget(m_p2PositionLabel);
    p2Layout->addWidget(p2PosGroup);

    QHBoxLayout* p2ExpRow = new QHBoxLayout();
    QLabel* p2ExpLabel = new QLabel("Exposure:");
    p2ExpLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    p2ExpRow->addWidget(p2ExpLabel);
    m_p2ExposureSpin = new QSpinBox();
    m_p2ExposureSpin->setRange(1, 100000);
    m_p2ExposureSpin->setValue(800);
    m_p2ExposureSpin->setSuffix(" us");
    m_p2ExposureSpin->setStyleSheet(spinStyle);
    p2ExpRow->addWidget(m_p2ExposureSpin);
    m_applyP2ExposureBtn = new QPushButton("Apply");
    m_applyP2ExposureBtn->setFixedSize(60, 28);
    m_applyP2ExposureBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "font-size: 11px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #f39c12; }");
    connect(m_applyP2ExposureBtn, &QPushButton::clicked, this, [this]() {
        applyCameraExposure(static_cast<double>(m_p2ExposureSpin->value()));
    });
    p2ExpRow->addWidget(m_applyP2ExposureBtn);
    p2Layout->addLayout(p2ExpRow);

    p2Layout->addStretch();

    QHBoxLayout* p2BtnRow = new QHBoxLayout();
    m_captureP2Btn = new QPushButton("Capture Point 2");
    m_captureP2Btn->setFixedHeight(40);
    m_captureP2Btn->setStyleSheet(blueBtnStyle);
    connect(m_captureP2Btn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onCaptureP2);
    p2BtnRow->addWidget(m_captureP2Btn);

    m_cancelP2Btn = new QPushButton("Cancel");
    m_cancelP2Btn->setFixedHeight(40);
    m_cancelP2Btn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelP2Btn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(m_line.valid ? Captured : Idle);
    });
    p2BtnRow->addWidget(m_cancelP2Btn);
    p2Layout->addLayout(p2BtnRow);

    m_wizardStack->addWidget(p2Page);  // index 2

    // ========================================
    // Page 3: Captured
    // ========================================
    QWidget* capturedPage = new QWidget();
    QVBoxLayout* capturedLayout = new QVBoxLayout(capturedPage);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #27ae60; border-radius: 6px;");
    m_summaryLabel->setWordWrap(true);
    capturedLayout->addWidget(m_summaryLabel);

    capturedLayout->addStretch();

    m_reteachBtn = new QPushButton("Re-teach");
    m_reteachBtn->setFixedHeight(40);
    m_reteachBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #f39c12; }");
    connect(m_reteachBtn, &QPushButton::clicked, this, &TeachPlacementLineWidget::onReteach);
    capturedLayout->addWidget(m_reteachBtn);

    m_wizardStack->addWidget(capturedPage);  // index 3

    mainLayout->addWidget(m_wizardStack, 1);
}

void TeachPlacementLineWidget::setState(WizardState state)
{
    m_state = state;

    switch (state) {
    case Idle:
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        if (m_line.valid) {
            m_idleLabel->setText(QString(
                "%1 Line:\n"
                "  P1: X=%2  Y=%3\n"
                "  P2: X=%4  Y=%5\n"
                "  Angle: %6%7")
                .arg(m_lineName)
                .arg(m_line.p1x, 0, 'f', 4).arg(m_line.p1y, 0, 'f', 4)
                .arg(m_line.p2x, 0, 'f', 4).arg(m_line.p2y, 0, 'f', 4)
                .arg(m_line.angleDeg, 0, 'f', 3)
                .arg(QChar(0x00B0)));
        } else {
            m_idleLabel->setText(QString("No line captured for %1.").arg(m_lineName));
        }
        break;

    case TeachingP1:
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer->start(100);
        break;

    case TeachingP2:
        m_wizardStack->setCurrentIndex(2);
        m_positionTimer->start(100);
        break;

    case Captured: {
        m_wizardStack->setCurrentIndex(3);
        m_positionTimer->stop();
        double len = PlacementCorrectionMath::LineLength(m_line.p1x, m_line.p1y, m_line.p2x, m_line.p2y);
        m_summaryLabel->setText(QString(
            "%1 Line Captured\n\n"
            "  P1: X=%2  Y=%3  Z=%4\n"
            "  P2: X=%5  Y=%6  Z=%7\n\n"
            "  Line Length: %8 mm\n"
            "  Line Angle:  %9%10")
            .arg(m_lineName)
            .arg(m_line.p1x, 0, 'f', 4).arg(m_line.p1y, 0, 'f', 4).arg(m_line.p1z, 0, 'f', 4)
            .arg(m_line.p2x, 0, 'f', 4).arg(m_line.p2y, 0, 'f', 4).arg(m_line.p2z, 0, 'f', 4)
            .arg(len, 0, 'f', 3)
            .arg(m_line.angleDeg, 0, 'f', 3)
            .arg(QChar(0x00B0)));
        break;
    }
    }
}

void TeachPlacementLineWidget::onCaptureP1()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);

    m_line.p1x = pos.x;
    m_line.p1y = pos.y;
    m_line.p1z = pos.z;
    m_line.p1Exposure = static_cast<double>(m_p1ExposureSpin->value());
    m_line.p1ImageFile = capturePointImage("P1");

    std::cout << "[TeachPlacementLine] " << m_lineName.toStdString()
              << " P1: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z
              << " exp=" << m_line.p1Exposure << "us" << std::endl;

    // Pre-fill P2 exposure with same value as P1
    m_p2ExposureSpin->setValue(m_p1ExposureSpin->value());

    setState(TeachingP2);
}

void TeachPlacementLineWidget::onCaptureP2()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);

    m_line.p2x = pos.x;
    m_line.p2y = pos.y;
    m_line.p2z = pos.z;
    m_line.p2Exposure = static_cast<double>(m_p2ExposureSpin->value());
    m_line.p2ImageFile = capturePointImage("P2");
    m_line.valid = true;

    // Compute line angle
    m_line.angleDeg = PlacementCorrectionMath::ComputeLineAngle(
        m_line.p1x, m_line.p1y, m_line.p2x, m_line.p2y);

    std::cout << "[TeachPlacementLine] " << m_lineName.toStdString()
              << " P2: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z
              << " Angle=" << m_line.angleDeg << " deg" << std::endl;

    setState(Captured);
    emit lineCaptured(m_line);
}

void TeachPlacementLineWidget::onReteach()
{
    setState(TeachingP1);
}

void TeachPlacementLineWidget::applyCameraExposure(double exposureUs)
{
    if (!m_context || !m_context->cameraManager || m_cameraId.empty()) return;

    ICameraHardware::ExposureSettings expSettings;
    expSettings.auto_exposure = false;
    expSettings.auto_gain = false;
    expSettings.gain = 0.0;
    expSettings.exposure_time = exposureUs;

    bool ok = m_context->cameraManager->ApplyExposureSettings(m_cameraId, expSettings);
    std::cout << "[TeachPlacementLine] Camera \"" << m_cameraId
              << "\" exposure=" << exposureUs << "us "
              << (ok ? "OK" : "FAILED") << std::endl;
}

std::string TeachPlacementLineWidget::capturePointImage(const std::string& pointLabel)
{
    if (!m_context || !m_context->cameraManager || m_cameraId.empty()) {
        return "";
    }

    // Build image directory: config/<product>/images/trainer/
    std::string imageDir = "config/images/trainer";
    if (m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        std::filesystem::path productPath(
            m_context->productPositionManager->GetCurrentProductPath());
        imageDir = (productPath.parent_path() / "images" / "trainer").string();
    }
    std::filesystem::create_directories(imageDir);

    // Generate unique filename
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string filename = m_lineName.toStdString() + "_" + pointLabel + "_"
                         + std::to_string(ms) + ".bmp";

    m_context->cameraManager->SetImageOutputDirectory(imageDir);
    bool ok = m_context->cameraManager->CaptureImage(m_cameraId, filename);
    if (ok) {
        std::cout << "[TeachPlacementLine] Captured image: " << imageDir << "/" << filename << std::endl;
        return filename;
    }
    std::cerr << "[TeachPlacementLine] Failed to capture image for " << pointLabel << std::endl;
    return "";
}

void TeachPlacementLineWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager) {
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);
    QString text = QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4);

    if (m_state == TeachingP1)
        m_p1PositionLabel->setText(text);
    else if (m_state == TeachingP2)
        m_p2PositionLabel->setText(text);
}

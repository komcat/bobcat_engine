// EdgeTeachWidget.cpp - Two-point XYZ capture widget for Edge Transfer Trainer
#include "EdgeTeachWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "CameraManager.h"
#include "product_position_manager.h"
#include "SequenceEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QMetaObject>
#include <chrono>
#include <filesystem>
#include <iostream>

EdgeTeachWidget::EdgeTeachWidget(const QString& edgeName,
                                 ApplicationContext* context,
                                 QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_deviceName("gantry-main")
    , m_edgeName(edgeName)
    , m_positionTimer(new QTimer(this))
{
    m_edge.name = edgeName.toStdString();
    setupUI();
    connect(m_positionTimer, &QTimer::timeout,
            this, &EdgeTeachWidget::updatePositionDisplay);
}

void EdgeTeachWidget::SetDeviceName(const std::string& deviceName)
{
    m_deviceName = deviceName;
}

void EdgeTeachWidget::SetCameraId(const std::string& cameraId)
{
    m_cameraId = cameraId;
}

void EdgeTeachWidget::SetEdge(const EdgePoints& e)
{
    m_edge = e;
    setState(e.valid ? Captured : Idle);
}

void EdgeTeachWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel(QString("Teach %1 Edge").arg(m_edgeName));
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        QString("Capture two gantry-main XYZ points to define the %1 edge.\n"
                "XY distance, Z delta, and angles are computed in EdgeTransferMath.")
            .arg(m_edgeName));
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    m_wizardStack = new QStackedWidget();

    const QString blueBtnStyle =
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }";

    const QString cancelBtnStyle =
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }";

    const QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    // ---- Page 0: Idle ----
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_idleLabel = new QLabel(QString("No points captured for %1 edge.").arg(m_edgeName));
    m_idleLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_idleLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleLabel);
    idleLayout->addStretch();

    m_startTeachBtn = new QPushButton(QString("Teach %1 Edge").arg(m_edgeName));
    m_startTeachBtn->setFixedHeight(40);
    m_startTeachBtn->setStyleSheet(blueBtnStyle);
    connect(m_startTeachBtn, &QPushButton::clicked, this, [this]() {
        setState(TeachingP1);
    });
    idleLayout->addWidget(m_startTeachBtn);
    m_wizardStack->addWidget(idlePage);

    // ---- Page 1: Teaching P1 ----
    QWidget* p1Page = new QWidget();
    QVBoxLayout* p1Layout = new QVBoxLayout(p1Page);

    m_p1InstructionLabel = new QLabel(
        QString("Step 1/2: Jog gantry to %1 Point 1, then press Capture")
            .arg(m_edgeName));
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
    m_p1PositionLabel->setStyleSheet(
        "font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_p1PositionLabel->setAlignment(Qt::AlignCenter);
    p1PosLayout->addWidget(m_p1PositionLabel);
    p1Layout->addWidget(p1PosGroup);
    p1Layout->addStretch();

    const QString navBtnStyle =
        "QPushButton { background-color: #16a085; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #1abc9c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    m_autoNavP1BtnTeach = new QPushButton("Auto-Nav to previously taught P1  (0.5 mm/s)");
    m_autoNavP1BtnTeach->setFixedHeight(36);
    m_autoNavP1BtnTeach->setStyleSheet(navBtnStyle);
    m_autoNavP1BtnTeach->setToolTip(
        "Moves gantry-main to the previously captured P1 position at safe velocity.");
    connect(m_autoNavP1BtnTeach, &QPushButton::clicked, this, &EdgeTeachWidget::onAutoNavP1);
    p1Layout->addWidget(m_autoNavP1BtnTeach);

    QHBoxLayout* p1BtnRow = new QHBoxLayout();
    m_captureP1Btn = new QPushButton("Capture Point 1");
    m_captureP1Btn->setFixedHeight(40);
    m_captureP1Btn->setStyleSheet(blueBtnStyle);
    connect(m_captureP1Btn, &QPushButton::clicked, this, &EdgeTeachWidget::onCaptureP1);
    p1BtnRow->addWidget(m_captureP1Btn);

    m_cancelP1Btn = new QPushButton("Cancel");
    m_cancelP1Btn->setFixedHeight(40);
    m_cancelP1Btn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelP1Btn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(m_edge.valid ? Captured : Idle);
    });
    p1BtnRow->addWidget(m_cancelP1Btn);
    p1Layout->addLayout(p1BtnRow);

    m_wizardStack->addWidget(p1Page);

    // ---- Page 2: Teaching P2 ----
    QWidget* p2Page = new QWidget();
    QVBoxLayout* p2Layout = new QVBoxLayout(p2Page);

    m_p2InstructionLabel = new QLabel(
        QString("Step 2/2: Jog gantry to %1 Point 2, then press Capture")
            .arg(m_edgeName));
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
    m_p2PositionLabel->setStyleSheet(
        "font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_p2PositionLabel->setAlignment(Qt::AlignCenter);
    p2PosLayout->addWidget(m_p2PositionLabel);
    p2Layout->addWidget(p2PosGroup);
    p2Layout->addStretch();

    m_autoNavP2BtnTeach = new QPushButton("Auto-Nav to previously taught P2  (0.5 mm/s)");
    m_autoNavP2BtnTeach->setFixedHeight(36);
    m_autoNavP2BtnTeach->setStyleSheet(navBtnStyle);
    m_autoNavP2BtnTeach->setToolTip(
        "Moves gantry-main to the previously captured P2 position at safe velocity.");
    connect(m_autoNavP2BtnTeach, &QPushButton::clicked, this, &EdgeTeachWidget::onAutoNavP2);
    p2Layout->addWidget(m_autoNavP2BtnTeach);

    QHBoxLayout* p2BtnRow = new QHBoxLayout();
    m_captureP2Btn = new QPushButton("Capture Point 2");
    m_captureP2Btn->setFixedHeight(40);
    m_captureP2Btn->setStyleSheet(blueBtnStyle);
    connect(m_captureP2Btn, &QPushButton::clicked, this, &EdgeTeachWidget::onCaptureP2);
    p2BtnRow->addWidget(m_captureP2Btn);

    m_cancelP2Btn = new QPushButton("Cancel");
    m_cancelP2Btn->setFixedHeight(40);
    m_cancelP2Btn->setStyleSheet(cancelBtnStyle);
    connect(m_cancelP2Btn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(m_edge.valid ? Captured : Idle);
    });
    p2BtnRow->addWidget(m_cancelP2Btn);
    p2Layout->addLayout(p2BtnRow);

    m_wizardStack->addWidget(p2Page);

    // ---- Page 3: Captured ----
    QWidget* capturedPage = new QWidget();
    QVBoxLayout* capturedLayout = new QVBoxLayout(capturedPage);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #27ae60; border-radius: 6px; "
        "font-family: monospace;");
    m_summaryLabel->setWordWrap(true);
    capturedLayout->addWidget(m_summaryLabel);
    capturedLayout->addStretch();

    QHBoxLayout* capNavRow = new QHBoxLayout();
    m_autoNavP1BtnCaptured = new QPushButton("Auto-Nav to P1  (0.5 mm/s)");
    m_autoNavP1BtnCaptured->setFixedHeight(36);
    m_autoNavP1BtnCaptured->setStyleSheet(navBtnStyle);
    connect(m_autoNavP1BtnCaptured, &QPushButton::clicked,
            this, &EdgeTeachWidget::onAutoNavP1);
    capNavRow->addWidget(m_autoNavP1BtnCaptured);

    m_autoNavP2BtnCaptured = new QPushButton("Auto-Nav to P2  (0.5 mm/s)");
    m_autoNavP2BtnCaptured->setFixedHeight(36);
    m_autoNavP2BtnCaptured->setStyleSheet(navBtnStyle);
    connect(m_autoNavP2BtnCaptured, &QPushButton::clicked,
            this, &EdgeTeachWidget::onAutoNavP2);
    capNavRow->addWidget(m_autoNavP2BtnCaptured);
    capturedLayout->addLayout(capNavRow);

    m_reteachBtn = new QPushButton("Re-teach");
    m_reteachBtn->setFixedHeight(40);
    m_reteachBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #f39c12; }");
    connect(m_reteachBtn, &QPushButton::clicked, this, &EdgeTeachWidget::onReteach);
    capturedLayout->addWidget(m_reteachBtn);

    m_wizardStack->addWidget(capturedPage);

    mainLayout->addWidget(m_wizardStack, 1);

    // ---- Global Stop button (hidden until a nav is running) ----
    m_stopNavBtn = new QPushButton("STOP Auto-Nav  (EmergencyStop gantry-main)");
    m_stopNavBtn->setFixedHeight(36);
    m_stopNavBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 16px; font-size: 13px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }");
    m_stopNavBtn->setToolTip(
        "Stops the current Auto-Nav move by emergency-stopping gantry-main.");
    m_stopNavBtn->setVisible(false);
    connect(m_stopNavBtn, &QPushButton::clicked,
            this, &EdgeTeachWidget::onStopAutoNav);
    mainLayout->addWidget(m_stopNavBtn);
}

void EdgeTeachWidget::setState(WizardState state)
{
    m_state = state;

    // Auto-Nav buttons are only useful once an edge has been previously
    // captured (so there is a point to navigate back to).
    const bool navEnabled = m_edge.valid && m_navActiveDevice.empty();
    if (m_autoNavP1BtnTeach)    m_autoNavP1BtnTeach->setEnabled(navEnabled);
    if (m_autoNavP2BtnTeach)    m_autoNavP2BtnTeach->setEnabled(navEnabled);
    if (m_autoNavP1BtnCaptured) m_autoNavP1BtnCaptured->setEnabled(navEnabled);
    if (m_autoNavP2BtnCaptured) m_autoNavP2BtnCaptured->setEnabled(navEnabled);

    switch (state) {
    case Idle:
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        if (m_edge.valid) {
            m_idleLabel->setText(QString(
                "%1 edge (re-teach to replace):\n"
                "  P1: X=%2  Y=%3  Z=%4\n"
                "  P2: X=%5  Y=%6  Z=%7")
                .arg(m_edgeName)
                .arg(m_edge.p1x, 0, 'f', 4).arg(m_edge.p1y, 0, 'f', 4).arg(m_edge.p1z, 0, 'f', 4)
                .arg(m_edge.p2x, 0, 'f', 4).arg(m_edge.p2y, 0, 'f', 4).arg(m_edge.p2z, 0, 'f', 4));
        } else {
            m_idleLabel->setText(QString("No points captured for %1 edge.").arg(m_edgeName));
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
        m_summaryLabel->setText(QString(
            "%1 Edge Captured\n\n"
            "  P1: X=%2  Y=%3  Z=%4\n"
            "  P2: X=%5  Y=%6  Z=%7\n\n"
            "  XY distance : %8 mm\n"
            "  Z delta     : %9 mm\n"
            "  Angle to +X : %10%11\n"
            "  Angle to +Y : %12%13\n"
            "  Vertical tilt : %14%15")
            .arg(m_edgeName)
            .arg(m_edge.p1x, 0, 'f', 4).arg(m_edge.p1y, 0, 'f', 4).arg(m_edge.p1z, 0, 'f', 4)
            .arg(m_edge.p2x, 0, 'f', 4).arg(m_edge.p2y, 0, 'f', 4).arg(m_edge.p2z, 0, 'f', 4)
            .arg(m_edge.xyDistance, 0, 'f', 4)
            .arg(m_edge.zDelta, 0, 'f', 4)
            .arg(m_edge.angleToX, 0, 'f', 4).arg(QChar(0x00B0))
            .arg(m_edge.angleToY, 0, 'f', 4).arg(QChar(0x00B0))
            .arg(m_edge.verticalTilt, 0, 'f', 4).arg(QChar(0x00B0)));
        break;
    }
    }
}

void EdgeTeachWidget::onCaptureP1()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }
    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);
    m_edge.p1x = pos.x;
    m_edge.p1y = pos.y;
    m_edge.p1z = pos.z;
    m_edge.p1ImageFile = capturePointImage("P1");
    std::cout << "[EdgeTeach] " << m_edgeName.toStdString()
              << " P1: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z
              << " img=" << m_edge.p1ImageFile << std::endl;
    setState(TeachingP2);
}

void EdgeTeachWidget::onCaptureP2()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }
    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);
    m_edge.p2x = pos.x;
    m_edge.p2y = pos.y;
    m_edge.p2z = pos.z;
    m_edge.p2ImageFile = capturePointImage("P2");
    m_edge.valid = true;
    EdgeTransferMath::Recalc(m_edge);

    std::cout << "[EdgeTeach] " << m_edgeName.toStdString()
              << " P2: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z
              << " | angleToX=" << m_edge.angleToX
              << " tilt=" << m_edge.verticalTilt << std::endl;

    setState(Captured);
    emit edgeCaptured(m_edge);
}

void EdgeTeachWidget::onReteach()
{
    setState(TeachingP1);
}

void EdgeTeachWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager) return;

    auto pos = m_context->motionManager->GetCurrentPosition(m_deviceName);
    QString text = QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4);

    if (m_state == TeachingP1)      m_p1PositionLabel->setText(text);
    else if (m_state == TeachingP2) m_p2PositionLabel->setText(text);
}

std::string EdgeTeachWidget::capturePointImage(const std::string& pointLabel)
{
    if (!m_context || !m_context->cameraManager || m_cameraId.empty()) {
        return "";
    }

    // Build image directory: <product>/images/trainer  (or config/images/trainer)
    std::string imageDir = "config/images/trainer";
    if (m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        std::filesystem::path productPath(
            m_context->productPositionManager->GetCurrentProductPath());
        imageDir = (productPath.parent_path() / "images" / "trainer").string();
    }
    std::filesystem::create_directories(imageDir);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string filename = "edge_" + m_edgeName.toStdString() + "_" + pointLabel
                         + "_" + std::to_string(ms) + ".bmp";

    m_context->cameraManager->SetImageOutputDirectory(imageDir);
    bool ok = m_context->cameraManager->CaptureImage(m_cameraId, filename);
    if (ok) {
        std::cout << "[EdgeTeach] Captured image: " << imageDir << "/"
                  << filename << std::endl;
        return filename;
    }
    std::cerr << "[EdgeTeach] Failed to capture image for " << pointLabel
              << " (camera=" << m_cameraId << ")" << std::endl;
    return "";
}

void EdgeTeachWidget::onStopAutoNav()
{
    if (!m_context || !m_context->motionManager) return;
    if (m_navActiveDevice.empty()) return;   // nothing to stop
    const std::string device = m_navActiveDevice;
    bool ok = m_context->motionManager->EmergencyStop(device);
    std::cout << "[EdgeTeach] STOP Auto-Nav on " << device
              << "  EmergencyStop=" << (ok ? "OK" : "FAIL") << std::endl;
    // The worker thread's MoveToAbs will unblock (motion halted). It will
    // then run the velocity-restore + UI callback, which clears the active
    // device and re-enables the nav buttons.
}

void EdgeTeachWidget::onAutoNavP1()
{
    if (!m_edge.valid) return;
    startAutoNavAsync(m_edge.p1x, m_edge.p1y, m_edge.p1z, "P1");
}

void EdgeTeachWidget::onAutoNavP2()
{
    if (!m_edge.valid) return;
    startAutoNavAsync(m_edge.p2x, m_edge.p2y, m_edge.p2z, "P2");
}

// Background safe-mover for Auto-Nav: SetSystemVelocity(0.5) -> MoveToAbs ->
// SetSystemVelocity(50). Disables all auto-nav buttons while running.
// Marshals the completion back to the UI thread via QMetaObject::invokeMethod.
void EdgeTeachWidget::startAutoNavAsync(double tx, double ty, double tz,
                                        const QString& which)
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Auto-Nav", "Motion manager not available.");
        return;
    }
    if (m_context->sequenceEngine && m_context->sequenceEngine->IsExecuting()) {
        QMessageBox::warning(this, "Auto-Nav Blocked",
            "A recipe is currently running. Stop the program before Auto-Nav.");
        return;
    }
    if (!m_navActiveDevice.empty()) return;   // double-click guard

    constexpr double kSafeVel    = 0.5;  // mm/s during the move (per spec)
    constexpr double kDefaultVel = 50.0; // mm/s restored after

    auto setNavEnabled = [this](bool en) {
        const bool e = en && m_edge.valid;
        if (m_autoNavP1BtnTeach)    m_autoNavP1BtnTeach->setEnabled(e);
        if (m_autoNavP2BtnTeach)    m_autoNavP2BtnTeach->setEnabled(e);
        if (m_autoNavP1BtnCaptured) m_autoNavP1BtnCaptured->setEnabled(e);
        if (m_autoNavP2BtnCaptured) m_autoNavP2BtnCaptured->setEnabled(e);
        // Stop button is the inverse of "nav enabled": shown only when
        // a nav is currently in flight.
        if (m_stopNavBtn) {
            m_stopNavBtn->setVisible(!en);
            m_stopNavBtn->setEnabled(!en);
        }
    };
    setNavEnabled(false);
    m_navActiveDevice = m_deviceName;

    auto* mm = m_context->motionManager;
    std::string device = m_deviceName;
    std::string edgeName = m_edgeName.toStdString();
    QString whichCopy = which;

    m_navFuture = std::async(std::launch::async,
        [this, mm, device, tx, ty, tz, whichCopy, edgeName, setNavEnabled]() {
        bool velSlowOk    = mm->SetSystemVelocity(device, kSafeVel);
        bool moveOk       = mm->MoveToAbs(device, tx, ty, tz);
        bool velRestoreOk = mm->SetSystemVelocity(device, kDefaultVel);

        std::cout << "[EdgeTeach] " << edgeName
                  << " Auto-Nav to " << whichCopy.toStdString()
                  << " (" << tx << ", " << ty << ", " << tz << ")"
                  << "  velSlow="    << (velSlowOk ? "OK" : "FAIL")
                  << "  move="       << (moveOk ? "OK" : "FAIL")
                  << "  velRestore=" << (velRestoreOk ? "OK" : "FAIL") << std::endl;

        QMetaObject::invokeMethod(this,
            [this, moveOk, whichCopy, setNavEnabled]() {
            m_navActiveDevice.clear();
            setNavEnabled(true);
            if (!moveOk) {
                QMessageBox::warning(this, "Auto-Nav Failed",
                    QString("MoveToAbs to %1 failed.").arg(whichCopy));
            }
        }, Qt::QueuedConnection);
    });
}

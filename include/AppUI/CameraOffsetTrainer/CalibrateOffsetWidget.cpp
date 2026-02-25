// CalibrateOffsetWidget.cpp - 6-step calibration wizard for camera offset
#include "CalibrateOffsetWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QMetaObject>
#include <QApplication>
#include <iostream>

#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include <nlohmann/json.hpp>
#include <fstream>
#endif

CalibrateOffsetWidget::CalibrateOffsetWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_calibrationSet(nullptr)
    , m_state(Idle)
    , m_wizardStack(nullptr)
    , m_idleSummaryLabel(nullptr)
    , m_startBtn(nullptr)
    , m_stepLabel(nullptr)
    , m_instructionLabel(nullptr)
    , m_positionLabel(nullptr)
    , m_visionProfileCombo(nullptr)
    , m_refreshProfilesBtn(nullptr)
    , m_detectCenterBtn(nullptr)
    , m_confirmBtn(nullptr)
    , m_cancelBtn(nullptr)
    , m_summaryLabel(nullptr)
    , m_doneBtn(nullptr)
{
    setupUI();

    connect(&m_positionTimer, &QTimer::timeout, this, &CalibrateOffsetWidget::updatePositionDisplay);
}

void CalibrateOffsetWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    QLabel* title = new QLabel("Calibrate Camera Offset");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff; margin-bottom: 10px;");
    mainLayout->addWidget(title);

    m_wizardStack = new QStackedWidget();

    QString btnStyle =
        "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 6px 16px; font-size: 12px; }"
        "QPushButton:hover { background-color: #3d5a8c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    QString greenBtnStyle =
        "QPushButton { background-color: #2d6b3f; color: white; border: 1px solid #3d8c5a; "
        "border-radius: 3px; padding: 6px 16px; font-size: 12px; }"
        "QPushButton:hover { background-color: #3d8c5a; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    // ========================================
    // PAGE 0: Idle - summary + start button
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);
    idleLayout->setAlignment(Qt::AlignTop);

    m_idleSummaryLabel = new QLabel("No calibration data.\nClick 'Start Calibration' to begin.");
    m_idleSummaryLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_idleSummaryLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleSummaryLabel);

    idleLayout->addStretch();

    m_startBtn = new QPushButton("Start Calibration");
    m_startBtn->setFixedHeight(40);
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    connect(m_startBtn, &QPushButton::clicked, this, &CalibrateOffsetWidget::onStartCalibration);
    idleLayout->addWidget(m_startBtn);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // PAGE 1: Active - shared for steps 1-6
    // ========================================
    QWidget* activePage = new QWidget();
    QVBoxLayout* activeLayout = new QVBoxLayout(activePage);

    m_stepLabel = new QLabel("Step 1/6");
    m_stepLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_stepLabel->setWordWrap(true);
    m_stepLabel->setAlignment(Qt::AlignCenter);
    activeLayout->addWidget(m_stepLabel);

    m_instructionLabel = new QLabel("Instruction text");
    m_instructionLabel->setStyleSheet("color: #ccc; font-size: 13px; padding: 8px 0;");
    m_instructionLabel->setWordWrap(true);
    activeLayout->addWidget(m_instructionLabel);

    // Live position display
    QGroupBox* posGroup = new QGroupBox("Current Position");
    posGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* posLayout = new QVBoxLayout(posGroup);
    m_positionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_positionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_positionLabel->setAlignment(Qt::AlignCenter);
    posLayout->addWidget(m_positionLabel);
    activeLayout->addWidget(posGroup);

    // Vision profile section (same layout as TeachReferenceWidget)
    QGroupBox* visionGroup = new QGroupBox("Vision Detect (Optional)");
    visionGroup->setStyleSheet(posGroup->styleSheet());
    QVBoxLayout* visionLayout = new QVBoxLayout(visionGroup);

    QHBoxLayout* profileRow = new QHBoxLayout();
    QLabel* profileLabel = new QLabel("Profile:");
    profileLabel->setStyleSheet("color: #aaa; font-size: 12px;");
    profileRow->addWidget(profileLabel);

    m_visionProfileCombo = new QComboBox();
    m_visionProfileCombo->setStyleSheet(
        "QComboBox { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #2d3e5f; color: white; selection-background-color: #3d5a8c; }"
    );
    m_visionProfileCombo->setMinimumWidth(200);
    profileRow->addWidget(m_visionProfileCombo, 1);

    m_refreshProfilesBtn = new QPushButton("R");
    m_refreshProfilesBtn->setFixedSize(28, 28);
    m_refreshProfilesBtn->setToolTip("Refresh profile list");
    m_refreshProfilesBtn->setStyleSheet(
        "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3d5a8c; }"
    );
    connect(m_refreshProfilesBtn, &QPushButton::clicked, this, &CalibrateOffsetWidget::refreshVisionProfiles);
    profileRow->addWidget(m_refreshProfilesBtn);
    visionLayout->addLayout(profileRow);

    m_detectCenterBtn = new QPushButton("Detect && Center");
    m_detectCenterBtn->setFixedHeight(36);
    m_detectCenterBtn->setStyleSheet(
        "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
        "font-size: 13px; border-radius: 4px; padding: 6px 16px; }"
        "QPushButton:hover { background-color: #9b59b6; }"
        "QPushButton:disabled { background-color: #4a2560; color: #888; }"
    );
    m_detectCenterBtn->setToolTip("Execute vision profile to detect feature, then move gantry to center on it");
    connect(m_detectCenterBtn, &QPushButton::clicked, this, &CalibrateOffsetWidget::onDetectCenter);
    visionLayout->addWidget(m_detectCenterBtn);

    activeLayout->addWidget(visionGroup);

    activeLayout->addStretch();

    // Action buttons
    QHBoxLayout* actionRow = new QHBoxLayout();
    m_confirmBtn = new QPushButton("Capture Position");
    m_confirmBtn->setFixedHeight(40);
    m_confirmBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    connect(m_confirmBtn, &QPushButton::clicked, this, &CalibrateOffsetWidget::onConfirm);
    actionRow->addWidget(m_confirmBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(40);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
    );
    connect(m_cancelBtn, &QPushButton::clicked, this, &CalibrateOffsetWidget::onCancel);
    actionRow->addWidget(m_cancelBtn);

    activeLayout->addLayout(actionRow);

    m_wizardStack->addWidget(activePage);  // index 1

    // ========================================
    // PAGE 2: Summary - final offset result
    // ========================================
    QWidget* summaryPage = new QWidget();
    QVBoxLayout* summaryLayout = new QVBoxLayout(summaryPage);

    m_summaryLabel = new QLabel("Offset: ...");
    m_summaryLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_summaryLabel->setWordWrap(true);
    summaryLayout->addWidget(m_summaryLabel);

    summaryLayout->addStretch();

    QHBoxLayout* summaryBtnRow = new QHBoxLayout();
    m_doneBtn = new QPushButton("Done");
    m_doneBtn->setFixedHeight(40);
    m_doneBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
    );
    connect(m_doneBtn, &QPushButton::clicked, this, &CalibrateOffsetWidget::onDone);
    summaryBtnRow->addWidget(m_doneBtn);
    summaryLayout->addLayout(summaryBtnRow);

    m_wizardStack->addWidget(summaryPage);  // index 2

    mainLayout->addWidget(m_wizardStack, 1);
}

void CalibrateOffsetWidget::SetCalibrationSet(CameraOffsetCalibrationSet* cs)
{
    m_calibrationSet = cs;
    refreshVisionProfiles();
    setState(Idle);
}

void CalibrateOffsetWidget::setState(CalibrationState state)
{
    m_state = state;

    switch (state) {
    case Idle: {
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer.stop();

        if (m_calibrationSet && m_calibrationSet->offsetValid) {
            QString info = QString(
                "Existing calibration data:\n\n"
                "Low-Mag Position:\n  X=%1  Y=%2  Z=%3\n\n"
                "High-Mag Position:\n  X=%4  Y=%5  Z=%6\n\n"
                "Offset (High - Low):\n  dX=%7  dY=%8  dZ=%9")
                .arg(m_calibrationSet->lowMagPosition.x, 0, 'f', 4)
                .arg(m_calibrationSet->lowMagPosition.y, 0, 'f', 4)
                .arg(m_calibrationSet->lowMagPosition.z, 0, 'f', 4)
                .arg(m_calibrationSet->highMagPosition.x, 0, 'f', 4)
                .arg(m_calibrationSet->highMagPosition.y, 0, 'f', 4)
                .arg(m_calibrationSet->highMagPosition.z, 0, 'f', 4)
                .arg(m_calibrationSet->offsetX, 0, 'f', 4)
                .arg(m_calibrationSet->offsetY, 0, 'f', 4)
                .arg(m_calibrationSet->offsetZ, 0, 'f', 4);

            // Show vision profile info if saved
            if (!m_calibrationSet->lowMagVisionProfilePath.empty()) {
                info += QString("\n\nLow-Mag Vision: %1")
                    .arg(QFileInfo(QString::fromStdString(m_calibrationSet->lowMagVisionProfilePath)).baseName());
            }
            if (!m_calibrationSet->highMagVisionProfilePath.empty()) {
                info += QString("\nHigh-Mag Vision: %1")
                    .arg(QFileInfo(QString::fromStdString(m_calibrationSet->highMagVisionProfilePath)).baseName());
            }

            info += "\n\nClick 'Start Calibration' to recalibrate.";
            m_idleSummaryLabel->setText(info);
        } else {
            m_idleSummaryLabel->setText("No calibration data.\nClick 'Start Calibration' to begin.");
        }
        break;
    }

    case NavigateToLowMag: {
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer.start(100);
        m_stepLabel->setText("Step 1/6: Navigate to Low-Mag Position");
        m_confirmBtn->setEnabled(false);
        m_detectCenterBtn->setEnabled(false);

        // If prior calibration exists, auto-navigate
        if (m_calibrationSet && m_calibrationSet->lowMagPosition.valid) {
            m_instructionLabel->setText("Navigating to previously saved low-mag position...");
            navigateToPosition(m_calibrationSet->lowMagPosition, SetCenterLowMag);
        } else {
            m_instructionLabel->setText("No prior low-mag position. Please jog the gantry manually\n"
                                        "to center the fiducial in the low-mag camera view.");
            setState(SetCenterLowMag);
        }
        break;
    }

    case SetCenterLowMag: {
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer.start(100);
        m_stepLabel->setText("Step 2/6: Center Low-Mag Camera on Fiducial");
        m_instructionLabel->setText(
            "Jog the gantry so the fiducial is centered in the low-mag camera view.\n"
            "Use 'Detect & Center' with a vision profile for auto-centering, or jog manually.\n"
            "When centered, click 'Capture Position'.");
        m_confirmBtn->setEnabled(true);
        m_detectCenterBtn->setEnabled(true);
        break;
    }

    case ConfirmLowMag: {
        // Capture current position
        if (m_context && m_context->motionManager) {
            auto pos = m_context->motionManager->GetCurrentPosition(
                m_calibrationSet ? m_calibrationSet->deviceName : "gantry-main");
            m_pendingLowMagPos.x = pos.x;
            m_pendingLowMagPos.y = pos.y;
            m_pendingLowMagPos.z = pos.z;
            m_pendingLowMagPos.valid = true;
        }
        std::cout << "[CalibrateOffset] Low-mag position captured: X=" << m_pendingLowMagPos.x
                  << " Y=" << m_pendingLowMagPos.y << " Z=" << m_pendingLowMagPos.z << std::endl;

        // Save the vision profile path for low-mag
        if (m_calibrationSet && m_visionProfileCombo->currentIndex() > 0) {
            m_calibrationSet->lowMagVisionProfilePath =
                m_visionProfileCombo->currentData().toString().toStdString();
        }

        // Proceed to high-mag navigation
        setState(NavigateToHighMag);
        break;
    }

    case NavigateToHighMag: {
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer.start(100);
        m_stepLabel->setText("Step 3/6: Navigate to High-Mag Position");
        m_confirmBtn->setEnabled(false);
        m_detectCenterBtn->setEnabled(false);

        // Reset vision profile combo for high-mag step
        m_visionProfileCombo->setCurrentIndex(0);

        if (m_calibrationSet && m_calibrationSet->highMagPosition.valid) {
            m_instructionLabel->setText("Navigating to previously saved high-mag position...");
            navigateToPosition(m_calibrationSet->highMagPosition, SetCenterHighMag);
        } else {
            m_instructionLabel->setText("No prior high-mag position. Please jog the gantry manually\n"
                                        "to center the fiducial in the high-mag camera view.");
            setState(SetCenterHighMag);
        }
        break;
    }

    case SetCenterHighMag: {
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer.start(100);
        m_stepLabel->setText("Step 4/6: Center High-Mag Camera on Fiducial");
        m_instructionLabel->setText(
            "Jog the gantry so the SAME fiducial is centered in the high-mag camera view.\n"
            "Use 'Detect & Center' with a vision profile for auto-centering, or jog manually.\n"
            "When centered, click 'Capture Position'.");
        m_confirmBtn->setEnabled(true);
        m_detectCenterBtn->setEnabled(true);
        break;
    }

    case ConfirmHighMag: {
        // Capture current position
        if (m_context && m_context->motionManager) {
            auto pos = m_context->motionManager->GetCurrentPosition(
                m_calibrationSet ? m_calibrationSet->deviceName : "gantry-main");
            m_pendingHighMagPos.x = pos.x;
            m_pendingHighMagPos.y = pos.y;
            m_pendingHighMagPos.z = pos.z;
            m_pendingHighMagPos.valid = true;
        }
        std::cout << "[CalibrateOffset] High-mag position captured: X=" << m_pendingHighMagPos.x
                  << " Y=" << m_pendingHighMagPos.y << " Z=" << m_pendingHighMagPos.z << std::endl;

        // Save the vision profile path for high-mag
        if (m_calibrationSet && m_visionProfileCombo->currentIndex() > 0) {
            m_calibrationSet->highMagVisionProfilePath =
                m_visionProfileCombo->currentData().toString().toStdString();
        }

        // Compute offset (high - low) and finalize
        if (m_calibrationSet) {
            m_calibrationSet->lowMagPosition = m_pendingLowMagPos;
            m_calibrationSet->highMagPosition = m_pendingHighMagPos;
            m_calibrationSet->offsetX = m_pendingHighMagPos.x - m_pendingLowMagPos.x;
            m_calibrationSet->offsetY = m_pendingHighMagPos.y - m_pendingLowMagPos.y;
            m_calibrationSet->offsetZ = m_pendingHighMagPos.z - m_pendingLowMagPos.z;
            m_calibrationSet->offsetValid = true;

            std::cout << "[CalibrateOffset] Offset computed: dX=" << m_calibrationSet->offsetX
                      << " dY=" << m_calibrationSet->offsetY
                      << " dZ=" << m_calibrationSet->offsetZ << std::endl;
        }

        setState(Summary);
        break;
    }

    case Summary: {
        m_wizardStack->setCurrentIndex(2);
        m_positionTimer.stop();

        if (m_calibrationSet) {
            m_summaryLabel->setText(QString(
                "Calibration Complete\n\n"
                "Low-Mag Position:\n  X=%1  Y=%2  Z=%3\n\n"
                "High-Mag Position:\n  X=%4  Y=%5  Z=%6\n\n"
                "Computed Offset (High - Low):\n"
                "  dX = %7\n  dY = %8\n  dZ = %9\n\n"
                "Calibration saved successfully.")
                .arg(m_pendingLowMagPos.x, 0, 'f', 4)
                .arg(m_pendingLowMagPos.y, 0, 'f', 4)
                .arg(m_pendingLowMagPos.z, 0, 'f', 4)
                .arg(m_pendingHighMagPos.x, 0, 'f', 4)
                .arg(m_pendingHighMagPos.y, 0, 'f', 4)
                .arg(m_pendingHighMagPos.z, 0, 'f', 4)
                .arg(m_calibrationSet->offsetX, 0, 'f', 4)
                .arg(m_calibrationSet->offsetY, 0, 'f', 4)
                .arg(m_calibrationSet->offsetZ, 0, 'f', 4));

            emit calibrationCompleted(*m_calibrationSet);
        }
        break;
    }
    }
}

void CalibrateOffsetWidget::navigateToPosition(const CameraPosition& pos, CalibrationState nextState)
{
    if (!m_context || !m_context->motionManager || !m_calibrationSet) {
        setState(nextState);
        return;
    }

    std::string deviceName = m_calibrationSet->deviceName;
    double targetX = pos.x;
    double targetY = pos.y;
    double targetZ = pos.z;

    m_moveFuture = std::async(std::launch::async, [this, deviceName, targetX, targetY, targetZ, nextState]() {
        bool success = m_context->motionManager->MoveToAbs(deviceName, targetX, targetY, targetZ);
        QMetaObject::invokeMethod(this, [this, nextState, success]() {
            if (success) {
                std::cout << "[CalibrateOffset] Navigation complete" << std::endl;
            } else {
                std::cout << "[CalibrateOffset] Navigation failed, proceeding to manual mode" << std::endl;
            }
            setState(nextState);
        }, Qt::QueuedConnection);
    });
}

void CalibrateOffsetWidget::onStartCalibration()
{
    if (!m_calibrationSet) {
        QMessageBox::warning(this, "Error", "No calibration set selected.");
        return;
    }

    m_pendingLowMagPos = CameraPosition();
    m_pendingHighMagPos = CameraPosition();

    // Reset vision profile combo
    refreshVisionProfiles();

    setState(NavigateToLowMag);
}

void CalibrateOffsetWidget::onConfirm()
{
    switch (m_state) {
    case SetCenterLowMag:
        setState(ConfirmLowMag);
        break;
    case SetCenterHighMag:
        setState(ConfirmHighMag);
        break;
    default:
        break;
    }
}

void CalibrateOffsetWidget::onDetectCenter()
{
#ifdef HAS_OPENCV
    if (!m_context || !m_context->motionManager || !m_calibrationSet) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    if (m_visionProfileCombo->currentIndex() <= 0) {
        QMessageBox::information(this, "No Profile",
            "Select a vision profile from the dropdown first.");
        return;
    }

    std::string profilePath = m_visionProfileCombo->currentData().toString().toStdString();
    std::string device = m_calibrationSet->deviceName;

    // Read camera name from profile JSON
    std::string cameraName;
    {
        std::ifstream file(profilePath);
        if (!file.is_open()) {
            QMessageBox::warning(this, "Error",
                "Cannot open vision profile: " + QString::fromStdString(profilePath));
            return;
        }
        nlohmann::json j;
        file >> j;
        file.close();
        if (j.contains("camera") && j["camera"].contains("cameraId")) {
            cameraName = j["camera"].value("cameraId", "");
        }
    }

    if (cameraName.empty()) {
        QMessageBox::warning(this, "No Camera",
            "Vision profile does not specify a camera ID.");
        return;
    }

    // Disable buttons during detection
    m_detectCenterBtn->setEnabled(false);
    m_detectCenterBtn->setText("Detecting...");
    m_confirmBtn->setEnabled(false);

    std::cout << "[CalibrateOffset] Detect & Center: profile=" << profilePath
              << " camera=" << cameraName << " device=" << device << std::endl;

    m_detectFuture = std::async(std::launch::async,
        [this, profilePath, cameraName, device]() {
        VisionCommandHandler visionHandler(m_context);
        SequenceContext ctx;
        std::string error;

        std::map<std::string, std::string> params;
        params["profile_path"] = profilePath;
        params["camera"] = cameraName;
        params["motion_device"] = device;
        params["calculate_world_position"] = "true";

        bool ok = visionHandler.ExecuteCommand(
            nullptr, "vision", "execute_profile", params, error, &ctx);

        if (!ok) {
            std::cout << "[CalibrateOffset] Vision execution error: " << error << std::endl;
            QMetaObject::invokeMethod(qApp, [this, error]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_confirmBtn->setEnabled(true);
                QMessageBox::warning(this, "Detection Failed",
                    QString::fromStdString("Vision command failed: " + error));
            }, Qt::QueuedConnection);
            return;
        }

        bool visionSuccess = ctx.GetBoolean("vision.result.success", false);
        if (!visionSuccess) {
            std::string visionError = ctx.GetString("vision.result.error", "Unknown error");
            std::cout << "[CalibrateOffset] Vision failed: " << visionError << std::endl;
            QMetaObject::invokeMethod(qApp, [this, visionError]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_confirmBtn->setEnabled(true);
                QMessageBox::warning(this, "Detection Failed",
                    QString::fromStdString("Vision result: " + visionError));
            }, Qt::QueuedConnection);
            return;
        }

        if (!ctx.HasValue("vision.detected.world_x_mm") ||
            !ctx.HasValue("vision.detected.world_y_mm")) {
            std::cout << "[CalibrateOffset] No feature detected (no world position)" << std::endl;
            QMetaObject::invokeMethod(qApp, [this]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_confirmBtn->setEnabled(true);
                QMessageBox::warning(this, "No Feature",
                    "Vision ran successfully but no feature center was detected.\n"
                    "Check that the profile returns circle/point results.");
            }, Qt::QueuedConnection);
            return;
        }

        double worldX = static_cast<double>(ctx.GetNumeric("vision.detected.world_x_mm", 0.0f));
        double worldY = static_cast<double>(ctx.GetNumeric("vision.detected.world_y_mm", 0.0f));

        std::cout << "[CalibrateOffset] Detected feature at world: X=" << worldX
                  << " Y=" << worldY << std::endl;

        auto currentPos = m_context->motionManager->GetCurrentPosition(device);
        std::cout << "[CalibrateOffset] Moving from X=" << currentPos.x << " Y=" << currentPos.y
                  << " to X=" << worldX << " Y=" << worldY << std::endl;

        bool moveOk = m_context->motionManager->MoveToAbs(
            device, worldX, worldY, currentPos.z, 100.0, true);

        if (!moveOk) {
            std::cout << "[CalibrateOffset] Move failed" << std::endl;
            QMetaObject::invokeMethod(qApp, [this]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_confirmBtn->setEnabled(true);
                QMessageBox::warning(this, "Move Failed",
                    "Failed to move gantry to detected position.");
            }, Qt::QueuedConnection);
            return;
        }

        std::cout << "[CalibrateOffset] Centered on feature at X=" << worldX
                  << " Y=" << worldY << std::endl;

        QMetaObject::invokeMethod(qApp, [this]() {
            m_detectCenterBtn->setEnabled(true);
            m_detectCenterBtn->setText("Detect && Center");
            m_confirmBtn->setEnabled(true);
        }, Qt::QueuedConnection);
    });
#else
    QMessageBox::information(this, "Not Available",
        "Vision features require OpenCV. Build with HAS_OPENCV enabled.");
#endif
}

void CalibrateOffsetWidget::onCancel()
{
    m_positionTimer.stop();
    setState(Idle);
}

void CalibrateOffsetWidget::onDone()
{
    setState(Idle);
}

void CalibrateOffsetWidget::refreshVisionProfiles()
{
    m_visionProfileCombo->clear();
    m_visionProfileCombo->addItem("(None - manual only)", QString());

    QString visionDir = getVisionDirectory();
    QDir dir(visionDir);
    if (!dir.exists()) {
        std::cout << "[CalibrateOffset] Vision directory not found: "
                  << visionDir.toStdString() << std::endl;
        return;
    }

    QStringList filters;
    filters << "*.json";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name);

    QFileInfoList files = dir.entryInfoList();
    int count = 0;
    for (const QFileInfo& fileInfo : files) {
        if (fileInfo.fileName().endsWith(".recipe.json")) {
            continue;
        }
        m_visionProfileCombo->addItem(fileInfo.baseName(), fileInfo.absoluteFilePath());
        count++;
    }

    std::cout << "[CalibrateOffset] Loaded " << count << " vision profiles from: "
              << visionDir.toStdString() << std::endl;
}

QString CalibrateOffsetWidget::getVisionDirectory() const
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(
            m_context->productPositionManager->GetCurrentProductPath());
        QString productDir = QFileInfo(productFilePath).path();
        return productDir + "/vision";
    }
    return "config/vision";
}

void CalibrateOffsetWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager) {
        m_positionLabel->setText("X: ---  Y: ---  Z: ---");
        return;
    }

    std::string deviceName = (m_calibrationSet) ? m_calibrationSet->deviceName : "gantry-main";
    auto pos = m_context->motionManager->GetCurrentPosition(deviceName);

    m_positionLabel->setText(QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4));
}

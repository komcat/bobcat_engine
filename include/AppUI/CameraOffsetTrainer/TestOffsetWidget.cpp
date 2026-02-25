// TestOffsetWidget.cpp - Test wizard for verifying camera offset calibration
#include "TestOffsetWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "CameraManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QMetaObject>
#include <QDateTime>
#include <QPixmap>
#include <iostream>

TestOffsetWidget::TestOffsetWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_calibrationSet(nullptr)
    , m_state(Ready)
    , m_wizardStack(nullptr)
    , m_readySummaryLabel(nullptr)
    , m_startTestBtn(nullptr)
    , m_stepLabel(nullptr)
    , m_instructionLabel(nullptr)
    , m_positionLabel(nullptr)
    , m_runVisionBtn(nullptr)
    , m_confirmBtn(nullptr)
    , m_cancelBtn(nullptr)
    , m_comparisonLabel(nullptr)
    , m_showImagesBtn(nullptr)
    , m_lowMagImageLabel(nullptr)
    , m_highMagImageLabel(nullptr)
    , m_lowMagPosOverlay(nullptr)
    , m_highMagPosOverlay(nullptr)
    , m_doneBtn(nullptr)
{
    setupUI();

    connect(&m_positionTimer, &QTimer::timeout, this, &TestOffsetWidget::updatePositionDisplay);
}

void TestOffsetWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    QLabel* title = new QLabel("Test Camera Offset");
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
    // PAGE 0: Ready - pre-check + start
    // ========================================
    QWidget* readyPage = new QWidget();
    QVBoxLayout* readyLayout = new QVBoxLayout(readyPage);
    readyLayout->setAlignment(Qt::AlignTop);

    m_readySummaryLabel = new QLabel("No calibration data available.\nPlease calibrate the offset first.");
    m_readySummaryLabel->setStyleSheet("color: #ccc; font-size: 13px; padding: 10px;");
    m_readySummaryLabel->setWordWrap(true);
    readyLayout->addWidget(m_readySummaryLabel);

    m_startTestBtn = new QPushButton("Start Test");
    m_startTestBtn->setStyleSheet(greenBtnStyle);
    m_startTestBtn->setFixedWidth(200);
    m_startTestBtn->setEnabled(false);
    connect(m_startTestBtn, &QPushButton::clicked, this, &TestOffsetWidget::onStartTest);
    readyLayout->addWidget(m_startTestBtn);
    readyLayout->addStretch();

    m_wizardStack->addWidget(readyPage);  // index 0

    // ========================================
    // PAGE 1: Active - step instructions + position + actions
    // ========================================
    QWidget* activePage = new QWidget();
    QVBoxLayout* activeLayout = new QVBoxLayout(activePage);
    activeLayout->setAlignment(Qt::AlignTop);

    m_stepLabel = new QLabel("Step 1/4");
    m_stepLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #5a9fd4;");
    activeLayout->addWidget(m_stepLabel);

    m_instructionLabel = new QLabel("Instruction text");
    m_instructionLabel->setStyleSheet("color: #ccc; font-size: 13px; padding: 8px 0;");
    m_instructionLabel->setWordWrap(true);
    activeLayout->addWidget(m_instructionLabel);

    // Live position display
    QGroupBox* posGroup = new QGroupBox("Current Gantry Position");
    posGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* posLayout = new QVBoxLayout(posGroup);
    m_positionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_positionLabel->setStyleSheet(
        "font-family: 'RobotoMono'; font-size: 14px; color: #7fc97f; "
        "background-color: #1a1f2e; padding: 8px; border-radius: 3px;");
    posLayout->addWidget(m_positionLabel);
    activeLayout->addWidget(posGroup);

    // Action buttons
    QHBoxLayout* actionRow = new QHBoxLayout();
    m_runVisionBtn = new QPushButton("Run Vision");
    m_runVisionBtn->setStyleSheet(btnStyle);
    connect(m_runVisionBtn, &QPushButton::clicked, this, &TestOffsetWidget::onRunVision);
    actionRow->addWidget(m_runVisionBtn);

    m_confirmBtn = new QPushButton("Confirm Position");
    m_confirmBtn->setStyleSheet(greenBtnStyle);
    connect(m_confirmBtn, &QPushButton::clicked, this, &TestOffsetWidget::onConfirm);
    actionRow->addWidget(m_confirmBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setStyleSheet(btnStyle);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TestOffsetWidget::onCancel);
    actionRow->addWidget(m_cancelBtn);

    actionRow->addStretch();
    activeLayout->addLayout(actionRow);
    activeLayout->addStretch();

    m_wizardStack->addWidget(activePage);  // index 1

    // ========================================
    // PAGE 2: Comparison - calibrated vs tested offset
    // ========================================
    QWidget* comparisonPage = new QWidget();
    QVBoxLayout* compLayout = new QVBoxLayout(comparisonPage);
    compLayout->setAlignment(Qt::AlignTop);

    QLabel* compTitle = new QLabel("Test Results - Offset Comparison");
    compTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #5a9fd4;");
    compLayout->addWidget(compTitle);

    m_comparisonLabel = new QLabel("...");
    m_comparisonLabel->setStyleSheet(
        "color: #ccc; font-size: 13px; padding: 15px; font-family: 'RobotoMono'; "
        "background-color: #1a1f2e; border: 1px solid #3d5a8c; border-radius: 4px;");
    m_comparisonLabel->setWordWrap(true);
    compLayout->addWidget(m_comparisonLabel);

    m_showImagesBtn = new QPushButton("Show Captured Images");
    m_showImagesBtn->setStyleSheet(btnStyle);
    m_showImagesBtn->setFixedWidth(200);
    connect(m_showImagesBtn, &QPushButton::clicked, this, [this]() {
        setState(ShowImages);
    });
    compLayout->addWidget(m_showImagesBtn);
    compLayout->addStretch();

    m_wizardStack->addWidget(comparisonPage);  // index 2

    // ========================================
    // PAGE 3: Images - side-by-side captured images
    // ========================================
    QWidget* imagesPage = new QWidget();
    QVBoxLayout* imgLayout = new QVBoxLayout(imagesPage);
    imgLayout->setAlignment(Qt::AlignTop);

    QLabel* imgTitle = new QLabel("Captured Images");
    imgTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #5a9fd4;");
    imgLayout->addWidget(imgTitle);

    QHBoxLayout* imgRow = new QHBoxLayout();

    // Low-mag image
    QVBoxLayout* lowMagCol = new QVBoxLayout();
    QLabel* lowMagTitle = new QLabel("Low-Mag Camera");
    lowMagTitle->setStyleSheet("color: #aaa; font-weight: bold; font-size: 12px;");
    lowMagTitle->setAlignment(Qt::AlignCenter);
    lowMagCol->addWidget(lowMagTitle);

    m_lowMagImageLabel = new QLabel("(no image)");
    m_lowMagImageLabel->setFixedSize(320, 240);
    m_lowMagImageLabel->setAlignment(Qt::AlignCenter);
    m_lowMagImageLabel->setStyleSheet(
        "background-color: #1a1f2e; border: 1px solid #3d5a8c; border-radius: 3px; color: #666;");
    lowMagCol->addWidget(m_lowMagImageLabel);

    m_lowMagPosOverlay = new QLabel("");
    m_lowMagPosOverlay->setStyleSheet("color: #7fc97f; font-size: 10px; font-family: 'RobotoMono';");
    m_lowMagPosOverlay->setAlignment(Qt::AlignCenter);
    lowMagCol->addWidget(m_lowMagPosOverlay);

    imgRow->addLayout(lowMagCol);

    // High-mag image
    QVBoxLayout* highMagCol = new QVBoxLayout();
    QLabel* highMagTitle = new QLabel("High-Mag Camera");
    highMagTitle->setStyleSheet("color: #aaa; font-weight: bold; font-size: 12px;");
    highMagTitle->setAlignment(Qt::AlignCenter);
    highMagCol->addWidget(highMagTitle);

    m_highMagImageLabel = new QLabel("(no image)");
    m_highMagImageLabel->setFixedSize(320, 240);
    m_highMagImageLabel->setAlignment(Qt::AlignCenter);
    m_highMagImageLabel->setStyleSheet(
        "background-color: #1a1f2e; border: 1px solid #3d5a8c; border-radius: 3px; color: #666;");
    highMagCol->addWidget(m_highMagImageLabel);

    m_highMagPosOverlay = new QLabel("");
    m_highMagPosOverlay->setStyleSheet("color: #7fc97f; font-size: 10px; font-family: 'RobotoMono';");
    m_highMagPosOverlay->setAlignment(Qt::AlignCenter);
    highMagCol->addWidget(m_highMagPosOverlay);

    imgRow->addLayout(highMagCol);
    imgLayout->addLayout(imgRow);

    m_doneBtn = new QPushButton("Done");
    m_doneBtn->setStyleSheet(greenBtnStyle);
    m_doneBtn->setFixedWidth(120);
    connect(m_doneBtn, &QPushButton::clicked, this, &TestOffsetWidget::onDone);
    imgLayout->addWidget(m_doneBtn);
    imgLayout->addStretch();

    m_wizardStack->addWidget(imagesPage);  // index 3

    mainLayout->addWidget(m_wizardStack, 1);
}

void TestOffsetWidget::SetCalibrationSet(CameraOffsetCalibrationSet* cs)
{
    m_calibrationSet = cs;
    setState(Ready);
}

void TestOffsetWidget::setState(TestState state)
{
    m_state = state;

    switch (state) {
    case Ready: {
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer.stop();

        if (m_calibrationSet && m_calibrationSet->offsetValid) {
            m_startTestBtn->setEnabled(true);
            QString summary = QString(
                "Calibrated offset:\n"
                "  dX=%1  dY=%2  dZ=%3\n\n"
                "Low-Mag Camera: %4\nHigh-Mag Camera: %5\n\n"
                "Click 'Start Test' to verify the calibrated offset.")
                .arg(m_calibrationSet->offsetX, 0, 'f', 4)
                .arg(m_calibrationSet->offsetY, 0, 'f', 4)
                .arg(m_calibrationSet->offsetZ, 0, 'f', 4)
                .arg(QString::fromStdString(m_calibrationSet->lowMagCameraId))
                .arg(QString::fromStdString(m_calibrationSet->highMagCameraId));

            if (m_calibrationSet->lastTestResult.valid) {
                summary += QString("\n\nLast test (%1):\n  Deviation: dX=%2  dY=%3  dZ=%4")
                    .arg(QString::fromStdString(m_calibrationSet->lastTestResult.testedAt))
                    .arg(m_calibrationSet->lastTestResult.deviationX, 0, 'f', 4)
                    .arg(m_calibrationSet->lastTestResult.deviationY, 0, 'f', 4)
                    .arg(m_calibrationSet->lastTestResult.deviationZ, 0, 'f', 4);
            }

            m_readySummaryLabel->setText(summary);
        } else {
            m_startTestBtn->setEnabled(false);
            m_readySummaryLabel->setText("No calibration data available.\nPlease calibrate the offset first.");
        }
        break;
    }

    case NavigateToLowMag: {
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer.start(100);
        m_stepLabel->setText("Step 1/4: Navigate to Low-Mag Position");
        m_confirmBtn->setEnabled(false);
        m_runVisionBtn->setEnabled(false);

        if (m_calibrationSet && m_calibrationSet->lowMagPosition.valid) {
            m_instructionLabel->setText("Navigating to calibrated low-mag position...");
            navigateToPosition(m_calibrationSet->lowMagPosition, ConfirmLowMag);
        } else {
            m_instructionLabel->setText("No calibrated low-mag position. Jog manually to the fiducial.");
            m_confirmBtn->setEnabled(true);
            m_runVisionBtn->setEnabled(true);
        }
        break;
    }

    case ConfirmLowMag: {
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer.start(100);
        m_stepLabel->setText("Step 2/4: Confirm Low-Mag Position");
        m_instructionLabel->setText(
            "The gantry is at the low-mag position.\n"
            "Fine-adjust if needed, then click 'Confirm Position' to capture image.");
        m_confirmBtn->setEnabled(true);
        m_runVisionBtn->setEnabled(true);
        break;
    }

    case NavigateToHighMag: {
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer.start(100);
        m_stepLabel->setText("Step 3/4: Navigate to High-Mag Position");
        m_confirmBtn->setEnabled(false);
        m_runVisionBtn->setEnabled(false);

        if (m_calibrationSet && m_calibrationSet->highMagPosition.valid) {
            m_instructionLabel->setText("Navigating to calibrated high-mag position...");
            navigateToPosition(m_calibrationSet->highMagPosition, ConfirmHighMag);
        } else {
            m_instructionLabel->setText("No calibrated high-mag position. Jog manually.");
            m_confirmBtn->setEnabled(true);
            m_runVisionBtn->setEnabled(true);
        }
        break;
    }

    case ConfirmHighMag: {
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer.start(100);
        m_stepLabel->setText("Step 4/4: Confirm High-Mag Position");
        m_instructionLabel->setText(
            "The gantry is at the high-mag position.\n"
            "Fine-adjust if needed, then click 'Confirm Position' to capture image.");
        m_confirmBtn->setEnabled(true);
        m_runVisionBtn->setEnabled(true);
        break;
    }

    case ShowComparison: {
        m_wizardStack->setCurrentIndex(2);
        m_positionTimer.stop();

        if (!m_calibrationSet) break;

        double testedOffX = m_testedHighMagPos.x - m_testedLowMagPos.x;
        double testedOffY = m_testedHighMagPos.y - m_testedLowMagPos.y;
        double testedOffZ = m_testedHighMagPos.z - m_testedLowMagPos.z;

        double devX = testedOffX - m_calibrationSet->offsetX;
        double devY = testedOffY - m_calibrationSet->offsetY;
        double devZ = testedOffZ - m_calibrationSet->offsetZ;

        // Store test result
        auto& tr = m_calibrationSet->lastTestResult;
        tr.testedLowMagPos = m_testedLowMagPos;
        tr.testedHighMagPos = m_testedHighMagPos;
        tr.testedOffsetX = testedOffX;
        tr.testedOffsetY = testedOffY;
        tr.testedOffsetZ = testedOffZ;
        tr.deviationX = devX;
        tr.deviationY = devY;
        tr.deviationZ = devZ;
        tr.lowMagImagePath = m_lowMagImagePath;
        tr.highMagImagePath = m_highMagImagePath;
        tr.testedAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
        tr.valid = true;

        m_comparisonLabel->setText(QString(
            "                    Calibrated      Tested          Deviation\n"
            "  Offset X:    %1    %2    %3\n"
            "  Offset Y:    %4    %5    %6\n"
            "  Offset Z:    %7    %8    %9\n\n"
            "Low-Mag Tested Pos:  X=%10  Y=%11  Z=%12\n"
            "High-Mag Tested Pos: X=%13  Y=%14  Z=%15")
            .arg(m_calibrationSet->offsetX, 12, 'f', 4)
            .arg(testedOffX, 12, 'f', 4)
            .arg(devX, 12, 'f', 4)
            .arg(m_calibrationSet->offsetY, 12, 'f', 4)
            .arg(testedOffY, 12, 'f', 4)
            .arg(devY, 12, 'f', 4)
            .arg(m_calibrationSet->offsetZ, 12, 'f', 4)
            .arg(testedOffZ, 12, 'f', 4)
            .arg(devZ, 12, 'f', 4)
            .arg(m_testedLowMagPos.x, 0, 'f', 4)
            .arg(m_testedLowMagPos.y, 0, 'f', 4)
            .arg(m_testedLowMagPos.z, 0, 'f', 4)
            .arg(m_testedHighMagPos.x, 0, 'f', 4)
            .arg(m_testedHighMagPos.y, 0, 'f', 4)
            .arg(m_testedHighMagPos.z, 0, 'f', 4));

        // Enable show images only if images were captured
        m_showImagesBtn->setEnabled(!m_lowMagImagePath.empty() || !m_highMagImagePath.empty());

        emit testCompleted(tr);

        std::cout << "[TestOffset] Test complete - deviation: dX=" << devX
                  << " dY=" << devY << " dZ=" << devZ << std::endl;
        break;
    }

    case ShowImages: {
        m_wizardStack->setCurrentIndex(3);
        m_positionTimer.stop();

        // Load and display low-mag image
        if (!m_lowMagImagePath.empty()) {
            QPixmap lowPix(QString::fromStdString(m_lowMagImagePath));
            if (!lowPix.isNull()) {
                m_lowMagImageLabel->setPixmap(
                    lowPix.scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            } else {
                m_lowMagImageLabel->setText("Failed to load image");
            }
            m_lowMagPosOverlay->setText(QString("X=%1  Y=%2  Z=%3")
                .arg(m_testedLowMagPos.x, 0, 'f', 4)
                .arg(m_testedLowMagPos.y, 0, 'f', 4)
                .arg(m_testedLowMagPos.z, 0, 'f', 4));
        } else {
            m_lowMagImageLabel->setText("(no image captured)");
            m_lowMagPosOverlay->clear();
        }

        // Load and display high-mag image
        if (!m_highMagImagePath.empty()) {
            QPixmap highPix(QString::fromStdString(m_highMagImagePath));
            if (!highPix.isNull()) {
                m_highMagImageLabel->setPixmap(
                    highPix.scaled(320, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            } else {
                m_highMagImageLabel->setText("Failed to load image");
            }
            m_highMagPosOverlay->setText(QString("X=%1  Y=%2  Z=%3")
                .arg(m_testedHighMagPos.x, 0, 'f', 4)
                .arg(m_testedHighMagPos.y, 0, 'f', 4)
                .arg(m_testedHighMagPos.z, 0, 'f', 4));
        } else {
            m_highMagImageLabel->setText("(no image captured)");
            m_highMagPosOverlay->clear();
        }
        break;
    }
    }
}

void TestOffsetWidget::navigateToPosition(const CameraPosition& pos, TestState nextState)
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
                std::cout << "[TestOffset] Navigation complete" << std::endl;
            } else {
                std::cout << "[TestOffset] Navigation failed, proceeding to manual mode" << std::endl;
            }
            setState(nextState);
        }, Qt::QueuedConnection);
    });
}

void TestOffsetWidget::onStartTest()
{
    if (!m_calibrationSet || !m_calibrationSet->offsetValid) {
        QMessageBox::warning(this, "Error", "No valid calibration data. Please calibrate first.");
        return;
    }

    m_testedLowMagPos = CameraPosition();
    m_testedHighMagPos = CameraPosition();
    m_lowMagImagePath.clear();
    m_highMagImagePath.clear();

    setState(NavigateToLowMag);
}

void TestOffsetWidget::onConfirm()
{
    if (!m_context || !m_context->motionManager || !m_calibrationSet) {
        return;
    }

    std::string deviceName = m_calibrationSet->deviceName;

    switch (m_state) {
    case ConfirmLowMag: {
        // Capture position
        auto pos = m_context->motionManager->GetCurrentPosition(deviceName);
        m_testedLowMagPos.x = pos.x;
        m_testedLowMagPos.y = pos.y;
        m_testedLowMagPos.z = pos.z;
        m_testedLowMagPos.valid = true;

        // Capture image from low-mag camera
        if (m_context->cameraManager && !m_calibrationSet->lowMagCameraId.empty()) {
            std::string filename = "test_lowmag_" +
                QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss").toStdString();
            if (m_context->cameraManager->CaptureImage(m_calibrationSet->lowMagCameraId, filename)) {
                m_lowMagImagePath = m_context->cameraManager->GetLastCapturedImagePath();
                std::cout << "[TestOffset] Low-mag image captured: " << m_lowMagImagePath << std::endl;
            }
        }

        std::cout << "[TestOffset] Low-mag position confirmed: X=" << m_testedLowMagPos.x
                  << " Y=" << m_testedLowMagPos.y << " Z=" << m_testedLowMagPos.z << std::endl;

        setState(NavigateToHighMag);
        break;
    }

    case ConfirmHighMag: {
        // Capture position
        auto pos = m_context->motionManager->GetCurrentPosition(deviceName);
        m_testedHighMagPos.x = pos.x;
        m_testedHighMagPos.y = pos.y;
        m_testedHighMagPos.z = pos.z;
        m_testedHighMagPos.valid = true;

        // Capture image from high-mag camera
        if (m_context->cameraManager && !m_calibrationSet->highMagCameraId.empty()) {
            std::string filename = "test_highmag_" +
                QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss").toStdString();
            if (m_context->cameraManager->CaptureImage(m_calibrationSet->highMagCameraId, filename)) {
                m_highMagImagePath = m_context->cameraManager->GetLastCapturedImagePath();
                std::cout << "[TestOffset] High-mag image captured: " << m_highMagImagePath << std::endl;
            }
        }

        std::cout << "[TestOffset] High-mag position confirmed: X=" << m_testedHighMagPos.x
                  << " Y=" << m_testedHighMagPos.y << " Z=" << m_testedHighMagPos.z << std::endl;

        setState(ShowComparison);
        break;
    }

    default:
        break;
    }
}

void TestOffsetWidget::onRunVision()
{
#ifdef HAS_OPENCV
    QMessageBox::information(this, "Vision",
        "Vision auto-centering is available when configured.\n"
        "Uses the vision profile from the calibration set.");
#else
    QMessageBox::information(this, "Vision",
        "Vision auto-centering requires OpenCV (HAS_OPENCV build flag).\n"
        "Please use manual centering instead.");
#endif
}

void TestOffsetWidget::onCancel()
{
    m_positionTimer.stop();
    setState(Ready);
}

void TestOffsetWidget::onDone()
{
    setState(Ready);
}

void TestOffsetWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager) {
        m_positionLabel->setText("X: ---  Y: ---  Z: ---");
        return;
    }

    std::string deviceName = (m_calibrationSet) ? m_calibrationSet->deviceName : "gantry-main";
    auto pos = m_context->motionManager->GetCurrentPosition(deviceName);

    m_positionLabel->setText(QString("X: %1    Y: %2    Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4));
}

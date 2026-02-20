// TeachReferenceWidget.cpp - Wizard for teaching reference features and computing local frame
#include "TeachReferenceWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QApplication>
#include <iostream>

#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include <nlohmann/json.hpp>
#include <fstream>
#endif

TeachReferenceWidget::TeachReferenceWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &TeachReferenceWidget::updatePositionDisplay);
}

void TeachReferenceWidget::SetTrainingSet(TrainingSet* ts)
{
    m_trainingSet = ts;
    refreshVisionProfiles();
    setState(Idle);
}

void TeachReferenceWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Teach Reference Points");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        "Teach 2-3 reference features (landmarks) that define the local coordinate frame.\n"
        "Ref1 = origin, Ref1->Ref2 = X-axis. All bonding points will be converted to local coordinates.");
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    m_wizardStack = new QStackedWidget();

    // ========================================
    // Page 0: Idle - show current refs
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_currentRefsLabel = new QLabel("No reference points taught yet.");
    m_currentRefsLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_currentRefsLabel->setWordWrap(true);
    idleLayout->addWidget(m_currentRefsLabel);

    idleLayout->addStretch();

    m_startTeachBtn = new QPushButton("Teach Reference Points");
    m_startTeachBtn->setFixedHeight(40);
    m_startTeachBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    connect(m_startTeachBtn, &QPushButton::clicked, this, &TeachReferenceWidget::onStartTeach);
    idleLayout->addWidget(m_startTeachBtn);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Teaching refs
    // ========================================
    QWidget* teachPage = new QWidget();
    QVBoxLayout* teachLayout = new QVBoxLayout(teachPage);

    m_instructionLabel = new QLabel();
    m_instructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setAlignment(Qt::AlignCenter);
    teachLayout->addWidget(m_instructionLabel);

    QGroupBox* posGroup = new QGroupBox("Current Position");
    posGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QVBoxLayout* posLayout = new QVBoxLayout(posGroup);
    m_positionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_positionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_positionLabel->setAlignment(Qt::AlignCenter);
    posLayout->addWidget(m_positionLabel);
    teachLayout->addWidget(posGroup);

    // Vision profile section
    QGroupBox* visionGroup = new QGroupBox("Vision Detect (Optional)");
    visionGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
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
    connect(m_refreshProfilesBtn, &QPushButton::clicked, this, &TeachReferenceWidget::refreshVisionProfiles);
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
    connect(m_detectCenterBtn, &QPushButton::clicked, this, &TeachReferenceWidget::onDetectCenter);
    visionLayout->addWidget(m_detectCenterBtn);

    teachLayout->addWidget(visionGroup);

    teachLayout->addStretch();

    QHBoxLayout* teachBtnRow = new QHBoxLayout();
    m_captureBtn = new QPushButton("Capture Position");
    m_captureBtn->setFixedHeight(40);
    m_captureBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    m_skipBtn = new QPushButton("Skip (2 refs only)");
    m_skipBtn->setFixedHeight(40);
    m_skipBtn->setStyleSheet(
        "QPushButton { background-color: #7f8c8d; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #95a5a6; }"
    );
    m_skipBtn->setVisible(false);
    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(40);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
    );

    teachBtnRow->addWidget(m_captureBtn);
    teachBtnRow->addWidget(m_skipBtn);
    teachBtnRow->addWidget(m_cancelBtn);
    teachLayout->addLayout(teachBtnRow);

    connect(m_captureBtn, &QPushButton::clicked, this, &TeachReferenceWidget::onCaptureRef);
    connect(m_skipBtn, &QPushButton::clicked, this, &TeachReferenceWidget::onSkipRef3);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TeachReferenceWidget::onCancelTeach);

    m_wizardStack->addWidget(teachPage);  // index 1

    // ========================================
    // Page 2: Computed summary
    // ========================================
    QWidget* summaryPage = new QWidget();
    QVBoxLayout* summaryLayout = new QVBoxLayout(summaryPage);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_summaryLabel->setWordWrap(true);
    summaryLayout->addWidget(m_summaryLabel);

    summaryLayout->addStretch();

    QHBoxLayout* summaryBtnRow = new QHBoxLayout();
    m_confirmBtn = new QPushButton("Confirm & Apply");
    m_confirmBtn->setFixedHeight(40);
    m_confirmBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
    );
    m_redoBtn = new QPushButton("Redo");
    m_redoBtn->setFixedHeight(40);
    m_redoBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #f39c12; }"
    );

    summaryBtnRow->addWidget(m_confirmBtn);
    summaryBtnRow->addWidget(m_redoBtn);
    summaryLayout->addLayout(summaryBtnRow);

    connect(m_confirmBtn, &QPushButton::clicked, this, &TeachReferenceWidget::onConfirm);
    connect(m_redoBtn, &QPushButton::clicked, this, &TeachReferenceWidget::onRedo);

    m_wizardStack->addWidget(summaryPage);  // index 2

    mainLayout->addWidget(m_wizardStack, 1);
}

void TeachReferenceWidget::setState(WizardState state)
{
    m_state = state;

    switch (state) {
    case Idle: {
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();

        // Update the idle label with current reference info
        if (m_trainingSet && !m_trainingSet->referencePoints.empty()) {
            QString info = "Current reference points:\n\n";
            for (const auto& rp : m_trainingSet->referencePoints) {
                QString profileInfo = rp.visionProfilePath.empty()
                    ? "" : QString("  [vision: %1]").arg(
                        QFileInfo(QString::fromStdString(rp.visionProfilePath)).baseName());
                info += QString("  %1: X=%2  Y=%3  Z=%4%5\n")
                    .arg(QString::fromStdString(rp.name))
                    .arg(rp.globalX, 0, 'f', 4)
                    .arg(rp.globalY, 0, 'f', 4)
                    .arg(rp.globalZ, 0, 'f', 4)
                    .arg(profileInfo);
            }
            if (m_trainingSet->valid) {
                info += QString("\nFrame computed:\n"
                    "  Origin: (%1, %2)\n"
                    "  X-axis: (%3, %4)\n"
                    "  Y-axis: (%5, %6)")
                    .arg(m_trainingSet->originX, 0, 'f', 4)
                    .arg(m_trainingSet->originY, 0, 'f', 4)
                    .arg(m_trainingSet->xAxisX, 0, 'f', 6)
                    .arg(m_trainingSet->xAxisY, 0, 'f', 6)
                    .arg(m_trainingSet->yAxisX, 0, 'f', 6)
                    .arg(m_trainingSet->yAxisY, 0, 'f', 6);
            }
            m_currentRefsLabel->setText(info);
        } else {
            m_currentRefsLabel->setText("No reference points taught yet.\n\n"
                "You need at least 2 reference features to define a local frame.");
        }

        bool hasBonding = m_trainingSet && !m_trainingSet->bondingPoints.empty();
        m_startTeachBtn->setEnabled(hasBonding);
        if (!hasBonding) {
            m_currentRefsLabel->setText(m_currentRefsLabel->text() +
                "\n\nNote: Teach bonding positions first before teaching references.");
        }
        break;
    }

    case TeachRef1:
        m_wizardStack->setCurrentIndex(1);
        m_instructionLabel->setText(
            "Step 1: Jog to Reference Feature 1 (this will be the ORIGIN)\n"
            "Manually jog or use 'Detect & Center' to align, then click 'Capture Position'");
        m_skipBtn->setVisible(false);
        m_positionTimer->start(100);
        break;

    case TeachRef2:
        m_wizardStack->setCurrentIndex(1);
        m_instructionLabel->setText(
            "Step 2: Jog to Reference Feature 2\n"
            "The X-axis will be defined from Ref1 -> Ref2\n"
            "Manually jog or use 'Detect & Center', then click 'Capture Position'");
        m_skipBtn->setVisible(false);
        m_positionTimer->start(100);
        break;

    case TeachRef3:
        m_wizardStack->setCurrentIndex(1);
        m_instructionLabel->setText(
            "Step 3 (Optional): Jog to Reference Feature 3 for verification\n"
            "Click 'Capture Position' or 'Skip'");
        m_skipBtn->setVisible(true);
        m_positionTimer->start(100);
        break;

    case Computed:
        m_wizardStack->setCurrentIndex(2);
        m_positionTimer->stop();
        break;
    }
}

void TeachReferenceWidget::onStartTeach()
{
    if (!m_trainingSet) return;

    if (m_trainingSet->bondingPoints.empty()) {
        QMessageBox::information(this, "No Bonding Points",
            "Please teach at least one bonding position before teaching references.");
        return;
    }

    m_refCount = 0;
    for (auto& rp : m_pendingRefs) {
        rp = ReferencePoint();
    }
    refreshVisionProfiles();
    setState(TeachRef1);
}

void TeachReferenceWidget::onCaptureRef()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    std::string device = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(device);

    // Get selected vision profile path (store with the reference point)
    std::string profilePath;
    if (m_visionProfileCombo->currentIndex() > 0) {
        profilePath = m_visionProfileCombo->currentData().toString().toStdString();
    }

    switch (m_state) {
    case TeachRef1:
        m_pendingRefs[0].name = "Ref1";
        m_pendingRefs[0].globalX = pos.x;
        m_pendingRefs[0].globalY = pos.y;
        m_pendingRefs[0].globalZ = pos.z;
        m_pendingRefs[0].visionProfilePath = profilePath;
        m_pendingRefs[0].valid = true;
        m_refCount = 1;
        std::cout << "[TeachReference] Ref1: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z;
        if (!profilePath.empty()) std::cout << " profile=" << profilePath;
        std::cout << std::endl;
        setState(TeachRef2);
        break;

    case TeachRef2:
        m_pendingRefs[1].name = "Ref2";
        m_pendingRefs[1].globalX = pos.x;
        m_pendingRefs[1].globalY = pos.y;
        m_pendingRefs[1].globalZ = pos.z;
        m_pendingRefs[1].visionProfilePath = profilePath;
        m_pendingRefs[1].valid = true;
        m_refCount = 2;
        std::cout << "[TeachReference] Ref2: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z;
        if (!profilePath.empty()) std::cout << " profile=" << profilePath;
        std::cout << std::endl;
        setState(TeachRef3);
        break;

    case TeachRef3:
        m_pendingRefs[2].name = "Ref3";
        m_pendingRefs[2].globalX = pos.x;
        m_pendingRefs[2].globalY = pos.y;
        m_pendingRefs[2].globalZ = pos.z;
        m_pendingRefs[2].visionProfilePath = profilePath;
        m_pendingRefs[2].valid = true;
        m_refCount = 3;
        std::cout << "[TeachReference] Ref3: X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z;
        if (!profilePath.empty()) std::cout << " profile=" << profilePath;
        std::cout << std::endl;
        computeFrame();
        break;

    default:
        break;
    }
}

void TeachReferenceWidget::onSkipRef3()
{
    m_refCount = 2;
    std::cout << "[TeachReference] Skipped Ref3" << std::endl;
    computeFrame();
}

void TeachReferenceWidget::onDetectCenter()
{
#ifdef HAS_OPENCV
    if (!m_context || !m_context->motionManager || !m_trainingSet) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    if (m_visionProfileCombo->currentIndex() <= 0) {
        QMessageBox::information(this, "No Profile",
            "Select a vision profile from the dropdown first.");
        return;
    }

    std::string profilePath = m_visionProfileCombo->currentData().toString().toStdString();
    std::string device = m_trainingSet->deviceName;

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
    m_captureBtn->setEnabled(false);

    std::cout << "[TeachReference] Detect & Center: profile=" << profilePath
              << " camera=" << cameraName << " device=" << device << std::endl;

    m_detectFuture = std::async(std::launch::async,
        [this, profilePath, cameraName, device]() {
        // Execute vision profile via VisionCommandHandler
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
            std::cout << "[TeachReference] Vision execution error: " << error << std::endl;
            QMetaObject::invokeMethod(qApp, [this, error]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_captureBtn->setEnabled(true);
                QMessageBox::warning(this, "Detection Failed",
                    QString::fromStdString("Vision command failed: " + error));
            }, Qt::QueuedConnection);
            return;
        }

        // Check if vision result was successful
        bool visionSuccess = ctx.GetBoolean("vision.result.success", false);

        if (!visionSuccess) {
            std::string visionError = ctx.GetString("vision.result.error", "Unknown error");
            std::cout << "[TeachReference] Vision failed: " << visionError << std::endl;
            QMetaObject::invokeMethod(qApp, [this, visionError]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_captureBtn->setEnabled(true);
                QMessageBox::warning(this, "Detection Failed",
                    QString::fromStdString("Vision result: " + visionError));
            }, Qt::QueuedConnection);
            return;
        }

        // Check if world position was calculated
        if (!ctx.HasValue("vision.detected.world_x_mm") ||
            !ctx.HasValue("vision.detected.world_y_mm")) {
            std::cout << "[TeachReference] No feature detected (no world position)" << std::endl;
            QMetaObject::invokeMethod(qApp, [this]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_captureBtn->setEnabled(true);
                QMessageBox::warning(this, "No Feature",
                    "Vision ran successfully but no feature center was detected.\n"
                    "Check that the profile returns circle/point results.");
            }, Qt::QueuedConnection);
            return;
        }

        double worldX = static_cast<double>(ctx.GetNumeric("vision.detected.world_x_mm", 0.0f));
        double worldY = static_cast<double>(ctx.GetNumeric("vision.detected.world_y_mm", 0.0f));

        std::cout << "[TeachReference] Detected feature at world: X=" << worldX
                  << " Y=" << worldY << std::endl;

        // Move gantry to center camera crosshair on the detected feature
        auto currentPos = m_context->motionManager->GetCurrentPosition(device);
        std::cout << "[TeachReference] Moving from X=" << currentPos.x << " Y=" << currentPos.y
                  << " to X=" << worldX << " Y=" << worldY << std::endl;

        bool moveOk = m_context->motionManager->MoveToAbs(
            device, worldX, worldY, currentPos.z, 100.0, true);

        if (!moveOk) {
            std::cout << "[TeachReference] Move failed" << std::endl;
            QMetaObject::invokeMethod(qApp, [this]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_captureBtn->setEnabled(true);
                QMessageBox::warning(this, "Move Failed",
                    "Failed to move gantry to detected position.");
            }, Qt::QueuedConnection);
            return;
        }

        std::cout << "[TeachReference] Centered on feature at X=" << worldX
                  << " Y=" << worldY << std::endl;

        QMetaObject::invokeMethod(qApp, [this]() {
            m_detectCenterBtn->setEnabled(true);
            m_detectCenterBtn->setText("Detect && Center");
            m_captureBtn->setEnabled(true);
        }, Qt::QueuedConnection);
    });
#else
    QMessageBox::information(this, "Not Available",
        "Vision features require OpenCV. Build with HAS_OPENCV enabled.");
#endif
}

void TeachReferenceWidget::refreshVisionProfiles()
{
    m_visionProfileCombo->clear();
    m_visionProfileCombo->addItem("(None - manual only)", QString());

    QString visionDir = getVisionDirectory();
    QDir dir(visionDir);
    if (!dir.exists()) {
        std::cout << "[TeachReference] Vision directory not found: "
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
        // Skip recipe files
        if (fileInfo.fileName().endsWith(".recipe.json")) {
            continue;
        }
        m_visionProfileCombo->addItem(fileInfo.baseName(), fileInfo.absoluteFilePath());
        count++;
    }

    std::cout << "[TeachReference] Loaded " << count << " vision profiles from: "
              << visionDir.toStdString() << std::endl;
}

QString TeachReferenceWidget::getVisionDirectory() const
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

void TeachReferenceWidget::computeFrame()
{
    if (!m_trainingSet || m_refCount < 2) return;

    // Compute coordinate axes from Ref1 -> Ref2
    double originX, originY, xAxisX, xAxisY, yAxisX, yAxisY;
    AccuracyTrainerMath::ComputeCoordinateAxes(
        m_pendingRefs[0].globalX, m_pendingRefs[0].globalY,
        m_pendingRefs[1].globalX, m_pendingRefs[1].globalY,
        originX, originY, xAxisX, xAxisY, yAxisX, yAxisY);

    // Build summary
    QString summary = QString(
        "Reference Frame Computed\n\n"
        "Ref1 (Origin): X=%1  Y=%2  Z=%3\n"
        "Ref2:          X=%4  Y=%5  Z=%6\n")
        .arg(m_pendingRefs[0].globalX, 0, 'f', 4)
        .arg(m_pendingRefs[0].globalY, 0, 'f', 4)
        .arg(m_pendingRefs[0].globalZ, 0, 'f', 4)
        .arg(m_pendingRefs[1].globalX, 0, 'f', 4)
        .arg(m_pendingRefs[1].globalY, 0, 'f', 4)
        .arg(m_pendingRefs[1].globalZ, 0, 'f', 4);

    if (m_refCount >= 3 && m_pendingRefs[2].valid) {
        summary += QString("Ref3:          X=%1  Y=%2  Z=%3\n")
            .arg(m_pendingRefs[2].globalX, 0, 'f', 4)
            .arg(m_pendingRefs[2].globalY, 0, 'f', 4)
            .arg(m_pendingRefs[2].globalZ, 0, 'f', 4);
    }

    // Show vision profile info
    for (int i = 0; i < m_refCount; ++i) {
        if (!m_pendingRefs[i].visionProfilePath.empty()) {
            summary += QString("Ref%1 vision: %2\n")
                .arg(i + 1)
                .arg(QFileInfo(QString::fromStdString(m_pendingRefs[i].visionProfilePath)).baseName());
        }
    }

    double refDist = AccuracyTrainerMath::Distance(
        m_pendingRefs[0].globalX, m_pendingRefs[0].globalY,
        m_pendingRefs[1].globalX, m_pendingRefs[1].globalY);

    summary += QString(
        "\nOrigin: (%1, %2)\n"
        "X-axis: (%3, %4)\n"
        "Y-axis: (%5, %6)\n"
        "Ref distance: %7 mm\n")
        .arg(originX, 0, 'f', 4).arg(originY, 0, 'f', 4)
        .arg(xAxisX, 0, 'f', 6).arg(xAxisY, 0, 'f', 6)
        .arg(yAxisX, 0, 'f', 6).arg(yAxisY, 0, 'f', 6)
        .arg(refDist, 0, 'f', 4);

    // Convert bonding points to local coordinates
    summary += "\nBonding Points in Local Coords:\n";
    for (auto& bp : m_trainingSet->bondingPoints) {
        double lx, ly;
        AccuracyTrainerMath::WorldToLocal(
            bp.globalX, bp.globalY,
            originX, originY,
            xAxisX, xAxisY,
            yAxisX, yAxisY,
            lx, ly);
        summary += QString("  %1: local(%2, %3)\n")
            .arg(QString::fromStdString(bp.name))
            .arg(lx, 0, 'f', 4)
            .arg(ly, 0, 'f', 4);
    }

    // Store computed frame temporarily for confirm
    m_trainingSet->originX = originX;
    m_trainingSet->originY = originY;
    m_trainingSet->xAxisX = xAxisX;
    m_trainingSet->xAxisY = xAxisY;
    m_trainingSet->yAxisX = yAxisX;
    m_trainingSet->yAxisY = yAxisY;

    m_summaryLabel->setText(summary);
    setState(Computed);
}

void TeachReferenceWidget::onConfirm()
{
    if (!m_trainingSet) return;

    // Store reference points
    m_trainingSet->referencePoints.clear();
    for (int i = 0; i < m_refCount; ++i) {
        m_trainingSet->referencePoints.push_back(m_pendingRefs[i]);
    }

    // Convert all bonding points to local coordinates
    for (auto& bp : m_trainingSet->bondingPoints) {
        AccuracyTrainerMath::WorldToLocal(
            bp.globalX, bp.globalY,
            m_trainingSet->originX, m_trainingSet->originY,
            m_trainingSet->xAxisX, m_trainingSet->xAxisY,
            m_trainingSet->yAxisX, m_trainingSet->yAxisY,
            bp.localX, bp.localY);
    }

    m_trainingSet->valid = true;

    emit referenceFrameComputed(*m_trainingSet);

    std::cout << "[TeachReference] Frame confirmed with " << m_refCount << " refs, "
              << m_trainingSet->bondingPoints.size() << " bonding points converted" << std::endl;

    setState(Idle);
}

void TeachReferenceWidget::onRedo()
{
    setState(TeachRef1);
}

void TeachReferenceWidget::onCancelTeach()
{
    m_positionTimer->stop();
    setState(Idle);
}

void TeachReferenceWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) {
        m_positionLabel->setText("X: ---  Y: ---  Z: ---");
        return;
    }

    std::string device = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(device);

    m_positionLabel->setText(QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4));
}

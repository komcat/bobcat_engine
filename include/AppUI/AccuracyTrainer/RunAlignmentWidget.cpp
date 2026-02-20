// RunAlignmentWidget.cpp - Run alignment mode
#include "RunAlignmentWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <iostream>
#include <cmath>

#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include <nlohmann/json.hpp>
#include <fstream>
#endif

RunAlignmentWidget::RunAlignmentWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
{
    setupUI();
    connect(m_positionTimer, &QTimer::timeout, this, &RunAlignmentWidget::updatePositionDisplay);
}

void RunAlignmentWidget::SetTrainingSet(TrainingSet* ts)
{
    m_trainingSet = ts;
    setState(Ready);
}

void RunAlignmentWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Run Alignment");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    // Main content area
    QHBoxLayout* contentLayout = new QHBoxLayout();

    // Left: wizard stack
    m_wizardStack = new QStackedWidget();

    // ========================================
    // Page 0: Ready
    // ========================================
    QWidget* readyPage = new QWidget();
    QVBoxLayout* readyLayout = new QVBoxLayout(readyPage);

    m_readyLabel = new QLabel();
    m_readyLabel->setStyleSheet(
        "font-size: 13px; color: #aaa; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px;");
    m_readyLabel->setWordWrap(true);
    readyLayout->addWidget(m_readyLabel);

    readyLayout->addStretch();

    m_startBtn = new QPushButton("Run Alignment");
    m_startBtn->setFixedHeight(45);
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 16px; border-radius: 4px; padding: 10px 30px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #555; color: #888; }"
    );
    connect(m_startBtn, &QPushButton::clicked, this, &RunAlignmentWidget::onStartAlignment);
    readyLayout->addWidget(m_startBtn);

    m_wizardStack->addWidget(readyPage);  // index 0

    // ========================================
    // Page 1: Ref adjustment
    // ========================================
    QWidget* refPage = new QWidget();
    QVBoxLayout* refLayout = new QVBoxLayout(refPage);

    m_refInstructionLabel = new QLabel();
    m_refInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_refInstructionLabel->setWordWrap(true);
    m_refInstructionLabel->setAlignment(Qt::AlignCenter);
    refLayout->addWidget(m_refInstructionLabel);

    QGroupBox* refPosGroup = new QGroupBox("Current Position");
    refPosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QVBoxLayout* refPosLayout = new QVBoxLayout(refPosGroup);
    m_refPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_refPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_refPositionLabel->setAlignment(Qt::AlignCenter);
    refPosLayout->addWidget(m_refPositionLabel);
    refLayout->addWidget(refPosGroup);

    refLayout->addStretch();

    QHBoxLayout* refBtnRow = new QHBoxLayout();
    m_captureRefBtn = new QPushButton("Capture Adjusted Position");
    m_captureRefBtn->setFixedHeight(40);
    m_captureRefBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setFixedHeight(40);
    m_cancelBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
    );
    refBtnRow->addWidget(m_captureRefBtn);
    refBtnRow->addWidget(m_cancelBtn);
    refLayout->addLayout(refBtnRow);

    connect(m_captureRefBtn, &QPushButton::clicked, this, &RunAlignmentWidget::onCaptureActualRef);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_positionTimer->stop();
        setState(Ready);
    });

    m_wizardStack->addWidget(refPage);  // index 1

    // ========================================
    // Page 2: Moving to bonding positions
    // ========================================
    QWidget* bondPage = new QWidget();
    QVBoxLayout* bondLayout = new QVBoxLayout(bondPage);

    m_bondInstructionLabel = new QLabel();
    m_bondInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #00ff88; font-weight: bold; padding: 15px; "
        "background-color: #1a3a2f; border-radius: 6px;");
    m_bondInstructionLabel->setWordWrap(true);
    m_bondInstructionLabel->setAlignment(Qt::AlignCenter);
    bondLayout->addWidget(m_bondInstructionLabel);

    QGroupBox* bondPosGroup = new QGroupBox("Corrected Position");
    bondPosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QVBoxLayout* bondPosLayout = new QVBoxLayout(bondPosGroup);
    m_bondPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_bondPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_bondPositionLabel->setAlignment(Qt::AlignCenter);
    bondPosLayout->addWidget(m_bondPositionLabel);
    bondLayout->addWidget(bondPosGroup);

    bondLayout->addStretch();

    m_nextBondBtn = new QPushButton("Next Bonding Point");
    m_nextBondBtn->setFixedHeight(40);
    m_nextBondBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    connect(m_nextBondBtn, &QPushButton::clicked, this, &RunAlignmentWidget::onMoveToNextBonding);
    bondLayout->addWidget(m_nextBondBtn);

    m_wizardStack->addWidget(bondPage);  // index 2

    // ========================================
    // Page 3: Done
    // ========================================
    QWidget* donePage = new QWidget();
    QVBoxLayout* doneLayout = new QVBoxLayout(donePage);

    m_doneLabel = new QLabel();
    m_doneLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #27ae60; border-radius: 6px;");
    m_doneLabel->setWordWrap(true);
    doneLayout->addWidget(m_doneLabel);

    doneLayout->addStretch();

    m_finishBtn = new QPushButton("Finish");
    m_finishBtn->setFixedHeight(40);
    m_finishBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
    );
    connect(m_finishBtn, &QPushButton::clicked, this, &RunAlignmentWidget::onFinish);
    doneLayout->addWidget(m_finishBtn);

    m_wizardStack->addWidget(donePage);  // index 3

    contentLayout->addWidget(m_wizardStack, 6);

    // Right: log view
    m_logView = new QTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setStyleSheet(
        "QTextEdit { background-color: #0a0f1a; color: #aaa; border: 1px solid #3d5a8c; "
        "font-family: monospace; font-size: 11px; padding: 5px; }");
    contentLayout->addWidget(m_logView, 4);

    mainLayout->addLayout(contentLayout, 1);
}

void RunAlignmentWidget::setState(AlignState state)
{
    m_state = state;

    switch (state) {
    case Ready: {
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        m_logView->clear();

        if (!m_trainingSet || !m_trainingSet->valid) {
            m_readyLabel->setText(
                "Training set is not ready.\n\n"
                "Please complete:\n"
                "1. Teach at least one bonding position\n"
                "2. Teach reference points and compute the frame");
            m_startBtn->setEnabled(false);
        } else {
            int refCount = static_cast<int>(m_trainingSet->referencePoints.size());
            int bondCount = static_cast<int>(m_trainingSet->bondingPoints.size());
            int visionCount = 0;
            for (const auto& rp : m_trainingSet->referencePoints) {
                if (!rp.visionProfilePath.empty()) visionCount++;
            }
            QString visionNote = visionCount > 0
                ? QString("\nVision-enabled refs: %1 (auto-detect & center)").arg(visionCount)
                : QString("\nNo vision profiles (manual adjustment only)");
            m_readyLabel->setText(QString(
                "Training Set: %1\n"
                "Device: %2\n\n"
                "Reference Points: %3\n"
                "Bonding Points: %4%5\n\n"
                "The alignment process will:\n"
                "1. Move to each taught reference point\n"
                "2. Auto-detect via vision (or manual adjust if no profile)\n"
                "3. Compute the correction transform\n"
                "4. Move to each corrected bonding position")
                .arg(QString::fromStdString(m_trainingSet->name))
                .arg(QString::fromStdString(m_trainingSet->deviceName))
                .arg(refCount)
                .arg(bondCount)
                .arg(visionNote));
            m_startBtn->setEnabled(true);
        }
        break;
    }

    case MoveToRef:
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer->start(100);
        break;

    case AdjustRef:
        m_wizardStack->setCurrentIndex(1);
        m_positionTimer->start(100);
        break;

    case Computing:
        m_positionTimer->stop();
        break;

    case MoveToBonding:
        m_wizardStack->setCurrentIndex(2);
        m_positionTimer->start(100);
        break;

    case Done:
        m_wizardStack->setCurrentIndex(3);
        m_positionTimer->stop();
        break;
    }
}

void RunAlignmentWidget::onStartAlignment()
{
    if (!m_trainingSet || !m_trainingSet->valid) return;

    m_actualRefs.clear();
    m_correctedBonds.clear();
    m_currentRefIndex = 0;
    m_currentBondIndex = 0;
    m_logView->clear();

    appendLog("=== Alignment Started ===");
    appendLog(QString("Training Set: %1").arg(QString::fromStdString(m_trainingSet->name)));

    moveToCurrentRef();
}

void RunAlignmentWidget::moveToCurrentRef()
{
    if (!m_trainingSet || m_currentRefIndex >= static_cast<int>(m_trainingSet->referencePoints.size())) {
        // All refs captured, compute transform
        computeTransform();
        return;
    }

    const auto& ref = m_trainingSet->referencePoints[m_currentRefIndex];
    appendLog(QString("Moving to %1 (taught: X=%2, Y=%3, Z=%4)")
        .arg(QString::fromStdString(ref.name))
        .arg(ref.globalX, 0, 'f', 4)
        .arg(ref.globalY, 0, 'f', 4)
        .arg(ref.globalZ, 0, 'f', 4));

    if (!m_context || !m_context->motionManager) return;

    std::string device = m_trainingSet->deviceName;
    double gx = ref.globalX, gy = ref.globalY, gz = ref.globalZ;
    int refIdx = m_currentRefIndex;
    size_t refCount = m_trainingSet->referencePoints.size();
    std::string refName = ref.name;
    std::string visionProfile = ref.visionProfilePath;

    if (m_captureRefBtn) m_captureRefBtn->setEnabled(false);
    setState(MoveToRef);

    m_moveFuture = std::async(std::launch::async,
        [this, device, gx, gy, gz, refIdx, refCount, refName, visionProfile]() {
        bool moveOk = m_context->motionManager->MoveToAbs(device, gx, gy, gz, 100.0, true);

        if (!moveOk) {
            QMetaObject::invokeMethod(this, [this, refIdx, refCount, refName]() {
                appendLog("WARNING: Move failed! Please jog manually.");
                showManualAdjust(refIdx, refCount, refName);
            }, Qt::QueuedConnection);
            return;
        }

        QMetaObject::invokeMethod(this, [this]() {
            appendLog("Move complete.");
        }, Qt::QueuedConnection);

        // If vision profile exists, try auto detect & center
        if (!visionProfile.empty()) {
            QMetaObject::invokeMethod(this, [this, refName]() {
                appendLog(QString("Running vision profile for %1...")
                    .arg(QString::fromStdString(refName)));
            }, Qt::QueuedConnection);

            bool visionOk = runVisionDetectCenter(visionProfile, device);

            if (visionOk) {
                // Vision succeeded - auto-capture position and advance
                QMetaObject::invokeMethod(this, [this, refIdx, refCount, refName, device]() {
                    appendLog(QString("Vision centered on %1 - auto-capturing position")
                        .arg(QString::fromStdString(refName)));
                    autoCaptureRef(refIdx, device);
                }, Qt::QueuedConnection);
                return;
            }

            // Vision failed - fall back to manual
            QMetaObject::invokeMethod(this, [this, refIdx, refCount, refName]() {
                appendLog("Vision failed - falling back to manual adjustment.");
                showManualAdjust(refIdx, refCount, refName);
            }, Qt::QueuedConnection);
            return;
        }

        // No vision profile - manual adjustment
        QMetaObject::invokeMethod(this, [this, refIdx, refCount, refName]() {
            showManualAdjust(refIdx, refCount, refName);
        }, Qt::QueuedConnection);
    });
}

void RunAlignmentWidget::showManualAdjust(int refIdx, size_t refCount, const std::string& refName)
{
    m_refInstructionLabel->setText(QString(
        "Reference %1 of %2: %3\n\n"
        "Gantry moved to taught position.\n"
        "Jog to align with the ACTUAL reference feature,\n"
        "then click 'Capture Adjusted Position'")
        .arg(refIdx + 1)
        .arg(refCount)
        .arg(QString::fromStdString(refName)));

    setState(AdjustRef);
    if (m_captureRefBtn) m_captureRefBtn->setEnabled(true);
}

void RunAlignmentWidget::autoCaptureRef(int refIdx, const std::string& device)
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) return;

    auto pos = m_context->motionManager->GetCurrentPosition(device);

    const auto& taughtRef = m_trainingSet->referencePoints[refIdx];

    ReferencePoint actual;
    actual.name = taughtRef.name;
    actual.globalX = pos.x;
    actual.globalY = pos.y;
    actual.globalZ = pos.z;
    actual.valid = true;
    m_actualRefs.push_back(actual);

    double dx = pos.x - taughtRef.globalX;
    double dy = pos.y - taughtRef.globalY;

    appendLog(QString("Auto-captured %1: X=%2, Y=%3, Z=%4  (delta: dX=%5, dY=%6)")
        .arg(QString::fromStdString(actual.name))
        .arg(pos.x, 0, 'f', 4)
        .arg(pos.y, 0, 'f', 4)
        .arg(pos.z, 0, 'f', 4)
        .arg(dx, 0, 'f', 4)
        .arg(dy, 0, 'f', 4));

    m_currentRefIndex++;
    moveToCurrentRef();
}

bool RunAlignmentWidget::runVisionDetectCenter(
    const std::string& profilePath, const std::string& device)
{
#ifdef HAS_OPENCV
    // Read camera name from profile JSON
    std::string cameraName;
    {
        std::ifstream file(profilePath);
        if (!file.is_open()) {
            std::cout << "[RunAlignment] Cannot open vision profile: " << profilePath << std::endl;
            return false;
        }
        nlohmann::json j;
        file >> j;
        file.close();
        if (j.contains("camera") && j["camera"].contains("cameraId")) {
            cameraName = j["camera"].value("cameraId", "");
        }
    }
    if (cameraName.empty()) {
        std::cout << "[RunAlignment] Vision profile has no camera ID" << std::endl;
        return false;
    }

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
        std::cout << "[RunAlignment] Vision execution error: " << error << std::endl;
        return false;
    }

    bool visionSuccess = ctx.GetBoolean("vision.result.success", false);
    if (!visionSuccess) {
        std::string visionError = ctx.GetString("vision.result.error", "Unknown error");
        std::cout << "[RunAlignment] Vision failed: " << visionError << std::endl;
        return false;
    }

    if (!ctx.HasValue("vision.detected.world_x_mm") ||
        !ctx.HasValue("vision.detected.world_y_mm")) {
        std::cout << "[RunAlignment] No feature detected (no world position)" << std::endl;
        return false;
    }

    double worldX = static_cast<double>(ctx.GetNumeric("vision.detected.world_x_mm", 0.0f));
    double worldY = static_cast<double>(ctx.GetNumeric("vision.detected.world_y_mm", 0.0f));

    std::cout << "[RunAlignment] Detected feature at world: X=" << worldX
              << " Y=" << worldY << std::endl;

    auto currentPos = m_context->motionManager->GetCurrentPosition(device);
    bool moveOk = m_context->motionManager->MoveToAbs(
        device, worldX, worldY, currentPos.z, 100.0, true);

    if (!moveOk) {
        std::cout << "[RunAlignment] Move to detected position failed" << std::endl;
        return false;
    }

    std::cout << "[RunAlignment] Centered on feature at X=" << worldX
              << " Y=" << worldY << std::endl;
    return true;
#else
    (void)profilePath;
    (void)device;
    return false;
#endif
}

void RunAlignmentWidget::onCaptureActualRef()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) return;

    std::string device = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(device);

    const auto& taughtRef = m_trainingSet->referencePoints[m_currentRefIndex];

    ReferencePoint actual;
    actual.name = taughtRef.name;
    actual.globalX = pos.x;
    actual.globalY = pos.y;
    actual.globalZ = pos.z;
    actual.valid = true;
    m_actualRefs.push_back(actual);

    double dx = pos.x - taughtRef.globalX;
    double dy = pos.y - taughtRef.globalY;

    appendLog(QString("Captured %1: X=%2, Y=%3, Z=%4  (delta: dX=%5, dY=%6)")
        .arg(QString::fromStdString(actual.name))
        .arg(pos.x, 0, 'f', 4)
        .arg(pos.y, 0, 'f', 4)
        .arg(pos.z, 0, 'f', 4)
        .arg(dx, 0, 'f', 4)
        .arg(dy, 0, 'f', 4));

    m_currentRefIndex++;
    moveToCurrentRef();
}

void RunAlignmentWidget::computeTransform()
{
    appendLog("\n=== Computing Transform ===");

    if (m_actualRefs.size() < 2) {
        appendLog("ERROR: Need at least 2 reference points!");
        setState(Ready);
        return;
    }

    // Compute live frame from actual refs
    AccuracyTrainerMath::ComputeCoordinateAxes(
        m_actualRefs[0].globalX, m_actualRefs[0].globalY,
        m_actualRefs[1].globalX, m_actualRefs[1].globalY,
        m_liveOriginX, m_liveOriginY,
        m_liveXAxisX, m_liveXAxisY,
        m_liveYAxisX, m_liveYAxisY);

    appendLog(QString("Live Origin: (%1, %2)")
        .arg(m_liveOriginX, 0, 'f', 4)
        .arg(m_liveOriginY, 0, 'f', 4));
    appendLog(QString("Live X-axis: (%1, %2)")
        .arg(m_liveXAxisX, 0, 'f', 6)
        .arg(m_liveXAxisY, 0, 'f', 6));

    // Optional: if 3 refs, compute scale correction
    double scale = 1.0;
    if (m_actualRefs.size() >= 3 && m_trainingSet->referencePoints.size() >= 3) {
        double taughtDist = AccuracyTrainerMath::Distance(
            m_trainingSet->referencePoints[0].globalX, m_trainingSet->referencePoints[0].globalY,
            m_trainingSet->referencePoints[1].globalX, m_trainingSet->referencePoints[1].globalY);
        double liveDist = AccuracyTrainerMath::Distance(
            m_actualRefs[0].globalX, m_actualRefs[0].globalY,
            m_actualRefs[1].globalX, m_actualRefs[1].globalY);

        if (taughtDist > 1e-6) {
            scale = liveDist / taughtDist;
            appendLog(QString("Scale factor: %1 (taught=%2mm, live=%3mm)")
                .arg(scale, 0, 'f', 6)
                .arg(taughtDist, 0, 'f', 4)
                .arg(liveDist, 0, 'f', 4));
        }
    }

    // Transform each bonding point: local coords -> live world coords
    m_correctedBonds.clear();
    appendLog("\n=== Corrected Bonding Positions ===");
    for (const auto& bp : m_trainingSet->bondingPoints) {
        double scaledLocalX = bp.localX * scale;
        double scaledLocalY = bp.localY * scale;

        double corrX, corrY;
        AccuracyTrainerMath::LocalToWorld(
            scaledLocalX, scaledLocalY,
            m_liveOriginX, m_liveOriginY,
            m_liveXAxisX, m_liveXAxisY,
            m_liveYAxisX, m_liveYAxisY,
            corrX, corrY);

        CorrectedBond cb;
        cb.name = bp.name;
        cb.correctedX = corrX;
        cb.correctedY = corrY;
        cb.correctedZ = bp.globalZ;  // Keep original Z
        cb.deltaX = corrX - bp.globalX;
        cb.deltaY = corrY - bp.globalY;
        m_correctedBonds.push_back(cb);

        appendLog(QString("  %1: corrected(%2, %3)  delta(%4, %5)")
            .arg(QString::fromStdString(bp.name))
            .arg(corrX, 0, 'f', 4)
            .arg(corrY, 0, 'f', 4)
            .arg(cb.deltaX, 0, 'f', 4)
            .arg(cb.deltaY, 0, 'f', 4));
    }

    // Start moving to bonding points
    m_currentBondIndex = 0;
    if (!m_correctedBonds.empty()) {
        moveToCurrentBonding();
    } else {
        appendLog("No bonding points to move to.");
        setState(Done);
    }
}

void RunAlignmentWidget::moveToCurrentBonding()
{
    if (m_currentBondIndex >= static_cast<int>(m_correctedBonds.size())) {
        // All done
        appendLog("\n=== Alignment Complete ===");

        QString summary = "Alignment Complete\n\n";
        for (const auto& cb : m_correctedBonds) {
            summary += QString("%1: dX=%2  dY=%3\n")
                .arg(QString::fromStdString(cb.name))
                .arg(cb.deltaX, 0, 'f', 4)
                .arg(cb.deltaY, 0, 'f', 4);
        }
        m_doneLabel->setText(summary);
        setState(Done);
        return;
    }

    const auto& cb = m_correctedBonds[m_currentBondIndex];

    appendLog(QString("\nMoving to corrected %1: X=%2, Y=%3, Z=%4")
        .arg(QString::fromStdString(cb.name))
        .arg(cb.correctedX, 0, 'f', 4)
        .arg(cb.correctedY, 0, 'f', 4)
        .arg(cb.correctedZ, 0, 'f', 4));

    if (m_context && m_context->motionManager) {
        std::string device = m_trainingSet->deviceName;
        double cx = cb.correctedX, cy = cb.correctedY, cz = cb.correctedZ;
        double dx = cb.deltaX, dy = cb.deltaY;
        int bondIdx = m_currentBondIndex;
        size_t bondCount = m_correctedBonds.size();
        std::string bondName = cb.name;

        if (m_nextBondBtn) m_nextBondBtn->setEnabled(false);

        m_moveFuture = std::async(std::launch::async, [this, device, cx, cy, cz, dx, dy, bondIdx, bondCount, bondName]() {
            bool moveOk = m_context->motionManager->MoveToAbs(device, cx, cy, cz, 100.0, true);
            QMetaObject::invokeMethod(this, [this, moveOk, cx, cy, cz, dx, dy, bondIdx, bondCount, bondName]() {
                if (!moveOk) {
                    appendLog("WARNING: Move failed!");
                } else {
                    appendLog("Move complete.");
                }

                m_bondInstructionLabel->setText(QString(
                    "Bonding Point %1 of %2: %3\n\n"
                    "Corrected Position: X=%4  Y=%5  Z=%6\n"
                    "Correction Delta: dX=%7  dY=%8\n\n"
                    "Verify the position, then click 'Next'")
                    .arg(bondIdx + 1)
                    .arg(bondCount)
                    .arg(QString::fromStdString(bondName))
                    .arg(cx, 0, 'f', 4)
                    .arg(cy, 0, 'f', 4)
                    .arg(cz, 0, 'f', 4)
                    .arg(dx, 0, 'f', 4)
                    .arg(dy, 0, 'f', 4));

                if (m_nextBondBtn) m_nextBondBtn->setEnabled(true);
            }, Qt::QueuedConnection);
        });
    }

    m_bondPositionLabel->setText(QString("X: %1  Y: %2  Z: %3")
        .arg(cb.correctedX, 10, 'f', 4)
        .arg(cb.correctedY, 10, 'f', 4)
        .arg(cb.correctedZ, 10, 'f', 4));

    setState(MoveToBonding);
}

void RunAlignmentWidget::onMoveToNextBonding()
{
    m_currentBondIndex++;
    moveToCurrentBonding();
}

void RunAlignmentWidget::onFinish()
{
    setState(Ready);
}

void RunAlignmentWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) return;

    std::string device = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(device);

    QString posText = QString("X: %1  Y: %2  Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4);

    if (m_state == AdjustRef || m_state == MoveToRef) {
        m_refPositionLabel->setText(posText);
    }
}

void RunAlignmentWidget::appendLog(const QString& text)
{
    m_logView->append(text);
    std::cout << "[RunAlignment] " << text.toStdString() << std::endl;
}

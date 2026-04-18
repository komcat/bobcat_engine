// EdgeRetraceWidget.cpp - Retrace wizard for Edge Transfer
#include "EdgeRetraceWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "SequenceEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QMetaObject>
#include <cmath>
#include <utility>
#include <vector>
#include <iostream>

namespace {
constexpr const char* kDeg = "\xC2\xB0";
QString fmtDeg(double v) { return QString::number(v, 'f', 4) + QString::fromUtf8(kDeg); }
QString fmtMm(double v)  { return QString::number(v, 'f', 4) + " mm"; }
}

EdgeRetraceWidget::EdgeRetraceWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_posTimer(new QTimer(this))
{
    setupUI();
    connect(m_posTimer, &QTimer::timeout, this, &EdgeRetraceWidget::updatePositionDisplay);
    setState(Idle);
}

void EdgeRetraceWidget::SetData(const EdgeTransferSet& set)
{
    m_taughtSet = set;
    m_retracedSet = set;   // start from taught; each Wait step overwrites a point
    // Clear validity on retraced until Review is reached so accidental reads
    // can't confuse downstream consumers.
    m_retracedSet.referenceEdge.valid = false;
    m_retracedSet.targetEdge.valid    = false;
    setState(Idle);
}

void EdgeRetraceWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Edge Transfer Retrace");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        "Re-measure P1, P2 (reference) and P3, P4 (target) with manual confirmation.\n"
        "Each step auto-navigates gantry-main to the previously taught coordinate "
        "at 0.5 mm/s, then waits for you to confirm the new position.");
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    m_wizardStack = new QStackedWidget();

    const QString blueBtn =
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 13px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";
    const QString tealBtn =
        "QPushButton { background-color: #16a085; color: white; font-weight: bold; "
        "font-size: 12px; border-radius: 4px; padding: 6px 14px; }"
        "QPushButton:hover { background-color: #1abc9c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";
    const QString redBtn =
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 6px 16px; font-size: 12px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";
    const QString orangeBtn =
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 6px 16px; }"
        "QPushButton:hover { background-color: #f39c12; }";
    const QString greenBtn =
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 13px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";
    const QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    // -------- Page 0: Idle --------
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);
    m_idleLabel = new QLabel(
        "No retrace in progress.\n"
        "Click Start to re-measure all four taught points.");
    m_idleLabel->setStyleSheet(
        "color: #aaa; padding: 15px; background-color: #1a2a3f; "
        "border: 1px solid #3d5a8c; border-radius: 6px; font-size: 13px;");
    m_idleLabel->setWordWrap(true);
    idleLayout->addWidget(m_idleLabel);
    idleLayout->addStretch();
    m_startBtn = new QPushButton("Start Retrace");
    m_startBtn->setFixedHeight(40);
    m_startBtn->setStyleSheet(blueBtn);
    connect(m_startBtn, &QPushButton::clicked, this, &EdgeRetraceWidget::onStartRetrace);
    idleLayout->addWidget(m_startBtn);
    m_wizardStack->addWidget(idlePage);   // index 0

    // -------- Page 1: shared Wait (we rewrite labels per state) --------
    QWidget* waitPage = new QWidget();
    QVBoxLayout* waitLayout = new QVBoxLayout(waitPage);

    m_waitTitleLabel = new QLabel();
    m_waitTitleLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_waitTitleLabel->setWordWrap(true);
    m_waitTitleLabel->setAlignment(Qt::AlignCenter);
    waitLayout->addWidget(m_waitTitleLabel);

    QGroupBox* taughtGroup = new QGroupBox("Taught coordinate");
    taughtGroup->setStyleSheet(groupStyle);
    QVBoxLayout* taughtLayout = new QVBoxLayout(taughtGroup);
    m_waitTaughtLabel = new QLabel();
    m_waitTaughtLabel->setStyleSheet(
        "font-family: monospace; color: #00ff88; padding: 8px; font-size: 13px;");
    taughtLayout->addWidget(m_waitTaughtLabel);
    waitLayout->addWidget(taughtGroup);

    QGroupBox* liveGroup = new QGroupBox("Current gantry position");
    liveGroup->setStyleSheet(groupStyle);
    QVBoxLayout* liveLayout = new QVBoxLayout(liveGroup);
    m_waitLiveLabel = new QLabel("X: ---   Y: ---   Z: ---");
    m_waitLiveLabel->setStyleSheet(
        "font-family: monospace; color: #ffffff; padding: 8px; font-size: 15px;");
    m_waitLiveLabel->setAlignment(Qt::AlignCenter);
    liveLayout->addWidget(m_waitLiveLabel);
    waitLayout->addWidget(liveGroup);

    waitLayout->addStretch();

    QHBoxLayout* navRow = new QHBoxLayout();
    m_autoNavBtn = new QPushButton("Auto-Nav to taught (0.5 mm/s)");
    m_autoNavBtn->setFixedHeight(36);
    m_autoNavBtn->setStyleSheet(tealBtn);
    connect(m_autoNavBtn, &QPushButton::clicked, this, &EdgeRetraceWidget::onAutoNav);
    navRow->addWidget(m_autoNavBtn);

    m_stopBtn = new QPushButton("STOP");
    m_stopBtn->setFixedHeight(36);
    m_stopBtn->setStyleSheet(redBtn);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &EdgeRetraceWidget::onStopAutoNav);
    navRow->addWidget(m_stopBtn);
    waitLayout->addLayout(navRow);

    m_confirmHeaderLabel = new QLabel();
    m_confirmHeaderLabel->setStyleSheet(
        "color: #00ff88; font-weight: bold; font-size: 12px; padding: 4px 2px;");
    m_confirmHeaderLabel->setAlignment(Qt::AlignLeft);
    waitLayout->addWidget(m_confirmHeaderLabel);

    QHBoxLayout* confirmRow = new QHBoxLayout();
    m_confirmBtn = new QPushButton("Confirm (capture current position)");
    m_confirmBtn->setFixedHeight(40);
    m_confirmBtn->setStyleSheet(blueBtn);
    connect(m_confirmBtn, &QPushButton::clicked, this, &EdgeRetraceWidget::onConfirm);
    confirmRow->addWidget(m_confirmBtn);

    m_cancelBtn = new QPushButton("Cancel Retrace");
    m_cancelBtn->setFixedHeight(40);
    m_cancelBtn->setStyleSheet(redBtn);
    connect(m_cancelBtn, &QPushButton::clicked, this, &EdgeRetraceWidget::onCancel);
    confirmRow->addWidget(m_cancelBtn);
    waitLayout->addLayout(confirmRow);

    m_wizardStack->addWidget(waitPage);   // index 1 (shared across 4 wait states)

    // -------- Page 2: Review --------
    QWidget* reviewPage = new QWidget();
    QVBoxLayout* reviewLayout = new QVBoxLayout(reviewPage);

    m_reviewLabel = new QLabel("(Retrace not completed yet)");
    m_reviewLabel->setStyleSheet(
        "font-family: monospace; color: #e0e0e0; background-color: #1a2a3f; "
        "border: 1px solid #27ae60; border-radius: 6px; padding: 12px; font-size: 12px;");
    m_reviewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_reviewLabel->setWordWrap(true);
    reviewLayout->addWidget(m_reviewLabel);

    // Apply-corrections action row (full width, above the bottom buttons).
    m_applyCorrectionsBtn = new QPushButton(
        "Apply Corrections  (enabled axes, 0.1 mm/s sequentially)");
    m_applyCorrectionsBtn->setFixedHeight(38);
    m_applyCorrectionsBtn->setStyleSheet(tealBtn);
    m_applyCorrectionsBtn->setToolTip(
        "Applies only the axes where 'Enable X' is checked in the Mapping & Result page,\n"
        "one at a time, at 0.1 mm/s. After the last axis, the target-device\n"
        "velocity is restored to 1 mm/s.");
    connect(m_applyCorrectionsBtn, &QPushButton::clicked,
            this, &EdgeRetraceWidget::onApplyCorrections);
    reviewLayout->addWidget(m_applyCorrectionsBtn);

    m_applyStatusLabel = new QLabel();
    m_applyStatusLabel->setStyleSheet(
        "color: #f0c040; font-family: monospace; font-size: 11px; padding: 4px;");
    m_applyStatusLabel->setWordWrap(true);
    reviewLayout->addWidget(m_applyStatusLabel);

    reviewLayout->addStretch();

    QHBoxLayout* reviewBtnRow = new QHBoxLayout();
    m_saveRetracedBtn = new QPushButton("Save Retraced as New Taught");
    m_saveRetracedBtn->setFixedHeight(40);
    m_saveRetracedBtn->setStyleSheet(greenBtn);
    m_saveRetracedBtn->setToolTip(
        "Overwrite the current set's taught edges with the retraced values.\n"
        "The page's Save button then persists to disk.");
    connect(m_saveRetracedBtn, &QPushButton::clicked,
            this, &EdgeRetraceWidget::onSaveRetraced);
    reviewBtnRow->addWidget(m_saveRetracedBtn);

    m_startOverBtn = new QPushButton("Start Over");
    m_startOverBtn->setFixedHeight(40);
    m_startOverBtn->setStyleSheet(orangeBtn);
    connect(m_startOverBtn, &QPushButton::clicked,
            this, &EdgeRetraceWidget::onStartOver);
    reviewBtnRow->addWidget(m_startOverBtn);

    m_doneBtn = new QPushButton("Done");
    m_doneBtn->setFixedHeight(40);
    m_doneBtn->setStyleSheet(blueBtn);
    connect(m_doneBtn, &QPushButton::clicked,
            this, &EdgeRetraceWidget::onDone);
    reviewBtnRow->addWidget(m_doneBtn);
    reviewLayout->addLayout(reviewBtnRow);

    m_wizardStack->addWidget(reviewPage);  // index 2

    mainLayout->addWidget(m_wizardStack, 1);
}

void EdgeRetraceWidget::setState(WizardState s)
{
    m_state = s;
    switch (s) {
    case Idle:
        m_wizardStack->setCurrentIndex(0);
        m_posTimer->stop();
        m_idleLabel->setText(
            (m_taughtSet.referenceEdge.valid && m_taughtSet.targetEdge.valid)
            ? QString("Ready to retrace. Current taught set has both edges.\n"
                      "Clicking Start will re-measure P1, P2, P3, P4 one at a time.")
            : QString("Both reference and target edges must be taught before retrace.\n"
                      "Go back to steps 1 and 2 first."));
        m_startBtn->setEnabled(m_taughtSet.referenceEdge.valid &&
                               m_taughtSet.targetEdge.valid);
        break;

    case WaitRefP1:
    case WaitRefP2:
    case WaitTgtP1:
    case WaitTgtP2:
        m_wizardStack->setCurrentIndex(1);
        m_posTimer->start(100);
        refreshWaitPage();
        break;

    case Review:
        m_wizardStack->setCurrentIndex(2);
        m_posTimer->stop();
        // Recompute deltas + corrections from the retraced points.
        m_retracedSet.referenceEdge.valid = true;
        m_retracedSet.targetEdge.valid    = true;
        m_retracedSet.Recalculate();
        refreshReview();
        break;
    }
}

bool EdgeRetraceWidget::taughtForCurrentState(double& tx, double& ty, double& tz) const
{
    switch (m_state) {
    case WaitRefP1:
        tx = m_taughtSet.referenceEdge.p1x;
        ty = m_taughtSet.referenceEdge.p1y;
        tz = m_taughtSet.referenceEdge.p1z; return true;
    case WaitRefP2:
        tx = m_taughtSet.referenceEdge.p2x;
        ty = m_taughtSet.referenceEdge.p2y;
        tz = m_taughtSet.referenceEdge.p2z; return true;
    case WaitTgtP1:
        tx = m_taughtSet.targetEdge.p1x;
        ty = m_taughtSet.targetEdge.p1y;
        tz = m_taughtSet.targetEdge.p1z; return true;
    case WaitTgtP2:
        tx = m_taughtSet.targetEdge.p2x;
        ty = m_taughtSet.targetEdge.p2y;
        tz = m_taughtSet.targetEdge.p2z; return true;
    default: return false;
    }
}

void EdgeRetraceWidget::storeCurrentCapture(double x, double y, double z)
{
    switch (m_state) {
    case WaitRefP1:
        m_retracedSet.referenceEdge.p1x = x;
        m_retracedSet.referenceEdge.p1y = y;
        m_retracedSet.referenceEdge.p1z = z; break;
    case WaitRefP2:
        m_retracedSet.referenceEdge.p2x = x;
        m_retracedSet.referenceEdge.p2y = y;
        m_retracedSet.referenceEdge.p2z = z; break;
    case WaitTgtP1:
        m_retracedSet.targetEdge.p1x = x;
        m_retracedSet.targetEdge.p1y = y;
        m_retracedSet.targetEdge.p1z = z; break;
    case WaitTgtP2:
        m_retracedSet.targetEdge.p2x = x;
        m_retracedSet.targetEdge.p2y = y;
        m_retracedSet.targetEdge.p2z = z; break;
    default: break;
    }
}

void EdgeRetraceWidget::refreshWaitPage()
{
    static const QString titles[] = {
        "Step 1/4: Retrace  Reference P1",
        "Step 2/4: Retrace  Reference P2",
        "Step 3/4: Retrace  Target P1 (P3)",
        "Step 4/4: Retrace  Target P2 (P4)"
    };
    static const QString pointNames[] = {
        "Reference P1",
        "Reference P2",
        "Target P1 (P3)",
        "Target P2 (P4)"
    };
    int idx = int(m_state) - int(WaitRefP1);
    if (idx < 0 || idx > 3) return;
    m_waitTitleLabel->setText(titles[idx]);

    double tx = 0, ty = 0, tz = 0;
    taughtForCurrentState(tx, ty, tz);
    m_waitTaughtLabel->setText(QString("X=%1   Y=%2   Z=%3")
        .arg(tx, 0, 'f', 4).arg(ty, 0, 'f', 4).arg(tz, 0, 'f', 4));

    const QString& pt = pointNames[idx];
    if (m_autoNavBtn) {
        m_autoNavBtn->setText(QString("Auto-Nav to %1 (0.5 mm/s)").arg(pt));
    }
    if (m_confirmHeaderLabel) {
        m_confirmHeaderLabel->setText(QString("Capture %1 position:").arg(pt));
    }
    if (m_confirmBtn) {
        m_confirmBtn->setText(QString("Confirm %1").arg(pt));
    }
}

void EdgeRetraceWidget::refreshReview()
{
    auto pointRow = [](const QString& name, const EdgePoints& old, const EdgePoints& nw,
                       int pointIdx) {
        // pointIdx: 1 = P1, 2 = P2
        const double ox = (pointIdx == 1) ? old.p1x : old.p2x;
        const double oy = (pointIdx == 1) ? old.p1y : old.p2y;
        const double oz = (pointIdx == 1) ? old.p1z : old.p2z;
        const double nx = (pointIdx == 1) ? nw.p1x  : nw.p2x;
        const double ny = (pointIdx == 1) ? nw.p1y  : nw.p2y;
        const double nz = (pointIdx == 1) ? nw.p1z  : nw.p2z;
        return QString(
            "  %1\n"
            "    taught    : X=%2  Y=%3  Z=%4\n"
            "    retraced  : X=%5  Y=%6  Z=%7\n"
            "    delta     : dX=%8  dY=%9  dZ=%10")
            .arg(name)
            .arg(ox, 0, 'f', 4).arg(oy, 0, 'f', 4).arg(oz, 0, 'f', 4)
            .arg(nx, 0, 'f', 4).arg(ny, 0, 'f', 4).arg(nz, 0, 'f', 4)
            .arg(nx - ox, 0, 'f', 4).arg(ny - oy, 0, 'f', 4).arg(nz - oz, 0, 'f', 4);
    };

    const EdgePoints& rOld = m_taughtSet.referenceEdge;
    const EdgePoints& rNew = m_retracedSet.referenceEdge;
    const EdgePoints& tOld = m_taughtSet.targetEdge;
    const EdgePoints& tNew = m_retracedSet.targetEdge;

    QString text;
    text += "Per-point comparison (retraced - taught):\n";
    text += pointRow("Reference P1", rOld, rNew, 1) + "\n";
    text += pointRow("Reference P2", rOld, rNew, 2) + "\n";
    text += pointRow("Target    P1 (P3)", tOld, tNew, 1) + "\n";
    text += pointRow("Target    P2 (P4)", tOld, tNew, 2) + "\n\n";

    text += "Retraced edge metrics:\n";
    text += QString("  Reference edge : xyDist=%1  angleToX=%2  tilt=%3\n")
        .arg(fmtMm(rNew.xyDistance)).arg(fmtDeg(rNew.angleToX)).arg(fmtDeg(rNew.verticalTilt));
    text += QString("  Target edge    : xyDist=%1  angleToX=%2  tilt=%3\n\n")
        .arg(fmtMm(tNew.xyDistance)).arg(fmtDeg(tNew.angleToX)).arg(fmtDeg(tNew.verticalTilt));

    text += "Recomputed deltas (target - reference):\n";
    text += QString("  Delta yaw  = %1\n").arg(fmtDeg(m_retracedSet.deltaYawDeg));
    text += QString("  Delta tilt = %1\n\n").arg(fmtDeg(m_retracedSet.deltaTiltDeg));

    auto roleStr = [](EdgeAxisRole r) { return QString(EdgeAxisRoleToString(r)); };
    auto en      = [](bool b) { return b ? "enabled" : "disabled"; };
    text += QString("Suggested corrections  (mapping from current set, device=%1):\n")
        .arg(QString::fromStdString(m_retracedSet.config.targetDevice));
    text += QString("  U (%1, %2) : %3\n")
        .arg(roleStr(m_retracedSet.config.mapping.u))
        .arg(en(m_retracedSet.config.mapping.applyU))
        .arg(fmtDeg(m_retracedSet.correctionU));
    text += QString("  V (%1, %2) : %3\n")
        .arg(roleStr(m_retracedSet.config.mapping.v))
        .arg(en(m_retracedSet.config.mapping.applyV))
        .arg(fmtDeg(m_retracedSet.correctionV));
    text += QString("  W (%1, %2) : %3")
        .arg(roleStr(m_retracedSet.config.mapping.w))
        .arg(en(m_retracedSet.config.mapping.applyW))
        .arg(fmtDeg(m_retracedSet.correctionW));

    m_reviewLabel->setText(text);
}

// ===========================================================================
// Slots
// ===========================================================================

void EdgeRetraceWidget::onStartRetrace()
{
    if (!m_taughtSet.referenceEdge.valid || !m_taughtSet.targetEdge.valid) {
        QMessageBox::information(this, "Retrace",
            "Both reference and target edges must be taught first.");
        return;
    }
    // Fresh retrace buffer
    m_retracedSet = m_taughtSet;
    m_retracedSet.referenceEdge.valid = false;
    m_retracedSet.targetEdge.valid    = false;
    setState(WaitRefP1);
}

void EdgeRetraceWidget::onCancel()
{
    // Cancel out of the wait flow. Active nav must be stopped first.
    if (!m_navActiveDevice.empty()) {
        QMessageBox::information(this, "Cancel Retrace",
            "A nav move is in progress - press STOP first.");
        return;
    }
    setState(Idle);
}

void EdgeRetraceWidget::onAutoNav()
{
    if (m_state == Idle || m_state == Review) return;
    double tx = 0, ty = 0, tz = 0;
    if (!taughtForCurrentState(tx, ty, tz)) return;
    static const QString whiches[] = { "Ref P1", "Ref P2", "Tgt P1", "Tgt P2" };
    int idx = int(m_state) - int(WaitRefP1);
    startAutoNavAsync(tx, ty, tz, (idx >= 0 && idx < 4) ? whiches[idx] : "point");
}

void EdgeRetraceWidget::onStopAutoNav()
{
    if (!m_context || !m_context->motionManager) return;
    if (m_navActiveDevice.empty()) return;
    const std::string device = m_navActiveDevice;
    bool ok = m_context->motionManager->EmergencyStop(device);
    std::cout << "[EdgeRetrace] STOP Auto-Nav on " << device
              << "  EmergencyStop=" << (ok ? "OK" : "FAIL") << std::endl;
}

void EdgeRetraceWidget::onConfirm()
{
    if (m_state == Idle || m_state == Review) return;
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Retrace", "Motion manager not available.");
        return;
    }
    if (!m_navActiveDevice.empty()) {
        QMessageBox::information(this, "Retrace",
            "Wait for the nav move to finish before confirming.");
        return;
    }
    auto pos = m_context->motionManager->GetCurrentPosition("gantry-main");
    storeCurrentCapture(pos.x, pos.y, pos.z);

    std::cout << "[EdgeRetrace] Confirmed state=" << int(m_state)
              << " at X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z << std::endl;

    // Advance to next state in the chain.
    switch (m_state) {
    case WaitRefP1: setState(WaitRefP2); break;
    case WaitRefP2: setState(WaitTgtP1); break;
    case WaitTgtP1: setState(WaitTgtP2); break;
    case WaitTgtP2: setState(Review);    break;
    default: break;
    }
}

void EdgeRetraceWidget::onSaveRetraced()
{
    auto answer = QMessageBox::question(this, "Save Retraced",
        "Overwrite the current set's taught edges with the retraced values?\n\n"
        "This updates the set in memory; use the page's Save button to persist to disk.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    // Preserve the existing config (mapping, cameraId, etc.); only edges change.
    EdgeTransferSet updated = m_taughtSet;
    updated.referenceEdge = m_retracedSet.referenceEdge;
    updated.targetEdge    = m_retracedSet.targetEdge;
    updated.referenceEdge.valid = true;
    updated.targetEdge.valid    = true;
    updated.Recalculate();
    emit retraceCompleted(updated);
    setState(Idle);
}

void EdgeRetraceWidget::onStartOver()
{
    m_retracedSet = m_taughtSet;
    m_retracedSet.referenceEdge.valid = false;
    m_retracedSet.targetEdge.valid    = false;
    setState(WaitRefP1);
}

void EdgeRetraceWidget::onDone()
{
    setState(Idle);
}

void EdgeRetraceWidget::onApplyCorrections()
{
    if (m_applyInProgress) return;
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Apply Corrections",
            "Motion manager not available.");
        return;
    }
    if (m_context->sequenceEngine && m_context->sequenceEngine->IsExecuting()) {
        QMessageBox::warning(this, "Apply Corrections Blocked",
            "A recipe is currently running. Stop the program before applying.");
        return;
    }
    const std::string device = m_retracedSet.config.targetDevice;
    if (device.empty()) {
        QMessageBox::warning(this, "Apply Corrections",
            "No target device selected on the left panel.");
        return;
    }

    // Build (axis, delta) list filtered by per-axis Enable flags.
    std::vector<std::pair<std::string, double>> axisDeltas;
    if (m_retracedSet.config.mapping.applyU)
        axisDeltas.emplace_back("U", m_retracedSet.correctionU);
    if (m_retracedSet.config.mapping.applyV)
        axisDeltas.emplace_back("V", m_retracedSet.correctionV);
    if (m_retracedSet.config.mapping.applyW)
        axisDeltas.emplace_back("W", m_retracedSet.correctionW);
    if (axisDeltas.empty()) {
        QMessageBox::information(this, "Apply Corrections",
            "No axes are enabled for apply. Tick the Enable boxes for U / V / W\n"
            "in the 'Mapping & Result' page first.");
        return;
    }

    constexpr double kSafeVel    = 0.1;   // mm/s during each move
    constexpr double kRestoreVel = 1.0;   // mm/s restored after all moves

    m_applyInProgress = true;
    if (m_applyCorrectionsBtn) m_applyCorrectionsBtn->setEnabled(false);
    if (m_saveRetracedBtn)     m_saveRetracedBtn->setEnabled(false);
    if (m_startOverBtn)        m_startOverBtn->setEnabled(false);
    if (m_doneBtn)             m_doneBtn->setEnabled(false);
    m_applyStatusLabel->setText("Apply Corrections: starting...");

    auto* mm = m_context->motionManager;

    m_applyFuture = std::async(std::launch::async,
        [this, mm, device, axisDeltas]() {
        bool allOk = true;
        QString report;
        for (const auto& pair : axisDeltas) {
            const std::string axis = pair.first;   // structured-binding capture workaround
            const double delta = pair.second;
            if (std::abs(delta) < 1e-9) {
                report += QString("%1: skipped (delta=0)\n")
                    .arg(QString::fromStdString(axis));
                continue;
            }
            QMetaObject::invokeMethod(this, [this, axis, delta]() {
                m_applyStatusLabel->setText(QString(
                    "Apply Corrections: moving %1 by %2 deg at 0.1 mm/s...")
                    .arg(QString::fromStdString(axis))
                    .arg(delta, 0, 'f', 4));
            }, Qt::QueuedConnection);

            bool velOk  = mm->SetSystemVelocity(device, kSafeVel);
            bool moveOk = mm->MoveRelative(device, axis, delta, /*blocking*/ true);

            std::cout << "[EdgeRetrace] Apply axis=" << axis
                      << " delta=" << delta
                      << "  velSet=" << (velOk ? "OK" : "FAIL")
                      << "  move="   << (moveOk ? "OK" : "FAIL") << std::endl;

            report += QString("%1: delta=%2  move=%3\n")
                .arg(QString::fromStdString(axis))
                .arg(delta, 0, 'f', 4)
                .arg(moveOk ? "OK" : "FAIL");

            if (!moveOk) { allOk = false; break; }
        }

        // Always restore velocity, even if a move failed.
        bool restoreOk = mm->SetSystemVelocity(device, kRestoreVel);
        std::cout << "[EdgeRetrace] velocity restore="
                  << (restoreOk ? "OK" : "FAIL") << std::endl;

        QMetaObject::invokeMethod(this,
            [this, report, allOk, restoreOk]() {
            m_applyInProgress = false;
            if (m_applyCorrectionsBtn) m_applyCorrectionsBtn->setEnabled(true);
            if (m_saveRetracedBtn)     m_saveRetracedBtn->setEnabled(true);
            if (m_startOverBtn)        m_startOverBtn->setEnabled(true);
            if (m_doneBtn)             m_doneBtn->setEnabled(true);
            m_applyStatusLabel->setText(
                QString(allOk ? "Done: Apply Corrections" : "Failed: Apply Corrections") +
                (restoreOk ? "" : "  [velocity restore FAILED]"));
            if (!allOk) {
                QMessageBox::warning(this, "Apply Corrections Failed",
                    "One or more axis moves failed:\n" + report);
            }
        }, Qt::QueuedConnection);
    });
}

void EdgeRetraceWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager) return;
    if (m_state == Idle || m_state == Review) return;
    auto pos = m_context->motionManager->GetCurrentPosition("gantry-main");
    m_waitLiveLabel->setText(QString("X: %1   Y: %2   Z: %3")
        .arg(pos.x, 10, 'f', 4)
        .arg(pos.y, 10, 'f', 4)
        .arg(pos.z, 10, 'f', 4));
}

// ---------------------------------------------------------------------------
// Auto-Nav (0.5 mm/s safe move with STOP)
// ---------------------------------------------------------------------------

void EdgeRetraceWidget::startAutoNavAsync(double tx, double ty, double tz,
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
    if (!m_navActiveDevice.empty()) return;

    constexpr double kSafeVel    = 0.5;   // mm/s during the move
    constexpr double kDefaultVel = 50.0;  // mm/s restored after

    m_navActiveDevice = "gantry-main";
    m_autoNavBtn->setEnabled(false);
    m_confirmBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);

    auto* mm = m_context->motionManager;
    const std::string device = m_navActiveDevice;
    QString whichCopy = which;

    m_navFuture = std::async(std::launch::async,
        [this, mm, device, tx, ty, tz, whichCopy]() {
        bool velSlowOk    = mm->SetSystemVelocity(device, kSafeVel);
        bool moveOk       = mm->MoveToAbs(device, tx, ty, tz);
        bool velRestoreOk = mm->SetSystemVelocity(device, kDefaultVel);

        std::cout << "[EdgeRetrace] Auto-Nav to " << whichCopy.toStdString()
                  << " (" << tx << ", " << ty << ", " << tz << ")"
                  << "  velSlow="    << (velSlowOk ? "OK" : "FAIL")
                  << "  move="       << (moveOk ? "OK" : "FAIL")
                  << "  velRestore=" << (velRestoreOk ? "OK" : "FAIL") << std::endl;

        QMetaObject::invokeMethod(this, [this, moveOk, whichCopy]() {
            m_navActiveDevice.clear();
            m_autoNavBtn->setEnabled(true);
            m_confirmBtn->setEnabled(true);
            m_cancelBtn->setEnabled(true);
            m_stopBtn->setEnabled(false);
            if (!moveOk) {
                QMessageBox::warning(this, "Auto-Nav Failed",
                    QString("MoveToAbs to %1 failed.").arg(whichCopy));
            }
        }, Qt::QueuedConnection);
    });
}

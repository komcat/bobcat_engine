// EdgeResultWidget.cpp
#include "EdgeResultWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "SequenceEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QMetaObject>
#include <QString>
#include <cmath>
#include <iostream>

namespace {
constexpr const char* kDeg = "\xC2\xB0";   // UTF-8 degree sign

QString fmtDeg(double v)     { return QString::number(v, 'f', 4) + QString::fromUtf8(kDeg); }
QString fmtMm(double v)      { return QString::number(v, 'f', 4) + " mm"; }
}

EdgeResultWidget::EdgeResultWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_uRoleCombo(nullptr)
    , m_vRoleCombo(nullptr)
    , m_wRoleCombo(nullptr)
    , m_reverseUCheck(nullptr)
    , m_reverseVCheck(nullptr)
    , m_reverseWCheck(nullptr)
    , m_enableUCheck(nullptr)
    , m_enableVCheck(nullptr)
    , m_enableWCheck(nullptr)
    , m_applyUBtn(nullptr)
    , m_applyVBtn(nullptr)
    , m_applyWBtn(nullptr)
    , m_recalcBtn(nullptr)
    , m_applySelectedBtn(nullptr)
    , m_applyStatusLabel(nullptr)
    , m_refMetricsLabel(nullptr)
    , m_tgtMetricsLabel(nullptr)
    , m_deltaLabel(nullptr)
    , m_correctionLabel(nullptr)
    , m_stepLabel(nullptr)
{
    setupUI();
    refreshDisplay();
}

void EdgeResultWidget::populateRoleCombo(QComboBox* cb)
{
    cb->addItem("None",  int(EdgeAxisRole::None));
    cb->addItem("Roll",  int(EdgeAxisRole::Roll));
    cb->addItem("Pitch", int(EdgeAxisRole::Pitch));
    cb->addItem("Yaw",   int(EdgeAxisRole::Yaw));
}

void EdgeResultWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Edge Transfer Result");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    const QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    // ---- Axis mapping (target device is picked on the left panel) ----
    QGroupBox* cfgGroup = new QGroupBox("Axis Role Mapping  (target device \xE2\x86\x92 role)");
    cfgGroup->setStyleSheet(groupStyle);
    QGridLayout* cfgLayout = new QGridLayout(cfgGroup);

    // Per-axis action buttons run a single-axis MoveRelative at 0.1 mm/s.
    const QString applyBtnStyle =
        "QPushButton { background-color: #16a085; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 6px 14px; font-size: 12px; }"
        "QPushButton:hover { background-color: #1abc9c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    auto makeApplyBtn = [&](const QString& label) {
        QPushButton* b = new QPushButton(label);
        b->setFixedHeight(28);
        b->setStyleSheet(applyBtnStyle);
        b->setToolTip("Apply this axis alone at 0.1 mm/s, then restore 1 mm/s.");
        return b;
    };

    cfgLayout->addWidget(new QLabel("U role:"), 0, 0);
    m_uRoleCombo = new QComboBox();
    populateRoleCombo(m_uRoleCombo);
    cfgLayout->addWidget(m_uRoleCombo, 0, 1);
    m_reverseUCheck = new QCheckBox("Reverse U");
    cfgLayout->addWidget(m_reverseUCheck, 0, 2);
    m_enableUCheck = new QCheckBox("Enable U");
    m_enableUCheck->setToolTip("Include U axis when Apply Selected Corrections runs.");
    cfgLayout->addWidget(m_enableUCheck, 0, 3);
    m_applyUBtn = makeApplyBtn("Apply U");
    cfgLayout->addWidget(m_applyUBtn, 0, 4);

    cfgLayout->addWidget(new QLabel("V role:"), 1, 0);
    m_vRoleCombo = new QComboBox();
    populateRoleCombo(m_vRoleCombo);
    cfgLayout->addWidget(m_vRoleCombo, 1, 1);
    m_reverseVCheck = new QCheckBox("Reverse V");
    cfgLayout->addWidget(m_reverseVCheck, 1, 2);
    m_enableVCheck = new QCheckBox("Enable V");
    m_enableVCheck->setToolTip("Include V axis when Apply Selected Corrections runs.");
    cfgLayout->addWidget(m_enableVCheck, 1, 3);
    m_applyVBtn = makeApplyBtn("Apply V");
    cfgLayout->addWidget(m_applyVBtn, 1, 4);

    cfgLayout->addWidget(new QLabel("W role:"), 2, 0);
    m_wRoleCombo = new QComboBox();
    populateRoleCombo(m_wRoleCombo);
    cfgLayout->addWidget(m_wRoleCombo, 2, 1);
    m_reverseWCheck = new QCheckBox("Reverse W");
    cfgLayout->addWidget(m_reverseWCheck, 2, 2);
    m_enableWCheck = new QCheckBox("Enable W");
    m_enableWCheck->setToolTip("Include W axis when Apply Selected Corrections runs.");
    cfgLayout->addWidget(m_enableWCheck, 2, 3);
    m_applyWBtn = makeApplyBtn("Apply W");
    cfgLayout->addWidget(m_applyWBtn, 2, 4);

    m_recalcBtn = new QPushButton("Recalculate");
    m_recalcBtn->setFixedHeight(32);
    m_recalcBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 4px 16px; }"
        "QPushButton:hover { background-color: #3498db; }");
    cfgLayout->addWidget(m_recalcBtn, 3, 0, 1, 5);

    m_applySelectedBtn = new QPushButton("Apply Selected Corrections");
    m_applySelectedBtn->setFixedHeight(36);
    m_applySelectedBtn->setStyleSheet(
        "QPushButton { background-color: #16a085; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 16px; font-size: 13px; }"
        "QPushButton:hover { background-color: #1abc9c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }");
    m_applySelectedBtn->setToolTip(
        "Applies only the axes where 'Enable' is checked,\n"
        "one at a time, at 0.1 mm/s.");
    cfgLayout->addWidget(m_applySelectedBtn, 4, 0, 1, 5);

    m_applyStatusLabel = new QLabel();
    m_applyStatusLabel->setStyleSheet(
        "color: #f0c040; font-family: monospace; font-size: 11px; padding: 4px;");
    m_applyStatusLabel->setWordWrap(true);
    cfgLayout->addWidget(m_applyStatusLabel, 5, 0, 1, 5);

    mainLayout->addWidget(cfgGroup);

    connect(m_uRoleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EdgeResultWidget::onAnyConfigChanged);
    connect(m_vRoleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EdgeResultWidget::onAnyConfigChanged);
    connect(m_wRoleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EdgeResultWidget::onAnyConfigChanged);
    connect(m_reverseUCheck, &QCheckBox::toggled,
            this, &EdgeResultWidget::onAnyConfigChanged);
    connect(m_reverseVCheck, &QCheckBox::toggled,
            this, &EdgeResultWidget::onAnyConfigChanged);
    connect(m_reverseWCheck, &QCheckBox::toggled,
            this, &EdgeResultWidget::onAnyConfigChanged);
    connect(m_enableUCheck,  &QCheckBox::toggled,
            this, &EdgeResultWidget::onAnyConfigChanged);
    connect(m_enableVCheck,  &QCheckBox::toggled,
            this, &EdgeResultWidget::onAnyConfigChanged);
    connect(m_enableWCheck,  &QCheckBox::toggled,
            this, &EdgeResultWidget::onAnyConfigChanged);
    connect(m_applyUBtn, &QPushButton::clicked, this, &EdgeResultWidget::onApplyU);
    connect(m_applyVBtn, &QPushButton::clicked, this, &EdgeResultWidget::onApplyV);
    connect(m_applyWBtn, &QPushButton::clicked, this, &EdgeResultWidget::onApplyW);
    connect(m_applySelectedBtn, &QPushButton::clicked,
            this, &EdgeResultWidget::onApplySelected);
    connect(m_recalcBtn, &QPushButton::clicked, this, [this]() {
        emit recalculateRequested();
    });

    // ---- Per-edge metrics ----
    QGroupBox* metricsGroup = new QGroupBox("Per-edge Metrics");
    metricsGroup->setStyleSheet(groupStyle);
    QVBoxLayout* metricsLayout = new QVBoxLayout(metricsGroup);

    m_refMetricsLabel = new QLabel();
    m_refMetricsLabel->setStyleSheet(
        "font-family: monospace; color: #d0e0ff; background-color: #1a2a3f; "
        "border: 1px solid #3d5a8c; border-radius: 4px; padding: 8px;");
    m_refMetricsLabel->setTextFormat(Qt::PlainText);
    metricsLayout->addWidget(m_refMetricsLabel);

    m_tgtMetricsLabel = new QLabel();
    m_tgtMetricsLabel->setStyleSheet(
        "font-family: monospace; color: #d0ffd0; background-color: #1a2a3f; "
        "border: 1px solid #3d5a8c; border-radius: 4px; padding: 8px;");
    m_tgtMetricsLabel->setTextFormat(Qt::PlainText);
    metricsLayout->addWidget(m_tgtMetricsLabel);

    mainLayout->addWidget(metricsGroup);

    // ---- Deltas ----
    QGroupBox* deltaGroup = new QGroupBox("Deltas  (target \xE2\x88\x92 reference)");
    deltaGroup->setStyleSheet(groupStyle);
    QVBoxLayout* deltaLayout = new QVBoxLayout(deltaGroup);
    m_deltaLabel = new QLabel();
    m_deltaLabel->setStyleSheet(
        "font-family: monospace; color: #ffffff; background-color: #1a2a3f; "
        "border: 1px solid #e67e22; border-radius: 4px; padding: 8px; font-size: 13px;");
    m_deltaLabel->setTextFormat(Qt::PlainText);
    deltaLayout->addWidget(m_deltaLabel);
    mainLayout->addWidget(deltaGroup);

    // ---- Corrections ----
    QGroupBox* corrGroup = new QGroupBox("Suggested Corrections (UVW on target device)");
    corrGroup->setStyleSheet(groupStyle);
    QVBoxLayout* corrLayout = new QVBoxLayout(corrGroup);
    m_correctionLabel = new QLabel();
    m_correctionLabel->setStyleSheet(
        "font-family: monospace; color: #ffffff; background-color: #1a2a3f; "
        "border: 1px solid #27ae60; border-radius: 4px; padding: 8px; font-size: 13px;");
    m_correctionLabel->setTextFormat(Qt::PlainText);
    corrLayout->addWidget(m_correctionLabel);
    mainLayout->addWidget(corrGroup);

    // ---- Steps narrative ----
    QGroupBox* stepGroup = new QGroupBox("Calculation Steps");
    stepGroup->setStyleSheet(groupStyle);
    QVBoxLayout* stepLayout = new QVBoxLayout(stepGroup);
    m_stepLabel = new QLabel();
    m_stepLabel->setStyleSheet(
        "color: #cccccc; background-color: #111820; border: 1px solid #2d3e5f; "
        "border-radius: 4px; padding: 8px;");
    m_stepLabel->setTextFormat(Qt::PlainText);
    m_stepLabel->setWordWrap(true);
    stepLayout->addWidget(m_stepLabel);
    mainLayout->addWidget(stepGroup);

    mainLayout->addStretch();
}

void EdgeResultWidget::SetData(const EdgeTransferSet& set)
{
    m_set = set;

    // Reflect config into UI without re-emitting change signals.
    const QSignalBlocker b2(m_uRoleCombo);
    const QSignalBlocker b3(m_vRoleCombo);
    const QSignalBlocker b4(m_wRoleCombo);
    const QSignalBlocker b5(m_reverseUCheck);
    const QSignalBlocker b6(m_reverseVCheck);
    const QSignalBlocker b7(m_reverseWCheck);
    const QSignalBlocker b8(m_enableUCheck);
    const QSignalBlocker b9(m_enableVCheck);
    const QSignalBlocker b10(m_enableWCheck);

    auto findRole = [](QComboBox* cb, EdgeAxisRole r) {
        for (int i = 0; i < cb->count(); ++i) {
            if (cb->itemData(i).toInt() == int(r)) return i;
        }
        return 0;
    };
    m_uRoleCombo->setCurrentIndex(findRole(m_uRoleCombo, set.config.mapping.u));
    m_vRoleCombo->setCurrentIndex(findRole(m_vRoleCombo, set.config.mapping.v));
    m_wRoleCombo->setCurrentIndex(findRole(m_wRoleCombo, set.config.mapping.w));
    m_reverseUCheck->setChecked(set.config.mapping.reverseU);
    m_reverseVCheck->setChecked(set.config.mapping.reverseV);
    m_reverseWCheck->setChecked(set.config.mapping.reverseW);
    m_enableUCheck->setChecked(set.config.mapping.applyU);
    m_enableVCheck->setChecked(set.config.mapping.applyV);
    m_enableWCheck->setChecked(set.config.mapping.applyW);

    refreshDisplay();
}

void EdgeResultWidget::onAnyConfigChanged()
{
    // Preserve targetDevice + cameraId — they are driven from the left panel.
    EdgeTransferConfig cfg = m_set.config;
    cfg.mapping.u = EdgeAxisRole(m_uRoleCombo->currentData().toInt());
    cfg.mapping.v = EdgeAxisRole(m_vRoleCombo->currentData().toInt());
    cfg.mapping.w = EdgeAxisRole(m_wRoleCombo->currentData().toInt());
    cfg.mapping.reverseU = m_reverseUCheck->isChecked();
    cfg.mapping.reverseV = m_reverseVCheck->isChecked();
    cfg.mapping.reverseW = m_reverseWCheck->isChecked();
    cfg.mapping.applyU   = m_enableUCheck->isChecked();
    cfg.mapping.applyV   = m_enableVCheck->isChecked();
    cfg.mapping.applyW   = m_enableWCheck->isChecked();
    m_set.config = cfg;
    m_set.Recalculate();
    refreshDisplay();
    emit configChanged(cfg);
}

void EdgeResultWidget::SetApplyButtonsLocked(bool locked)
{
    m_externallyLocked = locked;
    // If apply is already running, its own guard keeps buttons disabled;
    // otherwise reflect the external lock immediately.
    const bool en = !locked && !m_applyInProgress;
    if (m_applyUBtn)        m_applyUBtn->setEnabled(en);
    if (m_applyVBtn)        m_applyVBtn->setEnabled(en);
    if (m_applyWBtn)        m_applyWBtn->setEnabled(en);
    if (m_applySelectedBtn) m_applySelectedBtn->setEnabled(en);
}

void EdgeResultWidget::onApplySelected()
{
    if (!m_set.deltasValid) {
        QMessageBox::information(this, "Apply Selected Corrections",
            "Capture both edges first.");
        return;
    }
    // Only axes flagged applyX=true are included.
    std::vector<std::pair<std::string, double>> axisDeltas;
    if (m_set.config.mapping.applyU) axisDeltas.emplace_back("U", m_set.correctionU);
    if (m_set.config.mapping.applyV) axisDeltas.emplace_back("V", m_set.correctionV);
    if (m_set.config.mapping.applyW) axisDeltas.emplace_back("W", m_set.correctionW);
    if (axisDeltas.empty()) {
        QMessageBox::information(this, "Apply Selected Corrections",
            "No axes are enabled. Tick the Enable boxes for U / V / W first.");
        return;
    }
    applyCorrectionsAsync(std::move(axisDeltas), "Apply Selected Corrections");
}

void EdgeResultWidget::onApplyU()
{
    if (!m_set.deltasValid) {
        QMessageBox::information(this, "Apply U",
            "Capture both edges first.");
        return;
    }
    applyCorrectionsAsync({{"U", m_set.correctionU}}, "Apply U");
}

void EdgeResultWidget::onApplyV()
{
    if (!m_set.deltasValid) {
        QMessageBox::information(this, "Apply V",
            "Capture both edges first.");
        return;
    }
    applyCorrectionsAsync({{"V", m_set.correctionV}}, "Apply V");
}

void EdgeResultWidget::onApplyW()
{
    if (!m_set.deltasValid) {
        QMessageBox::information(this, "Apply W",
            "Capture both edges first.");
        return;
    }
    applyCorrectionsAsync({{"W", m_set.correctionW}}, "Apply W");
}

// Moves each (axis, delta) one at a time on the target hex device, at
// 0.1 mm/s, restoring 1 mm/s after the last axis. Zero-delta axes skipped.
void EdgeResultWidget::applyCorrectionsAsync(
    std::vector<std::pair<std::string, double>> axisDeltas,
    const QString& actionName)
{
    if (m_applyInProgress) return;
    if (m_externallyLocked) {
        QMessageBox::information(this, actionName,
            "Blocked: a gantry nav is currently in progress.");
        return;
    }
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, actionName, "Motion manager not available.");
        return;
    }
    if (m_context->sequenceEngine && m_context->sequenceEngine->IsExecuting()) {
        QMessageBox::warning(this, actionName + " Blocked",
            "A recipe is currently running. Stop the program before applying.");
        return;
    }
    const std::string device = m_set.config.targetDevice;
    if (device.empty()) {
        QMessageBox::warning(this, actionName,
            "No target device selected on the left panel.");
        return;
    }

    constexpr double kSafeVel    = 0.1;  // mm/s during each move
    constexpr double kRestoreVel = 1.0;  // mm/s restored after all moves

    m_applyInProgress = true;
    if (m_applyUBtn)        m_applyUBtn->setEnabled(false);
    if (m_applyVBtn)        m_applyVBtn->setEnabled(false);
    if (m_applyWBtn)        m_applyWBtn->setEnabled(false);
    if (m_applySelectedBtn) m_applySelectedBtn->setEnabled(false);
    m_applyStatusLabel->setText(actionName + ": starting...");
    emit applyBusyChanged(true);

    auto* mm = m_context->motionManager;
    QString actionCopy = actionName;

    m_applyFuture = std::async(std::launch::async,
        [this, mm, device, axisDeltas, actionCopy]() {
        bool allOk = true;
        QString report;
        for (const auto& pair : axisDeltas) {
            const std::string axis = pair.first;  // structured-binding workaround
            const double delta = pair.second;
            if (std::abs(delta) < 1e-9) {
                report += QString("%1: skipped (delta=0)\n").arg(QString::fromStdString(axis));
                continue;
            }
            QMetaObject::invokeMethod(this, [this, axis, delta, actionCopy]() {
                m_applyStatusLabel->setText(QString(
                    "%1: moving %2 by %3 deg at 0.1 mm/s...")
                    .arg(actionCopy)
                    .arg(QString::fromStdString(axis))
                    .arg(delta, 0, 'f', 4));
            }, Qt::QueuedConnection);

            bool velOk  = mm->SetSystemVelocity(device, kSafeVel);
            bool moveOk = mm->MoveRelative(device, axis, delta, /*blocking*/ true);

            std::cout << "[EdgeResult] " << actionCopy.toStdString()
                      << " axis=" << axis << " delta=" << delta
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
        std::cout << "[EdgeResult] velocity restore=" << (restoreOk ? "OK" : "FAIL")
                  << std::endl;

        QMetaObject::invokeMethod(this,
            [this, actionCopy, report, allOk, restoreOk]() {
            m_applyInProgress = false;
            // Respect external lock: don't re-enable buttons if the page
            // is still gating us (e.g. a summary nav is still running).
            const bool en = !m_externallyLocked;
            if (m_applyUBtn)        m_applyUBtn->setEnabled(en);
            if (m_applyVBtn)        m_applyVBtn->setEnabled(en);
            if (m_applyWBtn)        m_applyWBtn->setEnabled(en);
            if (m_applySelectedBtn) m_applySelectedBtn->setEnabled(en);
            m_applyStatusLabel->setText(
                (allOk ? "Done: " : "Failed: ") + actionCopy +
                (restoreOk ? "" : "  [velocity restore FAILED]"));
            emit applyBusyChanged(false);
            if (!allOk) {
                QMessageBox::warning(this, actionCopy + " Failed",
                    "One or more axis moves failed:\n" + report);
            }
        }, Qt::QueuedConnection);
    });
}

void EdgeResultWidget::refreshDisplay()
{
    auto formatEdge = [](const EdgePoints& e, const char* label) {
        if (!e.valid) {
            return QString("%1  (not captured)").arg(label);
        }
        return QString(
            "%1\n"
            "  P1: X=%2  Y=%3  Z=%4\n"
            "  P2: X=%5  Y=%6  Z=%7\n"
            "  XY distance : %8\n"
            "  Z delta     : %9\n"
            "  Angle to +X : %10\n"
            "  Angle to +Y : %11\n"
            "  Vertical tilt: %12")
            .arg(label)
            .arg(e.p1x, 0, 'f', 4).arg(e.p1y, 0, 'f', 4).arg(e.p1z, 0, 'f', 4)
            .arg(e.p2x, 0, 'f', 4).arg(e.p2y, 0, 'f', 4).arg(e.p2z, 0, 'f', 4)
            .arg(fmtMm(e.xyDistance))
            .arg(fmtMm(e.zDelta))
            .arg(fmtDeg(e.angleToX))
            .arg(fmtDeg(e.angleToY))
            .arg(fmtDeg(e.verticalTilt));
    };

    m_refMetricsLabel->setText(formatEdge(m_set.referenceEdge, "Reference edge (P1, P2)"));
    m_tgtMetricsLabel->setText(formatEdge(m_set.targetEdge,    "Target edge    (P3, P4)"));

    if (!m_set.deltasValid) {
        m_deltaLabel->setText("Capture both edges to see deltas.");
        m_correctionLabel->setText("Capture both edges to see corrections.");
        m_stepLabel->setText(
            "1) Capture reference edge (P1,P2) and target edge (P3,P4) in gantry XYZ.\n"
            "2) Per edge, compute: xyDistance, zDelta, angleToX, angleToY, verticalTilt.\n"
            "3) Delta yaw = target.angleToX - reference.angleToX (wrapped to [-180,180]).\n"
            "4) Delta tilt = target.verticalTilt - reference.verticalTilt.\n"
            "5) For each UVW role: correction = -delta (sign-reversed if configured).");
        return;
    }

    m_deltaLabel->setText(QString(
        "Delta yaw (XY plane, atan2(dy,dx)):   %1\n"
        "Delta vertical tilt (elevation)  :   %2")
        .arg(fmtDeg(m_set.deltaYawDeg))
        .arg(fmtDeg(m_set.deltaTiltDeg)));

    auto roleLabel = [](EdgeAxisRole r) { return QString(EdgeAxisRoleToString(r)); };

    auto rev = [](bool b) { return b ? "reversed" : "normal"; };
    auto en  = [](bool b) { return b ? "enabled"  : "disabled"; };
    m_correctionLabel->setText(QString(
        "Target device: %1\n"
        "  U (%2, %3, %4) : %5\n"
        "  V (%6, %7, %8) : %9\n"
        "  W (%10, %11, %12) : %13")
        .arg(QString::fromStdString(m_set.config.targetDevice))
        .arg(roleLabel(m_set.config.mapping.u)).arg(rev(m_set.config.mapping.reverseU)).arg(en(m_set.config.mapping.applyU)).arg(fmtDeg(m_set.correctionU))
        .arg(roleLabel(m_set.config.mapping.v)).arg(rev(m_set.config.mapping.reverseV)).arg(en(m_set.config.mapping.applyV)).arg(fmtDeg(m_set.correctionV))
        .arg(roleLabel(m_set.config.mapping.w)).arg(rev(m_set.config.mapping.reverseW)).arg(en(m_set.config.mapping.applyW)).arg(fmtDeg(m_set.correctionW)));

    const EdgePoints& R = m_set.referenceEdge;
    const EdgePoints& T = m_set.targetEdge;

    m_stepLabel->setText(QString(
        "Step 1  Reference edge: dx=%1 dy=%2 dz=%3\n"
        "        xyDist=%4   angleToX=%5   verticalTilt=%6\n"
        "Step 2  Target edge:    dx=%7 dy=%8 dz=%9\n"
        "        xyDist=%10  angleToX=%11   verticalTilt=%12\n"
        "Step 3  deltaYaw  = %11 - %5  = %13\n"
        "Step 4  deltaTilt = %12 - %6  = %14\n"
        "Step 5  Corrections = -delta (per-axis sign-reversed when checked):\n"
        "        U=%15  V=%16  W=%17")
        .arg(R.p2x - R.p1x, 0, 'f', 4).arg(R.p2y - R.p1y, 0, 'f', 4).arg(R.p2z - R.p1z, 0, 'f', 4)
        .arg(fmtMm(R.xyDistance)).arg(fmtDeg(R.angleToX)).arg(fmtDeg(R.verticalTilt))
        .arg(T.p2x - T.p1x, 0, 'f', 4).arg(T.p2y - T.p1y, 0, 'f', 4).arg(T.p2z - T.p1z, 0, 'f', 4)
        .arg(fmtMm(T.xyDistance)).arg(fmtDeg(T.angleToX)).arg(fmtDeg(T.verticalTilt))
        .arg(fmtDeg(m_set.deltaYawDeg))
        .arg(fmtDeg(m_set.deltaTiltDeg))
        .arg(fmtDeg(m_set.correctionU))
        .arg(fmtDeg(m_set.correctionV))
        .arg(fmtDeg(m_set.correctionW)));
}

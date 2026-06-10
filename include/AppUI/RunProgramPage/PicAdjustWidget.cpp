#include "PicAdjustWidget.h"
#include "PicAdjustStore.h"
#include "ApplicationContext.h"
#include "motions/unified_motion_manager.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <iostream>

namespace {
constexpr const char* kAxes[] = { "X", "Y", "Z" };
}

PicAdjustWidget::PicAdjustWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent), m_context(context)
{
    m_filePath = PicAdjustStore::DefaultFilePath();
    buildUI();
    // Best-effort load on construction; fine if file does not exist yet.
    const bool loaded = PicAdjustStore::Load(m_set, m_settings, m_filePath);
    if (loaded) {
        appendLog(QString("Loaded saved points/settings from %1").arg(QString::fromStdString(m_filePath)));
        for (int i = 0; i < kPicPointCount; ++i) refreshPointLabel(i);
        refreshResults();
    }
    // Push m_settings into UI (signals suppressed). Always runs so fresh-state
    // defaults (invertU/V/W = true) propagate to the checkboxes too.
    m_loadingSettings = true;
    if (m_invertU)            m_invertU->setChecked(m_settings.invertU);
    if (m_invertV)            m_invertV->setChecked(m_settings.invertV);
    if (m_invertW)            m_invertW->setChecked(m_settings.invertW);
    if (m_invertDistance)     m_invertDistance->setChecked(m_settings.invertDistance);
    if (m_distanceTargetSpin) m_distanceTargetSpin->setValue(m_settings.targetDistanceUm);
    if (m_targetDeviceCombo) {
        const int idx = m_targetDeviceCombo->findText(QString::fromStdString(m_settings.targetDevice));
        if (idx >= 0) m_targetDeviceCombo->setCurrentIndex(idx);
    }
    if (m_lockSigns) m_lockSigns->setChecked(m_settings.lockSigns);
    const bool editable = !m_settings.lockSigns;
    if (m_invertU)        m_invertU->setEnabled(editable);
    if (m_invertV)        m_invertV->setEnabled(editable);
    if (m_invertW)        m_invertW->setEnabled(editable);
    if (m_invertDistance) m_invertDistance->setEnabled(editable);
    m_loadingSettings = false;
    refreshDistanceLabel();
}

PicAdjustWidget::~PicAdjustWidget() {
    if (m_moveFuture.valid()) m_moveFuture.wait();
}

void PicAdjustWidget::buildUI() {
    // Outer two-column layout. Left column holds the existing PIC Adjust UI;
    // right column hosts the gantry-main manual jog panel.
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(12);

    auto* root = new QVBoxLayout();
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);
    outer->addLayout(root, /*stretch=*/3);

    // Top row: header + lock-invert-signs toggle
    auto* headerRow = new QHBoxLayout();
    auto* header = new QLabel(
        QString("PIC Adjust  —  device: <b>%1</b>  —  safe velocity: %2 mm/s")
            .arg(QString::fromStdString(m_deviceName))
            .arg(m_safeVelocity, 0, 'f', 1));
    headerRow->addWidget(header, /*stretch=*/1);
    m_lockSigns = new QCheckBox("Lock invert signs");
    m_lockSigns->setToolTip("When checked, the Invert sign checkboxes below are read-only.");
    connect(m_lockSigns, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_invertU)        m_invertU->setEnabled(!checked);
        if (m_invertV)        m_invertV->setEnabled(!checked);
        if (m_invertW)        m_invertW->setEnabled(!checked);
        if (m_invertDistance) m_invertDistance->setEnabled(!checked);
        onSettingsChanged();
    });
    headerRow->addWidget(m_lockSigns);
    root->addLayout(headerRow);

    auto makeRow = [&](QGroupBox* parent, int slot) {
        auto* rowLayout = static_cast<QVBoxLayout*>(parent->layout());
        auto* h = new QHBoxLayout();
        h->setSpacing(8);

        auto* name = new QLabel(QString::fromUtf8(PicPointName(static_cast<PicPointId>(slot))));
        name->setFixedWidth(28);
        QFont nameFont = name->font();
        nameFont.setBold(true);
        name->setFont(nameFont);
        h->addWidget(name);

        m_posLabels[slot] = new QLabel("X: ---  Y: ---  Z: ---");
        m_posLabels[slot]->setStyleSheet("font-family: monospace;");
        m_posLabels[slot]->setMinimumWidth(280);
        h->addWidget(m_posLabels[slot], /*stretch*/ 1);

        m_captureBtns[slot] = new QPushButton("Capture");
        m_captureBtns[slot]->setFixedWidth(90);
        connect(m_captureBtns[slot], &QPushButton::clicked, this, [this, slot]() { onCaptureClicked(slot); });
        h->addWidget(m_captureBtns[slot]);

        m_moveBtns[slot] = new QPushButton("Move To");
        m_moveBtns[slot]->setFixedWidth(90);
        connect(m_moveBtns[slot], &QPushButton::clicked, this, [this, slot]() { onMoveToClicked(slot); });
        h->addWidget(m_moveBtns[slot]);

        rowLayout->addLayout(h);
    };

    // Reference line group
    auto* refBox = new QGroupBox("Reference Line (focus)");
    new QVBoxLayout(refBox);
    makeRow(refBox, static_cast<int>(PicPointId::S1));
    makeRow(refBox, static_cast<int>(PicPointId::S2));
    root->addWidget(refBox);

    // PIC die corners group
    auto* dieBox = new QGroupBox("PIC Die Corners");
    new QVBoxLayout(dieBox);
    makeRow(dieBox, static_cast<int>(PicPointId::P1));
    makeRow(dieBox, static_cast<int>(PicPointId::P2));
    makeRow(dieBox, static_cast<int>(PicPointId::P3));
    makeRow(dieBox, static_cast<int>(PicPointId::P4));
    root->addWidget(dieBox);

    // Results group
    auto* resBox = new QGroupBox("Results (degrees)");
    auto* resLayout = new QVBoxLayout(resBox);
    auto makeResLabel = [](const QString& s) {
        auto* l = new QLabel(s);
        l->setStyleSheet("font-family: monospace;");
        return l;
    };
    m_uRefLabel    = makeResLabel("U_ref   (S1->S2 XY)        = ---");
    m_wRefLabel    = makeResLabel("W_ref   (S1->S2 ZY)        = ---");
    m_uPicLabel    = makeResLabel("U_pic   (P2->P4 XY)        = ---");
    m_wPicLabel    = makeResLabel("W_pic   (P2->P4 ZY)        = ---");
    m_wRollLLabel  = makeResLabel("W_check (P1->P3 ZY)        = ---");
    m_vPitchTLabel = makeResLabel("V_pitch (P1->P2 ZX)        = ---");
    m_vPitchBLabel = makeResLabel("V_pitch (P3->P4 ZX)        = ---");
    m_deltaULabel  = makeResLabel("ΔU (U_pic - U_ref)         = ---");
    m_deltaWLabel  = makeResLabel("ΔW (W_pic - W_ref)         = ---");
    m_vAvgLabel    = makeResLabel("V  (avg V_pitch)           = ---");
    for (QLabel* l : { m_uRefLabel, m_wRefLabel, m_uPicLabel,
                        m_wPicLabel, m_wRollLLabel,
                        m_vPitchTLabel, m_vPitchBLabel,
                        m_deltaULabel, m_deltaWLabel, m_vAvgLabel }) {
        resLayout->addWidget(l);
    }
    root->addWidget(resBox);

    // Apply correction group (hex rotation about externally-set pivot)
    auto* applyBox = new QGroupBox("Apply Correction (relative rotation on hex device)");
    auto* applyLayout = new QVBoxLayout(applyBox);

    auto* devRow = new QHBoxLayout();
    devRow->addWidget(new QLabel("Target device:"));
    m_targetDeviceCombo = new QComboBox();
    m_targetDeviceCombo->addItem("hex-left");
    m_targetDeviceCombo->addItem("hex-right");
    m_targetDeviceCombo->setFixedWidth(140);
    connect(m_targetDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ onSettingsChanged(); });
    devRow->addWidget(m_targetDeviceCombo);
    devRow->addStretch();
    devRow->addWidget(new QLabel(
        QString("safe sys vel: %1 mm/s").arg(m_hexSafeVelocity, 0, 'f', 1)));
    applyLayout->addLayout(devRow);

    auto makeApplyRow = [&](const QString& label, QPushButton*& btnOut, QCheckBox*& invOut, char axis) {
        auto* h = new QHBoxLayout();
        btnOut = new QPushButton(label);
        btnOut->setFixedWidth(160);
        connect(btnOut, &QPushButton::clicked, this, [this, axis]() { onApplyClicked(axis); });
        h->addWidget(btnOut);

        invOut = new QCheckBox("Invert sign");
        connect(invOut, &QCheckBox::toggled, this, [this](bool){ onSettingsChanged(); });
        h->addWidget(invOut);
        h->addStretch();
        applyLayout->addLayout(h);
    };
    makeApplyRow("Apply ΔU",  m_applyUBtn, m_invertU, 'U');
    makeApplyRow("Apply V",   m_applyVBtn, m_invertV, 'V');
    makeApplyRow("Apply ΔW",  m_applyWBtn, m_invertW, 'W');
    root->addWidget(applyBox);

    // Distance Adjustment group: move hex toward target gap.
    // Gap = avg perpendicular XY distance from (P2, P4) to the S1->S2 line.
    auto* distBox = new QGroupBox("Distance Adjustment (perpendicular XY to S1-S2 line)");
    auto* distLayout = new QVBoxLayout(distBox);

    m_distanceCurrentLabel = new QLabel("Current gap = ---  mm");
    m_distanceCurrentLabel->setStyleSheet("font-family: monospace;");
    distLayout->addWidget(m_distanceCurrentLabel);

    auto* distInputRow = new QHBoxLayout();
    distInputRow->addWidget(new QLabel("Target gap:"));
    m_distanceTargetSpin = new QDoubleSpinBox();
    m_distanceTargetSpin->setRange(0.0, 100000.0);
    m_distanceTargetSpin->setDecimals(1);
    m_distanceTargetSpin->setSingleStep(10.0);
    m_distanceTargetSpin->setValue(m_settings.targetDistanceUm);
    m_distanceTargetSpin->setSuffix(" µm");
    m_distanceTargetSpin->setFixedWidth(120);
    connect(m_distanceTargetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ onSettingsChanged(); refreshDistanceLabel(); });
    distInputRow->addWidget(m_distanceTargetSpin);
    distInputRow->addStretch();
    distLayout->addLayout(distInputRow);

    auto* distActionRow = new QHBoxLayout();
    m_applyDistanceBtn = new QPushButton("Move hex Z toward target");
    m_applyDistanceBtn->setFixedWidth(220);
    connect(m_applyDistanceBtn, &QPushButton::clicked, this, &PicAdjustWidget::onApplyDistanceClicked);
    distActionRow->addWidget(m_applyDistanceBtn);
    m_invertDistance = new QCheckBox("Invert sign");
    connect(m_invertDistance, &QCheckBox::toggled, this, [this](bool){ onSettingsChanged(); refreshDistanceLabel(); });
    distActionRow->addWidget(m_invertDistance);
    distActionRow->addStretch();
    distLayout->addLayout(distActionRow);
    root->addWidget(distBox);

    // Action row
    auto* actionRow = new QHBoxLayout();
    auto* saveBtn      = new QPushButton("Save");
    auto* loadBtn      = new QPushButton("Load");
    auto* recomputeBtn = new QPushButton("Recompute");
    saveBtn->setFixedWidth(90);
    loadBtn->setFixedWidth(90);
    recomputeBtn->setFixedWidth(110);
    connect(saveBtn,      &QPushButton::clicked, this, &PicAdjustWidget::onSaveClicked);
    connect(loadBtn,      &QPushButton::clicked, this, &PicAdjustWidget::onLoadClicked);
    connect(recomputeBtn, &QPushButton::clicked, this, &PicAdjustWidget::onRecomputeClicked);
    actionRow->addWidget(saveBtn);
    actionRow->addWidget(loadBtn);
    actionRow->addWidget(recomputeBtn);
    actionRow->addStretch();
    root->addLayout(actionRow);

    // Log
    m_logEdit = new QPlainTextEdit();
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumHeight(120);
    m_logEdit->setStyleSheet("font-family: monospace; font-size: 11px;");
    root->addWidget(m_logEdit);

    // ----- Right column: gantry-main manual jog -----
    auto* rightCol = new QVBoxLayout();
    rightCol->setContentsMargins(0, 0, 0, 0);
    rightCol->setSpacing(6);
    outer->addLayout(rightCol, /*stretch=*/1);

    auto* jogBox = new QGroupBox(
        QString("Jog %1").arg(QString::fromStdString(m_deviceName)));
    auto* jogLayout = new QVBoxLayout(jogBox);

    // Step selector (2 / 5 / 10 µm)
    auto* stepRow = new QHBoxLayout();
    stepRow->addWidget(new QLabel("Step:"));
    m_jogStep2  = new QRadioButton("2 µm");
    m_jogStep5  = new QRadioButton("5 µm");
    m_jogStep10 = new QRadioButton("10 µm");
    m_jogStep10->setChecked(true);
    auto* stepGroup = new QButtonGroup(this);
    stepGroup->addButton(m_jogStep2);
    stepGroup->addButton(m_jogStep5);
    stepGroup->addButton(m_jogStep10);
    stepRow->addWidget(m_jogStep2);
    stepRow->addWidget(m_jogStep5);
    stepRow->addWidget(m_jogStep10);
    stepRow->addStretch();
    jogLayout->addLayout(stepRow);

    // Per-axis +/- rows
    auto makeJogRow = [&](const QString& axisLabel, QPushButton*& minusBtn, QPushButton*& plusBtn,
                         const QString& axisCode) {
        auto* row = new QHBoxLayout();
        auto* lbl = new QLabel(axisLabel);
        lbl->setFixedWidth(20);
        QFont f = lbl->font(); f.setBold(true); lbl->setFont(f);
        row->addWidget(lbl);
        minusBtn = new QPushButton(QString("%1 −").arg(axisCode));
        plusBtn  = new QPushButton(QString("%1 +").arg(axisCode));
        minusBtn->setFixedWidth(80);
        plusBtn ->setFixedWidth(80);
        connect(minusBtn, &QPushButton::clicked, this, [this, axisCode]() { onJogClicked(axisCode, -1); });
        connect(plusBtn,  &QPushButton::clicked, this, [this, axisCode]() { onJogClicked(axisCode, +1); });
        row->addWidget(minusBtn);
        row->addWidget(plusBtn);
        row->addStretch();
        jogLayout->addLayout(row);
    };
    makeJogRow("X:", m_jogXmBtn, m_jogXpBtn, "X");
    makeJogRow("Y:", m_jogYmBtn, m_jogYpBtn, "Y");
    makeJogRow("Z:", m_jogZmBtn, m_jogZpBtn, "Z");

    rightCol->addWidget(jogBox);

    // Quick action: drop hex-right by a fixed 150 µm in Z (e.g. clearance before retest)
    auto* hexQuickBox = new QGroupBox("Hex quick step");
    auto* hexQuickLayout = new QVBoxLayout(hexQuickBox);
    auto* hexDownBtn = new QPushButton("Move hex-right down 150 µm (X−)");
    connect(hexDownBtn, &QPushButton::clicked, this, &PicAdjustWidget::onMoveHexDownClicked);
    hexQuickLayout->addWidget(hexDownBtn);
    rightCol->addWidget(hexQuickBox);

    rightCol->addStretch();
}

void PicAdjustWidget::onCaptureClicked(int slot) {
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "PIC Adjust", "Motion manager not available.");
        return;
    }
    if (slot < 0 || slot >= kPicPointCount) return;

    PositionStruct pos = m_context->motionManager->GetCurrentPosition(m_deviceName);
    auto& p = m_set.points[slot];
    p.captured = true;
    p.x = pos.x;
    p.y = pos.y;
    p.z = pos.z;

    refreshPointLabel(slot);
    refreshResults();

    if (PicAdjustStore::Save(m_set, m_settings, m_filePath)) {
        appendLog(QString("Captured %1 at X=%2 Y=%3 Z=%4 (saved)")
            .arg(QString::fromUtf8(PicPointName(static_cast<PicPointId>(slot))))
            .arg(p.x, 0, 'f', 4).arg(p.y, 0, 'f', 4).arg(p.z, 0, 'f', 4));
    } else {
        appendLog(QString("Captured %1 — SAVE FAILED to %2")
            .arg(QString::fromUtf8(PicPointName(static_cast<PicPointId>(slot))))
            .arg(QString::fromStdString(m_filePath)));
    }
}

void PicAdjustWidget::onMoveToClicked(int slot) {
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "PIC Adjust", "Motion manager not available.");
        return;
    }
    if (slot < 0 || slot >= kPicPointCount) return;
    if (m_moveInFlight) {
        appendLog("Move already in flight; ignored.");
        return;
    }
    const auto& p = m_set.points[slot];
    if (!p.captured) {
        QMessageBox::information(this, "PIC Adjust",
            QString("%1 has not been captured yet.")
                .arg(QString::fromUtf8(PicPointName(static_cast<PicPointId>(slot)))));
        return;
    }
    startMoveToAsync(slot);
}

void PicAdjustWidget::startMoveToAsync(int slot) {
    setRowEnabled(false);
    m_moveInFlight = true;

    const PicPoint target = m_set.points[slot];
    const QString  name   = QString::fromUtf8(PicPointName(static_cast<PicPointId>(slot)));
    auto* mm    = m_context->motionManager;
    auto  dev   = m_deviceName;
    double safe = m_safeVelocity;

    appendLog(QString("Move-to %1: X=%2 Y=%3 Z=%4 at safe %5 mm/s")
        .arg(name)
        .arg(target.x, 0, 'f', 4).arg(target.y, 0, 'f', 4).arg(target.z, 0, 'f', 4)
        .arg(safe, 0, 'f', 1));

    m_moveFuture = std::async(std::launch::async,
        [this, mm, dev, target, name, safe]() {
        // Snapshot per-axis velocities so we can restore exactly what was there.
        double v0[3] = {0.0, 0.0, 0.0};
        bool   have[3] = { false, false, false };
        for (int i = 0; i < 3; ++i) {
            have[i] = mm->GetVelocity(dev, kAxes[i], v0[i]);
        }
        // Apply safe velocity per axis (best-effort: log any axis that fails).
        for (int i = 0; i < 3; ++i) {
            if (!mm->SetVelocity(dev, kAxes[i], safe)) {
                std::cerr << "[PicAdjust] SetVelocity " << kAxes[i] << " -> " << safe << " failed\n";
            }
        }

        bool moveOk = mm->MoveToAbs(dev, target.x, target.y, target.z, safe, /*blocking*/ true);

        // Restore velocities for axes where we captured a valid prior value.
        for (int i = 0; i < 3; ++i) {
            if (have[i]) {
                mm->SetVelocity(dev, kAxes[i], v0[i]);
            }
        }

        QString line = moveOk
            ? QString("Move-to %1 complete (velocities restored).").arg(name)
            : QString("Move-to %1 FAILED (velocities restored).").arg(name);

        QMetaObject::invokeMethod(this, [this, line]() {
            appendLog(line);
            m_moveInFlight = false;
            setRowEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void PicAdjustWidget::setRowEnabled(bool enabled) {
    for (int i = 0; i < kPicPointCount; ++i) {
        if (m_captureBtns[i]) m_captureBtns[i]->setEnabled(enabled);
        if (m_moveBtns[i])    m_moveBtns[i]->setEnabled(enabled);
    }
    if (m_applyUBtn)        m_applyUBtn->setEnabled(enabled);
    if (m_applyVBtn)        m_applyVBtn->setEnabled(enabled);
    if (m_applyWBtn)        m_applyWBtn->setEnabled(enabled);
    if (m_applyDistanceBtn) m_applyDistanceBtn->setEnabled(enabled);
    if (m_jogXmBtn) m_jogXmBtn->setEnabled(enabled);
    if (m_jogXpBtn) m_jogXpBtn->setEnabled(enabled);
    if (m_jogYmBtn) m_jogYmBtn->setEnabled(enabled);
    if (m_jogYpBtn) m_jogYpBtn->setEnabled(enabled);
    if (m_jogZmBtn) m_jogZmBtn->setEnabled(enabled);
    if (m_jogZpBtn) m_jogZpBtn->setEnabled(enabled);
}

void PicAdjustWidget::onJogClicked(const QString& axis, int sign) {
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "PIC Adjust", "Motion manager not available.");
        return;
    }
    if (m_moveInFlight) { appendLog("Move in flight; jog ignored."); return; }

    double stepUm = 10.0;
    if (m_jogStep2  && m_jogStep2->isChecked())  stepUm = 2.0;
    else if (m_jogStep5  && m_jogStep5->isChecked())  stepUm = 5.0;
    else if (m_jogStep10 && m_jogStep10->isChecked()) stepUm = 10.0;
    const double distMm = (sign >= 0 ? +1.0 : -1.0) * stepUm / 1000.0;
    startJogAsync(axis, distMm);
}

void PicAdjustWidget::onMoveHexDownClicked() {
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "PIC Adjust", "Motion manager not available.");
        return;
    }
    if (m_moveInFlight) { appendLog("Move in flight; ignored."); return; }
    QMessageBox::StandardButton confirm = QMessageBox::question(this, "Move hex-right down",
        QString("Move hex-right X by −0.150 mm (down) at %1 mm/s?").arg(m_hexSafeVelocity, 0, 'f', 2),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) return;

    setRowEnabled(false);
    m_moveInFlight = true;

    auto* mm = m_context->motionManager;
    const std::string dev = "hex-right";
    const double safe = m_hexSafeVelocity;
    const double deltaMm = -0.150;

    appendLog(QString("Move %1 X by %2 mm (safe sys vel %3 mm/s)")
        .arg(QString::fromStdString(dev)).arg(deltaMm, 0, 'f', 5).arg(safe, 0, 'f', 2));

    m_moveFuture = std::async(std::launch::async, [this, mm, dev, deltaMm, safe]() {
        PositionStruct cur = mm->GetCurrentPosition(dev);
        PositionStruct target = cur;
        target.x = cur.x + deltaMm;

        double v0 = 0.0;
        const bool haveV0 = mm->GetSystemVelocity(dev, v0);
        if (haveV0) mm->SetSystemVelocity(dev, safe);
        const bool moveOk = mm->MoveToAbs6(dev, target, /*blocking*/ true);
        if (haveV0) mm->SetSystemVelocity(dev, v0);

        const QString line = moveOk
            ? QString("Hex quick step complete: %1 X=%2 -> %3 (vel restored)")
                .arg(QString::fromStdString(dev))
                .arg(cur.x, 0, 'f', 5).arg(target.x, 0, 'f', 5)
            : QString("Hex quick step FAILED on %1 (vel restored)")
                .arg(QString::fromStdString(dev));
        QMetaObject::invokeMethod(this, [this, line]() {
            appendLog(line);
            m_moveInFlight = false;
            setRowEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void PicAdjustWidget::startJogAsync(const QString& axis, double distMm) {
    setRowEnabled(false);
    m_moveInFlight = true;

    auto* mm = m_context->motionManager;
    const std::string dev = m_deviceName;
    const std::string axisStr = axis.toStdString();

    appendLog(QString("Jog %1 %2: %3 mm")
        .arg(QString::fromStdString(dev)).arg(axis).arg(distMm, 0, 'f', 5));

    m_moveFuture = std::async(std::launch::async,
        [this, mm, dev, axisStr, distMm, axis]() {
        const bool ok = mm->MoveRelative(dev, axisStr, distMm, /*blocking*/ true);
        const QString line = ok
            ? QString("Jog %1 complete (%2 mm)").arg(axis).arg(distMm, 0, 'f', 5)
            : QString("Jog %1 FAILED").arg(axis);
        QMetaObject::invokeMethod(this, [this, line]() {
            appendLog(line);
            m_moveInFlight = false;
            setRowEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void PicAdjustWidget::onApplyClicked(char axis) {
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "PIC Adjust", "Motion manager not available.");
        return;
    }
    if (m_moveInFlight) {
        appendLog("Move already in flight; ignored.");
        return;
    }

    const PicAdjustResult r = PicAdjustMath::Compute(m_set);
    std::optional<double> delta;
    bool invert = false;
    QString label;
    switch (axis) {
        case 'U': delta = r.deltaUDeg; invert = m_invertU && m_invertU->isChecked(); label = "ΔU"; break;
        case 'V': delta = r.vAvgDeg;   invert = m_invertV && m_invertV->isChecked(); label = "V";  break;
        case 'W': delta = r.deltaWDeg; invert = m_invertW && m_invertW->isChecked(); label = "ΔW"; break;
        default: return;
    }
    if (!delta) {
        appendLog(QString("Cannot apply %1: required points not captured.").arg(label));
        return;
    }
    const double signed_delta = invert ? -*delta : *delta;

    const QString dev = m_targetDeviceCombo ? m_targetDeviceCombo->currentText() : QString("hex-left");
    QMessageBox::StandardButton confirm = QMessageBox::question(this, "Apply correction",
        QString("Apply %1 = %2° on %3 (axis %4)%5 ?")
            .arg(label)
            .arg(signed_delta, 0, 'f', 5)
            .arg(dev)
            .arg(QChar(axis))
            .arg(invert ? "  [sign inverted]" : ""),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) return;

    startApplyAsync(axis, signed_delta);
}

void PicAdjustWidget::startApplyAsync(char axis, double deltaDeg) {
    setRowEnabled(false);
    m_moveInFlight = true;

    const std::string dev = m_targetDeviceCombo
        ? m_targetDeviceCombo->currentText().toStdString()
        : std::string("hex-left");
    auto* mm   = m_context->motionManager;
    double safe = m_hexSafeVelocity;

    appendLog(QString("Apply %1 on %2: %3°  (safe sys vel %4 mm/s)")
        .arg(QChar(axis)).arg(QString::fromStdString(dev))
        .arg(deltaDeg, 0, 'f', 5).arg(safe, 0, 'f', 2));

    m_moveFuture = std::async(std::launch::async,
        [this, mm, dev, axis, deltaDeg, safe]() {
        PositionStruct cur = mm->GetCurrentPosition(dev);
        PositionStruct target = cur;
        switch (axis) {
            case 'U': target.u = cur.u + deltaDeg; break;
            case 'V': target.v = cur.v + deltaDeg; break;
            case 'W': target.w = cur.w + deltaDeg; break;
            default: break;
        }

        // PI hex uses system velocity; fall through gracefully for non-PI devices.
        double v0 = 0.0;
        const bool haveV0 = mm->GetSystemVelocity(dev, v0);
        if (haveV0) {
            if (!mm->SetSystemVelocity(dev, safe)) {
                std::cerr << "[PicAdjust] SetSystemVelocity " << dev << " -> " << safe << " failed\n";
            }
        }

        const bool moveOk = mm->MoveToAbs6(dev, target, /*blocking*/ true);

        if (haveV0) {
            mm->SetSystemVelocity(dev, v0);
        }

        double curAxis = 0.0, tgtAxis = 0.0;
        switch (axis) {
            case 'U': curAxis = cur.u; tgtAxis = target.u; break;
            case 'V': curAxis = cur.v; tgtAxis = target.v; break;
            case 'W': curAxis = cur.w; tgtAxis = target.w; break;
        }
        const QString line = moveOk
            ? QString("Apply %1 complete: %2 %3=%4 -> %5  (vel restored)")
                .arg(QChar(axis)).arg(QString::fromStdString(dev)).arg(QChar(axis))
                .arg(curAxis, 0, 'f', 5).arg(tgtAxis, 0, 'f', 5)
            : QString("Apply %1 FAILED on %2 (vel restored)")
                .arg(QChar(axis)).arg(QString::fromStdString(dev));

        QMetaObject::invokeMethod(this, [this, line]() {
            appendLog(line);
            m_moveInFlight = false;
            setRowEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void PicAdjustWidget::onSaveClicked() {
    persistAll();
}

void PicAdjustWidget::onLoadClicked() {
    PicAdjustSet next;
    PicAdjustSettings nextSettings;
    if (!PicAdjustStore::Load(next, nextSettings, m_filePath)) {
        appendLog(QString("Load failed or file missing: %1").arg(QString::fromStdString(m_filePath)));
        return;
    }
    m_set = next;
    m_settings = nextSettings;
    for (int i = 0; i < kPicPointCount; ++i) refreshPointLabel(i);
    refreshResults();
    m_loadingSettings = true;
    if (m_invertU)            m_invertU->setChecked(m_settings.invertU);
    if (m_invertV)            m_invertV->setChecked(m_settings.invertV);
    if (m_invertW)            m_invertW->setChecked(m_settings.invertW);
    if (m_invertDistance)     m_invertDistance->setChecked(m_settings.invertDistance);
    if (m_distanceTargetSpin) m_distanceTargetSpin->setValue(m_settings.targetDistanceUm);
    if (m_targetDeviceCombo) {
        const int idx = m_targetDeviceCombo->findText(QString::fromStdString(m_settings.targetDevice));
        if (idx >= 0) m_targetDeviceCombo->setCurrentIndex(idx);
    }
    if (m_lockSigns) m_lockSigns->setChecked(m_settings.lockSigns);
    {
        const bool editable = !m_settings.lockSigns;
        if (m_invertU)        m_invertU->setEnabled(editable);
        if (m_invertV)        m_invertV->setEnabled(editable);
        if (m_invertW)        m_invertW->setEnabled(editable);
        if (m_invertDistance) m_invertDistance->setEnabled(editable);
    }
    m_loadingSettings = false;
    refreshDistanceLabel();
    appendLog(QString("Loaded from %1").arg(QString::fromStdString(m_filePath)));
}

void PicAdjustWidget::onSettingsChanged() {
    if (m_loadingSettings) return;
    if (m_invertU)            m_settings.invertU         = m_invertU->isChecked();
    if (m_invertV)            m_settings.invertV         = m_invertV->isChecked();
    if (m_invertW)            m_settings.invertW         = m_invertW->isChecked();
    if (m_invertDistance)     m_settings.invertDistance  = m_invertDistance->isChecked();
    if (m_distanceTargetSpin) m_settings.targetDistanceUm = m_distanceTargetSpin->value();
    if (m_targetDeviceCombo)  m_settings.targetDevice    = m_targetDeviceCombo->currentText().toStdString();
    if (m_lockSigns)          m_settings.lockSigns       = m_lockSigns->isChecked();
    persistAll();
}

void PicAdjustWidget::persistAll() {
    if (PicAdjustStore::Save(m_set, m_settings, m_filePath)) {
        // Quiet save: do not spam the log for routine setting toggles.
    } else {
        appendLog(QString("SAVE FAILED to %1").arg(QString::fromStdString(m_filePath)));
    }
}

void PicAdjustWidget::refreshDistanceLabel() {
    if (!m_distanceCurrentLabel) return;
    const PicDistanceInfo info = PicAdjustMath::ComputeDistance(m_set);
    if (info.currentGapMm) {
        const double gapMm    = *info.currentGapMm;
        const double targetMm = (m_distanceTargetSpin ? m_distanceTargetSpin->value() : m_settings.targetDistanceUm) / 1000.0;
        const double naiveDelta = -(gapMm - targetMm);   // hex Z delta needed (sign: Z- closes a positive gap)
        const bool invert = m_invertDistance && m_invertDistance->isChecked();
        const double appliedDelta = invert ? -naiveDelta : naiveDelta;
        auto fmt = [](const std::optional<double>& v) {
            return v ? QString::number(*v, 'f', 5) : QString("---");
        };
        m_distanceCurrentLabel->setText(QString(
            "perp(P2,S1)=%1 mm   perp(P4,S2)=%2 mm   avg=%3 mm   target=%4 mm   suggested hex Z move=%5 mm")
            .arg(fmt(info.perpP2Mm))
            .arg(fmt(info.perpP4Mm))
            .arg(gapMm,        0, 'f', 5)
            .arg(targetMm,     0, 'f', 5)
            .arg(appliedDelta, 0, 'f', 5));
    } else {
        m_distanceCurrentLabel->setText("Current gap = ---  (capture S1, S2, P2 and P4 first)");
    }
}

void PicAdjustWidget::onApplyDistanceClicked() {
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "PIC Adjust", "Motion manager not available.");
        return;
    }
    if (m_moveInFlight) { appendLog("Move already in flight; ignored."); return; }

    const PicDistanceInfo info = PicAdjustMath::ComputeDistance(m_set);
    if (!info.currentGapMm) {
        appendLog("Cannot apply distance move: need S1, S2 and at least one of P2/P4 captured.");
        return;
    }
    const double gapMm    = *info.currentGapMm;
    const double targetMm = (m_distanceTargetSpin ? m_distanceTargetSpin->value() : m_settings.targetDistanceUm) / 1000.0;
    const double naiveDelta = -(gapMm - targetMm);
    const bool invert = m_invertDistance && m_invertDistance->isChecked();
    const double appliedDelta = invert ? -naiveDelta : naiveDelta;

    const QString dev = m_targetDeviceCombo ? m_targetDeviceCombo->currentText() : QString("hex-right");
    QMessageBox::StandardButton confirm = QMessageBox::question(this, "Move closer",
        QString("Move %1 Z by %2 mm  (current gap=%3 mm, target=%4 mm)%5 ?")
            .arg(dev)
            .arg(appliedDelta, 0, 'f', 5)
            .arg(gapMm, 0, 'f', 5)
            .arg(targetMm, 0, 'f', 5)
            .arg(invert ? "  [sign inverted]" : ""),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) return;

    startDistanceMoveAsync(appliedDelta);
}

void PicAdjustWidget::startDistanceMoveAsync(double deltaMm) {
    setRowEnabled(false);
    m_moveInFlight = true;

    const std::string dev = m_targetDeviceCombo
        ? m_targetDeviceCombo->currentText().toStdString()
        : std::string("hex-right");
    auto* mm   = m_context->motionManager;
    double safe = m_hexSafeVelocity;

    appendLog(QString("Distance move %1: Z+%2 mm  (safe sys vel %3 mm/s)")
        .arg(QString::fromStdString(dev))
        .arg(deltaMm, 0, 'f', 5)
        .arg(safe, 0, 'f', 2));

    m_moveFuture = std::async(std::launch::async,
        [this, mm, dev, deltaMm, safe]() {
        PositionStruct cur = mm->GetCurrentPosition(dev);
        PositionStruct target = cur;
        target.z = cur.z + deltaMm;

        double v0 = 0.0;
        const bool haveV0 = mm->GetSystemVelocity(dev, v0);
        if (haveV0) {
            if (!mm->SetSystemVelocity(dev, safe)) {
                std::cerr << "[PicAdjust] SetSystemVelocity " << dev << " -> " << safe << " failed\n";
            }
        }
        const bool moveOk = mm->MoveToAbs6(dev, target, /*blocking*/ true);
        if (haveV0) mm->SetSystemVelocity(dev, v0);

        const QString line = moveOk
            ? QString("Distance move complete: %1 Z=%2 -> %3  (vel restored)")
                .arg(QString::fromStdString(dev))
                .arg(cur.z, 0, 'f', 5).arg(target.z, 0, 'f', 5)
            : QString("Distance move FAILED on %1 (vel restored)")
                .arg(QString::fromStdString(dev));

        QMetaObject::invokeMethod(this, [this, line]() {
            appendLog(line);
            m_moveInFlight = false;
            setRowEnabled(true);
        }, Qt::QueuedConnection);
    });
}

void PicAdjustWidget::onRecomputeClicked() {
    refreshResults();
}

void PicAdjustWidget::refreshPointLabel(int slot) {
    if (slot < 0 || slot >= kPicPointCount) return;
    QLabel* l = m_posLabels[slot];
    if (!l) return;
    const auto& p = m_set.points[slot];
    if (!p.captured) {
        l->setText("X: ---  Y: ---  Z: ---");
        l->setStyleSheet("font-family: monospace; color: #888;");
    } else {
        l->setText(QString("X: %1  Y: %2  Z: %3")
            .arg(p.x, 10, 'f', 4)
            .arg(p.y, 10, 'f', 4)
            .arg(p.z, 10, 'f', 4));
        l->setStyleSheet("font-family: monospace; color: #00cc88;");
    }
}

void PicAdjustWidget::refreshResults() {
    const PicAdjustResult r = PicAdjustMath::Compute(m_set);
    auto fmt = [](const std::optional<double>& v) -> QString {
        return v ? QString::number(*v, 'f', 5) : QString("---");
    };
    m_uRefLabel   ->setText(QString("U_ref   (S1->S2 XY)        = %1").arg(fmt(r.uRefDeg)));
    m_wRefLabel   ->setText(QString("W_ref   (S1->S2 ZY)        = %1").arg(fmt(r.wRefDeg)));
    m_uPicLabel   ->setText(QString("U_pic   (P2->P4 XY)        = %1").arg(fmt(r.uPicDeg)));
    m_wPicLabel   ->setText(QString("W_pic   (P2->P4 ZY)        = %1").arg(fmt(r.wPicDeg)));
    m_wRollLLabel ->setText(QString("W_check (P1->P3 ZY)        = %1").arg(fmt(r.wRollLeftDeg)));
    m_vPitchTLabel->setText(QString("V_pitch (P1->P2 ZX)        = %1").arg(fmt(r.vPitchTopDeg)));
    m_vPitchBLabel->setText(QString("V_pitch (P3->P4 ZX)        = %1").arg(fmt(r.vPitchBotDeg)));
    m_deltaULabel ->setText(QString("ΔU (U_pic - U_ref)         = %1").arg(fmt(r.deltaUDeg)));
    m_deltaWLabel ->setText(QString("ΔW (W_pic - W_ref)         = %1").arg(fmt(r.deltaWDeg)));
    m_vAvgLabel   ->setText(QString("V  (avg V_pitch)           = %1").arg(fmt(r.vAvgDeg)));
    refreshDistanceLabel();
}

void PicAdjustWidget::appendLog(const QString& s) {
    if (m_logEdit) m_logEdit->appendPlainText(s);
    std::cout << "[PicAdjust] " << s.toStdString() << std::endl;
}

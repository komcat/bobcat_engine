// TeachBondingWidget.cpp - Wizard for teaching bonding positions via corner capture
#include "TeachBondingWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <iostream>
#include <cmath>

TeachBondingWidget::TeachBondingWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
{
    setupUI();

    connect(m_positionTimer, &QTimer::timeout, this, &TeachBondingWidget::updatePositionDisplay);
}

void TeachBondingWidget::SetTrainingSet(TrainingSet* ts)
{
    m_trainingSet = ts;
    setState(Idle);
    refreshList();
}

void TeachBondingWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Teach Bonding Positions");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        "Define bonding positions by jogging to 2-3 corners of the placement area.\n"
        "The center will be computed automatically.");
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    m_wizardStack = new QStackedWidget();

    // ========================================
    // Page 0: Idle - list of bonding points
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_bondingList = new QListWidget();
    m_bondingList->setStyleSheet(
        "QListWidget { background-color: #1a1f2e; color: white; border: 1px solid #3d5a8c; }"
        "QListWidget::item { padding: 6px; }"
        "QListWidget::item:selected { background-color: #3d5a8c; }"
    );
    idleLayout->addWidget(m_bondingList, 1);

    QHBoxLayout* idleBtnRow = new QHBoxLayout();
    m_addNewBtn = new QPushButton("Add New Bonding Point");
    m_addNewBtn->setFixedHeight(35);
    m_addNewBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px 15px; }"
        "QPushButton:hover { background-color: #2ecc71; }"
    );
    m_tuneBtn = new QPushButton("Tune Selected");
    m_tuneBtn->setFixedHeight(35);
    m_tuneBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px 15px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    m_removeBtn = new QPushButton("Remove Selected");
    m_removeBtn->setFixedHeight(35);
    m_removeBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px 15px; }"
        "QPushButton:hover { background-color: #e74c3c; }"
    );
    idleBtnRow->addWidget(m_addNewBtn);
    idleBtnRow->addWidget(m_tuneBtn);
    idleBtnRow->addWidget(m_removeBtn);
    idleLayout->addLayout(idleBtnRow);

    m_reteachBtn = new QPushButton("Reteach Selected");
    m_reteachBtn->setFixedHeight(35);
    m_reteachBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px 15px; }"
        "QPushButton:hover { background-color: #f39c12; }"
        "QPushButton:disabled { background-color: #555; color: #888; }");
    idleLayout->addWidget(m_reteachBtn);

    connect(m_addNewBtn, &QPushButton::clicked, this, &TeachBondingWidget::onAddNew);
    connect(m_reteachBtn, &QPushButton::clicked, this, &TeachBondingWidget::onReteachSelected);
    connect(m_tuneBtn, &QPushButton::clicked, this, &TeachBondingWidget::onTuneSelected);
    connect(m_removeBtn, &QPushButton::clicked, this, &TeachBondingWidget::onRemoveSelected);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Teaching corners (shared page)
    // ========================================
    QWidget* teachPage = new QWidget();
    QVBoxLayout* teachLayout = new QVBoxLayout(teachPage);

    // Name input
    QHBoxLayout* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel("Name:"));
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("e.g., Bond1");
    m_nameEdit->setStyleSheet(
        "QLineEdit { background-color: #1a1f2e; color: white; border: 1px solid #3d5a8c; "
        "padding: 5px; border-radius: 3px; }");
    nameRow->addWidget(m_nameEdit);
    teachLayout->addLayout(nameRow);

    m_instructionLabel = new QLabel();
    m_instructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setAlignment(Qt::AlignCenter);
    teachLayout->addWidget(m_instructionLabel);

    // Live position display
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

    teachLayout->addStretch();

    // Buttons
    QHBoxLayout* teachBtnRow = new QHBoxLayout();
    m_captureBtn = new QPushButton("Capture Position");
    m_captureBtn->setFixedHeight(40);
    m_captureBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }"
    );
    m_skipBtn = new QPushButton("Skip (2 corners only)");
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

    connect(m_captureBtn, &QPushButton::clicked, this, &TeachBondingWidget::onCaptureCorner);
    connect(m_skipBtn, &QPushButton::clicked, this, &TeachBondingWidget::onSkipCorner3);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TeachBondingWidget::onCancelTeach);

    m_wizardStack->addWidget(teachPage);  // index 1

    // ========================================
    // Page 2: Summary
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
    m_confirmBtn = new QPushButton("Confirm & Add");
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

    connect(m_confirmBtn, &QPushButton::clicked, this, &TeachBondingWidget::onConfirmBonding);
    connect(m_redoBtn, &QPushButton::clicked, this, &TeachBondingWidget::onRedoBonding);

    m_wizardStack->addWidget(summaryPage);  // index 2

    // ========================================
    // Page 3: Tune bonding point
    // ========================================
    QWidget* tunePage = new QWidget();
    QVBoxLayout* tuneLayout = new QVBoxLayout(tunePage);

    m_tuneInfoLabel = new QLabel();
    m_tuneInfoLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 10px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_tuneInfoLabel->setWordWrap(true);
    m_tuneInfoLabel->setAlignment(Qt::AlignCenter);
    tuneLayout->addWidget(m_tuneInfoLabel);

    // Spinbox style
    QString spinStyle =
        "QDoubleSpinBox { background-color: #1a1f2e; color: #00ff88; border: 1px solid #4d6e9f; "
        "padding: 6px; border-radius: 3px; font-size: 14px; font-family: monospace; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 20px; }";
    QString labelStyle = "color: #ccc; font-size: 13px; font-weight: bold;";

    // Absolute position group
    QGroupBox* absGroup = new QGroupBox("Absolute Position");
    absGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QGridLayout* absGrid = new QGridLayout(absGroup);

    QLabel* absXLabel = new QLabel("X:");
    absXLabel->setStyleSheet(labelStyle);
    absGrid->addWidget(absXLabel, 0, 0);
    m_tuneAbsX = new QDoubleSpinBox();
    m_tuneAbsX->setStyleSheet(spinStyle);
    m_tuneAbsX->setRange(-999.0, 999.0);
    m_tuneAbsX->setDecimals(4);
    m_tuneAbsX->setSingleStep(0.01);
    absGrid->addWidget(m_tuneAbsX, 0, 1);

    QLabel* absYLabel = new QLabel("Y:");
    absYLabel->setStyleSheet(labelStyle);
    absGrid->addWidget(absYLabel, 1, 0);
    m_tuneAbsY = new QDoubleSpinBox();
    m_tuneAbsY->setStyleSheet(spinStyle);
    m_tuneAbsY->setRange(-999.0, 999.0);
    m_tuneAbsY->setDecimals(4);
    m_tuneAbsY->setSingleStep(0.01);
    absGrid->addWidget(m_tuneAbsY, 1, 1);

    QLabel* absZLabel = new QLabel("Z:");
    absZLabel->setStyleSheet(labelStyle);
    absGrid->addWidget(absZLabel, 2, 0);
    m_tuneAbsZ = new QDoubleSpinBox();
    m_tuneAbsZ->setStyleSheet(spinStyle);
    m_tuneAbsZ->setRange(-999.0, 999.0);
    m_tuneAbsZ->setDecimals(4);
    m_tuneAbsZ->setSingleStep(0.01);
    absGrid->addWidget(m_tuneAbsZ, 2, 1);

    tuneLayout->addWidget(absGroup);

    // Local position group (in reference frame)
    QGroupBox* localGroup = new QGroupBox("Local Position (Reference Frame)");
    localGroup->setStyleSheet(absGroup->styleSheet());
    QGridLayout* localGrid = new QGridLayout(localGroup);

    QLabel* localXLabel = new QLabel("Local X:");
    localXLabel->setStyleSheet(labelStyle);
    localGrid->addWidget(localXLabel, 0, 0);
    m_tuneLocalX = new QDoubleSpinBox();
    m_tuneLocalX->setStyleSheet(spinStyle);
    m_tuneLocalX->setRange(-999.0, 999.0);
    m_tuneLocalX->setDecimals(4);
    m_tuneLocalX->setSingleStep(0.01);
    localGrid->addWidget(m_tuneLocalX, 0, 1);

    QLabel* localYLabel = new QLabel("Local Y:");
    localYLabel->setStyleSheet(labelStyle);
    localGrid->addWidget(localYLabel, 1, 0);
    m_tuneLocalY = new QDoubleSpinBox();
    m_tuneLocalY->setStyleSheet(spinStyle);
    m_tuneLocalY->setRange(-999.0, 999.0);
    m_tuneLocalY->setDecimals(4);
    m_tuneLocalY->setSingleStep(0.01);
    localGrid->addWidget(m_tuneLocalY, 1, 1);

    tuneLayout->addWidget(localGroup);

    // Offset group
    QGroupBox* offsetGroup = new QGroupBox("Offset (add to global position)");
    offsetGroup->setStyleSheet(absGroup->styleSheet());
    QGridLayout* offsetGrid = new QGridLayout(offsetGroup);

    QLabel* dxLabel = new QLabel("dX:");
    dxLabel->setStyleSheet(labelStyle);
    offsetGrid->addWidget(dxLabel, 0, 0);
    m_tuneOffsetX = new QDoubleSpinBox();
    m_tuneOffsetX->setStyleSheet(spinStyle);
    m_tuneOffsetX->setRange(-99.0, 99.0);
    m_tuneOffsetX->setDecimals(4);
    m_tuneOffsetX->setSingleStep(0.01);
    m_tuneOffsetX->setValue(0.0);
    offsetGrid->addWidget(m_tuneOffsetX, 0, 1);

    QLabel* dyLabel = new QLabel("dY:");
    dyLabel->setStyleSheet(labelStyle);
    offsetGrid->addWidget(dyLabel, 1, 0);
    m_tuneOffsetY = new QDoubleSpinBox();
    m_tuneOffsetY->setStyleSheet(spinStyle);
    m_tuneOffsetY->setRange(-99.0, 99.0);
    m_tuneOffsetY->setDecimals(4);
    m_tuneOffsetY->setSingleStep(0.01);
    m_tuneOffsetY->setValue(0.0);
    offsetGrid->addWidget(m_tuneOffsetY, 1, 1);

    m_tuneApplyOffsetBtn = new QPushButton("Apply Offset");
    m_tuneApplyOffsetBtn->setFixedHeight(35);
    m_tuneApplyOffsetBtn->setStyleSheet(
        "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px 15px; }"
        "QPushButton:hover { background-color: #9b59b6; }");
    offsetGrid->addWidget(m_tuneApplyOffsetBtn, 0, 2, 2, 1);

    tuneLayout->addWidget(offsetGroup);

    tuneLayout->addStretch();

    // Save / Cancel buttons
    QHBoxLayout* tuneBtnRow = new QHBoxLayout();
    m_tuneSaveBtn = new QPushButton("Save");
    m_tuneSaveBtn->setFixedHeight(40);
    m_tuneSaveBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }");
    m_tuneCancelBtn = new QPushButton("Cancel");
    m_tuneCancelBtn->setFixedHeight(40);
    m_tuneCancelBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }");

    tuneBtnRow->addWidget(m_tuneSaveBtn);
    tuneBtnRow->addWidget(m_tuneCancelBtn);
    tuneLayout->addLayout(tuneBtnRow);

    connect(m_tuneApplyOffsetBtn, &QPushButton::clicked, this, &TeachBondingWidget::onApplyTuneOffset);
    connect(m_tuneSaveBtn, &QPushButton::clicked, this, &TeachBondingWidget::onSaveTune);
    connect(m_tuneCancelBtn, &QPushButton::clicked, this, &TeachBondingWidget::onCancelTune);

    // Bidirectional sync: global <-> local
    auto globalChanged = [this]() { onTuneGlobalChanged(); };
    connect(m_tuneAbsX, &QDoubleSpinBox::valueChanged, this, globalChanged);
    connect(m_tuneAbsY, &QDoubleSpinBox::valueChanged, this, globalChanged);
    auto localChanged = [this]() { onTuneLocalChanged(); };
    connect(m_tuneLocalX, &QDoubleSpinBox::valueChanged, this, localChanged);
    connect(m_tuneLocalY, &QDoubleSpinBox::valueChanged, this, localChanged);

    m_wizardStack->addWidget(tunePage);  // index 3

    mainLayout->addWidget(m_wizardStack, 1);
}

void TeachBondingWidget::setState(WizardState state)
{
    m_state = state;

    switch (state) {
    case Idle:
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        break;

    case TeachCorner1:
        m_wizardStack->setCurrentIndex(1);
        m_instructionLabel->setText(
            "Step 1/3: Jog to TOP-LEFT corner of the placement area\n"
            "Then click 'Capture Position'");
        m_skipBtn->setVisible(false);
        m_nameEdit->setEnabled(true);
        m_positionTimer->start(100);
        break;

    case TeachCorner2:
        m_wizardStack->setCurrentIndex(1);
        m_instructionLabel->setText(
            "Step 2/3: Jog to BOTTOM-RIGHT corner of the placement area\n"
            "Then click 'Capture Position'");
        m_skipBtn->setVisible(false);
        m_nameEdit->setEnabled(false);
        m_positionTimer->start(100);
        break;

    case TeachCorner3:
        m_wizardStack->setCurrentIndex(1);
        m_instructionLabel->setText(
            "Step 3/3 (Optional): Jog to TOP-RIGHT corner for verification\n"
            "Click 'Capture Position' or 'Skip'");
        m_skipBtn->setVisible(true);
        m_nameEdit->setEnabled(false);
        m_positionTimer->start(100);
        break;

    case Tuning:
        m_wizardStack->setCurrentIndex(3);
        m_positionTimer->stop();
        break;

    case Summary: {
        m_wizardStack->setCurrentIndex(2);
        m_positionTimer->stop();

        // Compute center from corners
        if (m_pendingPoint.hasThirdCorner) {
            // 3-corner: centroid of TL, BR, TR (and implied BL)
            double blX = m_pendingPoint.cornerTL_X + m_pendingPoint.cornerBR_X - m_pendingPoint.cornerTR_X;
            double blY = m_pendingPoint.cornerTL_Y + m_pendingPoint.cornerBR_Y - m_pendingPoint.cornerTR_Y;
            m_pendingPoint.globalX = (m_pendingPoint.cornerTL_X + m_pendingPoint.cornerBR_X
                                      + m_pendingPoint.cornerTR_X + blX) / 4.0;
            m_pendingPoint.globalY = (m_pendingPoint.cornerTL_Y + m_pendingPoint.cornerBR_Y
                                      + m_pendingPoint.cornerTR_Y + blY) / 4.0;
        } else {
            // 2-corner: midpoint of TL and BR
            m_pendingPoint.globalX = (m_pendingPoint.cornerTL_X + m_pendingPoint.cornerBR_X) / 2.0;
            m_pendingPoint.globalY = (m_pendingPoint.cornerTL_Y + m_pendingPoint.cornerBR_Y) / 2.0;
        }

        double dist = AccuracyTrainerMath::Distance(
            m_pendingPoint.cornerTL_X, m_pendingPoint.cornerTL_Y,
            m_pendingPoint.cornerBR_X, m_pendingPoint.cornerBR_Y);

        m_pendingPoint.valid = true;

        QString summary = QString(
            "Bonding Point: %1\n\n"
            "Top-Left:     X=%2  Y=%3\n"
            "Bottom-Right: X=%4  Y=%5\n")
            .arg(QString::fromStdString(m_pendingPoint.name))
            .arg(m_pendingPoint.cornerTL_X, 0, 'f', 4)
            .arg(m_pendingPoint.cornerTL_Y, 0, 'f', 4)
            .arg(m_pendingPoint.cornerBR_X, 0, 'f', 4)
            .arg(m_pendingPoint.cornerBR_Y, 0, 'f', 4);

        if (m_pendingPoint.hasThirdCorner) {
            summary += QString("Top-Right:    X=%1  Y=%2\n")
                .arg(m_pendingPoint.cornerTR_X, 0, 'f', 4)
                .arg(m_pendingPoint.cornerTR_Y, 0, 'f', 4);
        }

        summary += QString(
            "\nComputed Center: X=%1  Y=%2  Z=%3\n"
            "Diagonal distance: %4 mm")
            .arg(m_pendingPoint.globalX, 0, 'f', 4)
            .arg(m_pendingPoint.globalY, 0, 'f', 4)
            .arg(m_pendingPoint.globalZ, 0, 'f', 4)
            .arg(dist, 0, 'f', 4);

        m_summaryLabel->setText(summary);
        break;
    }
    }
}

void TeachBondingWidget::onAddNew()
{
    m_reteachIndex = -1;
    m_pendingPoint = BondingPoint();

    // Auto-name
    int nextIdx = m_trainingSet ? static_cast<int>(m_trainingSet->bondingPoints.size()) + 1 : 1;
    m_nameEdit->setText(QString("Bond%1").arg(nextIdx));

    setState(TeachCorner1);
}

void TeachBondingWidget::onReteachSelected()
{
    int row = m_bondingList->currentRow();
    if (row < 0 || !m_trainingSet || row >= static_cast<int>(m_trainingSet->bondingPoints.size())) {
        QMessageBox::information(this, "Reteach", "Please select a bonding point first.");
        return;
    }
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    const auto& bp = m_trainingSet->bondingPoints[row];
    m_reteachIndex = row;

    std::string device = m_trainingSet->deviceName;
    double gx = bp.globalX;
    double gy = bp.globalY;
    double gz = bp.globalZ;
    std::string bpName = bp.name;

    // Disable buttons during move
    m_reteachBtn->setEnabled(false);
    m_reteachBtn->setText("Moving...");
    m_addNewBtn->setEnabled(false);
    m_tuneBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);

    std::cout << "[TeachBonding] Reteach: moving to " << bpName
              << " at (" << gx << ", " << gy << ", " << gz << ")" << std::endl;

    m_moveFuture = std::async(std::launch::async,
        [this, device, gx, gy, gz, bpName]() {
        bool moveOk = m_context->motionManager->MoveToAbs(device, gx, gy, gz, 100.0, true);

        QMetaObject::invokeMethod(this, [this, moveOk, bpName]() {
            m_reteachBtn->setEnabled(true);
            m_reteachBtn->setText("Reteach Selected");
            m_addNewBtn->setEnabled(true);
            m_tuneBtn->setEnabled(true);
            m_removeBtn->setEnabled(true);

            if (!moveOk) {
                QMessageBox::warning(this, "Move Failed",
                    "Failed to move to taught position. You can still reteach manually.");
            }

            // Start corner teach wizard with existing name
            m_pendingPoint = BondingPoint();
            m_nameEdit->setText(QString::fromStdString(bpName));

            std::cout << "[TeachBonding] Reteach: move complete, starting corner teach for " << bpName << std::endl;
            setState(TeachCorner1);
        }, Qt::QueuedConnection);
    });
}

void TeachBondingWidget::onRemoveSelected()
{
    int row = m_bondingList->currentRow();
    if (row < 0) {
        return;
    }
    emit bondingPointRemoved(row);
    refreshList();
}

void TeachBondingWidget::onCaptureCorner()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    std::string device = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(device);

    switch (m_state) {
    case TeachCorner1:
        m_pendingPoint.name = m_nameEdit->text().toStdString();
        if (m_pendingPoint.name.empty()) {
            m_pendingPoint.name = "Bond";
        }
        m_pendingPoint.cornerTL_X = pos.x;
        m_pendingPoint.cornerTL_Y = pos.y;
        m_pendingPoint.globalZ = pos.z;  // Z from first corner
        std::cout << "[TeachBonding] Corner 1 (TL): X=" << pos.x << " Y=" << pos.y << " Z=" << pos.z << std::endl;
        setState(TeachCorner2);
        break;

    case TeachCorner2:
        m_pendingPoint.cornerBR_X = pos.x;
        m_pendingPoint.cornerBR_Y = pos.y;
        std::cout << "[TeachBonding] Corner 2 (BR): X=" << pos.x << " Y=" << pos.y << std::endl;
        setState(TeachCorner3);
        break;

    case TeachCorner3:
        m_pendingPoint.cornerTR_X = pos.x;
        m_pendingPoint.cornerTR_Y = pos.y;
        m_pendingPoint.hasThirdCorner = true;
        std::cout << "[TeachBonding] Corner 3 (TR): X=" << pos.x << " Y=" << pos.y << std::endl;
        setState(Summary);
        break;

    default:
        break;
    }
}

void TeachBondingWidget::onSkipCorner3()
{
    m_pendingPoint.hasThirdCorner = false;
    std::cout << "[TeachBonding] Skipped corner 3" << std::endl;
    setState(Summary);
}

void TeachBondingWidget::onConfirmBonding()
{
    if (m_reteachIndex >= 0 && m_trainingSet &&
        m_reteachIndex < static_cast<int>(m_trainingSet->bondingPoints.size())) {
        // Reteach: replace existing bonding point
        m_trainingSet->bondingPoints[m_reteachIndex] = m_pendingPoint;

        // Recompute local coordinates if reference frame exists
        if (m_trainingSet->valid) {
            auto& bp = m_trainingSet->bondingPoints[m_reteachIndex];
            AccuracyTrainerMath::WorldToLocal(
                bp.globalX, bp.globalY,
                m_trainingSet->originX, m_trainingSet->originY,
                m_trainingSet->xAxisX, m_trainingSet->xAxisY,
                m_trainingSet->yAxisX, m_trainingSet->yAxisY,
                bp.localX, bp.localY);
        }

        std::cout << "[TeachBonding] Reteach confirmed for " << m_pendingPoint.name
                  << " center=(" << m_pendingPoint.globalX << ", " << m_pendingPoint.globalY << ")" << std::endl;

        emit bondingPointModified(m_reteachIndex);
        m_reteachIndex = -1;
    } else {
        // Normal add
        emit bondingPointAdded(m_pendingPoint);
        std::cout << "[TeachBonding] Confirmed bonding point: " << m_pendingPoint.name
                  << " center=(" << m_pendingPoint.globalX << ", " << m_pendingPoint.globalY << ")" << std::endl;
    }

    refreshList();
    setState(Idle);
}

void TeachBondingWidget::onRedoBonding()
{
    setState(TeachCorner1);
}

void TeachBondingWidget::onCancelTeach()
{
    m_positionTimer->stop();
    m_reteachIndex = -1;
    setState(Idle);
}

void TeachBondingWidget::onTuneSelected()
{
    int row = m_bondingList->currentRow();
    if (row < 0 || !m_trainingSet || row >= static_cast<int>(m_trainingSet->bondingPoints.size())) {
        QMessageBox::information(this, "Tune", "Please select a bonding point first.");
        return;
    }

    m_tuneIndex = row;
    const auto& bp = m_trainingSet->bondingPoints[row];

    bool hasFrame = m_trainingSet->valid;
    m_tuneInfoLabel->setText(QString("Tuning: %1%2")
        .arg(QString::fromStdString(bp.name))
        .arg(hasFrame ? "" : "  (no reference frame - local coords unavailable)"));

    // Populate all fields with sync guard to prevent cascading updates
    m_tuneSyncing = true;

    m_tuneAbsX->setValue(bp.globalX);
    m_tuneAbsY->setValue(bp.globalY);
    m_tuneAbsZ->setValue(bp.globalZ);
    m_tuneLocalX->setValue(bp.localX);
    m_tuneLocalY->setValue(bp.localY);

    // Enable/disable local fields based on whether frame exists
    m_tuneLocalX->setEnabled(hasFrame);
    m_tuneLocalY->setEnabled(hasFrame);

    m_tuneSyncing = false;

    // Reset offsets
    m_tuneOffsetX->setValue(0.0);
    m_tuneOffsetY->setValue(0.0);

    std::cout << "[TeachBonding] Tuning " << bp.name
              << " global=(" << bp.globalX << ", " << bp.globalY << ", " << bp.globalZ << ")"
              << " local=(" << bp.localX << ", " << bp.localY << ")" << std::endl;

    setState(Tuning);
}

void TeachBondingWidget::onApplyTuneOffset()
{
    double dx = m_tuneOffsetX->value();
    double dy = m_tuneOffsetY->value();

    m_tuneAbsX->setValue(m_tuneAbsX->value() + dx);
    m_tuneAbsY->setValue(m_tuneAbsY->value() + dy);

    // Reset offsets after applying
    m_tuneOffsetX->setValue(0.0);
    m_tuneOffsetY->setValue(0.0);

    std::cout << "[TeachBonding] Applied offset dX=" << dx << " dY=" << dy
              << " -> X=" << m_tuneAbsX->value() << " Y=" << m_tuneAbsY->value() << std::endl;
}

void TeachBondingWidget::onTuneGlobalChanged()
{
    if (m_tuneSyncing || !m_trainingSet || !m_trainingSet->valid) return;
    m_tuneSyncing = true;

    // Global changed -> recalculate local
    double localX = 0.0, localY = 0.0;
    AccuracyTrainerMath::WorldToLocal(
        m_tuneAbsX->value(), m_tuneAbsY->value(),
        m_trainingSet->originX, m_trainingSet->originY,
        m_trainingSet->xAxisX, m_trainingSet->xAxisY,
        m_trainingSet->yAxisX, m_trainingSet->yAxisY,
        localX, localY);
    m_tuneLocalX->setValue(localX);
    m_tuneLocalY->setValue(localY);

    m_tuneSyncing = false;
}

void TeachBondingWidget::onTuneLocalChanged()
{
    if (m_tuneSyncing || !m_trainingSet || !m_trainingSet->valid) return;
    m_tuneSyncing = true;

    // Local changed -> recalculate global
    double worldX = 0.0, worldY = 0.0;
    AccuracyTrainerMath::LocalToWorld(
        m_tuneLocalX->value(), m_tuneLocalY->value(),
        m_trainingSet->originX, m_trainingSet->originY,
        m_trainingSet->xAxisX, m_trainingSet->xAxisY,
        m_trainingSet->yAxisX, m_trainingSet->yAxisY,
        worldX, worldY);
    m_tuneAbsX->setValue(worldX);
    m_tuneAbsY->setValue(worldY);

    m_tuneSyncing = false;
}

void TeachBondingWidget::onSaveTune()
{
    if (!m_trainingSet || m_tuneIndex < 0 ||
        m_tuneIndex >= static_cast<int>(m_trainingSet->bondingPoints.size())) {
        return;
    }

    auto& bp = m_trainingSet->bondingPoints[m_tuneIndex];
    double oldX = bp.globalX, oldY = bp.globalY, oldZ = bp.globalZ;

    bp.globalX = m_tuneAbsX->value();
    bp.globalY = m_tuneAbsY->value();
    bp.globalZ = m_tuneAbsZ->value();

    // Also shift corners by the same delta to keep them consistent
    double shiftX = bp.globalX - oldX;
    double shiftY = bp.globalY - oldY;
    bp.cornerTL_X += shiftX;
    bp.cornerTL_Y += shiftY;
    bp.cornerBR_X += shiftX;
    bp.cornerBR_Y += shiftY;
    if (bp.hasThirdCorner) {
        bp.cornerTR_X += shiftX;
        bp.cornerTR_Y += shiftY;
    }

    // Save local coordinates (already synced via bidirectional spinbox link)
    bp.localX = m_tuneLocalX->value();
    bp.localY = m_tuneLocalY->value();

    std::cout << "[TeachBonding] Saved tune for " << bp.name
              << " global=(" << bp.globalX << ", " << bp.globalY << ", " << bp.globalZ << ")"
              << " local=(" << bp.localX << ", " << bp.localY << ")"
              << " shift=(" << shiftX << ", " << shiftY << ")" << std::endl;

    refreshList();
    emit bondingPointModified(m_tuneIndex);
    m_tuneIndex = -1;
    setState(Idle);
}

void TeachBondingWidget::onCancelTune()
{
    m_tuneIndex = -1;
    setState(Idle);
}

void TeachBondingWidget::updatePositionDisplay()
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

void TeachBondingWidget::refreshList()
{
    m_bondingList->clear();
    if (!m_trainingSet) return;

    for (const auto& bp : m_trainingSet->bondingPoints) {
        QString text = QString("%1  |  Center: (%2, %3)  Z=%4")
            .arg(QString::fromStdString(bp.name))
            .arg(bp.globalX, 0, 'f', 3)
            .arg(bp.globalY, 0, 'f', 3)
            .arg(bp.globalZ, 0, 'f', 3);
        m_bondingList->addItem(text);
    }
}

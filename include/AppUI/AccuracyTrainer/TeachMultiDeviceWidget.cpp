// TeachMultiDeviceWidget.cpp - Wizard for teaching hex device positions
#include "TeachMultiDeviceWidget.h"
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
#include <cmath>

#ifdef HAS_OPENCV
#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include <nlohmann/json.hpp>
#include <fstream>
#endif

TeachMultiDeviceWidget::TeachMultiDeviceWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_positionTimer(new QTimer(this))
{
    setupUI();

    connect(m_positionTimer, &QTimer::timeout, this, &TeachMultiDeviceWidget::updatePositionDisplay);
}

void TeachMultiDeviceWidget::SetTrainingSet(TrainingSet* ts)
{
    m_trainingSet = ts;
    refreshVisionProfiles();
    setState(Idle);
    refreshList();
    rebuildAssignmentUI();
}

void TeachMultiDeviceWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QLabel* title = new QLabel("Teach Multi-Device Positions");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff;");
    mainLayout->addWidget(title);

    QLabel* desc = new QLabel(
        "Teach hex device working positions and gantry viewing positions.\n"
        "During Run Multi-Device Alignment, both devices will auto-navigate\n"
        "to taught positions before corner capture.");
    desc->setStyleSheet("color: #aaa; font-size: 12px;");
    desc->setWordWrap(true);
    mainLayout->addWidget(desc);

    m_wizardStack = new QStackedWidget();

    // ========================================
    // Page 0: Idle - list of taught positions
    // ========================================
    QWidget* idlePage = new QWidget();
    QVBoxLayout* idleLayout = new QVBoxLayout(idlePage);

    m_teachList = new QListWidget();
    m_teachList->setStyleSheet(
        "QListWidget { background-color: #1a1f2e; color: white; border: 1px solid #3d5a8c; }"
        "QListWidget::item { padding: 6px; }"
        "QListWidget::item:selected { background-color: #3d5a8c; }");
    idleLayout->addWidget(m_teachList, 1);

    // Device selection for new teach
    QHBoxLayout* deviceRow = new QHBoxLayout();
    deviceRow->addWidget(new QLabel("Hex Device:"));
    m_hexDeviceCombo = new QComboBox();
    m_hexDeviceCombo->setStyleSheet(
        "QComboBox { background-color: #1a1f2e; color: white; border: 1px solid #4d6e9f; "
        "padding: 5px 10px; border-radius: 3px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #1a1f2e; color: white; "
        "selection-background-color: #3d5a8c; }");

    // Populate hex devices from motion manager
    if (m_context && m_context->motionManager) {
        auto devices = m_context->motionManager->GetAllDeviceNames();
        for (const auto& dev : devices) {
            // Only show hex devices
            if (dev.find("hex") != std::string::npos) {
                m_hexDeviceCombo->addItem(QString::fromStdString(dev));
            }
        }
    }
    if (m_hexDeviceCombo->count() == 0) {
        m_hexDeviceCombo->addItem("hex-left");
        m_hexDeviceCombo->addItem("hex-right");
    }
    deviceRow->addWidget(m_hexDeviceCombo, 1);
    idleLayout->addLayout(deviceRow);

    QHBoxLayout* idleBtnRow = new QHBoxLayout();
    m_teachBtn = new QPushButton("Teach Position");
    m_teachBtn->setFixedHeight(35);
    m_teachBtn->setStyleSheet(
        "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px 15px; }"
        "QPushButton:hover { background-color: #9b59b6; }");
    m_removeBtn = new QPushButton("Remove Selected");
    m_removeBtn->setFixedHeight(35);
    m_removeBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px 15px; }"
        "QPushButton:hover { background-color: #e74c3c; }");
    idleBtnRow->addWidget(m_teachBtn);
    idleBtnRow->addWidget(m_removeBtn);
    idleLayout->addLayout(idleBtnRow);

    m_reteachBtn = new QPushButton("Reteach Selected Position");
    m_reteachBtn->setFixedHeight(35);
    m_reteachBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px 15px; }"
        "QPushButton:hover { background-color: #f39c12; }"
        "QPushButton:disabled { background-color: #555; color: #888; }");
    idleLayout->addWidget(m_reteachBtn);

    m_moveToBtn = new QPushButton("Move Devices to Selected Position");
    m_moveToBtn->setFixedHeight(35);
    m_moveToBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 5px 15px; }"
        "QPushButton:hover { background-color: #3498db; }"
        "QPushButton:disabled { background-color: #555; color: #888; }");
    idleLayout->addWidget(m_moveToBtn);

    connect(m_teachBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onTeach);
    connect(m_reteachBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onReteachSelected);
    connect(m_removeBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onRemoveSelected);
    connect(m_moveToBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onMoveToPosition);

    // Device assignment section
    QGroupBox* assignGroup = new QGroupBox("Bonding Point Device Assignments");
    assignGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* assignGroupLayout = new QVBoxLayout(assignGroup);

    QLabel* assignHint = new QLabel(
        "Assign each bonding point to gantry-main (default) or a hex device.");
    assignHint->setStyleSheet("color: #777; font-size: 11px; padding: 4px;");
    assignHint->setWordWrap(true);
    assignGroupLayout->addWidget(assignHint);

    m_assignContainer = new QWidget();
    m_assignContainer->setStyleSheet(
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 6px; padding: 8px;");
    assignGroupLayout->addWidget(m_assignContainer);
    idleLayout->addWidget(assignGroup, 1);

    m_wizardStack->addWidget(idlePage);  // index 0

    // ========================================
    // Page 1: Position hex device
    // ========================================
    QWidget* hexPage = new QWidget();
    QVBoxLayout* hexLayout = new QVBoxLayout(hexPage);

    m_hexInstructionLabel = new QLabel();
    m_hexInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #e0a0ff; font-weight: bold; padding: 15px; "
        "background-color: #2d2e5f; border-radius: 6px;");
    m_hexInstructionLabel->setWordWrap(true);
    m_hexInstructionLabel->setAlignment(Qt::AlignCenter);
    hexLayout->addWidget(m_hexInstructionLabel);

    QGroupBox* hexPosGroup = new QGroupBox("Hex Device Position (6-axis)");
    hexPosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* hexPosLayout = new QVBoxLayout(hexPosGroup);
    m_hexPositionLabel = new QLabel("X: ---  Y: ---  Z: ---\nU: ---  V: ---  W: ---");
    m_hexPositionLabel->setStyleSheet("font-size: 16px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_hexPositionLabel->setAlignment(Qt::AlignCenter);
    hexPosLayout->addWidget(m_hexPositionLabel);
    hexLayout->addWidget(hexPosGroup);

    hexLayout->addStretch();

    QHBoxLayout* hexBtnRow = new QHBoxLayout();
    m_captureHexBtn = new QPushButton("Capture Hex Position");
    m_captureHexBtn->setFixedHeight(40);
    m_captureHexBtn->setStyleSheet(
        "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #9b59b6; }");
    m_cancelHexBtn = new QPushButton("Cancel");
    m_cancelHexBtn->setFixedHeight(40);
    m_cancelHexBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }");
    hexBtnRow->addWidget(m_captureHexBtn);
    hexBtnRow->addWidget(m_cancelHexBtn);
    hexLayout->addLayout(hexBtnRow);

    connect(m_captureHexBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCaptureHexPosition);
    connect(m_cancelHexBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCancel);

    m_wizardStack->addWidget(hexPage);  // index 1

    // ========================================
    // Page 2: Position gantry to view hex tip
    // ========================================
    QWidget* gantryPage = new QWidget();
    QVBoxLayout* gantryLayout = new QVBoxLayout(gantryPage);

    m_gantryInstructionLabel = new QLabel();
    m_gantryInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #f0c040; font-weight: bold; padding: 15px; "
        "background-color: #2d3e5f; border-radius: 6px;");
    m_gantryInstructionLabel->setWordWrap(true);
    m_gantryInstructionLabel->setAlignment(Qt::AlignCenter);
    gantryLayout->addWidget(m_gantryInstructionLabel);

    // Teaching mode selector
    QHBoxLayout* modeRow = new QHBoxLayout();
    QLabel* modeLabel = new QLabel("Teaching Mode:");
    modeLabel->setStyleSheet("color: #aaa; font-size: 12px; font-weight: bold;");
    modeRow->addWidget(modeLabel);

    m_gantryModeCombo = new QComboBox();
    m_gantryModeCombo->addItem("Single Point (manual)");
    m_gantryModeCombo->addItem("Corner Pattern (2-3 points)");
    m_gantryModeCombo->setStyleSheet(
        "QComboBox { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #2d3e5f; color: white; selection-background-color: #3d5a8c; }");
    modeRow->addWidget(m_gantryModeCombo, 1);
    gantryLayout->addLayout(modeRow);

    connect(m_gantryModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TeachMultiDeviceWidget::onGantryModeChanged);

    // Stacked widget to switch between single-point and corner-pattern sub-pages
    m_gantryModeStack = new QStackedWidget();

    // --- Sub-page 0: Single Point mode (original UI) ---
    m_singlePointPage = new QWidget();
    QVBoxLayout* singleLayout = new QVBoxLayout(m_singlePointPage);
    singleLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* gantryPosGroup = new QGroupBox("Gantry Position (XYZ)");
    gantryPosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* gantryPosLayout = new QVBoxLayout(gantryPosGroup);
    m_gantryPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_gantryPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_gantryPositionLabel->setAlignment(Qt::AlignCenter);
    gantryPosLayout->addWidget(m_gantryPositionLabel);
    singleLayout->addWidget(gantryPosGroup);

    // Vision profile section for auto-detect hex tip
    QGroupBox* visionGroup = new QGroupBox("Vision Detect (Optional)");
    visionGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
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
        "QComboBox QAbstractItemView { background-color: #2d3e5f; color: white; selection-background-color: #3d5a8c; }");
    m_visionProfileCombo->setMinimumWidth(200);
    profileRow->addWidget(m_visionProfileCombo, 1);

    m_refreshProfilesBtn = new QPushButton("R");
    m_refreshProfilesBtn->setFixedSize(28, 28);
    m_refreshProfilesBtn->setToolTip("Refresh profile list");
    m_refreshProfilesBtn->setStyleSheet(
        "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3d5a8c; }");
    connect(m_refreshProfilesBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::refreshVisionProfiles);
    profileRow->addWidget(m_refreshProfilesBtn);
    visionLayout->addLayout(profileRow);

    m_detectCenterBtn = new QPushButton("Detect && Center");
    m_detectCenterBtn->setFixedHeight(36);
    m_detectCenterBtn->setStyleSheet(
        "QPushButton { background-color: #8e44ad; color: white; font-weight: bold; "
        "font-size: 13px; border-radius: 4px; padding: 6px 16px; }"
        "QPushButton:hover { background-color: #9b59b6; }"
        "QPushButton:disabled { background-color: #4a2560; color: #888; }");
    m_detectCenterBtn->setToolTip("Execute vision profile to detect hex tip, then move gantry to center on it");
    connect(m_detectCenterBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onDetectCenter);
    visionLayout->addWidget(m_detectCenterBtn);

    singleLayout->addWidget(visionGroup);
    singleLayout->addStretch();

    QHBoxLayout* gantryBtnRow = new QHBoxLayout();
    m_captureGantryBtn = new QPushButton("Capture Gantry View Position");
    m_captureGantryBtn->setFixedHeight(40);
    m_captureGantryBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }");
    m_cancelGantryBtn = new QPushButton("Cancel");
    m_cancelGantryBtn->setFixedHeight(40);
    m_cancelGantryBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }");
    gantryBtnRow->addWidget(m_captureGantryBtn);
    gantryBtnRow->addWidget(m_cancelGantryBtn);
    singleLayout->addLayout(gantryBtnRow);

    connect(m_captureGantryBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCaptureGantryPosition);
    connect(m_cancelGantryBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCancel);

    m_gantryModeStack->addWidget(m_singlePointPage);  // sub-index 0

    // --- Sub-page 1: Corner Pattern mode ---
    m_cornerPatternPage = new QWidget();
    QVBoxLayout* cornerLayout = new QVBoxLayout(m_cornerPatternPage);
    cornerLayout->setContentsMargins(0, 0, 0, 0);

    m_cornerInstructionLabel = new QLabel();
    m_cornerInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #e0a0ff; font-weight: bold; padding: 12px; "
        "background-color: #2d2e5f; border-radius: 6px;");
    m_cornerInstructionLabel->setWordWrap(true);
    m_cornerInstructionLabel->setAlignment(Qt::AlignCenter);
    cornerLayout->addWidget(m_cornerInstructionLabel);

    QGroupBox* cornerPosGroup = new QGroupBox("Gantry Position (XYZ)");
    cornerPosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* cornerPosLayout = new QVBoxLayout(cornerPosGroup);
    m_cornerPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_cornerPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_cornerPositionLabel->setAlignment(Qt::AlignCenter);
    cornerPosLayout->addWidget(m_cornerPositionLabel);
    cornerLayout->addWidget(cornerPosGroup);

    // Status label showing captured corners so far
    m_cornerStatusLabel = new QLabel();
    m_cornerStatusLabel->setStyleSheet(
        "font-size: 12px; color: #aaa; padding: 8px; "
        "background-color: #1a2a3f; border: 1px solid #3d5a8c; border-radius: 4px;");
    m_cornerStatusLabel->setWordWrap(true);
    cornerLayout->addWidget(m_cornerStatusLabel);

    cornerLayout->addStretch();

    QHBoxLayout* cornerBtnRow = new QHBoxLayout();
    m_captureCornerBtn = new QPushButton("Capture Corner");
    m_captureCornerBtn->setFixedHeight(40);
    m_captureCornerBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }");
    m_skipCorner3Btn = new QPushButton("Skip (2 corners only)");
    m_skipCorner3Btn->setFixedHeight(40);
    m_skipCorner3Btn->setStyleSheet(
        "QPushButton { background-color: #7f8c8d; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #95a5a6; }");
    m_skipCorner3Btn->setVisible(false);
    m_cancelCornerBtn = new QPushButton("Cancel");
    m_cancelCornerBtn->setFixedHeight(40);
    m_cancelCornerBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }");
    cornerBtnRow->addWidget(m_captureCornerBtn);
    cornerBtnRow->addWidget(m_skipCorner3Btn);
    cornerBtnRow->addWidget(m_cancelCornerBtn);
    cornerLayout->addLayout(cornerBtnRow);

    connect(m_captureCornerBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCaptureGantryCorner);
    connect(m_skipCorner3Btn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onSkipGantryCorner3);
    connect(m_cancelCornerBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCancel);

    m_gantryModeStack->addWidget(m_cornerPatternPage);  // sub-index 1

    gantryLayout->addWidget(m_gantryModeStack, 1);

    m_wizardStack->addWidget(gantryPage);  // index 2

    // ========================================
    // Page 3: Capture Z Component height
    // ========================================
    QWidget* zCompPage = new QWidget();
    QVBoxLayout* zCompLayout = new QVBoxLayout(zCompPage);

    m_zCompInstructionLabel = new QLabel();
    m_zCompInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #80d0ff; font-weight: bold; padding: 15px; "
        "background-color: #1a3a4f; border-radius: 6px;");
    m_zCompInstructionLabel->setWordWrap(true);
    m_zCompInstructionLabel->setAlignment(Qt::AlignCenter);
    zCompLayout->addWidget(m_zCompInstructionLabel);

    QGroupBox* zCompPosGroup = new QGroupBox("Gantry Position (XYZ)");
    zCompPosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* zCompPosLayout = new QVBoxLayout(zCompPosGroup);
    m_zCompPositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_zCompPositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_zCompPositionLabel->setAlignment(Qt::AlignCenter);
    zCompPosLayout->addWidget(m_zCompPositionLabel);
    zCompLayout->addWidget(zCompPosGroup);

    // Component and bondline thickness spinboxes
    QGroupBox* thicknessGroup = new QGroupBox("Component Parameters");
    thicknessGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* thicknessLayout = new QVBoxLayout(thicknessGroup);

    QHBoxLayout* tcompRow = new QHBoxLayout();
    QLabel* tcompLabel = new QLabel("Component Thickness (mm):");
    tcompLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    tcompRow->addWidget(tcompLabel);
    m_componentThicknessSpin = new QDoubleSpinBox();
    m_componentThicknessSpin->setRange(0.0, 50.0);
    m_componentThicknessSpin->setDecimals(3);
    m_componentThicknessSpin->setValue(1.2);
    m_componentThicknessSpin->setSingleStep(0.1);
    m_componentThicknessSpin->setStyleSheet(
        "QDoubleSpinBox { background-color: #1a1f2e; color: white; border: 1px solid #4d6e9f; "
        "padding: 4px; border-radius: 3px; font-size: 13px; }");
    tcompRow->addWidget(m_componentThicknessSpin);
    thicknessLayout->addLayout(tcompRow);

    QHBoxLayout* bltRow = new QHBoxLayout();
    QLabel* bltLabel = new QLabel("Bondline Thickness (mm):");
    bltLabel->setStyleSheet("color: #ccc; font-size: 12px;");
    bltRow->addWidget(bltLabel);
    m_bondlineThicknessSpin = new QDoubleSpinBox();
    m_bondlineThicknessSpin->setRange(0.0, 5.0);
    m_bondlineThicknessSpin->setDecimals(3);
    m_bondlineThicknessSpin->setValue(0.05);
    m_bondlineThicknessSpin->setSingleStep(0.01);
    m_bondlineThicknessSpin->setStyleSheet(
        "QDoubleSpinBox { background-color: #1a1f2e; color: white; border: 1px solid #4d6e9f; "
        "padding: 4px; border-radius: 3px; font-size: 13px; }");
    bltRow->addWidget(m_bondlineThicknessSpin);
    thicknessLayout->addLayout(bltRow);

    zCompLayout->addWidget(thicknessGroup);
    zCompLayout->addStretch();

    QHBoxLayout* zCompBtnRow = new QHBoxLayout();
    m_captureZCompBtn = new QPushButton("Capture Z Component Height");
    m_captureZCompBtn->setFixedHeight(40);
    m_captureZCompBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }");
    m_cancelZCompBtn = new QPushButton("Cancel");
    m_cancelZCompBtn->setFixedHeight(40);
    m_cancelZCompBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }");
    zCompBtnRow->addWidget(m_captureZCompBtn);
    zCompBtnRow->addWidget(m_cancelZCompBtn);
    zCompLayout->addLayout(zCompBtnRow);

    connect(m_captureZCompBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCaptureZComp);
    connect(m_cancelZCompBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCancel);

    m_wizardStack->addWidget(zCompPage);  // index 3

    // ========================================
    // Page 4: Capture Z Placement height
    // ========================================
    QWidget* zPlacePage = new QWidget();
    QVBoxLayout* zPlaceLayout = new QVBoxLayout(zPlacePage);

    m_zPlaceInstructionLabel = new QLabel();
    m_zPlaceInstructionLabel->setStyleSheet(
        "font-size: 14px; color: #80d0ff; font-weight: bold; padding: 15px; "
        "background-color: #1a3a4f; border-radius: 6px;");
    m_zPlaceInstructionLabel->setWordWrap(true);
    m_zPlaceInstructionLabel->setAlignment(Qt::AlignCenter);
    zPlaceLayout->addWidget(m_zPlaceInstructionLabel);

    QGroupBox* zPlacePosGroup = new QGroupBox("Gantry Position (XYZ)");
    zPlacePosGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* zPlacePosLayout = new QVBoxLayout(zPlacePosGroup);
    m_zPlacePositionLabel = new QLabel("X: ---  Y: ---  Z: ---");
    m_zPlacePositionLabel->setStyleSheet("font-size: 18px; color: #00ff88; font-family: monospace; padding: 10px;");
    m_zPlacePositionLabel->setAlignment(Qt::AlignCenter);
    zPlacePosLayout->addWidget(m_zPlacePositionLabel);
    zPlaceLayout->addWidget(zPlacePosGroup);

    zPlaceLayout->addStretch();

    QHBoxLayout* zPlaceBtnRow = new QHBoxLayout();
    m_captureZPlaceBtn = new QPushButton("Capture Z Placement Height");
    m_captureZPlaceBtn->setFixedHeight(40);
    m_captureZPlaceBtn->setStyleSheet(
        "QPushButton { background-color: #2980b9; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #3498db; }");
    m_cancelZPlaceBtn = new QPushButton("Cancel");
    m_cancelZPlaceBtn->setFixedHeight(40);
    m_cancelZPlaceBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #e74c3c; }");
    zPlaceBtnRow->addWidget(m_captureZPlaceBtn);
    zPlaceBtnRow->addWidget(m_cancelZPlaceBtn);
    zPlaceLayout->addLayout(zPlaceBtnRow);

    connect(m_captureZPlaceBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCaptureZPlace);
    connect(m_cancelZPlaceBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onCancel);

    m_wizardStack->addWidget(zPlacePage);  // index 4

    // ========================================
    // Page 5: Summary
    // ========================================
    QWidget* summaryPage = new QWidget();
    QVBoxLayout* summaryLayout = new QVBoxLayout(summaryPage);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setStyleSheet(
        "font-size: 13px; color: #ffffff; padding: 15px; "
        "background-color: #1a2a3f; border: 1px solid #8e44ad; border-radius: 6px;");
    m_summaryLabel->setWordWrap(true);
    summaryLayout->addWidget(m_summaryLabel);

    summaryLayout->addStretch();

    QHBoxLayout* summaryBtnRow = new QHBoxLayout();
    m_confirmBtn = new QPushButton("Confirm & Save");
    m_confirmBtn->setFixedHeight(40);
    m_confirmBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "font-size: 14px; border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #2ecc71; }");
    m_redoBtn = new QPushButton("Redo");
    m_redoBtn->setFixedHeight(40);
    m_redoBtn->setStyleSheet(
        "QPushButton { background-color: #e67e22; color: white; font-weight: bold; "
        "border-radius: 4px; padding: 8px 20px; }"
        "QPushButton:hover { background-color: #f39c12; }");

    summaryBtnRow->addWidget(m_confirmBtn);
    summaryBtnRow->addWidget(m_redoBtn);
    summaryLayout->addLayout(summaryBtnRow);

    connect(m_confirmBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onConfirm);
    connect(m_redoBtn, &QPushButton::clicked, this, &TeachMultiDeviceWidget::onRedo);

    m_wizardStack->addWidget(summaryPage);  // index 5

    mainLayout->addWidget(m_wizardStack, 1);
}

void TeachMultiDeviceWidget::setState(WizardState state)
{
    m_state = state;

    switch (state) {
    case Idle:
        m_wizardStack->setCurrentIndex(0);
        m_positionTimer->stop();
        break;

    case PositionHex:
        m_wizardStack->setCurrentIndex(1);
        m_hexInstructionLabel->setText(QString(
            "Step 1/4: Jog %1 to its working position\n\n"
            "Move the hex device to the position where its\n"
            "end-effector is ready for corner capture.\n"
            "Then click 'Capture Hex Position'")
            .arg(QString::fromStdString(m_pendingTeach.deviceName)));
        m_positionTimer->start(100);
        break;

    case PositionGantry: {
        m_wizardStack->setCurrentIndex(2);
        m_gantryModeCombo->setEnabled(true);
        // Restore teaching mode from previous teach (0=single, 1 or 2=corner pattern)
        int comboIdx = (m_pendingTeach.gantryTeachMode > 0) ? 1 : 0;
        m_gantryModeCombo->setCurrentIndex(comboIdx);
        m_gantryModeStack->setCurrentIndex(comboIdx);
        m_gantryInstructionLabel->setText(QString(
            "Step 2/4: Jog gantry to view %1 tip\n\n"
            "Single Point: Move gantry camera to center on the end-effector.\n"
            "Corner Pattern: Capture 2-3 corners to compute center automatically.")
            .arg(QString::fromStdString(m_pendingTeach.deviceName)));
        m_positionTimer->start(100);
        break;
    }

    case GantryCorner1:
        m_wizardStack->setCurrentIndex(2);
        m_gantryModeCombo->setEnabled(false);
        m_gantryModeStack->setCurrentIndex(1);
        m_cornerInstructionLabel->setText(QString(
            "Corner 1/3: Jog gantry to TOP-LEFT corner\n\n"
            "Move the gantry camera crosshair to the\n"
            "TOP-LEFT corner of the %1 end-effector.\n"
            "Then click 'Capture Corner'")
            .arg(QString::fromStdString(m_pendingTeach.deviceName)));
        m_skipCorner3Btn->setVisible(false);
        m_cornerStatusLabel->setText("No corners captured yet.");
        m_positionTimer->start(100);
        break;

    case GantryCorner2:
        m_wizardStack->setCurrentIndex(2);
        m_gantryModeCombo->setEnabled(false);
        m_gantryModeStack->setCurrentIndex(1);
        m_cornerInstructionLabel->setText(QString(
            "Corner 2/3: Jog gantry to BOTTOM-RIGHT corner\n\n"
            "Move the gantry camera crosshair to the\n"
            "BOTTOM-RIGHT corner of the %1 end-effector.\n"
            "Then click 'Capture Corner'")
            .arg(QString::fromStdString(m_pendingTeach.deviceName)));
        m_skipCorner3Btn->setVisible(false);
        m_cornerStatusLabel->setText(QString(
            "Top-Left:  X=%1  Y=%2")
            .arg(m_cornerTL_X, 0, 'f', 4)
            .arg(m_cornerTL_Y, 0, 'f', 4));
        m_positionTimer->start(100);
        break;

    case GantryCorner3:
        m_wizardStack->setCurrentIndex(2);
        m_gantryModeCombo->setEnabled(false);
        m_gantryModeStack->setCurrentIndex(1);
        m_cornerInstructionLabel->setText(QString(
            "Corner 3/3 (Optional): Jog gantry to TOP-RIGHT corner\n\n"
            "Move the gantry camera crosshair to the\n"
            "TOP-RIGHT corner of the %1 end-effector.\n"
            "Click 'Capture Corner' or 'Skip (2 corners only)'")
            .arg(QString::fromStdString(m_pendingTeach.deviceName)));
        m_skipCorner3Btn->setVisible(true);
        m_cornerStatusLabel->setText(QString(
            "Top-Left:      X=%1  Y=%2\n"
            "Bottom-Right:  X=%3  Y=%4")
            .arg(m_cornerTL_X, 0, 'f', 4)
            .arg(m_cornerTL_Y, 0, 'f', 4)
            .arg(m_cornerBR_X, 0, 'f', 4)
            .arg(m_cornerBR_Y, 0, 'f', 4));
        m_positionTimer->start(100);
        break;

    case CaptureZComp:
        m_wizardStack->setCurrentIndex(3);
        m_zCompInstructionLabel->setText(QString(
            "Step 3/4: Capture Z Component Height for %1\n\n"
            "Jog gantry Z to touch the TOP of the component\n"
            "(or the height where the component surface is).\n"
            "Set component and bondline thickness, then click 'Capture'")
            .arg(QString::fromStdString(m_pendingTeach.deviceName)));
        // Pre-populate spinboxes from pending teach data
        m_componentThicknessSpin->setValue(m_pendingTeach.componentThickness);
        m_bondlineThicknessSpin->setValue(m_pendingTeach.bondlineThickness);
        m_positionTimer->start(100);
        // Auto-navigate gantry to previously taught zComp position (reteach)
        if (std::abs(m_pendingTeach.zComp) > 1e-6 && m_context && m_context->motionManager && m_trainingSet) {
            std::string gantryDev = m_trainingSet->deviceName;
            double tx = m_pendingTeach.zCompX, ty = m_pendingTeach.zCompY, tz = m_pendingTeach.zComp;
            m_captureZCompBtn->setEnabled(false);
            m_moveFuture = std::async(std::launch::async, [this, gantryDev, tx, ty, tz]() {
                // Move to full XYZ if XY was stored, otherwise Z-only
                if (std::abs(tx) > 1e-6 || std::abs(ty) > 1e-6) {
                    m_context->motionManager->MoveToAbs(gantryDev, tx, ty, tz, 100.0, true);
                } else {
                    auto cur = m_context->motionManager->GetCurrentPosition(gantryDev);
                    m_context->motionManager->MoveToAbs(gantryDev, cur.x, cur.y, tz, 100.0, true);
                }
                QMetaObject::invokeMethod(this, [this]() {
                    m_captureZCompBtn->setEnabled(true);
                }, Qt::QueuedConnection);
            });
        }
        break;

    case CaptureZPlace:
        m_wizardStack->setCurrentIndex(4);
        m_zPlaceInstructionLabel->setText(QString(
            "Step 4/4: Capture Z Placement Height for %1\n\n"
            "Jog gantry Z to the PLACEMENT SURFACE\n"
            "(substrate or PCB surface where component will be placed).\n"
            "Then click 'Capture'")
            .arg(QString::fromStdString(m_pendingTeach.deviceName)));
        m_positionTimer->start(100);
        // Auto-navigate gantry to previously taught zPlace position (reteach)
        if (std::abs(m_pendingTeach.zPlace) > 1e-6 && m_context && m_context->motionManager && m_trainingSet) {
            std::string gantryDev = m_trainingSet->deviceName;
            double tx = m_pendingTeach.zPlaceX, ty = m_pendingTeach.zPlaceY, tz = m_pendingTeach.zPlace;
            m_captureZPlaceBtn->setEnabled(false);
            m_moveFuture = std::async(std::launch::async, [this, gantryDev, tx, ty, tz]() {
                // Move to full XYZ if XY was stored, otherwise Z-only
                if (std::abs(tx) > 1e-6 || std::abs(ty) > 1e-6) {
                    m_context->motionManager->MoveToAbs(gantryDev, tx, ty, tz, 100.0, true);
                } else {
                    auto cur = m_context->motionManager->GetCurrentPosition(gantryDev);
                    m_context->motionManager->MoveToAbs(gantryDev, cur.x, cur.y, tz, 100.0, true);
                }
                QMetaObject::invokeMethod(this, [this]() {
                    m_captureZPlaceBtn->setEnabled(true);
                }, Qt::QueuedConnection);
            });
        }
        break;

    case Summary: {
        m_wizardStack->setCurrentIndex(5);
        m_positionTimer->stop();

        QString summary = QString(
            "Taught Position for: %1\n\n"
            "Hex Device Position (6-axis):\n"
            "  X=%2  Y=%3  Z=%4\n"
            "  U=%5  V=%6  W=%7\n\n"
            "Gantry Viewing Position:\n"
            "  X=%8  Y=%9  Z=%10")
            .arg(QString::fromStdString(m_pendingTeach.deviceName))
            .arg(m_pendingTeach.hexX, 0, 'f', 4)
            .arg(m_pendingTeach.hexY, 0, 'f', 4)
            .arg(m_pendingTeach.hexZ, 0, 'f', 4)
            .arg(m_pendingTeach.hexU, 0, 'f', 4)
            .arg(m_pendingTeach.hexV, 0, 'f', 4)
            .arg(m_pendingTeach.hexW, 0, 'f', 4)
            .arg(m_pendingTeach.gantryViewX, 0, 'f', 4)
            .arg(m_pendingTeach.gantryViewY, 0, 'f', 4)
            .arg(m_pendingTeach.gantryViewZ, 0, 'f', 4);

        // Show teaching mode
        const char* modeNames[] = { "Single Point", "2-Corner", "3-Corner" };
        int modeIdx = m_pendingTeach.gantryTeachMode;
        if (modeIdx >= 0 && modeIdx <= 2) {
            summary += QString("\n  Teaching Mode: %1").arg(modeNames[modeIdx]);
        }

        // Show corner details if corner pattern was used
        if (m_cornerTL_X != 0.0 || m_cornerTL_Y != 0.0 ||
            m_cornerBR_X != 0.0 || m_cornerBR_Y != 0.0) {
            summary += QString("\n\nCorner Pattern (computed center):\n"
                "  Top-Left:      X=%1  Y=%2\n"
                "  Bottom-Right:  X=%3  Y=%4")
                .arg(m_cornerTL_X, 0, 'f', 4)
                .arg(m_cornerTL_Y, 0, 'f', 4)
                .arg(m_cornerBR_X, 0, 'f', 4)
                .arg(m_cornerBR_Y, 0, 'f', 4);
            if (m_hasThirdCorner) {
                summary += QString("\n  Top-Right:     X=%1  Y=%2")
                    .arg(m_cornerTR_X, 0, 'f', 4)
                    .arg(m_cornerTR_Y, 0, 'f', 4);
            }
            double dist = AccuracyTrainerMath::Distance(
                m_cornerTL_X, m_cornerTL_Y, m_cornerBR_X, m_cornerBR_Y);
            summary += QString("\n  Diagonal: %1 mm").arg(dist, 0, 'f', 4);
        }

        if (!m_pendingTeach.visionProfilePath.empty()) {
            summary += QString("\n\nVision Profile: %1").arg(
                QFileInfo(QString::fromStdString(m_pendingTeach.visionProfilePath)).baseName());
        }

        // Z placement adjustment info
        if (std::abs(m_pendingTeach.hexZAdjustment) > 1e-6) {
            summary += QString("\n\nZ Placement Adjustment:\n"
                "  Z Component: %1\n"
                "  Z Placement: %2\n"
                "  Component Thickness: %3\n"
                "  Bondline Thickness: %4\n"
                "  Hex Z Adjustment: %5")
                .arg(m_pendingTeach.zComp, 0, 'f', 4)
                .arg(m_pendingTeach.zPlace, 0, 'f', 4)
                .arg(m_pendingTeach.componentThickness, 0, 'f', 3)
                .arg(m_pendingTeach.bondlineThickness, 0, 'f', 3)
                .arg(m_pendingTeach.hexZAdjustment, 0, 'f', 4);
        }

        m_summaryLabel->setText(summary);
        break;
    }
    }
}

void TeachMultiDeviceWidget::onTeach()
{
    if (!m_trainingSet) return;

    m_pendingTeach = HexDeviceTeachData();
    m_pendingTeach.deviceName = m_hexDeviceCombo->currentText().toStdString();

    // Reset corner data
    m_cornerTL_X = m_cornerTL_Y = m_cornerTL_Z = 0.0;
    m_cornerBR_X = m_cornerBR_Y = 0.0;
    m_cornerTR_X = m_cornerTR_Y = 0.0;
    m_hasThirdCorner = false;

    std::cout << "[TeachMultiDevice] Starting teach for: " << m_pendingTeach.deviceName << std::endl;

    refreshVisionProfiles();
    setState(PositionHex);
}

void TeachMultiDeviceWidget::onReteachSelected()
{
    int row = m_teachList->currentRow();
    if (row < 0 || !m_trainingSet) {
        QMessageBox::information(this, "No Selection", "Please select a taught position to reteach.");
        return;
    }
    if (row >= static_cast<int>(m_trainingSet->hexDevicePositions.size())) return;
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    const auto& hd = m_trainingSet->hexDevicePositions[row];
    if (!hd.valid) {
        QMessageBox::warning(this, "Invalid", "Selected position is not valid.");
        return;
    }

    // Prepare pending teach with the same device name, carrying over Z params and teach mode
    m_pendingTeach = HexDeviceTeachData();
    m_pendingTeach.deviceName = hd.deviceName;
    m_pendingTeach.gantryTeachMode = hd.gantryTeachMode;
    m_pendingTeach.zCompX = hd.zCompX;
    m_pendingTeach.zCompY = hd.zCompY;
    m_pendingTeach.zComp = hd.zComp;
    m_pendingTeach.zPlaceX = hd.zPlaceX;
    m_pendingTeach.zPlaceY = hd.zPlaceY;
    m_pendingTeach.zPlace = hd.zPlace;
    m_pendingTeach.componentThickness = hd.componentThickness;
    m_pendingTeach.bondlineThickness = hd.bondlineThickness;
    m_pendingTeach.hexZAdjustment = hd.hexZAdjustment;

    // Reset corner data
    m_cornerTL_X = m_cornerTL_Y = m_cornerTL_Z = 0.0;
    m_cornerBR_X = m_cornerBR_Y = 0.0;
    m_cornerTR_X = m_cornerTR_Y = 0.0;
    m_hasThirdCorner = false;

    std::string hexDevice = hd.deviceName;
    PositionStruct hexTarget;
    hexTarget.x = hd.hexX;
    hexTarget.y = hd.hexY;
    hexTarget.z = hd.hexZ;
    hexTarget.u = hd.hexU;
    hexTarget.v = hd.hexV;
    hexTarget.w = hd.hexW;

    std::string gantryDevice = m_trainingSet->deviceName;
    double gx = hd.gantryViewX;
    double gy = hd.gantryViewY;
    double gz = hd.gantryViewZ;

    // Disable all buttons during move
    m_reteachBtn->setEnabled(false);
    m_reteachBtn->setText("Moving to taught position...");
    m_teachBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);
    m_moveToBtn->setEnabled(false);

    std::cout << "[TeachMultiDevice] Reteach: moving to taught position for " << hexDevice << std::endl;

    m_moveFuture = std::async(std::launch::async,
        [this, gantryDevice, gx, gy, gz, hexDevice, hexTarget]() {
        bool gantryOk = m_context->motionManager->MoveToAbs(gantryDevice, gx, gy, gz, 100.0, true);
        bool hexOk = m_context->motionManager->MoveToAbs6(hexDevice, hexTarget, true);

        QMetaObject::invokeMethod(this, [this, gantryOk, hexOk, gantryDevice, hexDevice]() {
            m_reteachBtn->setEnabled(true);
            m_reteachBtn->setText("Reteach Selected Position");
            m_teachBtn->setEnabled(true);
            m_removeBtn->setEnabled(true);
            m_moveToBtn->setEnabled(true);

            if (!gantryOk || !hexOk) {
                QString msg;
                if (!gantryOk && !hexOk) {
                    msg = "Both gantry and hex moves failed. Reteach cancelled.";
                } else if (!gantryOk) {
                    msg = QString("Gantry move failed. %1 moved OK. Continue reteach anyway?")
                        .arg(QString::fromStdString(hexDevice));
                } else {
                    msg = QString("Gantry moved OK. %1 move failed. Continue reteach anyway?")
                        .arg(QString::fromStdString(hexDevice));
                }

                if (!gantryOk && !hexOk) {
                    QMessageBox::warning(this, "Move Failed", msg);
                    return;
                }
                // Partial failure - ask user
                auto answer = QMessageBox::question(this, "Partial Move", msg,
                    QMessageBox::Yes | QMessageBox::No);
                if (answer != QMessageBox::Yes) return;
            }

            std::cout << "[TeachMultiDevice] Reteach: arrived at taught position, starting wizard" << std::endl;
            refreshVisionProfiles();
            setState(PositionHex);
        }, Qt::QueuedConnection);
    });
}

void TeachMultiDeviceWidget::onRemoveSelected()
{
    int row = m_teachList->currentRow();
    if (row < 0 || !m_trainingSet) return;

    if (row < static_cast<int>(m_trainingSet->hexDevicePositions.size())) {
        std::string removed = m_trainingSet->hexDevicePositions[row].deviceName;
        m_trainingSet->hexDevicePositions.erase(m_trainingSet->hexDevicePositions.begin() + row);
        refreshList();
        rebuildAssignmentUI();
        emit hexPositionsUpdated(m_trainingSet->hexDevicePositions);
        std::cout << "[TeachMultiDevice] Removed teach data for: " << removed << std::endl;
    }
}

void TeachMultiDeviceWidget::onMoveToPosition()
{
    int row = m_teachList->currentRow();
    if (row < 0 || !m_trainingSet) {
        QMessageBox::information(this, "No Selection", "Please select a taught position from the list.");
        return;
    }
    if (row >= static_cast<int>(m_trainingSet->hexDevicePositions.size())) return;
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    const auto& hd = m_trainingSet->hexDevicePositions[row];
    if (!hd.valid) {
        QMessageBox::warning(this, "Invalid", "Selected position is not valid.");
        return;
    }

    std::string hexDevice = hd.deviceName;
    PositionStruct hexTarget;
    hexTarget.x = hd.hexX;
    hexTarget.y = hd.hexY;
    hexTarget.z = hd.hexZ;
    hexTarget.u = hd.hexU;
    hexTarget.v = hd.hexV;
    hexTarget.w = hd.hexW;

    std::string gantryDevice = m_trainingSet->deviceName;
    double gx = hd.gantryViewX;
    double gy = hd.gantryViewY;
    double gz = hd.gantryViewZ;

    // Disable buttons during move
    m_moveToBtn->setEnabled(false);
    m_moveToBtn->setText("Moving...");
    m_teachBtn->setEnabled(false);
    m_removeBtn->setEnabled(false);

    std::cout << "[TeachMultiDevice] Moving " << hexDevice << " and " << gantryDevice
              << " to taught position" << std::endl;

    m_moveFuture = std::async(std::launch::async,
        [this, gantryDevice, gx, gy, gz, hexDevice, hexTarget]() {
        bool gantryOk = m_context->motionManager->MoveToAbs(gantryDevice, gx, gy, gz, 100.0, true);
        bool hexOk = m_context->motionManager->MoveToAbs6(hexDevice, hexTarget, true);

        QMetaObject::invokeMethod(this, [this, gantryOk, hexOk, gantryDevice, hexDevice]() {
            m_moveToBtn->setEnabled(true);
            m_moveToBtn->setText("Move Devices to Selected Position");
            m_teachBtn->setEnabled(true);
            m_removeBtn->setEnabled(true);

            if (!gantryOk && !hexOk) {
                QMessageBox::warning(this, "Move Failed",
                    QString("Both %1 and %2 moves failed.")
                        .arg(QString::fromStdString(gantryDevice))
                        .arg(QString::fromStdString(hexDevice)));
            } else if (!gantryOk) {
                QMessageBox::warning(this, "Partial Move",
                    QString("%1 move failed, %2 move succeeded.")
                        .arg(QString::fromStdString(gantryDevice))
                        .arg(QString::fromStdString(hexDevice)));
            } else if (!hexOk) {
                QMessageBox::warning(this, "Partial Move",
                    QString("%1 move succeeded, %2 move failed.")
                        .arg(QString::fromStdString(gantryDevice))
                        .arg(QString::fromStdString(hexDevice)));
            } else {
                std::cout << "[TeachMultiDevice] Both moves completed successfully" << std::endl;
            }
        }, Qt::QueuedConnection);
    });
}

void TeachMultiDeviceWidget::onCaptureHexPosition()
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    auto pos = m_context->motionManager->GetCurrentPosition(m_pendingTeach.deviceName);
    m_pendingTeach.hexX = pos.x;
    m_pendingTeach.hexY = pos.y;
    m_pendingTeach.hexZ = pos.z;
    m_pendingTeach.hexU = pos.u;
    m_pendingTeach.hexV = pos.v;
    m_pendingTeach.hexW = pos.w;

    std::cout << "[TeachMultiDevice] Captured hex position: X=" << pos.x
              << " Y=" << pos.y << " Z=" << pos.z
              << " U=" << pos.u << " V=" << pos.v << " W=" << pos.w << std::endl;

    setState(PositionGantry);
}

void TeachMultiDeviceWidget::onCaptureGantryPosition()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    std::string gantryDevice = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(gantryDevice);
    m_pendingTeach.gantryViewX = pos.x;
    m_pendingTeach.gantryViewY = pos.y;
    m_pendingTeach.gantryViewZ = pos.z;
    m_pendingTeach.gantryTeachMode = 0;  // single point
    m_pendingTeach.valid = true;

    // Save selected vision profile
    if (m_visionProfileCombo->currentIndex() > 0) {
        m_pendingTeach.visionProfilePath = m_visionProfileCombo->currentData().toString().toStdString();
    } else {
        m_pendingTeach.visionProfilePath.clear();
    }

    std::cout << "[TeachMultiDevice] Captured gantry view position: X=" << pos.x
              << " Y=" << pos.y << " Z=" << pos.z;
    if (!m_pendingTeach.visionProfilePath.empty()) {
        std::cout << " vision=" << m_pendingTeach.visionProfilePath;
    }
    std::cout << std::endl;

    setState(CaptureZComp);
}

void TeachMultiDeviceWidget::onGantryModeChanged(int index)
{
    if (m_state != PositionGantry) return;

    if (index == 1) {
        // Corner Pattern mode - start corner capture flow
        m_cornerTL_X = m_cornerTL_Y = m_cornerTL_Z = 0.0;
        m_cornerBR_X = m_cornerBR_Y = 0.0;
        m_cornerTR_X = m_cornerTR_Y = 0.0;
        m_hasThirdCorner = false;
        setState(GantryCorner1);
    } else {
        // Single point mode - stay on PositionGantry with single-point sub-page
        m_gantryModeStack->setCurrentIndex(0);
    }
}

void TeachMultiDeviceWidget::onCaptureGantryCorner()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    std::string gantryDevice = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(gantryDevice);

    switch (m_state) {
    case GantryCorner1:
        m_cornerTL_X = pos.x;
        m_cornerTL_Y = pos.y;
        m_cornerTL_Z = pos.z;
        std::cout << "[TeachMultiDevice] Corner 1 (TL): X=" << pos.x
                  << " Y=" << pos.y << " Z=" << pos.z << std::endl;
        setState(GantryCorner2);
        break;

    case GantryCorner2:
        m_cornerBR_X = pos.x;
        m_cornerBR_Y = pos.y;
        std::cout << "[TeachMultiDevice] Corner 2 (BR): X=" << pos.x
                  << " Y=" << pos.y << std::endl;
        setState(GantryCorner3);
        break;

    case GantryCorner3:
        m_cornerTR_X = pos.x;
        m_cornerTR_Y = pos.y;
        m_hasThirdCorner = true;
        std::cout << "[TeachMultiDevice] Corner 3 (TR): X=" << pos.x
                  << " Y=" << pos.y << std::endl;
        finalizeCornerCapture();
        break;

    default:
        break;
    }
}

void TeachMultiDeviceWidget::onSkipGantryCorner3()
{
    m_hasThirdCorner = false;
    std::cout << "[TeachMultiDevice] Skipped corner 3" << std::endl;
    finalizeCornerCapture();
}

void TeachMultiDeviceWidget::onConfirm()
{
    if (!m_trainingSet) return;

    // Replace existing entry for same device, or add new
    bool replaced = false;
    for (auto& hd : m_trainingSet->hexDevicePositions) {
        if (hd.deviceName == m_pendingTeach.deviceName) {
            hd = m_pendingTeach;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m_trainingSet->hexDevicePositions.push_back(m_pendingTeach);
    }

    std::cout << "[TeachMultiDevice] " << (replaced ? "Updated" : "Added")
              << " teach data for: " << m_pendingTeach.deviceName << std::endl;

    refreshList();
    rebuildAssignmentUI();
    emit hexPositionsUpdated(m_trainingSet->hexDevicePositions);
    setState(Idle);
}

void TeachMultiDeviceWidget::onRedo()
{
    // Reset corner data on redo
    m_cornerTL_X = m_cornerTL_Y = m_cornerTL_Z = 0.0;
    m_cornerBR_X = m_cornerBR_Y = 0.0;
    m_cornerTR_X = m_cornerTR_Y = 0.0;
    m_hasThirdCorner = false;
    setState(PositionHex);
}

void TeachMultiDeviceWidget::onDetectCenter()
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
    m_captureGantryBtn->setEnabled(false);

    std::cout << "[TeachMultiDevice] Detect & Center: profile=" << profilePath
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
            std::cout << "[TeachMultiDevice] Vision execution error: " << error << std::endl;
            QMetaObject::invokeMethod(qApp, [this, error]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_captureGantryBtn->setEnabled(true);
                QMessageBox::warning(this, "Detection Failed",
                    QString::fromStdString("Vision command failed: " + error));
            }, Qt::QueuedConnection);
            return;
        }

        bool visionSuccess = ctx.GetBoolean("vision.result.success", false);
        if (!visionSuccess) {
            std::string visionError = ctx.GetString("vision.result.error", "Unknown error");
            std::cout << "[TeachMultiDevice] Vision failed: " << visionError << std::endl;
            QMetaObject::invokeMethod(qApp, [this, visionError]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_captureGantryBtn->setEnabled(true);
                QMessageBox::warning(this, "Detection Failed",
                    QString::fromStdString("Vision result: " + visionError));
            }, Qt::QueuedConnection);
            return;
        }

        if (!ctx.HasValue("vision.detected.world_x_mm") ||
            !ctx.HasValue("vision.detected.world_y_mm")) {
            std::cout << "[TeachMultiDevice] No feature detected (no world position)" << std::endl;
            QMetaObject::invokeMethod(qApp, [this]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_captureGantryBtn->setEnabled(true);
                QMessageBox::warning(this, "No Feature",
                    "Vision ran successfully but no feature center was detected.\n"
                    "Check that the profile returns circle/point results.");
            }, Qt::QueuedConnection);
            return;
        }

        double worldX = static_cast<double>(ctx.GetNumeric("vision.detected.world_x_mm", 0.0f));
        double worldY = static_cast<double>(ctx.GetNumeric("vision.detected.world_y_mm", 0.0f));

        std::cout << "[TeachMultiDevice] Detected feature at world: X=" << worldX
                  << " Y=" << worldY << std::endl;

        auto currentPos = m_context->motionManager->GetCurrentPosition(device);
        bool moveOk = m_context->motionManager->MoveToAbs(
            device, worldX, worldY, currentPos.z, 100.0, true);

        if (!moveOk) {
            std::cout << "[TeachMultiDevice] Move to detected position failed" << std::endl;
            QMetaObject::invokeMethod(qApp, [this]() {
                m_detectCenterBtn->setEnabled(true);
                m_detectCenterBtn->setText("Detect && Center");
                m_captureGantryBtn->setEnabled(true);
                QMessageBox::warning(this, "Move Failed",
                    "Failed to move gantry to detected position.");
            }, Qt::QueuedConnection);
            return;
        }

        std::cout << "[TeachMultiDevice] Centered on feature at X=" << worldX
                  << " Y=" << worldY << std::endl;

        QMetaObject::invokeMethod(qApp, [this]() {
            m_detectCenterBtn->setEnabled(true);
            m_detectCenterBtn->setText("Detect && Center");
            m_captureGantryBtn->setEnabled(true);
        }, Qt::QueuedConnection);
    });
#else
    QMessageBox::information(this, "Not Available",
        "Vision features require OpenCV. Build with HAS_OPENCV enabled.");
#endif
}

void TeachMultiDeviceWidget::onCaptureZComp()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    std::string gantryDevice = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(gantryDevice);
    m_pendingTeach.zCompX = pos.x;
    m_pendingTeach.zCompY = pos.y;
    m_pendingTeach.zComp = pos.z;
    m_pendingTeach.componentThickness = m_componentThicknessSpin->value();
    m_pendingTeach.bondlineThickness = m_bondlineThicknessSpin->value();

    std::cout << "[TeachMultiDevice] Captured Z Component pos: X=" << pos.x
              << " Y=" << pos.y << " Z=" << pos.z
              << " Tcomp=" << m_pendingTeach.componentThickness
              << " BLT=" << m_pendingTeach.bondlineThickness << std::endl;

    setState(CaptureZPlace);
}

void TeachMultiDeviceWidget::onCaptureZPlace()
{
    if (!m_context || !m_context->motionManager || !m_trainingSet) {
        QMessageBox::warning(this, "Error", "Motion manager not available.");
        return;
    }

    std::string gantryDevice = m_trainingSet->deviceName;
    auto pos = m_context->motionManager->GetCurrentPosition(gantryDevice);
    m_pendingTeach.zPlaceX = pos.x;
    m_pendingTeach.zPlaceY = pos.y;
    m_pendingTeach.zPlace = pos.z;

    // Compute hex Z adjustment: negated gap between component bottom and placement surface
    // gap = zPlace - zComp - (Tcomp + BLT), then subtract from hex Z (lower the hex)
    m_pendingTeach.hexZAdjustment = -(m_pendingTeach.zPlace - m_pendingTeach.zComp
        - (m_pendingTeach.componentThickness + m_pendingTeach.bondlineThickness));

    std::cout << "[TeachMultiDevice] Captured Z Placement pos: X=" << pos.x
              << " Y=" << pos.y << " Z=" << pos.z
              << " hexZAdjustment=" << m_pendingTeach.hexZAdjustment << std::endl;

    setState(Summary);
}

void TeachMultiDeviceWidget::finalizeCornerCapture()
{
    // Calculate center from captured corners
    double centerX = 0.0;
    double centerY = 0.0;

    if (m_hasThirdCorner) {
        // 3-corner: infer 4th corner (BL) and compute centroid of the rectangle
        double blX = m_cornerTL_X + m_cornerBR_X - m_cornerTR_X;
        double blY = m_cornerTL_Y + m_cornerBR_Y - m_cornerTR_Y;
        centerX = (m_cornerTL_X + m_cornerBR_X + m_cornerTR_X + blX) / 4.0;
        centerY = (m_cornerTL_Y + m_cornerBR_Y + m_cornerTR_Y + blY) / 4.0;
    } else {
        // 2-corner: midpoint of TL and BR diagonal
        centerX = (m_cornerTL_X + m_cornerBR_X) / 2.0;
        centerY = (m_cornerTL_Y + m_cornerBR_Y) / 2.0;
    }

    double dist = AccuracyTrainerMath::Distance(
        m_cornerTL_X, m_cornerTL_Y, m_cornerBR_X, m_cornerBR_Y);

    std::cout << "[TeachMultiDevice] Corner center: X=" << centerX << " Y=" << centerY
              << " diagonal=" << dist << "mm"
              << (m_hasThirdCorner ? " (3-corner)" : " (2-corner)") << std::endl;

    // Store computed center as gantry view position
    m_pendingTeach.gantryViewX = centerX;
    m_pendingTeach.gantryViewY = centerY;
    m_pendingTeach.gantryViewZ = m_cornerTL_Z;
    m_pendingTeach.gantryTeachMode = m_hasThirdCorner ? 2 : 1;  // 2=3-corner, 1=2-corner
    m_pendingTeach.valid = true;

    // Move gantry to the computed center
    if (m_context && m_context->motionManager && m_trainingSet) {
        std::string gantryDevice = m_trainingSet->deviceName;

        m_captureCornerBtn->setEnabled(false);
        m_skipCorner3Btn->setEnabled(false);
        m_cancelCornerBtn->setEnabled(false);
        m_cornerInstructionLabel->setText("Moving gantry to computed center...");

        m_moveFuture = std::async(std::launch::async,
            [this, gantryDevice, centerX, centerY]() {
            double curZ = m_context->motionManager->GetCurrentPosition(gantryDevice).z;
            bool moveOk = m_context->motionManager->MoveToAbs(
                gantryDevice, centerX, centerY, curZ, 100.0, true);

            QMetaObject::invokeMethod(this, [this, moveOk]() {
                m_captureCornerBtn->setEnabled(true);
                m_skipCorner3Btn->setEnabled(true);
                m_cancelCornerBtn->setEnabled(true);

                if (!moveOk) {
                    std::cout << "[TeachMultiDevice] Move to corner center failed" << std::endl;
                    QMessageBox::warning(this, "Move Failed",
                        "Failed to move gantry to computed center.\n"
                        "The computed position will still be used.");
                } else {
                    std::cout << "[TeachMultiDevice] Gantry moved to corner center" << std::endl;
                }

                setState(CaptureZComp);
            }, Qt::QueuedConnection);
        });
    } else {
        setState(CaptureZComp);
    }
}

void TeachMultiDeviceWidget::refreshVisionProfiles()
{
    m_visionProfileCombo->clear();
    m_visionProfileCombo->addItem("(None - manual only)", QString());

    QString visionDir = getVisionDirectory();
    QDir dir(visionDir);
    if (!dir.exists()) {
        std::cout << "[TeachMultiDevice] Vision directory not found: "
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

    std::cout << "[TeachMultiDevice] Loaded " << count << " vision profiles from: "
              << visionDir.toStdString() << std::endl;
}

QString TeachMultiDeviceWidget::getVisionDirectory() const
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

void TeachMultiDeviceWidget::onCancel()
{
    m_positionTimer->stop();
    setState(Idle);
}

void TeachMultiDeviceWidget::updatePositionDisplay()
{
    if (!m_context || !m_context->motionManager) return;

    if (m_state == PositionHex) {
        auto pos = m_context->motionManager->GetCurrentPosition(m_pendingTeach.deviceName);
        m_hexPositionLabel->setText(QString("X: %1  Y: %2  Z: %3\nU: %4  V: %5  W: %6")
            .arg(pos.x, 10, 'f', 4)
            .arg(pos.y, 10, 'f', 4)
            .arg(pos.z, 10, 'f', 4)
            .arg(pos.u, 10, 'f', 4)
            .arg(pos.v, 10, 'f', 4)
            .arg(pos.w, 10, 'f', 4));
    } else if (m_state == PositionGantry && m_trainingSet) {
        std::string gantryDevice = m_trainingSet->deviceName;
        auto pos = m_context->motionManager->GetCurrentPosition(gantryDevice);
        m_gantryPositionLabel->setText(QString("X: %1  Y: %2  Z: %3")
            .arg(pos.x, 10, 'f', 4)
            .arg(pos.y, 10, 'f', 4)
            .arg(pos.z, 10, 'f', 4));
    } else if ((m_state == GantryCorner1 || m_state == GantryCorner2 || m_state == GantryCorner3)
               && m_trainingSet) {
        std::string gantryDevice = m_trainingSet->deviceName;
        auto pos = m_context->motionManager->GetCurrentPosition(gantryDevice);
        m_cornerPositionLabel->setText(QString("X: %1  Y: %2  Z: %3")
            .arg(pos.x, 10, 'f', 4)
            .arg(pos.y, 10, 'f', 4)
            .arg(pos.z, 10, 'f', 4));
    } else if ((m_state == CaptureZComp || m_state == CaptureZPlace) && m_trainingSet) {
        std::string gantryDevice = m_trainingSet->deviceName;
        auto pos = m_context->motionManager->GetCurrentPosition(gantryDevice);
        QString posText = QString("X: %1  Y: %2  Z: %3")
            .arg(pos.x, 10, 'f', 4)
            .arg(pos.y, 10, 'f', 4)
            .arg(pos.z, 10, 'f', 4);
        if (m_state == CaptureZComp) {
            m_zCompPositionLabel->setText(posText);
        } else {
            m_zPlacePositionLabel->setText(posText);
        }
    }
}

void TeachMultiDeviceWidget::refreshList()
{
    m_teachList->clear();
    if (!m_trainingSet) return;

    for (const auto& hd : m_trainingSet->hexDevicePositions) {
        QString text = QString("%1  |  Hex(%2, %3, %4)  Gantry(%5, %6, %7)")
            .arg(QString::fromStdString(hd.deviceName))
            .arg(hd.hexX, 0, 'f', 3)
            .arg(hd.hexY, 0, 'f', 3)
            .arg(hd.hexZ, 0, 'f', 3)
            .arg(hd.gantryViewX, 0, 'f', 3)
            .arg(hd.gantryViewY, 0, 'f', 3)
            .arg(hd.gantryViewZ, 0, 'f', 3);
        // Show teach mode
        if (hd.gantryTeachMode == 1) {
            text += "  [2-corner]";
        } else if (hd.gantryTeachMode == 2) {
            text += "  [3-corner]";
        }
        if (!hd.visionProfilePath.empty()) {
            text += QString("  [vision: %1]").arg(
                QFileInfo(QString::fromStdString(hd.visionProfilePath)).baseName());
        }
        if (std::abs(hd.hexZAdjustment) > 1e-6) {
            text += QString("  [Zadj=%1]").arg(hd.hexZAdjustment, 0, 'f', 3);
        }
        if (!hd.valid) {
            text += "  [invalid]";
        }
        m_teachList->addItem(text);
    }
}

void TeachMultiDeviceWidget::rebuildAssignmentUI()
{
    m_assignCombos.clear();

    // Clear old layout
    QLayout* oldLayout = m_assignContainer->layout();
    if (oldLayout) {
        for (int i = 0; i < 1000; ++i) {
            QLayoutItem* item = oldLayout->takeAt(0);
            if (!item) break;
            delete item->widget();
            delete item;
        }
        delete oldLayout;
    }

    QVBoxLayout* layout = new QVBoxLayout(m_assignContainer);
    layout->setSpacing(4);
    layout->setContentsMargins(4, 4, 4, 4);

    if (!m_trainingSet || m_trainingSet->bondingPoints.empty()) {
        QLabel* noPoints = new QLabel("No bonding points defined yet.");
        noPoints->setStyleSheet("color: #666; font-size: 11px;");
        layout->addWidget(noPoints);
        return;
    }

    // Combo style shared by all
    QString comboStyle =
        "QComboBox { background-color: #1a1f2e; color: white; border: 1px solid #4d6e9f; "
        "padding: 4px 8px; border-radius: 3px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #1a1f2e; color: white; "
        "selection-background-color: #3d5a8c; }";

    for (const auto& bp : m_trainingSet->bondingPoints) {
        QHBoxLayout* row = new QHBoxLayout();

        QLabel* nameLabel = new QLabel(QString::fromStdString(bp.name));
        nameLabel->setStyleSheet("color: #ccc; font-size: 12px; font-weight: bold;");
        nameLabel->setFixedWidth(120);
        row->addWidget(nameLabel);

        QLabel* arrow = new QLabel("->");
        arrow->setStyleSheet("color: #888; font-size: 12px;");
        arrow->setFixedWidth(20);
        row->addWidget(arrow);

        QComboBox* combo = new QComboBox();
        combo->setStyleSheet(comboStyle);
        combo->setProperty("bondName", QString::fromStdString(bp.name));

        // gantry-main is always first option
        combo->addItem("gantry-main");

        // Add taught hex devices
        for (const auto& hd : m_trainingSet->hexDevicePositions) {
            if (hd.valid) {
                combo->addItem(QString::fromStdString(hd.deviceName));
            }
        }

        // Restore saved assignment
        auto it = m_trainingSet->bondingDeviceAssignments.find(bp.name);
        if (it != m_trainingSet->bondingDeviceAssignments.end()) {
            int idx = combo->findText(QString::fromStdString(it->second));
            if (idx >= 0) {
                combo->setCurrentIndex(idx);
            }
        }

        connect(combo, &QComboBox::currentTextChanged,
                this, &TeachMultiDeviceWidget::onAssignmentComboChanged);

        row->addWidget(combo, 1);
        m_assignCombos.push_back(combo);

        QWidget* rowWidget = new QWidget();
        rowWidget->setLayout(row);
        layout->addWidget(rowWidget);
    }

    layout->addStretch();
}

void TeachMultiDeviceWidget::onAssignmentComboChanged()
{
    if (!m_trainingSet) return;

    // Read all combos and update the map
    m_trainingSet->bondingDeviceAssignments.clear();
    for (auto* combo : m_assignCombos) {
        QString bondName = combo->property("bondName").toString();
        QString device = combo->currentText();
        // Only store non-default assignments (gantry-main is default)
        if (device != "gantry-main") {
            m_trainingSet->bondingDeviceAssignments[bondName.toStdString()] = device.toStdString();
        }
    }

    std::cout << "[TeachMultiDevice] Assignments updated: " << m_trainingSet->bondingDeviceAssignments.size()
              << " non-default" << std::endl;

    emit deviceAssignmentsChanged();
}

// PlacementCorrectionPage.cpp - Main page for Placement Correction Trainer
#include "PlacementCorrectionPage.h"
#include "TeachPlacementLineWidget.h"
#include "PlacementResultWidget.h"
#include "PlacementRetraceWidget.h"
#include "ExposureAdjustWidget.h"
#include "ImageGalleryWidget.h"
#include "SummaryDiagramWidget.h"
#include "RetraceLogViewer.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "SequenceEngine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QShowEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QProgressBar>
#include "FileSelectorDialog.h"
#include "CameraManager.h"
#include <iostream>
#include <filesystem>

PlacementCorrectionPage::PlacementCorrectionPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_navList(nullptr)
    , m_setLabel(nullptr)
    , m_newBtn(nullptr)
    , m_loadBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_targetDeviceCombo(nullptr)
    , m_cameraCombo(nullptr)
    , m_masterPositionCombo(nullptr)
    , m_masterUVWLabel(nullptr)
    , m_targetPositionCombo(nullptr)
    , m_targetUVWLabel(nullptr)
    , m_statusLabel(nullptr)
    , m_contentStack(nullptr)
    , m_welcomeWidget(nullptr)
    , m_teachRefLineWidget(nullptr)
    , m_teachTgtLineWidget(nullptr)
    , m_resultWidget(nullptr)
    , m_retraceWidget(nullptr)
    , m_exposureAdjustWidget(nullptr)
    , m_imageGalleryWidget(nullptr)
    , m_summaryDiagramWidget(nullptr)
    , m_topBarWidget(nullptr)
{
    updateStoreBasePath();
    setupUI();
    setSetLoaded(false);
}

void PlacementCorrectionPage::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top bar with back button (wrapped so it can be hidden)
    m_topBarWidget = new QWidget();
    QVBoxLayout* topBarContainer = new QVBoxLayout(m_topBarWidget);
    topBarContainer->setContentsMargins(0, 0, 0, 0);
    topBarContainer->setSpacing(0);

    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setContentsMargins(10, 5, 10, 5);

    QPushButton* backBtn = new QPushButton("Back to Main Menu");
    backBtn->setFixedHeight(32);
    backBtn->setStyleSheet(
        "QPushButton { background-color: #3d5a8c; color: white; font-weight: bold; "
        "padding: 5px 15px; border: 1px solid #4d6e9f; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4d6e9f; }");
    connect(backBtn, &QPushButton::clicked, this, &PlacementCorrectionPage::backToMainMenu);
    topBar->addWidget(backBtn);

    QLabel* titleLabel = new QLabel("Placement Correction Trainer");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff; padding: 0 15px;");
    topBar->addWidget(titleLabel);
    topBar->addStretch();

    topBarContainer->addLayout(topBar);

    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background-color: #3d5a8c;");
    sep->setFixedHeight(2);
    topBarContainer->addWidget(sep);

    mainLayout->addWidget(m_topBarWidget);

    // Splitter: left 30% | right 70%
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    // ========================================
    // LEFT PANEL
    // ========================================
    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(10, 10, 5, 10);
    leftLayout->setSpacing(8);

    QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    QString comboStyle =
        "QComboBox { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #2d3e5f; color: white; "
        "selection-background-color: #3d5a8c; }";

    // Training Set management group
    QGroupBox* tsGroup = new QGroupBox("Training Set");
    tsGroup->setStyleSheet(groupStyle);
    QVBoxLayout* tsLayout = new QVBoxLayout(tsGroup);

    m_setLabel = new QLabel("(none loaded)");
    m_setLabel->setStyleSheet(
        "QLabel { background-color: #1a1f2e; color: #ccc; border: 1px solid #3d5a8c; "
        "border-radius: 3px; padding: 5px 8px; font-size: 12px; }");
    tsLayout->addWidget(m_setLabel);

    QHBoxLayout* tsButtonRow = new QHBoxLayout();
    m_newBtn = new QPushButton("New");
    m_loadBtn = new QPushButton("Load");
    m_deleteBtn = new QPushButton("Delete");
    m_saveBtn = new QPushButton("Save");
    m_saveAsBtn = new QPushButton("Save As");

    for (auto* btn : {m_newBtn, m_loadBtn, m_deleteBtn, m_saveBtn, m_saveAsBtn}) {
        btn->setFixedHeight(28);
        btn->setStyleSheet(
            "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
            "border-radius: 3px; padding: 3px 8px; font-size: 11px; }"
            "QPushButton:hover { background-color: #3d5a8c; }"
            "QPushButton:disabled { background-color: #1a1f2e; color: #555; }");
    }

    tsButtonRow->addWidget(m_newBtn);
    tsButtonRow->addWidget(m_loadBtn);
    tsButtonRow->addWidget(m_deleteBtn);
    tsButtonRow->addWidget(m_saveBtn);
    tsButtonRow->addWidget(m_saveAsBtn);
    tsLayout->addLayout(tsButtonRow);

    connect(m_newBtn, &QPushButton::clicked, this, &PlacementCorrectionPage::onNewSet);
    connect(m_loadBtn, &QPushButton::clicked, this, &PlacementCorrectionPage::onLoadSet);
    connect(m_deleteBtn, &QPushButton::clicked, this, &PlacementCorrectionPage::onDeleteSet);
    connect(m_saveBtn, &QPushButton::clicked, this, &PlacementCorrectionPage::onSaveSet);
    connect(m_saveAsBtn, &QPushButton::clicked, this, &PlacementCorrectionPage::onSaveAsSet);

    leftLayout->addWidget(tsGroup);

    // Tabbed container: Device / Positions / Camera
    QTabWidget* cfgTabs = new QTabWidget();
    cfgTabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #3d5a8c; border-radius: 4px; "
        "background-color: #1a1f2e; }"
        "QTabBar::tab { background-color: #2d3e5f; color: #ccc; "
        "padding: 6px 12px; border: 1px solid #3d5a8c; border-bottom: none; "
        "border-top-left-radius: 4px; border-top-right-radius: 4px; font-size: 12px; }"
        "QTabBar::tab:selected { background-color: #3d5a8c; color: white; }"
        "QTabBar::tab:hover { background-color: #3d5a8c; }");

    // --- Tab: Device ---
    QWidget* deviceTab = new QWidget();
    QVBoxLayout* deviceTabLayout = new QVBoxLayout(deviceTab);
    deviceTabLayout->setContentsMargins(8, 10, 8, 8);

    m_targetDeviceCombo = new QComboBox();
    m_targetDeviceCombo->setStyleSheet(comboStyle);
    populateHexDevices();
    connect(m_targetDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlacementCorrectionPage::onTargetDeviceChanged);
    deviceTabLayout->addWidget(m_targetDeviceCombo);
    deviceTabLayout->addStretch(1);
    cfgTabs->addTab(deviceTab, "Device");

    // --- Tab: Positions ---
    QWidget* positionsTab = new QWidget();
    QVBoxLayout* leftLayout_positions = new QVBoxLayout(positionsTab);
    leftLayout_positions->setContentsMargins(8, 10, 8, 8);
    leftLayout_positions->setSpacing(8);

    QString moveBtnStyle =
        "QPushButton { background-color: #16a085; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 4px 10px; font-size: 11px; }"
        "QPushButton:hover { background-color: #1abc9c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    // Master Position group — labeled "Target Position" in UI; internal
    // data model name "master" kept for compatibility with saved JSON sets.
    QGroupBox* masterGroup = new QGroupBox("Target Position");
    masterGroup->setStyleSheet(groupStyle);
    masterGroup->setToolTip("first light position");
    QVBoxLayout* masterLayout = new QVBoxLayout(masterGroup);

    {
        QHBoxLayout* row = new QHBoxLayout();
        m_masterPositionCombo = new QComboBox();
        m_masterPositionCombo->setStyleSheet(comboStyle);
        row->addWidget(m_masterPositionCombo, 1);
        m_masterMoveBtn = new QPushButton("Move");
        m_masterMoveBtn->setFixedSize(60, 28);
        m_masterMoveBtn->setStyleSheet(moveBtnStyle);
        m_masterMoveBtn->setToolTip("Move selected hex device to this position");
        connect(m_masterMoveBtn, &QPushButton::clicked,
                this, &PlacementCorrectionPage::onMoveMaster);
        row->addWidget(m_masterMoveBtn);
        masterLayout->addLayout(row);
    }

    m_masterUVWLabel = new QLabel("UVW: ---");
    m_masterUVWLabel->setStyleSheet(
        "font-size: 11px; color: #aaa; font-family: monospace; padding: 4px;");
    m_masterUVWLabel->setWordWrap(true);
    masterLayout->addWidget(m_masterUVWLabel);

    leftLayout_positions->addWidget(masterGroup);

    // Target Position group
    QGroupBox* targetGroup = new QGroupBox("Position after correction");
    targetGroup->setStyleSheet(groupStyle);
    QVBoxLayout* targetLayout = new QVBoxLayout(targetGroup);

    {
        QHBoxLayout* row = new QHBoxLayout();
        m_targetPositionCombo = new QComboBox();
        m_targetPositionCombo->setStyleSheet(comboStyle);
        row->addWidget(m_targetPositionCombo, 1);
        m_targetMoveBtn = new QPushButton("Move");
        m_targetMoveBtn->setFixedSize(60, 28);
        m_targetMoveBtn->setStyleSheet(moveBtnStyle);
        m_targetMoveBtn->setToolTip("Move selected hex device to this position");
        connect(m_targetMoveBtn, &QPushButton::clicked,
                this, &PlacementCorrectionPage::onMoveTarget);
        row->addWidget(m_targetMoveBtn);
        targetLayout->addLayout(row);
    }

    m_targetUVWLabel = new QLabel("UVW: ---");
    m_targetUVWLabel->setStyleSheet(
        "font-size: 11px; color: #aaa; font-family: monospace; padding: 4px;");
    m_targetUVWLabel->setWordWrap(true);
    targetLayout->addWidget(m_targetUVWLabel);

    leftLayout_positions->addWidget(targetGroup);

    // Normal Placement Position group
    QGroupBox* normalGroup = new QGroupBox("Normal Placement Position");
    normalGroup->setStyleSheet(groupStyle);
    QVBoxLayout* normalLayout = new QVBoxLayout(normalGroup);

    {
        QHBoxLayout* row = new QHBoxLayout();
        m_normalPositionCombo = new QComboBox();
        m_normalPositionCombo->setStyleSheet(comboStyle);
        row->addWidget(m_normalPositionCombo, 1);
        m_normalMoveBtn = new QPushButton("Move");
        m_normalMoveBtn->setFixedSize(60, 28);
        m_normalMoveBtn->setStyleSheet(moveBtnStyle);
        m_normalMoveBtn->setToolTip("Move selected hex device to this position");
        connect(m_normalMoveBtn, &QPushButton::clicked,
                this, &PlacementCorrectionPage::onMoveNormal);
        row->addWidget(m_normalMoveBtn);
        normalLayout->addLayout(row);
    }

    leftLayout_positions->addWidget(normalGroup);
    leftLayout_positions->addStretch(1);
    cfgTabs->addTab(positionsTab, "Positions");

    // --- Tab: Camera ---
    QWidget* cameraTab = new QWidget();
    QVBoxLayout* cameraTabLayout = new QVBoxLayout(cameraTab);
    cameraTabLayout->setContentsMargins(8, 10, 8, 8);

    m_cameraCombo = new QComboBox();
    m_cameraCombo->setStyleSheet(comboStyle);
    if (m_context && m_context->cameraManager) {
        auto ids = m_context->cameraManager->GetCameraIds();
        for (const auto& id : ids) {
            m_cameraCombo->addItem(QString::fromStdString(id));
        }
    }
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int /*index*/) {
        m_currentSet.config.cameraId = m_cameraCombo->currentText().toStdString();
    });
    cameraTabLayout->addWidget(m_cameraCombo);
    cameraTabLayout->addStretch(1);
    cfgTabs->addTab(cameraTab, "Camera");

    leftLayout->addWidget(cfgTabs);

    // --- Move-in-progress status (hidden when idle) ---
    m_moveStatusWidget = new QWidget();
    QHBoxLayout* moveStatusLayout = new QHBoxLayout(m_moveStatusWidget);
    moveStatusLayout->setContentsMargins(0, 4, 0, 4);
    moveStatusLayout->setSpacing(6);

    m_moveProgressBar = new QProgressBar();
    m_moveProgressBar->setRange(0, 0);   // indeterminate / busy
    m_moveProgressBar->setTextVisible(false);
    m_moveProgressBar->setFixedHeight(22);
    m_moveProgressBar->setStyleSheet(
        "QProgressBar { background-color: #1a1f2e; border: 1px solid #3d5a8c; "
        "border-radius: 3px; }"
        "QProgressBar::chunk { background-color: #16a085; border-radius: 2px; }");
    moveStatusLayout->addWidget(m_moveProgressBar, 1);

    m_moveStopBtn = new QPushButton("Stop");
    m_moveStopBtn->setFixedSize(70, 26);
    m_moveStopBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "border-radius: 3px; padding: 2px 8px; font-size: 11px; }"
        "QPushButton:hover { background-color: #e74c3c; }");
    m_moveStopBtn->setToolTip("Emergency-stop the running move");
    connect(m_moveStopBtn, &QPushButton::clicked,
            this, &PlacementCorrectionPage::onStopMove);
    moveStatusLayout->addWidget(m_moveStopBtn);

    m_moveStatusWidget->setVisible(false);
    leftLayout->addWidget(m_moveStatusWidget);

    // Populate all combos after all are created
    populateNamedPositions();
    connect(m_masterPositionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlacementCorrectionPage::onMasterPositionChanged);
    connect(m_targetPositionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlacementCorrectionPage::onTargetPositionChanged);
    connect(m_normalPositionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PlacementCorrectionPage::onNormalPositionChanged);

    // Navigation list
    QGroupBox* navGroup = new QGroupBox("Workflow");
    navGroup->setStyleSheet(groupStyle);
    QVBoxLayout* navLayout = new QVBoxLayout(navGroup);
    m_navList = new QListWidget();
    m_navList->setStyleSheet(
        "QListWidget { background-color: #1a1f2e; color: white; border: none; font-size: 13px; }"
        "QListWidget::item { padding: 10px 8px; border-bottom: 1px solid #2d3e5f; }"
        "QListWidget::item:selected { background-color: #3d5a8c; }"
        "QListWidget::item:hover { background-color: #2d3e5f; }");
    m_navList->addItem("1. Teach Reference Line");
    m_navList->addItem("2. Teach Target Line");
    m_navList->addItem("3. Teach Normal Placement Line");
    m_navList->addItem("4. View Results");
    m_navList->addItem("5. Adjust Exposure");
    m_navList->addItem("6. Retrace & Review");
    m_navList->addItem("7. Reference Images");
    m_navList->addItem("8. Summary Diagram");
    m_navList->addItem("9. View Retrace Log");
    connect(m_navList, &QListWidget::currentRowChanged,
            this, &PlacementCorrectionPage::onNavItemChanged);
    navLayout->addWidget(m_navList);
    leftLayout->addWidget(navGroup);

    // Status label
    m_statusLabel = new QLabel("No training set loaded");
    m_statusLabel->setStyleSheet("color: #888; font-size: 11px; padding: 5px;");
    m_statusLabel->setWordWrap(true);
    leftLayout->addWidget(m_statusLabel);

    leftLayout->addStretch();

    // ========================================
    // RIGHT PANEL
    // ========================================
    m_contentStack = new QStackedWidget();

    // Welcome/placeholder widget (index 0)
    m_welcomeWidget = new QWidget();
    QVBoxLayout* welcomeLayout = new QVBoxLayout(m_welcomeWidget);
    welcomeLayout->setAlignment(Qt::AlignCenter);
    QLabel* welcomeLabel = new QLabel(
        "Welcome to Placement Correction Trainer\n\n"
        "Creates a master vector from reference to target edges,\n"
        "then uses retrace to compute corrected position data.\n\n"
        "1. Create or load a Training Set\n"
        "2. Select target device (hex-left / hex-right)\n"
        "3. Select master position (e.g. PLACE) and target position\n"
        "4. Teach Reference Line (2 edge points)\n"
        "5. Teach Target Line (2 edge points)\n"
        "6. View results (master vector, no correction)\n\n"
        "Select a workflow step from the left panel.");
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet("color: #aaa; font-size: 14px;");
    welcomeLayout->addWidget(welcomeLabel);
    m_contentStack->addWidget(m_welcomeWidget);  // index 0

    // TeachPlacementLineWidget for Reference (index 1)
    m_teachRefLineWidget = new TeachPlacementLineWidget("Reference", m_context, this);
    m_contentStack->addWidget(m_teachRefLineWidget);

    // TeachPlacementLineWidget for Target (index 2)
    m_teachTgtLineWidget = new TeachPlacementLineWidget("Target", m_context, this);
    m_contentStack->addWidget(m_teachTgtLineWidget);

    // TeachPlacementLineWidget for Normal Placement (index 3) — v3 feature
    m_teachNrmLineWidget = new TeachPlacementLineWidget("Normal", m_context, this);
    m_contentStack->addWidget(m_teachNrmLineWidget);

    // PlacementResultWidget (index 4)
    m_resultWidget = new PlacementResultWidget(m_context, this);
    m_contentStack->addWidget(m_resultWidget);

    // ExposureAdjustWidget (index 5)
    m_exposureAdjustWidget = new ExposureAdjustWidget(m_context, this);
    m_contentStack->addWidget(m_exposureAdjustWidget);

    // PlacementRetraceWidget (index 6)
    m_retraceWidget = new PlacementRetraceWidget(m_context, this);
    m_contentStack->addWidget(m_retraceWidget);

    // ImageGalleryWidget (index 7)
    m_imageGalleryWidget = new ImageGalleryWidget(m_context, this);
    m_contentStack->addWidget(m_imageGalleryWidget);

    // SummaryDiagramWidget (index 8)
    m_summaryDiagramWidget = new SummaryDiagramWidget(m_context, this);
    m_contentStack->addWidget(m_summaryDiagramWidget);

    // RetraceLogViewer (index 9)
    m_retraceLogViewer = new RetraceLogViewer(m_context, this);
    m_contentStack->addWidget(m_retraceLogViewer);

    // Wire signals
    connect(m_teachRefLineWidget, &TeachPlacementLineWidget::lineCaptured,
            this, [this](const PlacementLinePoints& lp) {
        m_currentSet.referenceLine = lp;
        m_currentSet.Recalculate();
        updateCurrentSet();
    });

    connect(m_teachTgtLineWidget, &TeachPlacementLineWidget::lineCaptured,
            this, [this](const PlacementLinePoints& lp) {
        m_currentSet.targetLine = lp;
        m_currentSet.Recalculate();
        updateCurrentSet();
    });

    connect(m_teachNrmLineWidget, &TeachPlacementLineWidget::lineCaptured,
            this, [this](const PlacementLinePoints& lp) {
        m_currentSet.normalPlacementLine = lp;
        m_currentSet.Recalculate();
        updateCurrentSet();
    });

    connect(m_exposureAdjustWidget, &ExposureAdjustWidget::adjustEnded,
            this, [this]() {
        m_navList->clearSelection();
        m_contentStack->setCurrentIndex(0);
    });

    connect(m_exposureAdjustWidget, &ExposureAdjustWidget::exposureSaved,
            this, [this](const PlacementCorrectionSet& updatedSet) {
        m_currentSet.referenceLine.p1Exposure = updatedSet.referenceLine.p1Exposure;
        m_currentSet.referenceLine.p2Exposure = updatedSet.referenceLine.p2Exposure;
        m_currentSet.targetLine.p1Exposure = updatedSet.targetLine.p1Exposure;
        m_currentSet.targetLine.p2Exposure = updatedSet.targetLine.p2Exposure;
        m_store.SaveSet(m_currentSet);
    });

    connect(m_retraceWidget, &PlacementRetraceWidget::retraceEnded,
            this, [this]() {
        m_navList->clearSelection();
        m_contentStack->setCurrentIndex(0);
    });

    connect(m_retraceWidget, &PlacementRetraceWidget::retraceSkipped,
            this, [this]() {
        m_navList->clearSelection();
        m_contentStack->setCurrentIndex(0);
    });

    connect(m_retraceWidget, &PlacementRetraceWidget::retraceCompleted,
            this, [this](const PlacementCorrectionSet& updatedSet) {
        m_currentSet.config.targetX = updatedSet.config.targetX;
        m_currentSet.config.targetY = updatedSet.config.targetY;
        m_currentSet.config.targetZ = updatedSet.config.targetZ;
        m_currentSet.config.targetU = updatedSet.config.targetU;
        m_currentSet.config.targetV = updatedSet.config.targetV;
        m_currentSet.config.targetW = updatedSet.config.targetW;
        updateTargetFromPosition();
        updateCurrentSet();
        m_store.SaveSet(m_currentSet);
    });

    // Manual tune offsets edited — persist immediately. m_currentSet already
    // reflects the new values (the retrace widget holds a pointer to it).
    connect(m_retraceWidget, &PlacementRetraceWidget::offsetsPersistRequested,
            this, [this]() {
        if (m_hasLoadedSet) {
            m_store.SaveSet(m_currentSet);
        }
    });

    connect(m_resultWidget, &PlacementResultWidget::recomputeRequested,
            this, [this]() {
        m_currentSet.config.targetDevice = m_targetDeviceCombo->currentText().toStdString();
        m_currentSet.config.masterPositionName = m_masterPositionCombo->currentText().toStdString();
        m_currentSet.config.targetPositionName = m_targetPositionCombo->currentText().toStdString();
        updateMasterFromPosition();
        updateTargetFromPosition();
        m_currentSet.Recalculate();
        m_resultWidget->SetData(m_currentSet);
        updateCurrentSet();
    });

    // Manual offset tuner on the View Results page re-saved the target
    // position — mirror the new target XYZUVW into m_currentSet, refresh
    // the sidebar readout, and persist the training set JSON.
    connect(m_resultWidget, &PlacementResultWidget::targetResaved,
            this, [this](const PlacementCorrectionSet& updatedSet) {
        m_currentSet.config.targetX = updatedSet.config.targetX;
        m_currentSet.config.targetY = updatedSet.config.targetY;
        m_currentSet.config.targetZ = updatedSet.config.targetZ;
        m_currentSet.config.targetU = updatedSet.config.targetU;
        m_currentSet.config.targetV = updatedSet.config.targetV;
        m_currentSet.config.targetW = updatedSet.config.targetW;
        updateTargetFromPosition();
        updateCurrentSet();
        if (m_hasLoadedSet) {
            m_store.SaveSet(m_currentSet);
        }
    });

    connect(m_summaryDiagramWidget, &SummaryDiagramWidget::thumbnailOffsetChanged,
            this, [this](int index, double dx, double dy) {
        if (index >= 0 && index < 4) {
            m_currentSet.summaryThumbOffsets[index].dx = dx;
            m_currentSet.summaryThumbOffsets[index].dy = dy;
            m_store.SaveSet(m_currentSet);
        }
    });

    // Add panels to splitter
    splitter->addWidget(leftPanel);
    splitter->addWidget(m_contentStack);
    splitter->setStretchFactor(0, 3);   // 30%
    splitter->setStretchFactor(1, 7);   // 70%
    splitter->setHandleWidth(3);

    mainLayout->addWidget(splitter, 1);
}

void PlacementCorrectionPage::onNavItemChanged(int currentRow)
{
    if (!m_hasLoadedSet && currentRow >= 0) {
        m_statusLabel->setText("Please create or load a training set first.");
        m_contentStack->setCurrentIndex(0);
        return;
    }

    switch (currentRow) {
    case 0:  // Teach Reference Line
        m_currentSet.config.cameraId = m_cameraCombo->currentText().toStdString();
        m_teachRefLineWidget->SetDeviceName(m_currentSet.measureDevice);
        m_teachRefLineWidget->SetCameraId(m_currentSet.config.cameraId);
        m_teachRefLineWidget->SetLine(m_currentSet.referenceLine);
        m_contentStack->setCurrentIndex(1);
        break;
    case 1:  // Teach Target Line
        m_currentSet.config.cameraId = m_cameraCombo->currentText().toStdString();
        m_teachTgtLineWidget->SetDeviceName(m_currentSet.measureDevice);
        m_teachTgtLineWidget->SetCameraId(m_currentSet.config.cameraId);
        m_teachTgtLineWidget->SetLine(m_currentSet.targetLine);
        m_contentStack->setCurrentIndex(2);
        break;
    case 2:  // Teach Normal Placement Line
        m_currentSet.config.cameraId = m_cameraCombo->currentText().toStdString();
        m_teachNrmLineWidget->SetDeviceName(m_currentSet.measureDevice);
        m_teachNrmLineWidget->SetCameraId(m_currentSet.config.cameraId);
        m_teachNrmLineWidget->SetLine(m_currentSet.normalPlacementLine);
        m_contentStack->setCurrentIndex(3);
        break;
    case 3:  // View Results
        m_currentSet.Recalculate();
        m_resultWidget->SetData(m_currentSet);
        m_contentStack->setCurrentIndex(4);
        break;
    case 4:  // Adjust Exposure
        m_currentSet.config.cameraId = m_cameraCombo->currentText().toStdString();
        m_exposureAdjustWidget->SetData(&m_currentSet);
        m_contentStack->setCurrentIndex(5);
        break;
    case 5:  // Retrace & Review
        m_currentSet.config.targetDevice = m_targetDeviceCombo->currentText().toStdString();
        m_currentSet.config.masterPositionName = m_masterPositionCombo->currentText().toStdString();
        m_currentSet.config.targetPositionName = m_targetPositionCombo->currentText().toStdString();
        updateMasterFromPosition();
        updateTargetFromPosition();
        m_currentSet.Recalculate();
        m_retraceWidget->SetData(&m_currentSet);
        m_contentStack->setCurrentIndex(6);
        break;
    case 6:  // Reference Images
        m_imageGalleryWidget->SetData(m_currentSet);
        m_contentStack->setCurrentIndex(7);
        break;
    case 7:  // Summary Diagram
        m_currentSet.Recalculate();
        m_summaryDiagramWidget->SetData(m_currentSet);
        m_contentStack->setCurrentIndex(8);
        break;
    case 8:  // View Retrace Log
        if (m_retraceLogViewer) m_retraceLogViewer->SetData(m_currentSet);
        m_contentStack->setCurrentIndex(9);
        break;
    default:
        m_contentStack->setCurrentIndex(0);
        break;
    }
}

void PlacementCorrectionPage::onNewSet()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Placement Correction Set",
        "Enter a name for the set:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    m_currentSet = PlacementCorrectionSet();
    m_currentSet.name = name.trimmed().toStdString();
    m_currentSet.measureDevice = "gantry-main";
    m_currentSet.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    m_currentSet.referenceLine.name = "Reference";
    m_currentSet.targetLine.name = "Target";

    // Set device and positions from current combo selections
    if (m_targetDeviceCombo->count() > 0) {
        m_currentSet.config.targetDevice = m_targetDeviceCombo->currentText().toStdString();
    }
    if (m_masterPositionCombo->count() > 0) {
        m_currentSet.config.masterPositionName = m_masterPositionCombo->currentText().toStdString();
    }
    if (m_targetPositionCombo->count() > 0) {
        m_currentSet.config.targetPositionName = m_targetPositionCombo->currentText().toStdString();
    }
    updateMasterFromPosition();
    updateTargetFromPosition();

    setSetLoaded(true);

    m_store.SaveSet(m_currentSet);
    m_setLabel->setText(name.trimmed());

    m_statusLabel->setText(QString("Created: %1\nRef: --- | Tgt: ---")
        .arg(name.trimmed()));

    std::cout << "[PlacementCorrectionPage] Created new set: " << m_currentSet.name << std::endl;
}

void PlacementCorrectionPage::onLoadSet()
{
    QString baseDir = QString::fromStdString(
        std::filesystem::absolute(m_store.GetBasePath()).string());

    QString filePath = FileSelectorDialog::getOpenFileName(
        this, baseDir, "*.json",
        "Load Placement Correction Set",
        "Training Set Files");

    if (filePath.isEmpty()) {
        return;
    }

    std::string stem = std::filesystem::path(filePath.toStdString()).stem().string();

    auto loaded = m_store.LoadSet(stem);
    if (!loaded.has_value()) {
        QMessageBox::warning(this, "Load Failed",
            "Could not load set: " + QString::fromStdString(stem));
        return;
    }

    m_currentSet = loaded.value();
    setSetLoaded(true);

    // Restore combos
    if (!m_currentSet.config.targetDevice.empty()) {
        int idx = m_targetDeviceCombo->findText(
            QString::fromStdString(m_currentSet.config.targetDevice));
        if (idx >= 0) m_targetDeviceCombo->setCurrentIndex(idx);
    }
    if (!m_currentSet.config.masterPositionName.empty()) {
        int idx = m_masterPositionCombo->findText(
            QString::fromStdString(m_currentSet.config.masterPositionName));
        if (idx >= 0) m_masterPositionCombo->setCurrentIndex(idx);
    }
    if (!m_currentSet.config.targetPositionName.empty()) {
        int idx = m_targetPositionCombo->findText(
            QString::fromStdString(m_currentSet.config.targetPositionName));
        if (idx >= 0) m_targetPositionCombo->setCurrentIndex(idx);
    }

    if (!m_currentSet.config.cameraId.empty()) {
        int idx = m_cameraCombo->findText(
            QString::fromStdString(m_currentSet.config.cameraId));
        if (idx >= 0) m_cameraCombo->setCurrentIndex(idx);
    }
    m_setLabel->setText(QString::fromStdString(stem));
    updateCurrentSet();

    std::cout << "[PlacementCorrectionPage] Loaded set: " << m_currentSet.name
              << " from: " << stem << ".json" << std::endl;
}

void PlacementCorrectionPage::onDeleteSet()
{
    auto sets = m_store.ListSets();
    if (sets.empty()) {
        QMessageBox::information(this, "Delete", "No training sets found.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Delete Training Set");
    dlg.setMinimumSize(400, 350);
    dlg.setStyleSheet(
        "QDialog { background-color: #1e2233; }"
        "QLabel { color: #ccc; }"
        "QListWidget { background-color: #1a1f2e; color: white; border: 1px solid #3d5a8c; "
        "font-size: 13px; }"
        "QListWidget::item { padding: 6px; }"
        "QListWidget::item:selected { background-color: #3d5a8c; }");

    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QLabel* label = new QLabel("Select a training set to delete:");
    label->setStyleSheet("font-size: 13px; color: #ccc; padding: 5px;");
    layout->addWidget(label);

    QListWidget* list = new QListWidget();
    for (const auto& s : sets) {
        list->addItem(QString::fromStdString(s));
    }
    layout->addWidget(list);

    QDialogButtonBox* buttons = new QDialogButtonBox();
    QPushButton* deleteBtn = buttons->addButton("Delete", QDialogButtonBox::DestructiveRole);
    deleteBtn->setStyleSheet(
        "QPushButton { background-color: #c0392b; color: white; font-weight: bold; "
        "padding: 6px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #e74c3c; }");
    deleteBtn->setEnabled(false);
    QPushButton* cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);
    cancelBtn->setStyleSheet(
        "QPushButton { background-color: #2d3e5f; color: white; padding: 6px 16px; "
        "border-radius: 4px; }"
        "QPushButton:hover { background-color: #3d5a8c; }");
    layout->addWidget(buttons);

    connect(list, &QListWidget::currentRowChanged, &dlg, [deleteBtn](int row) {
        deleteBtn->setEnabled(row >= 0);
    });
    connect(deleteBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted || !list->currentItem()) return;

    QString selected = list->currentItem()->text();
    auto confirm = QMessageBox::question(this, "Confirm Delete",
        "Delete '" + selected + "'?\nThis cannot be undone.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (confirm != QMessageBox::Yes) return;

    m_store.DeleteSet(selected.toStdString());

    // If we deleted the currently loaded set, clear it
    if (m_hasLoadedSet && (selected.toStdString() == m_currentSet.name ||
        selected.toStdString() == m_currentSet.sourceFilename)) {
        m_currentSet = PlacementCorrectionSet();
        setSetLoaded(false);
        m_setLabel->setText("(none loaded)");
        m_contentStack->setCurrentIndex(0);
    }

    std::cout << "[PlacementCorrectionPage] Deleted set: " << selected.toStdString() << std::endl;
}

void PlacementCorrectionPage::onSaveSet()
{
    if (!m_hasLoadedSet) {
        QMessageBox::information(this, "Save", "No set to save.");
        return;
    }

    // If the set already has a source filename (loaded from disk or previously
    // saved), overwrite directly without prompting. Otherwise fall back to Save As.
    if (m_currentSet.sourceFilename.empty()) {
        onSaveAsSet();
        return;
    }

    if (m_store.SaveSet(m_currentSet)) {
        QString name = QString::fromStdString(m_currentSet.name);
        m_setLabel->setText(name);
        m_statusLabel->setText(QString("Saved: %1").arg(name));
    } else {
        QMessageBox::warning(this, "Save Failed", "Could not save set.");
    }
}

void PlacementCorrectionPage::onSaveAsSet()
{
    if (!m_hasLoadedSet) {
        QMessageBox::information(this, "Save As", "No set to save.");
        return;
    }

    // Show save dialog with current name pre-filled and option to rename
    QDialog dlg(this);
    dlg.setWindowTitle("Save Training Set As");
    dlg.setMinimumSize(400, 200);
    dlg.setStyleSheet(
        "QDialog { background-color: #1e2233; }"
        "QLabel { color: #ccc; }"
        "QLineEdit { background-color: #1a1f2e; color: white; border: 1px solid #3d5a8c; "
        "border-radius: 3px; padding: 6px; font-size: 13px; }");

    QVBoxLayout* layout = new QVBoxLayout(&dlg);

    QLabel* label = new QLabel("Save training set as:");
    label->setStyleSheet("font-size: 13px; color: #ccc; padding: 5px;");
    layout->addWidget(label);

    QLineEdit* nameEdit = new QLineEdit(QString::fromStdString(m_currentSet.name));
    layout->addWidget(nameEdit);

    // Show existing sets for reference
    auto sets = m_store.ListSets();
    if (!sets.empty()) {
        QLabel* existingLabel = new QLabel("Existing sets:");
        existingLabel->setStyleSheet("font-size: 11px; color: #888; padding: 5px 5px 0 5px;");
        layout->addWidget(existingLabel);

        QListWidget* existingList = new QListWidget();
        existingList->setMaximumHeight(120);
        existingList->setStyleSheet(
            "QListWidget { background-color: #1a1f2e; color: #aaa; border: 1px solid #2d3e5f; "
            "font-size: 11px; }"
            "QListWidget::item { padding: 3px; }"
            "QListWidget::item:selected { background-color: #2d3e5f; }");
        for (const auto& s : sets) {
            existingList->addItem(QString::fromStdString(s));
        }
        layout->addWidget(existingList);

        // Click existing to fill name
        connect(existingList, &QListWidget::itemClicked, &dlg, [nameEdit](QListWidgetItem* item) {
            nameEdit->setText(item->text());
        });
    }

    QDialogButtonBox* buttons = new QDialogButtonBox();
    QPushButton* saveBtn = buttons->addButton("Save", QDialogButtonBox::AcceptRole);
    saveBtn->setStyleSheet(
        "QPushButton { background-color: #27ae60; color: white; font-weight: bold; "
        "padding: 6px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #2ecc71; }");
    QPushButton* cancelBtn = buttons->addButton(QDialogButtonBox::Cancel);
    cancelBtn->setStyleSheet(
        "QPushButton { background-color: #2d3e5f; color: white; padding: 6px 16px; "
        "border-radius: 4px; }"
        "QPushButton:hover { background-color: #3d5a8c; }");
    layout->addWidget(buttons);

    connect(saveBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    QString saveName = nameEdit->text().trimmed();
    if (saveName.isEmpty()) return;

    m_currentSet.name = saveName.toStdString();
    m_currentSet.sourceFilename = saveName.toStdString();

    if (m_store.SaveSet(m_currentSet)) {
        m_setLabel->setText(saveName);
        m_statusLabel->setText(QString("Saved: %1").arg(saveName));
    } else {
        QMessageBox::warning(this, "Save Failed", "Could not save set.");
    }
}

void PlacementCorrectionPage::onTargetDeviceChanged(int /*index*/)
{
    populateNamedPositions();

    if (m_hasLoadedSet) {
        m_currentSet.config.targetDevice = m_targetDeviceCombo->currentText().toStdString();
        updateMasterFromPosition();
        updateTargetFromPosition();
        updateCurrentSet();
    }
}

void PlacementCorrectionPage::onMasterPositionChanged(int /*index*/)
{
    if (m_hasLoadedSet && m_masterPositionCombo->count() > 0) {
        m_currentSet.config.masterPositionName = m_masterPositionCombo->currentText().toStdString();
        updateMasterFromPosition();
        updateCurrentSet();
    }
}

void PlacementCorrectionPage::onTargetPositionChanged(int /*index*/)
{
    if (m_hasLoadedSet && m_targetPositionCombo->count() > 0) {
        m_currentSet.config.targetPositionName = m_targetPositionCombo->currentText().toStdString();
        updateTargetFromPosition();
        updateCurrentSet();
    }
}

void PlacementCorrectionPage::populateHexDevices()
{
    m_targetDeviceCombo->clear();

    if (m_context && m_context->motionManager) {
        auto devices = m_context->motionManager->GetAllDeviceNames();
        for (const auto& dev : devices) {
            if (dev.find("hex") != std::string::npos) {
                m_targetDeviceCombo->addItem(QString::fromStdString(dev));
            }
        }
    }

    if (m_targetDeviceCombo->count() == 0) {
        m_targetDeviceCombo->addItem("hex-left");
        m_targetDeviceCombo->addItem("hex-right");
    }
}

void PlacementCorrectionPage::onNormalPositionChanged(int /*index*/)
{
    if (!m_normalPositionCombo) return;
    m_currentSet.config.normalPositionName =
        m_normalPositionCombo->currentText().toStdString();
}

// Background safe-move worker: lower velocity → MoveToAbs (using named-position
// coords) → restore velocity. Runs entirely off the UI thread; status updates
// are marshalled back via QMetaObject::invokeMethod.
void PlacementCorrectionPage::startSafeMoveAsync(const std::string& device,
                                                  const std::string& posName,
                                                  QPushButton* btn)
{
    if (!m_context || !m_context->motionManager) {
        QMessageBox::warning(this, "Move Failed", "Motion manager not available.");
        return;
    }
    if (device.empty()) {
        QMessageBox::warning(this, "Move Failed", "No target device selected.");
        return;
    }
    if (posName.empty()) {
        QMessageBox::warning(this, "Move Failed", "No position selected.");
        return;
    }

    // Interlock: refuse to start if a recipe is currently executing on the
    // SequenceEngine, or if a previous trainer move is still in flight.
    if (m_context->sequenceEngine && m_context->sequenceEngine->IsExecuting()) {
        QMessageBox::warning(this, "Move Blocked",
            "A recipe is currently running. Stop the running program before "
            "moving from the trainer.");
        return;
    }
    if (!m_moveActiveDevice.empty()) {
        QMessageBox::information(this, "Move Busy",
            QString("A move on \"%1\" is already in progress. Wait or click Stop.")
                .arg(QString::fromStdString(m_moveActiveDevice)));
        return;
    }

    // Resolve named position on the UI thread (motionManager calls are usually
    // safe but reading combo widgets isn't; we have the strings already).
    PositionStruct target;
    if (!m_context->motionManager->GetNamedPosition(device, posName, target)) {
        QMessageBox::warning(this, "Move Failed",
            QString("Position \"%1\" not found on %2.")
                .arg(QString::fromStdString(posName))
                .arg(QString::fromStdString(device)));
        return;
    }

    constexpr double kSafeVel    = 0.1;  // mm/s during the move
    constexpr double kDefaultVel = 2.0;  // mm/s restored after

    // Disable all three Move buttons while one is running
    auto setButtonsEnabled = [this](bool en) {
        if (m_masterMoveBtn) m_masterMoveBtn->setEnabled(en);
        if (m_targetMoveBtn) m_targetMoveBtn->setEnabled(en);
        if (m_normalMoveBtn) m_normalMoveBtn->setEnabled(en);
    };
    setButtonsEnabled(false);
    if (btn) btn->setText("Moving...");
    m_statusLabel->setText(QString("Moving %1 to \"%2\" @ 0.1 mm/s ...")
        .arg(QString::fromStdString(device))
        .arg(QString::fromStdString(posName)));

    m_moveActiveDevice = device;
    if (m_moveStatusWidget) m_moveStatusWidget->setVisible(true);
    if (m_moveStopBtn)      m_moveStopBtn->setEnabled(true);

    auto* mm = m_context->motionManager;

    m_moveFuture = std::async(std::launch::async,
        [this, mm, device, posName, target, btn, setButtonsEnabled]() {
        bool velSlowOk = mm->SetSystemVelocity(device, kSafeVel);
        // Coordinated 6-axis absolute move (XYZ + UVW). MoveToAbs is XYZ-only
        // and leaves UVW at the current value, which on a hex device causes
        // axis-by-axis sequencing for the rotation deltas.
        bool moveOk    = mm->MoveToAbs6(device, target, /*blocking*/ true);
        bool velRestoreOk = mm->SetSystemVelocity(device, kDefaultVel);

        std::cout << "[PlacementPage] Safe-move " << device << " -> " << posName
                  << "  velSlow=" << (velSlowOk ? "OK" : "FAIL")
                  << "  move="    << (moveOk ? "OK" : "FAIL")
                  << "  velRestore=" << (velRestoreOk ? "OK" : "FAIL") << std::endl;

        QMetaObject::invokeMethod(this,
            [this, device, posName, btn, moveOk, setButtonsEnabled]() {
            setButtonsEnabled(true);
            if (btn) btn->setText("Move");
            if (m_moveStatusWidget) m_moveStatusWidget->setVisible(false);
            m_moveActiveDevice.clear();
            if (moveOk) {
                m_statusLabel->setText(QString("Moved %1 to \"%2\"")
                    .arg(QString::fromStdString(device))
                    .arg(QString::fromStdString(posName)));
            } else {
                m_statusLabel->setText(QString("Move FAILED: %1 -> %2")
                    .arg(QString::fromStdString(device))
                    .arg(QString::fromStdString(posName)));
                QMessageBox::warning(this, "Move Failed",
                    QString("Failed to move %1 to \"%2\".")
                        .arg(QString::fromStdString(device))
                        .arg(QString::fromStdString(posName)));
            }
        }, Qt::QueuedConnection);
    });
}

void PlacementCorrectionPage::onMoveMaster()
{
    startSafeMoveAsync(m_targetDeviceCombo->currentText().toStdString(),
                       m_masterPositionCombo->currentText().toStdString(),
                       m_masterMoveBtn);
}

void PlacementCorrectionPage::onMoveTarget()
{
    startSafeMoveAsync(m_targetDeviceCombo->currentText().toStdString(),
                       m_targetPositionCombo->currentText().toStdString(),
                       m_targetMoveBtn);
}

void PlacementCorrectionPage::onMoveNormal()
{
    startSafeMoveAsync(m_targetDeviceCombo->currentText().toStdString(),
                       m_normalPositionCombo
                           ? m_normalPositionCombo->currentText().toStdString()
                           : std::string(),
                       m_normalMoveBtn);
}

void PlacementCorrectionPage::onStopMove()
{
    if (m_moveActiveDevice.empty() || !m_context || !m_context->motionManager) return;
    bool ok = m_context->motionManager->EmergencyStop(m_moveActiveDevice);
    std::cout << "[PlacementPage] EmergencyStop " << m_moveActiveDevice
              << " " << (ok ? "OK" : "FAILED") << std::endl;
    if (m_moveStopBtn) m_moveStopBtn->setEnabled(false);
    m_statusLabel->setText(QString("Stopping %1 ...")
        .arg(QString::fromStdString(m_moveActiveDevice)));
    // The blocking MoveToAbs in the worker thread will return (failed) on stop;
    // worker callback will hide the progress widget and re-enable buttons.
}

void PlacementCorrectionPage::populateNamedPositions()
{
    // Block signals to avoid cascading updates during population
    m_masterPositionCombo->blockSignals(true);
    m_targetPositionCombo->blockSignals(true);
    if (m_normalPositionCombo) m_normalPositionCombo->blockSignals(true);

    m_masterPositionCombo->clear();
    m_targetPositionCombo->clear();
    if (m_normalPositionCombo) m_normalPositionCombo->clear();

    std::string deviceName = m_targetDeviceCombo->currentText().toStdString();
    if (!deviceName.empty() && m_context && m_context->motionManager) {
        auto positions = m_context->motionManager->GetNamedPositionList(deviceName);
        for (const auto& pos : positions) {
            m_masterPositionCombo->addItem(QString::fromStdString(pos));
            m_targetPositionCombo->addItem(QString::fromStdString(pos));
            if (m_normalPositionCombo) m_normalPositionCombo->addItem(QString::fromStdString(pos));
        }
    }

    // Default master to "PLACE" if exists
    int placeIdx = m_masterPositionCombo->findText("PLACE", Qt::MatchFixedString);
    if (placeIdx >= 0) {
        m_masterPositionCombo->setCurrentIndex(placeIdx);
    }

    // Default target to "PLACE_ADJ" if exists, else same as master
    int adjIdx = m_targetPositionCombo->findText("PLACE_ADJ", Qt::MatchFixedString);
    if (adjIdx >= 0) {
        m_targetPositionCombo->setCurrentIndex(adjIdx);
    } else {
        int tgtPlaceIdx = m_targetPositionCombo->findText("PLACE", Qt::MatchFixedString);
        if (tgtPlaceIdx >= 0) {
            m_targetPositionCombo->setCurrentIndex(tgtPlaceIdx);
        }
    }

    // Restore normal selection from current set if present
    if (m_normalPositionCombo && !m_currentSet.config.normalPositionName.empty()) {
        int idx = m_normalPositionCombo->findText(
            QString::fromStdString(m_currentSet.config.normalPositionName),
            Qt::MatchFixedString);
        if (idx >= 0) m_normalPositionCombo->setCurrentIndex(idx);
    }

    m_masterPositionCombo->blockSignals(false);
    m_targetPositionCombo->blockSignals(false);
    if (m_normalPositionCombo) m_normalPositionCombo->blockSignals(false);

    updateMasterFromPosition();
    updateTargetFromPosition();
}

void PlacementCorrectionPage::updateMasterFromPosition()
{
    if (!m_masterUVWLabel) return;

    std::string deviceName = m_targetDeviceCombo->currentText().toStdString();
    std::string posName = m_masterPositionCombo->currentText().toStdString();

    if (deviceName.empty() || posName.empty()) {
        m_masterUVWLabel->setText("UVW: ---");
        return;
    }

    if (m_context && m_context->motionManager) {
        PositionStruct pos;
        bool found = m_context->motionManager->GetNamedPosition(deviceName, posName, pos);
        if (found) {
            m_currentSet.config.masterX = pos.x;
            m_currentSet.config.masterY = pos.y;
            m_currentSet.config.masterZ = pos.z;
            m_currentSet.config.masterU = pos.u;
            m_currentSet.config.masterV = pos.v;
            m_currentSet.config.masterW = pos.w;

            m_masterUVWLabel->setText(QString(
                "U: %1%4  V: %2%4  W: %3%4")
                .arg(pos.u, 0, 'f', 4)
                .arg(pos.v, 0, 'f', 4)
                .arg(pos.w, 0, 'f', 4)
                .arg(QChar(0x00B0)));

            m_currentSet.Recalculate();
            return;
        }
    }

    m_masterUVWLabel->setText(QString("Position \"%1\" not found")
        .arg(QString::fromStdString(posName)));
}

void PlacementCorrectionPage::updateTargetFromPosition()
{
    if (!m_targetUVWLabel) return;

    std::string deviceName = m_targetDeviceCombo->currentText().toStdString();
    std::string posName = m_targetPositionCombo->currentText().toStdString();

    if (deviceName.empty() || posName.empty()) {
        m_targetUVWLabel->setText("UVW: ---");
        return;
    }

    if (m_context && m_context->motionManager) {
        PositionStruct pos;
        bool found = m_context->motionManager->GetNamedPosition(deviceName, posName, pos);
        if (found) {
            m_currentSet.config.targetX = pos.x;
            m_currentSet.config.targetY = pos.y;
            m_currentSet.config.targetZ = pos.z;
            m_currentSet.config.targetU = pos.u;
            m_currentSet.config.targetV = pos.v;
            m_currentSet.config.targetW = pos.w;

            m_targetUVWLabel->setText(QString(
                "U: %1%4  V: %2%4  W: %3%4")
                .arg(pos.u, 0, 'f', 4)
                .arg(pos.v, 0, 'f', 4)
                .arg(pos.w, 0, 'f', 4)
                .arg(QChar(0x00B0)));
            return;
        }
    }

    m_targetUVWLabel->setText(QString("Position \"%1\" not found")
        .arg(QString::fromStdString(posName)));
}

void PlacementCorrectionPage::updateStoreBasePath()
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(
            m_context->productPositionManager->GetCurrentProductPath());
        QString productDir = QFileInfo(productFilePath).path();
        std::string path = (productDir + "/placement_correction").toStdString();
        m_store.SetBasePath(path);
        std::cout << "[PlacementCorrectionPage] Store base path set to product: " << path << std::endl;
    } else {
        m_store.SetBasePath("config/placement_correction");
        std::cout << "[PlacementCorrectionPage] No product loaded, using default: config/placement_correction" << std::endl;
    }
}

void PlacementCorrectionPage::updateCurrentSet()
{
    if (!m_hasLoadedSet) return;

    bool ref = m_currentSet.referenceLine.valid;
    bool tgt = m_currentSet.targetLine.valid;
    bool delta = m_currentSet.deltaValid;

    m_statusLabel->setText(QString("Set: %1\nDevice: %2\nMaster: %3 | Target: %4\nRef: %5 | Tgt: %6\nDelta: %7")
        .arg(QString::fromStdString(m_currentSet.name))
        .arg(QString::fromStdString(m_currentSet.config.targetDevice))
        .arg(QString::fromStdString(m_currentSet.config.masterPositionName))
        .arg(QString::fromStdString(m_currentSet.config.targetPositionName))
        .arg(ref ? "captured" : "---")
        .arg(tgt ? "captured" : "---")
        .arg(delta ? QString("%1%2").arg(m_currentSet.deltaAngleDeg, 0, 'f', 3).arg(QChar(0x00B0)) : "N/A"));
}

void PlacementCorrectionPage::setSetLoaded(bool loaded)
{
    m_hasLoadedSet = loaded;
    m_saveBtn->setEnabled(loaded);
    if (m_saveAsBtn) m_saveAsBtn->setEnabled(loaded);
    m_deleteBtn->setEnabled(loaded);

    if (!loaded) {
        m_statusLabel->setText("No training set loaded");
        m_navList->clearSelection();
    }
}

void PlacementCorrectionPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Save current selections
    QString masterSel = m_masterPositionCombo->currentText();
    QString targetSel = m_targetPositionCombo->currentText();

    // Refresh position lists from motion manager
    populateNamedPositions();

    // Restore selections if they still exist
    if (!masterSel.isEmpty()) {
        int idx = m_masterPositionCombo->findText(masterSel);
        if (idx >= 0) m_masterPositionCombo->setCurrentIndex(idx);
    }
    if (!targetSel.isEmpty()) {
        int idx = m_targetPositionCombo->findText(targetSel);
        if (idx >= 0) m_targetPositionCombo->setCurrentIndex(idx);
    }

    updateMasterFromPosition();
    updateTargetFromPosition();
}

void PlacementCorrectionPage::setTopBarVisible(bool visible)
{
    if (m_topBarWidget) {
        m_topBarWidget->setVisible(visible);
    }
}

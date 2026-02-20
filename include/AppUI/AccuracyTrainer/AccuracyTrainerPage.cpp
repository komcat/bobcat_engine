// AccuracyTrainerPage.cpp
#include "AccuracyTrainerPage.h"
#include "TeachBondingWidget.h"
#include "TeachReferenceWidget.h"
#include "RunAlignmentWidget.h"
#include "MultiDeviceRunAlignmentWidget.h"
#include "TeachMultiDeviceWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>
#include <iostream>

AccuracyTrainerPage::AccuracyTrainerPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_navList(nullptr)
    , m_trainingSetCombo(nullptr)
    , m_newBtn(nullptr)
    , m_loadBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_statusLabel(nullptr)
    , m_contentStack(nullptr)
    , m_teachBondingWidget(nullptr)
    , m_teachReferenceWidget(nullptr)
    , m_runAlignmentWidget(nullptr)
    , m_multiDeviceWidget(nullptr)
    , m_teachMultiDeviceWidget(nullptr)
    , m_welcomeWidget(nullptr)
    , m_deviceCombo(nullptr)
{
    setupUI();
    refreshTrainingSetList();
    setTrainingSetLoaded(false);
}

void AccuracyTrainerPage::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top bar with back button
    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setContentsMargins(10, 5, 10, 5);

    QPushButton* backBtn = new QPushButton("Back to Main Menu");
    backBtn->setFixedHeight(32);
    backBtn->setStyleSheet(
        "QPushButton { background-color: #3d5a8c; color: white; font-weight: bold; "
        "padding: 5px 15px; border: 1px solid #4d6e9f; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4d6e9f; }"
    );
    connect(backBtn, &QPushButton::clicked, this, &AccuracyTrainerPage::backToMainMenu);
    topBar->addWidget(backBtn);

    QLabel* titleLabel = new QLabel("Accuracy Trainer");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff; padding: 0 15px;");
    topBar->addWidget(titleLabel);
    topBar->addStretch();

    mainLayout->addLayout(topBar);

    // Separator
    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background-color: #3d5a8c;");
    sep->setFixedHeight(2);
    mainLayout->addWidget(sep);

    // Splitter: left 30% | right 70%
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    // ========================================
    // LEFT PANEL
    // ========================================
    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(10, 10, 5, 10);
    leftLayout->setSpacing(8);

    // Training Set management group
    QGroupBox* tsGroup = new QGroupBox("Training Set");
    tsGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
    QVBoxLayout* tsLayout = new QVBoxLayout(tsGroup);

    m_trainingSetCombo = new QComboBox();
    m_trainingSetCombo->setPlaceholderText("(none loaded)");
    tsLayout->addWidget(m_trainingSetCombo);

    QHBoxLayout* tsButtonRow = new QHBoxLayout();
    m_newBtn = new QPushButton("New");
    m_loadBtn = new QPushButton("Load");
    m_deleteBtn = new QPushButton("Delete");
    m_saveBtn = new QPushButton("Save");

    for (auto* btn : {m_newBtn, m_loadBtn, m_deleteBtn, m_saveBtn}) {
        btn->setFixedHeight(28);
        btn->setStyleSheet(
            "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
            "border-radius: 3px; padding: 3px 8px; font-size: 11px; }"
            "QPushButton:hover { background-color: #3d5a8c; }"
            "QPushButton:disabled { background-color: #1a1f2e; color: #555; }"
        );
    }

    tsButtonRow->addWidget(m_newBtn);
    tsButtonRow->addWidget(m_loadBtn);
    tsButtonRow->addWidget(m_deleteBtn);
    tsButtonRow->addWidget(m_saveBtn);
    tsLayout->addLayout(tsButtonRow);

    connect(m_newBtn, &QPushButton::clicked, this, &AccuracyTrainerPage::onNewTrainingSet);
    connect(m_loadBtn, &QPushButton::clicked, this, &AccuracyTrainerPage::onLoadTrainingSet);
    connect(m_deleteBtn, &QPushButton::clicked, this, &AccuracyTrainerPage::onDeleteTrainingSet);
    connect(m_saveBtn, &QPushButton::clicked, this, &AccuracyTrainerPage::onSaveTrainingSet);

    leftLayout->addWidget(tsGroup);

    // Device selection
    QGroupBox* deviceGroup = new QGroupBox("Motion Device");
    deviceGroup->setStyleSheet(tsGroup->styleSheet());
    QVBoxLayout* deviceLayout = new QVBoxLayout(deviceGroup);
    m_deviceCombo = new QComboBox();

    // Populate device list from motion manager
    if (m_context && m_context->motionManager) {
        auto devices = m_context->motionManager->GetAllDeviceNames();
        for (const auto& dev : devices) {
            m_deviceCombo->addItem(QString::fromStdString(dev));
        }
    }
    if (m_deviceCombo->count() == 0) {
        m_deviceCombo->addItem("gantry-main");
    }
    connect(m_deviceCombo, &QComboBox::currentTextChanged,
            this, &AccuracyTrainerPage::onDeviceChanged);
    deviceLayout->addWidget(m_deviceCombo);
    leftLayout->addWidget(deviceGroup);

    // Navigation list
    QGroupBox* navGroup = new QGroupBox("Workflow");
    navGroup->setStyleSheet(tsGroup->styleSheet());
    QVBoxLayout* navLayout = new QVBoxLayout(navGroup);
    m_navList = new QListWidget();
    m_navList->setStyleSheet(
        "QListWidget { background-color: #1a1f2e; color: white; border: none; font-size: 13px; }"
        "QListWidget::item { padding: 10px 8px; border-bottom: 1px solid #2d3e5f; }"
        "QListWidget::item:selected { background-color: #3d5a8c; }"
        "QListWidget::item:hover { background-color: #2d3e5f; }"
    );
    m_navList->addItem("1. Teach Bonding Positions");
    m_navList->addItem("2. Teach Reference Points");
    m_navList->addItem("3. Teach Multi-Device Positions");
    m_navList->addItem("4. Run Alignment");
    m_navList->addItem("5. Run Multi-Device Alignment");
    connect(m_navList, &QListWidget::currentRowChanged,
            this, &AccuracyTrainerPage::onNavItemChanged);
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

    // Welcome/placeholder widget
    m_welcomeWidget = new QWidget();
    QVBoxLayout* welcomeLayout = new QVBoxLayout(m_welcomeWidget);
    welcomeLayout->setAlignment(Qt::AlignCenter);
    QLabel* welcomeLabel = new QLabel(
        "Welcome to Accuracy Trainer\n\n"
        "1. Create or load a Training Set\n"
        "2. Teach bonding positions (define corners)\n"
        "3. Teach reference features (2-3 landmarks)\n"
        "4. Teach multi-device positions (hex + gantry view)\n"
        "5. Run Alignment to correct positions\n"
        "6. Run Multi-Device Alignment (gantry + hex)\n\n"
        "Select a workflow step from the left panel."
    );
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet("color: #aaa; font-size: 14px;");
    welcomeLayout->addWidget(welcomeLabel);
    m_contentStack->addWidget(m_welcomeWidget);  // index 0

    // Create wizard widgets
    m_teachBondingWidget = new TeachBondingWidget(m_context, this);
    m_contentStack->addWidget(m_teachBondingWidget);  // index 1

    m_teachReferenceWidget = new TeachReferenceWidget(m_context, this);
    m_contentStack->addWidget(m_teachReferenceWidget);  // index 2

    m_runAlignmentWidget = new RunAlignmentWidget(m_context, this);
    m_contentStack->addWidget(m_runAlignmentWidget);  // index 3

    m_multiDeviceWidget = new MultiDeviceRunAlignmentWidget(m_context, this);
    m_contentStack->addWidget(m_multiDeviceWidget);  // index 4

    m_teachMultiDeviceWidget = new TeachMultiDeviceWidget(m_context, this);
    m_contentStack->addWidget(m_teachMultiDeviceWidget);  // index 5

    // Wire widget signals for data updates
    connect(m_teachBondingWidget, &TeachBondingWidget::bondingPointAdded,
            this, [this](const BondingPoint& bp) {
        m_currentSet.bondingPoints.push_back(bp);
        updateCurrentTrainingSet();
    });

    connect(m_teachBondingWidget, &TeachBondingWidget::bondingPointRemoved,
            this, [this](int index) {
        if (index >= 0 && index < static_cast<int>(m_currentSet.bondingPoints.size())) {
            m_currentSet.bondingPoints.erase(m_currentSet.bondingPoints.begin() + index);
            updateCurrentTrainingSet();
        }
    });

    connect(m_teachBondingWidget, &TeachBondingWidget::bondingPointModified,
            this, [this](int /*index*/) {
        // Bonding point was modified in-place via the pointer; just refresh UI
        updateCurrentTrainingSet();
    });

    connect(m_teachReferenceWidget, &TeachReferenceWidget::referenceFrameComputed,
            this, [this](const TrainingSet& updatedSet) {
        // Update reference data from the widget
        m_currentSet.referencePoints = updatedSet.referencePoints;
        m_currentSet.originX = updatedSet.originX;
        m_currentSet.originY = updatedSet.originY;
        m_currentSet.xAxisX = updatedSet.xAxisX;
        m_currentSet.xAxisY = updatedSet.xAxisY;
        m_currentSet.yAxisX = updatedSet.yAxisX;
        m_currentSet.yAxisY = updatedSet.yAxisY;
        m_currentSet.bondingPoints = updatedSet.bondingPoints;
        m_currentSet.valid = true;
        updateCurrentTrainingSet();
    });

    connect(m_teachMultiDeviceWidget, &TeachMultiDeviceWidget::hexPositionsUpdated,
            this, [this](const std::vector<HexDeviceTeachData>& positions) {
        m_currentSet.hexDevicePositions = positions;
        updateCurrentTrainingSet();
    });

    connect(m_teachMultiDeviceWidget, &TeachMultiDeviceWidget::deviceAssignmentsChanged,
            this, [this]() {
        updateCurrentTrainingSet();
    });

    // Add panels to splitter
    splitter->addWidget(leftPanel);
    splitter->addWidget(m_contentStack);
    splitter->setStretchFactor(0, 3);   // 30%
    splitter->setStretchFactor(1, 7);   // 70%
    splitter->setHandleWidth(3);

    mainLayout->addWidget(splitter, 1);
}

void AccuracyTrainerPage::onNavItemChanged(int currentRow)
{
    if (!m_hasLoadedSet && currentRow >= 0) {
        m_statusLabel->setText("Please create or load a training set first.");
        m_contentStack->setCurrentIndex(0);
        return;
    }

    switch (currentRow) {
    case 0:  // Teach Bonding
        m_teachBondingWidget->SetTrainingSet(&m_currentSet);
        m_contentStack->setCurrentIndex(1);
        break;
    case 1:  // Teach Reference
        m_teachReferenceWidget->SetTrainingSet(&m_currentSet);
        m_contentStack->setCurrentIndex(2);
        break;
    case 2:  // Teach Multi-Device Positions
        m_teachMultiDeviceWidget->SetTrainingSet(&m_currentSet);
        m_contentStack->setCurrentIndex(5);
        break;
    case 3:  // Run Alignment
        m_runAlignmentWidget->SetTrainingSet(&m_currentSet);
        m_contentStack->setCurrentIndex(3);
        break;
    case 4:  // Run Multi-Device Alignment
        m_multiDeviceWidget->SetTrainingSet(&m_currentSet);
        m_contentStack->setCurrentIndex(4);
        break;
    default:
        m_contentStack->setCurrentIndex(0);
        break;
    }
}

void AccuracyTrainerPage::onNewTrainingSet()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Training Set",
        "Enter a name for the training set:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    m_currentSet = TrainingSet();
    m_currentSet.name = name.trimmed().toStdString();
    m_currentSet.deviceName = m_deviceCombo->currentText().toStdString();
    m_currentSet.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();

    setTrainingSetLoaded(true);

    // Auto-save the empty set
    m_store.SaveTrainingSet(m_currentSet);
    refreshTrainingSetList();

    // Select it in combo
    int idx = m_trainingSetCombo->findText(name.trimmed());
    if (idx >= 0) {
        m_trainingSetCombo->setCurrentIndex(idx);
    }

    m_statusLabel->setText(QString("Created: %1\nBonding pts: 0 | Refs: 0")
        .arg(name.trimmed()));

    std::cout << "[AccuracyTrainerPage] Created new training set: " << m_currentSet.name << std::endl;
}

void AccuracyTrainerPage::onLoadTrainingSet()
{
    QString name = m_trainingSetCombo->currentText();
    if (name.isEmpty()) {
        QMessageBox::information(this, "Load", "No training set selected.");
        return;
    }

    auto loaded = m_store.LoadTrainingSet(name.toStdString());
    if (!loaded.has_value()) {
        QMessageBox::warning(this, "Load Failed",
            "Could not load training set: " + name);
        return;
    }

    m_currentSet = loaded.value();
    setTrainingSetLoaded(true);

    // Set device combo to match
    int devIdx = m_deviceCombo->findText(QString::fromStdString(m_currentSet.deviceName));
    if (devIdx >= 0) {
        m_deviceCombo->setCurrentIndex(devIdx);
    }

    updateCurrentTrainingSet();
    std::cout << "[AccuracyTrainerPage] Loaded training set: " << m_currentSet.name << std::endl;
}

void AccuracyTrainerPage::onDeleteTrainingSet()
{
    QString name = m_trainingSetCombo->currentText();
    if (name.isEmpty()) {
        return;
    }

    auto result = QMessageBox::question(this, "Delete Training Set",
        "Delete training set '" + name + "'?\nThis cannot be undone.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result != QMessageBox::Yes) {
        return;
    }

    m_store.DeleteTrainingSet(name.toStdString());

    if (m_hasLoadedSet && m_currentSet.name == name.toStdString()) {
        m_currentSet = TrainingSet();
        setTrainingSetLoaded(false);
        m_contentStack->setCurrentIndex(0);
    }

    refreshTrainingSetList();
    std::cout << "[AccuracyTrainerPage] Deleted training set: " << name.toStdString() << std::endl;
}

void AccuracyTrainerPage::onSaveTrainingSet()
{
    if (!m_hasLoadedSet) {
        QMessageBox::information(this, "Save", "No training set to save.");
        return;
    }

    m_currentSet.deviceName = m_deviceCombo->currentText().toStdString();

    if (m_store.SaveTrainingSet(m_currentSet)) {
        m_statusLabel->setText(QString("Saved: %1")
            .arg(QString::fromStdString(m_currentSet.name)));
    } else {
        QMessageBox::warning(this, "Save Failed", "Could not save training set.");
    }
}

void AccuracyTrainerPage::onDeviceChanged(const QString& deviceName)
{
    if (m_hasLoadedSet) {
        m_currentSet.deviceName = deviceName.toStdString();
    }
}

void AccuracyTrainerPage::refreshTrainingSetList()
{
    m_trainingSetCombo->clear();
    auto names = m_store.ListTrainingSets();
    for (const auto& name : names) {
        m_trainingSetCombo->addItem(QString::fromStdString(name));
    }
}

void AccuracyTrainerPage::updateCurrentTrainingSet()
{
    if (!m_hasLoadedSet) return;

    int bondCount = static_cast<int>(m_currentSet.bondingPoints.size());
    int refCount = static_cast<int>(m_currentSet.referencePoints.size());
    int hexTeachCount = static_cast<int>(m_currentSet.hexDevicePositions.size());
    bool hasFrame = m_currentSet.valid;

    m_statusLabel->setText(QString("Set: %1\nBonding pts: %2 | Refs: %3 | Hex teach: %4\nFrame: %5")
        .arg(QString::fromStdString(m_currentSet.name))
        .arg(bondCount)
        .arg(refCount)
        .arg(hexTeachCount)
        .arg(hasFrame ? "computed" : "not set"));

    // Push updated data into widgets if they are visible
    if (m_contentStack->currentWidget() == m_teachBondingWidget) {
        m_teachBondingWidget->SetTrainingSet(&m_currentSet);
    }
    if (m_contentStack->currentWidget() == m_teachReferenceWidget) {
        m_teachReferenceWidget->SetTrainingSet(&m_currentSet);
    }
    if (m_contentStack->currentWidget() == m_runAlignmentWidget) {
        m_runAlignmentWidget->SetTrainingSet(&m_currentSet);
    }
    if (m_contentStack->currentWidget() == m_multiDeviceWidget) {
        m_multiDeviceWidget->SetTrainingSet(&m_currentSet);
    }
    // Note: m_teachMultiDeviceWidget is NOT re-pushed here.
    // It already has the pointer to m_currentSet and self-updates.
    // Re-pushing would cause re-entrancy crash (combo signal -> rebuild -> delete mid-signal).
    // It gets refreshed on navigation via onNavItemChanged().
}

void AccuracyTrainerPage::setTrainingSetLoaded(bool loaded)
{
    m_hasLoadedSet = loaded;
    m_saveBtn->setEnabled(loaded);
    m_deleteBtn->setEnabled(loaded);

    if (!loaded) {
        m_statusLabel->setText("No training set loaded");
        m_navList->clearSelection();
    }
}

// EdgeTransferPage.cpp
#include "EdgeTransferPage.h"
#include "EdgeTeachWidget.h"
#include "EdgeResultWidget.h"
#include "EdgeImageGalleryWidget.h"
#include "EdgeSummaryDiagramWidget.h"
#include "EdgeRetraceWidget.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include "unified_motion_manager.h"
#include "CameraManager.h"
#include "Common/FileSelectorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QFrame>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QShowEvent>
#include <QSignalBlocker>
#include <filesystem>
#include <iostream>

EdgeTransferPage::EdgeTransferPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_setLabel(nullptr)
    , m_newBtn(nullptr)
    , m_loadBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_saveAsBtn(nullptr)
    , m_navList(nullptr)
    , m_statusLabel(nullptr)
    , m_contentStack(nullptr)
    , m_welcomeWidget(nullptr)
    , m_teachRefWidget(nullptr)
    , m_teachTgtWidget(nullptr)
    , m_resultWidget(nullptr)
    , m_topBarWidget(nullptr)
{
    m_currentSet.name = "(none)";
    m_currentSet.referenceEdge.name = "Reference";
    m_currentSet.targetEdge.name = "Target";
    updateStoreBasePath();
    setupUI();
    setSetLoaded(false);
}

void EdgeTransferPage::setTopBarVisible(bool visible)
{
    if (m_topBarWidget) m_topBarWidget->setVisible(visible);
}

void EdgeTransferPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Pick up the product directory when the trainer is first shown / re-shown
    // (the user may have loaded a product in the meantime).
    updateStoreBasePath();
}

void EdgeTransferPage::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ---- Top bar (hideable when embedded in TrainerContainerPage) ----
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
    connect(backBtn, &QPushButton::clicked, this, &EdgeTransferPage::backToMainMenu);
    topBar->addWidget(backBtn);

    QLabel* titleLabel = new QLabel("Edge Transfer Trainer");
    titleLabel->setStyleSheet(
        "font-size: 16px; font-weight: bold; color: #ffffff; padding: 0 15px;");
    topBar->addWidget(titleLabel);
    topBar->addStretch();
    topBarContainer->addLayout(topBar);

    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background-color: #3d5a8c;");
    sep->setFixedHeight(2);
    topBarContainer->addWidget(sep);
    mainLayout->addWidget(m_topBarWidget);

    // ---- Body: left nav + right stack ----
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(10, 10, 5, 10);
    leftLayout->setSpacing(8);

    const QString groupStyle =
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }";

    // ---- Training Set management ----
    QGroupBox* tsGroup = new QGroupBox("Training Set");
    tsGroup->setStyleSheet(groupStyle);
    QVBoxLayout* tsLayout = new QVBoxLayout(tsGroup);

    m_setLabel = new QLabel("(none loaded)");
    m_setLabel->setStyleSheet(
        "QLabel { background-color: #1a1f2e; color: #ccc; border: 1px solid #3d5a8c; "
        "border-radius: 3px; padding: 5px 8px; font-size: 12px; }");
    tsLayout->addWidget(m_setLabel);

    QHBoxLayout* tsButtonRow = new QHBoxLayout();
    m_newBtn    = new QPushButton("New");
    m_loadBtn   = new QPushButton("Load");
    m_deleteBtn = new QPushButton("Delete");
    m_saveBtn   = new QPushButton("Save");
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

    connect(m_newBtn,    &QPushButton::clicked, this, &EdgeTransferPage::onNewSet);
    connect(m_loadBtn,   &QPushButton::clicked, this, &EdgeTransferPage::onLoadSet);
    connect(m_deleteBtn, &QPushButton::clicked, this, &EdgeTransferPage::onDeleteSet);
    connect(m_saveBtn,   &QPushButton::clicked, this, &EdgeTransferPage::onSaveSet);
    connect(m_saveAsBtn, &QPushButton::clicked, this, &EdgeTransferPage::onSaveAsSet);

    leftLayout->addWidget(tsGroup);

    // ---- Device / Camera tabs (mirrors PlacementCorrectionPage) ----
    const QString comboStyle =
        "QComboBox { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 4px 8px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: #2d3e5f; color: white; "
        "selection-background-color: #3d5a8c; }";

    QTabWidget* cfgTabs = new QTabWidget();
    cfgTabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #3d5a8c; border-radius: 4px; "
        "background-color: #1a1f2e; }"
        "QTabBar::tab { background-color: #2d3e5f; color: #ccc; "
        "padding: 6px 12px; border: 1px solid #3d5a8c; border-bottom: none; "
        "border-top-left-radius: 4px; border-top-right-radius: 4px; font-size: 12px; }"
        "QTabBar::tab:selected { background-color: #3d5a8c; color: white; }"
        "QTabBar::tab:hover { background-color: #3d5a8c; }");

    // Device tab
    QWidget* deviceTab = new QWidget();
    QVBoxLayout* deviceTabLayout = new QVBoxLayout(deviceTab);
    deviceTabLayout->setContentsMargins(8, 10, 8, 8);
    m_targetDeviceCombo = new QComboBox();
    m_targetDeviceCombo->setStyleSheet(comboStyle);
    populateHexDevices();
    connect(m_targetDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EdgeTransferPage::onTargetDeviceChanged);
    deviceTabLayout->addWidget(m_targetDeviceCombo);
    deviceTabLayout->addStretch(1);
    cfgTabs->addTab(deviceTab, "Device");

    // Camera tab
    QWidget* cameraTab = new QWidget();
    QVBoxLayout* cameraTabLayout = new QVBoxLayout(cameraTab);
    cameraTabLayout->setContentsMargins(8, 10, 8, 8);
    m_cameraCombo = new QComboBox();
    m_cameraCombo->setStyleSheet(comboStyle);
    populateCameras();
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EdgeTransferPage::onCameraChanged);
    cameraTabLayout->addWidget(m_cameraCombo);
    cameraTabLayout->addStretch(1);
    cfgTabs->addTab(cameraTab, "Camera");

    leftLayout->addWidget(cfgTabs);

    // ---- Workflow nav ----
    QGroupBox* navGroup = new QGroupBox("Workflow");
    navGroup->setStyleSheet(groupStyle);
    QVBoxLayout* navLayout = new QVBoxLayout(navGroup);
    m_navList = new QListWidget();
    m_navList->setStyleSheet(
        "QListWidget { background-color: #1a1f2e; color: white; border: none; font-size: 13px; }"
        "QListWidget::item { padding: 10px 8px; border-bottom: 1px solid #2d3e5f; }"
        "QListWidget::item:selected { background-color: #3d5a8c; }"
        "QListWidget::item:hover { background-color: #2d3e5f; }");
    m_navList->addItem("1. Reference edge (P1, P2)");
    m_navList->addItem("2. Target edge   (P3, P4)");
    m_navList->addItem("3. Mapping & Result");
    m_navList->addItem("4. Image Gallery");
    m_navList->addItem("5. Summary Diagram");
    m_navList->addItem("6. Retrace");
    connect(m_navList, &QListWidget::currentRowChanged,
            this, &EdgeTransferPage::onNavItemChanged);
    navLayout->addWidget(m_navList);
    leftLayout->addWidget(navGroup);

    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: #888; font-size: 11px; padding: 5px;");
    m_statusLabel->setWordWrap(true);
    leftLayout->addWidget(m_statusLabel);
    leftLayout->addStretch();

    // ---- Right stack ----
    m_contentStack = new QStackedWidget();

    m_welcomeWidget = new QWidget();
    QVBoxLayout* welcomeLayout = new QVBoxLayout(m_welcomeWidget);
    welcomeLayout->setAlignment(Qt::AlignCenter);
    QLabel* welcomeLabel = new QLabel(
        "Edge Transfer Trainer\n\n"
        "Capture a reference edge (P1, P2) and a target edge (P3, P4)\n"
        "from gantry-main, then view computed metrics and deltas.\n\n"
        "Create or load a Training Set to begin.");
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet("color: #aaa; font-size: 14px;");
    welcomeLayout->addWidget(welcomeLabel);
    m_contentStack->addWidget(m_welcomeWidget);   // index 0

    m_teachRefWidget = new EdgeTeachWidget("Reference", m_context, this);
    m_contentStack->addWidget(m_teachRefWidget);  // index 1

    m_teachTgtWidget = new EdgeTeachWidget("Target", m_context, this);
    m_contentStack->addWidget(m_teachTgtWidget);  // index 2

    m_resultWidget = new EdgeResultWidget(m_context, this);
    m_contentStack->addWidget(m_resultWidget);    // index 3

    m_galleryWidget = new EdgeImageGalleryWidget(m_context, this);
    m_contentStack->addWidget(m_galleryWidget);   // index 4

    m_summaryWidget = new EdgeSummaryDiagramWidget(m_context, this);
    m_contentStack->addWidget(m_summaryWidget);   // index 5

    m_retraceWidget = new EdgeRetraceWidget(m_context, this);
    m_contentStack->addWidget(m_retraceWidget);   // index 6

    // When the user accepts the retrace result, replace the current set's
    // taught edges with the retraced values and refresh downstream widgets.
    connect(m_retraceWidget, &EdgeRetraceWidget::retraceCompleted,
            this, [this](const EdgeTransferSet& updated) {
        // Preserve disk identity (name, source filename); overwrite edges.
        m_currentSet.referenceEdge = updated.referenceEdge;
        m_currentSet.targetEdge    = updated.targetEdge;
        m_currentSet.Recalculate();
        m_resultWidget->SetData(m_currentSet);
        if (m_galleryWidget) m_galleryWidget->SetData(m_currentSet);
        if (m_summaryWidget) m_summaryWidget->SetData(m_currentSet);
        if (m_retraceWidget) m_retraceWidget->SetData(m_currentSet);
        updateStatus();
        m_statusLabel->setText(
            QString("Retraced edges applied. Use Save to persist to disk."));
        std::cout << "[EdgeTransferPage] Retraced values applied to current set"
                  << std::endl;
    });

    // Bidirectional lock: block Apply buttons while summary nav is moving,
    // and block summary nav buttons while a correction is being applied.
    connect(m_summaryWidget, &EdgeSummaryDiagramWidget::navBusyChanged,
            this, [this](bool busy) {
        if (m_resultWidget) m_resultWidget->SetApplyButtonsLocked(busy);
    });
    connect(m_resultWidget, &EdgeResultWidget::applyBusyChanged,
            this, [this](bool busy) {
        if (m_summaryWidget) m_summaryWidget->SetNavBlockedExternal(busy);
    });

    connect(m_teachRefWidget, &EdgeTeachWidget::edgeCaptured,
            this, [this](const EdgePoints& e) {
        m_currentSet.referenceEdge = e;
        m_currentSet.Recalculate();
        m_resultWidget->SetData(m_currentSet);
        if (m_galleryWidget) m_galleryWidget->SetData(m_currentSet);
        if (m_summaryWidget) m_summaryWidget->SetData(m_currentSet);
        updateStatus();
    });

    connect(m_teachTgtWidget, &EdgeTeachWidget::edgeCaptured,
            this, [this](const EdgePoints& e) {
        m_currentSet.targetEdge = e;
        m_currentSet.Recalculate();
        m_resultWidget->SetData(m_currentSet);
        if (m_galleryWidget) m_galleryWidget->SetData(m_currentSet);
        if (m_summaryWidget) m_summaryWidget->SetData(m_currentSet);
        updateStatus();
    });

    connect(m_resultWidget, &EdgeResultWidget::configChanged,
            this, [this](const EdgeTransferConfig& cfg) {
        m_currentSet.config = cfg;
        m_currentSet.Recalculate();
        if (m_summaryWidget) m_summaryWidget->SetData(m_currentSet);
        updateStatus();
    });

    connect(m_resultWidget, &EdgeResultWidget::recalculateRequested,
            this, [this]() {
        m_currentSet.Recalculate();
        m_resultWidget->SetData(m_currentSet);
        if (m_summaryWidget) m_summaryWidget->SetData(m_currentSet);
        updateStatus();
    });

    splitter->addWidget(leftPanel);
    splitter->addWidget(m_contentStack);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 7);
    splitter->setHandleWidth(3);

    mainLayout->addWidget(splitter, 1);
    m_contentStack->setCurrentIndex(0);

    // Seed the current set's config from the (possibly empty) default combos,
    // so the result widget reflects what's selected on the left panel.
    if (m_targetDeviceCombo && m_targetDeviceCombo->count() > 0) {
        m_currentSet.config.targetDevice =
            m_targetDeviceCombo->currentText().toStdString();
    }
    if (m_cameraCombo && m_cameraCombo->count() > 0) {
        m_currentSet.config.cameraId = m_cameraCombo->currentText().toStdString();
    }
}

void EdgeTransferPage::populateHexDevices()
{
    if (!m_targetDeviceCombo) return;
    QSignalBlocker block(m_targetDeviceCombo);
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

void EdgeTransferPage::populateCameras()
{
    if (!m_cameraCombo) return;
    QSignalBlocker block(m_cameraCombo);
    m_cameraCombo->clear();
    m_cameraCombo->addItem("(none)");
    if (m_context && m_context->cameraManager) {
        auto ids = m_context->cameraManager->GetCameraIds();
        for (const auto& id : ids) {
            m_cameraCombo->addItem(QString::fromStdString(id));
        }
    }
}

void EdgeTransferPage::onTargetDeviceChanged(int /*index*/)
{
    if (!m_targetDeviceCombo) return;
    m_currentSet.config.targetDevice = m_targetDeviceCombo->currentText().toStdString();
    if (m_resultWidget) m_resultWidget->SetData(m_currentSet);
    updateStatus();
}

void EdgeTransferPage::onCameraChanged(int /*index*/)
{
    if (!m_cameraCombo) return;
    QString text = m_cameraCombo->currentText();
    std::string camId = (text == "(none)") ? std::string() : text.toStdString();
    m_currentSet.config.cameraId = camId;
    if (m_teachRefWidget) m_teachRefWidget->SetCameraId(camId);
    if (m_teachTgtWidget) m_teachTgtWidget->SetCameraId(camId);
    updateStatus();
}

void EdgeTransferPage::onNavItemChanged(int currentRow)
{
    if (!m_hasLoadedSet && currentRow >= 0) {
        m_statusLabel->setText("Create or load a training set first.");
        m_contentStack->setCurrentIndex(0);
        return;
    }
    switch (currentRow) {
    case 0:
        m_teachRefWidget->SetEdge(m_currentSet.referenceEdge);
        m_contentStack->setCurrentIndex(1);
        break;
    case 1:
        m_teachTgtWidget->SetEdge(m_currentSet.targetEdge);
        m_contentStack->setCurrentIndex(2);
        break;
    case 2:
        m_currentSet.Recalculate();
        m_resultWidget->SetData(m_currentSet);
        m_contentStack->setCurrentIndex(3);
        break;
    case 3:
        if (m_galleryWidget) m_galleryWidget->SetData(m_currentSet);
        m_contentStack->setCurrentIndex(4);
        break;
    case 4:
        m_currentSet.Recalculate();
        if (m_summaryWidget) m_summaryWidget->SetData(m_currentSet);
        m_contentStack->setCurrentIndex(5);
        break;
    case 5:
        if (m_retraceWidget) m_retraceWidget->SetData(m_currentSet);
        m_contentStack->setCurrentIndex(6);
        break;
    default:
        m_contentStack->setCurrentIndex(0);
        break;
    }
}

void EdgeTransferPage::onNewSet()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Edge Transfer Set",
        "Enter a name for the set:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    m_currentSet = EdgeTransferSet();
    m_currentSet.name = name.trimmed().toStdString();
    m_currentSet.measureDevice = "gantry-main";
    m_currentSet.referenceEdge.name = "Reference";
    m_currentSet.targetEdge.name = "Target";
    m_sourceFilename = m_currentSet.name;

    // Seed config from the current left-panel selections.
    if (m_targetDeviceCombo && m_targetDeviceCombo->count() > 0) {
        m_currentSet.config.targetDevice =
            m_targetDeviceCombo->currentText().toStdString();
    }
    if (m_cameraCombo && m_cameraCombo->count() > 0) {
        QString camText = m_cameraCombo->currentText();
        m_currentSet.config.cameraId = (camText == "(none)")
            ? std::string() : camText.toStdString();
    }
    if (m_teachRefWidget) m_teachRefWidget->SetCameraId(m_currentSet.config.cameraId);
    if (m_teachTgtWidget) m_teachTgtWidget->SetCameraId(m_currentSet.config.cameraId);

    setSetLoaded(true);

    (void)m_store.SaveSet(m_currentSet);
    m_setLabel->setText(name.trimmed());
    m_resultWidget->SetData(m_currentSet);
    updateStatus();

    std::cout << "[EdgeTransferPage] Created new set: " << m_currentSet.name << std::endl;
}

void EdgeTransferPage::onLoadSet()
{
    QString baseDir = QString::fromStdString(
        std::filesystem::absolute(m_store.GetBasePath()).string());

    QString filePath = FileSelectorDialog::getOpenFileName(
        this, baseDir, "*.json",
        "Load Edge Transfer Set", "Training Set Files");
    if (filePath.isEmpty()) return;

    std::string stem = std::filesystem::path(filePath.toStdString()).stem().string();

    auto loaded = m_store.LoadSet(stem);
    if (!loaded.has_value()) {
        QMessageBox::warning(this, "Load Failed",
            "Could not load set: " + QString::fromStdString(stem));
        return;
    }

    m_currentSet = loaded.value();
    m_sourceFilename = stem;
    setSetLoaded(true);

    // Restore combos from loaded config
    if (m_targetDeviceCombo && !m_currentSet.config.targetDevice.empty()) {
        QSignalBlocker b(m_targetDeviceCombo);
        int idx = m_targetDeviceCombo->findText(
            QString::fromStdString(m_currentSet.config.targetDevice));
        if (idx >= 0) m_targetDeviceCombo->setCurrentIndex(idx);
    }
    if (m_cameraCombo) {
        QSignalBlocker b(m_cameraCombo);
        if (m_currentSet.config.cameraId.empty()) {
            m_cameraCombo->setCurrentIndex(0);   // "(none)"
        } else {
            int idx = m_cameraCombo->findText(
                QString::fromStdString(m_currentSet.config.cameraId));
            if (idx >= 0) m_cameraCombo->setCurrentIndex(idx);
        }
    }
    if (m_teachRefWidget) m_teachRefWidget->SetCameraId(m_currentSet.config.cameraId);
    if (m_teachTgtWidget) m_teachTgtWidget->SetCameraId(m_currentSet.config.cameraId);

    m_setLabel->setText(QString::fromStdString(stem));
    m_resultWidget->SetData(m_currentSet);
    if (m_galleryWidget) m_galleryWidget->SetData(m_currentSet);
    if (m_summaryWidget) m_summaryWidget->SetData(m_currentSet);
    if (m_retraceWidget) m_retraceWidget->SetData(m_currentSet);
    updateStatus();

    std::cout << "[EdgeTransferPage] Loaded set: " << m_currentSet.name
              << " from " << stem << ".json" << std::endl;
}

void EdgeTransferPage::onDeleteSet()
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
    for (const auto& s : sets) list->addItem(QString::fromStdString(s));
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

    (void)m_store.DeleteSet(selected.toStdString());

    // If the deleted set was the currently loaded one, clear it.
    if (m_hasLoadedSet && (selected.toStdString() == m_currentSet.name ||
        selected.toStdString() == m_sourceFilename)) {
        m_currentSet = EdgeTransferSet();
        m_sourceFilename.clear();
        setSetLoaded(false);
        m_setLabel->setText("(none loaded)");
        m_contentStack->setCurrentIndex(0);
    }
    std::cout << "[EdgeTransferPage] Deleted set: " << selected.toStdString() << std::endl;
}

void EdgeTransferPage::onSaveSet()
{
    if (!m_hasLoadedSet) {
        QMessageBox::information(this, "Save", "No set to save.");
        return;
    }
    if (m_sourceFilename.empty()) {
        onSaveAsSet();
        return;
    }
    m_currentSet.name = m_sourceFilename;
    if (m_store.SaveSet(m_currentSet)) {
        m_setLabel->setText(QString::fromStdString(m_sourceFilename));
        m_statusLabel->setText(QString("Saved: %1")
            .arg(QString::fromStdString(m_sourceFilename)));
    } else {
        QMessageBox::warning(this, "Save Failed", "Could not save set.");
    }
}

void EdgeTransferPage::onSaveAsSet()
{
    if (!m_hasLoadedSet) {
        QMessageBox::information(this, "Save As", "No set to save.");
        return;
    }

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

    QLineEdit* nameEdit = new QLineEdit(QString::fromStdString(
        m_sourceFilename.empty() ? m_currentSet.name : m_sourceFilename));
    layout->addWidget(nameEdit);

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
        for (const auto& s : sets) existingList->addItem(QString::fromStdString(s));
        layout->addWidget(existingList);
        connect(existingList, &QListWidget::itemClicked, &dlg,
                [nameEdit](QListWidgetItem* item) { nameEdit->setText(item->text()); });
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

    connect(saveBtn,   &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    QString saveName = nameEdit->text().trimmed();
    if (saveName.isEmpty()) return;

    m_currentSet.name = saveName.toStdString();
    m_sourceFilename  = saveName.toStdString();

    if (m_store.SaveSet(m_currentSet)) {
        m_setLabel->setText(saveName);
        m_statusLabel->setText(QString("Saved: %1").arg(saveName));
    } else {
        QMessageBox::warning(this, "Save Failed", "Could not save set.");
    }
}

void EdgeTransferPage::setSetLoaded(bool loaded)
{
    m_hasLoadedSet = loaded;
    m_saveBtn->setEnabled(loaded);
    m_saveAsBtn->setEnabled(loaded);
    if (!loaded) {
        m_statusLabel->setText("No training set loaded");
        m_navList->clearSelection();
    }
}

void EdgeTransferPage::updateStoreBasePath()
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(
            m_context->productPositionManager->GetCurrentProductPath());
        QString productDir = QFileInfo(productFilePath).path();
        std::string path = (productDir + "/edge_transfer").toStdString();
        m_store.SetBasePath(path);
        std::cout << "[EdgeTransferPage] Store base path set to product: "
                  << path << std::endl;
    } else {
        m_store.SetBasePath("config/edge_transfer");
        std::cout << "[EdgeTransferPage] No product loaded, using default: "
                     "config/edge_transfer" << std::endl;
    }
}

void EdgeTransferPage::updateStatus()
{
    if (!m_hasLoadedSet) {
        m_statusLabel->setText("No training set loaded");
        return;
    }
    const QString ref = m_currentSet.referenceEdge.valid ? "captured" : "---";
    const QString tgt = m_currentSet.targetEdge.valid    ? "captured" : "---";
    m_statusLabel->setText(QString(
        "Set: %1\nRef: %2\nTarget: %3\nDeltas: %4")
        .arg(QString::fromStdString(m_currentSet.name))
        .arg(ref).arg(tgt)
        .arg(m_currentSet.deltasValid ? "valid" : "N/A"));
}

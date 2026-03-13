// ZOffsetPage.cpp
#include "ZOffsetPage.h"
#include "TeachZPointWidget.h"
#include "ZOffsetResultWidget.h"
#include "ZOffsetApplyWidget.h"
#include "ZOffsetRetraceWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QGroupBox>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QLineEdit>
#include <iostream>
#include <filesystem>

ZOffsetPage::ZOffsetPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_navList(nullptr)
    , m_setLabel(nullptr)
    , m_newBtn(nullptr)
    , m_loadBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_statusLabel(nullptr)
    , m_contentStack(nullptr)
    , m_welcomeWidget(nullptr)
    , m_teachRefWidget(nullptr)
    , m_teachTgtWidget(nullptr)
    , m_resultWidget(nullptr)
    , m_applyWidget(nullptr)
    , m_retraceWidget(nullptr)
    , m_topBarWidget(nullptr)
{
    updateStoreBasePath();
    setupUI();
    setSetLoaded(false);
}

void ZOffsetPage::setupUI()
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
        "QPushButton:hover { background-color: #4d6e9f; }"
    );
    connect(backBtn, &QPushButton::clicked, this, &ZOffsetPage::backToMainMenu);
    topBar->addWidget(backBtn);

    QLabel* titleLabel = new QLabel("Z Offset Trainer");
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

    // Training Set management group
    QGroupBox* tsGroup = new QGroupBox("Training Set");
    tsGroup->setStyleSheet(
        "QGroupBox { font-weight: bold; color: #aabbcc; border: 1px solid #3d5a8c; "
        "border-radius: 4px; margin-top: 10px; padding-top: 15px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    );
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

    connect(m_newBtn, &QPushButton::clicked, this, &ZOffsetPage::onNewSet);
    connect(m_loadBtn, &QPushButton::clicked, this, &ZOffsetPage::onLoadSet);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ZOffsetPage::onDeleteSet);
    connect(m_saveBtn, &QPushButton::clicked, this, &ZOffsetPage::onSaveSet);

    leftLayout->addWidget(tsGroup);

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
    m_navList->addItem("1. Teach Reference (P1)");
    m_navList->addItem("2. Teach Target (P2)");
    m_navList->addItem("3. Z Offset & Config");
    m_navList->addItem("4. Apply Correction");
    m_navList->addItem("5. Retrace & Apply");
    connect(m_navList, &QListWidget::currentRowChanged,
            this, &ZOffsetPage::onNavItemChanged);
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
        "Welcome to Z Offset Trainer\n\n"
        "1. Create or load a Training Set\n"
        "2. Teach Reference Surface (gantry Z)\n"
        "3. Teach Target Surface (gantry Z)\n"
        "4. View Z difference & configure correction\n"
        "5. Apply Z correction to hex device\n\n"
        "Select a workflow step from the left panel."
    );
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet("color: #aaa; font-size: 14px;");
    welcomeLayout->addWidget(welcomeLabel);
    m_contentStack->addWidget(m_welcomeWidget);  // index 0

    // TeachZPointWidget for Reference (index 1)
    m_teachRefWidget = new TeachZPointWidget("Reference", m_context, this);
    m_contentStack->addWidget(m_teachRefWidget);

    // TeachZPointWidget for Target (index 2)
    m_teachTgtWidget = new TeachZPointWidget("Target", m_context, this);
    m_contentStack->addWidget(m_teachTgtWidget);

    // ZOffsetResultWidget (index 3)
    m_resultWidget = new ZOffsetResultWidget(m_context, this);
    m_contentStack->addWidget(m_resultWidget);

    // ZOffsetApplyWidget (index 4)
    m_applyWidget = new ZOffsetApplyWidget(m_context, this);
    m_contentStack->addWidget(m_applyWidget);

    // ZOffsetRetraceWidget (index 5)
    m_retraceWidget = new ZOffsetRetraceWidget(m_context, this);
    m_contentStack->addWidget(m_retraceWidget);

    // Wire signals
    connect(m_teachRefWidget, &TeachZPointWidget::pointCaptured,
            this, [this](const ZOffsetPoint& pt) {
        m_currentSet.reference = pt;
        m_currentSet.RecalculateZDiff();
        updateCurrentSet();
    });

    connect(m_teachTgtWidget, &TeachZPointWidget::pointCaptured,
            this, [this](const ZOffsetPoint& pt) {
        m_currentSet.target = pt;
        m_currentSet.RecalculateZDiff();
        updateCurrentSet();
    });

    connect(m_resultWidget, &ZOffsetResultWidget::correctionConfigured,
            this, [this](const ZOffsetCorrectionConfig& cfg) {
        m_currentSet.correction = cfg;
        m_applyWidget->SetCorrection(cfg);
    });

    connect(m_applyWidget, &ZOffsetApplyWidget::correctionApplied,
            this, [this](double valueMm) {
        m_currentSet.correction.applied = true;
        m_currentSet.correction.appliedValueMm = valueMm;
        updateCurrentSet();
    });

    connect(m_retraceWidget, &ZOffsetRetraceWidget::retraceCompleted,
            this, [this](const ZOffsetSet& updatedSet) {
        m_currentSet.reference = updatedSet.reference;
        m_currentSet.target = updatedSet.target;
        m_currentSet.RecalculateZDiff();
        m_currentSet.valid = updatedSet.valid;
        updateCurrentSet();
    });

    connect(m_retraceWidget, &ZOffsetRetraceWidget::correctionApplied,
            this, [this](double valueMm) {
        m_currentSet.correction.applied = true;
        m_currentSet.correction.appliedValueMm = valueMm;
        updateCurrentSet();
        // Auto-save after retrace+apply
        m_store.SaveSet(m_currentSet);
    });

    // Add panels to splitter
    splitter->addWidget(leftPanel);
    splitter->addWidget(m_contentStack);
    splitter->setStretchFactor(0, 3);   // 30%
    splitter->setStretchFactor(1, 7);   // 70%
    splitter->setHandleWidth(3);

    mainLayout->addWidget(splitter, 1);
}

void ZOffsetPage::onNavItemChanged(int currentRow)
{
    if (!m_hasLoadedSet && currentRow >= 0) {
        m_statusLabel->setText("Please create or load a training set first.");
        m_contentStack->setCurrentIndex(0);
        return;
    }

    switch (currentRow) {
    case 0:  // Teach Reference
        m_teachRefWidget->SetDeviceName(m_currentSet.deviceName);
        m_teachRefWidget->SetPoint(m_currentSet.reference);
        m_contentStack->setCurrentIndex(1);
        break;
    case 1:  // Teach Target
        m_teachTgtWidget->SetDeviceName(m_currentSet.deviceName);
        m_teachTgtWidget->SetPoint(m_currentSet.target);
        m_contentStack->setCurrentIndex(2);
        break;
    case 2:  // Z Offset & Config
        m_currentSet.RecalculateZDiff();
        m_resultWidget->SetData(m_currentSet);
        m_contentStack->setCurrentIndex(3);
        break;
    case 3:  // Apply Correction
        m_applyWidget->SetCorrection(m_currentSet.correction);
        m_contentStack->setCurrentIndex(4);
        break;
    case 4:  // Retrace & Apply
        m_retraceWidget->SetData(&m_currentSet);
        m_contentStack->setCurrentIndex(5);
        break;
    default:
        m_contentStack->setCurrentIndex(0);
        break;
    }
}

void ZOffsetPage::onNewSet()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Z Offset Set",
        "Enter a name for the set:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    m_currentSet = ZOffsetSet();
    m_currentSet.name = name.trimmed().toStdString();
    m_currentSet.deviceName = "gantry-main";
    m_currentSet.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    m_currentSet.reference.name = "Reference";
    m_currentSet.target.name = "Target";

    setSetLoaded(true);

    m_store.SaveSet(m_currentSet);
    m_setLabel->setText(name.trimmed());

    m_statusLabel->setText(QString("Created: %1\nRef: --- | Tgt: ---")
        .arg(name.trimmed()));

    std::cout << "[ZOffsetPage] Created new set: " << m_currentSet.name << std::endl;
}

void ZOffsetPage::onLoadSet()
{
    QString baseDir = QString::fromStdString(
        std::filesystem::absolute(m_store.GetBasePath()).string());

    QString filePath = QFileDialog::getOpenFileName(
        this, "Load Z Offset Set", baseDir, "Set Files (*.json)");

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

    m_setLabel->setText(QString::fromStdString(stem));
    updateCurrentSet();

    std::cout << "[ZOffsetPage] Loaded set: " << m_currentSet.name
              << " from: " << stem << ".json" << std::endl;
}

void ZOffsetPage::onDeleteSet()
{
    if (!m_hasLoadedSet) return;

    QString name = QString::fromStdString(m_currentSet.name);
    if (name.isEmpty()) return;

    auto result = QMessageBox::question(this, "Delete Set",
        "Delete Z offset set '" + name + "'?\nThis cannot be undone.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result != QMessageBox::Yes) return;

    std::string deleteKey = m_currentSet.sourceFilename.empty()
        ? m_currentSet.name : m_currentSet.sourceFilename;
    m_store.DeleteSet(deleteKey);

    m_currentSet = ZOffsetSet();
    setSetLoaded(false);
    m_setLabel->setText("(none loaded)");
    m_contentStack->setCurrentIndex(0);

    std::cout << "[ZOffsetPage] Deleted set: " << name.toStdString() << std::endl;
}

void ZOffsetPage::onSaveSet()
{
    if (!m_hasLoadedSet) {
        QMessageBox::information(this, "Save", "No set to save.");
        return;
    }

    if (m_store.SaveSet(m_currentSet)) {
        m_statusLabel->setText(QString("Saved: %1")
            .arg(QString::fromStdString(m_currentSet.name)));
    } else {
        QMessageBox::warning(this, "Save Failed", "Could not save set.");
    }
}

void ZOffsetPage::updateStoreBasePath()
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(
            m_context->productPositionManager->GetCurrentProductPath());
        QString productDir = QFileInfo(productFilePath).path();
        std::string path = (productDir + "/z_offset").toStdString();
        m_store.SetBasePath(path);
        std::cout << "[ZOffsetPage] Store base path set to product: " << path << std::endl;
    } else {
        m_store.SetBasePath("config/z_offset");
        std::cout << "[ZOffsetPage] No product loaded, using default: config/z_offset" << std::endl;
    }
}

void ZOffsetPage::updateCurrentSet()
{
    if (!m_hasLoadedSet) return;

    bool ref = m_currentSet.reference.valid;
    bool tgt = m_currentSet.target.valid;
    bool zDiff = m_currentSet.zDiffValid;
    bool applied = m_currentSet.correction.applied;

    QString zDiffStr = "N/A";
    if (zDiff) {
        double um = m_currentSet.measuredZDiffMm * 1000.0;
        zDiffStr = QString("%1 %2m").arg(um, 0, 'f', 1).arg(QChar(0x00B5));
    }

    m_statusLabel->setText(QString("Set: %1\nRef: %2 | Tgt: %3\nZ Diff: %4 | Applied: %5")
        .arg(QString::fromStdString(m_currentSet.name))
        .arg(ref ? "captured" : "---")
        .arg(tgt ? "captured" : "---")
        .arg(zDiffStr)
        .arg(applied ? "yes" : "no"));
}

void ZOffsetPage::setSetLoaded(bool loaded)
{
    m_hasLoadedSet = loaded;
    m_saveBtn->setEnabled(loaded);
    m_deleteBtn->setEnabled(loaded);

    if (!loaded) {
        m_statusLabel->setText("No training set loaded");
        m_navList->clearSelection();
    }
}

void ZOffsetPage::setTopBarVisible(bool visible)
{
    if (m_topBarWidget) {
        m_topBarWidget->setVisible(visible);
    }
}

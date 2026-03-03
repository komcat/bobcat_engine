// CameraOffsetTrainerPage.cpp
#include "CameraOffsetTrainerPage.h"
#include "CalibrateOffsetWidget.h"
#include "TestOffsetWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "CameraManager.h"
#include "PneumaticManager.h"
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
#include <iostream>
#include <filesystem>

CameraOffsetTrainerPage::CameraOffsetTrainerPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_navList(nullptr)
    , m_calibrationSetLabel(nullptr)
    , m_newBtn(nullptr)
    , m_loadBtn(nullptr)
    , m_deleteBtn(nullptr)
    , m_saveBtn(nullptr)
    , m_lowMagCameraCombo(nullptr)
    , m_highMagCameraCombo(nullptr)
    , m_pneumaticSlideCombo(nullptr)
    , m_statusLabel(nullptr)
    , m_contentStack(nullptr)
    , m_welcomeWidget(nullptr)
    , m_calibrateWidget(nullptr)
    , m_testWidget(nullptr)
    , m_topBarWidget(nullptr)
{
    updateStoreBasePath();
    setupUI();
    setCalibrationSetLoaded(false);
}

void CameraOffsetTrainerPage::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top bar with back button (wrapped in a widget so it can be hidden)
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
    connect(backBtn, &QPushButton::clicked, this, &CameraOffsetTrainerPage::backToMainMenu);
    topBar->addWidget(backBtn);

    QLabel* titleLabel = new QLabel("Camera Offset Trainer");
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #ffffff; padding: 0 15px;");
    topBar->addWidget(titleLabel);
    topBar->addStretch();

    topBarContainer->addLayout(topBar);

    // Separator
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

    QString btnStyle =
        "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 3px 8px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3d5a8c; }"
        "QPushButton:disabled { background-color: #1a1f2e; color: #555; }";

    // Calibration Set management group
    QGroupBox* csGroup = new QGroupBox("Calibration Set");
    csGroup->setStyleSheet(groupStyle);
    QVBoxLayout* csLayout = new QVBoxLayout(csGroup);

    m_calibrationSetLabel = new QLabel("(none loaded)");
    m_calibrationSetLabel->setStyleSheet(
        "QLabel { background-color: #1a1f2e; color: #ccc; border: 1px solid #3d5a8c; "
        "border-radius: 3px; padding: 5px 8px; font-size: 12px; }");
    csLayout->addWidget(m_calibrationSetLabel);

    QHBoxLayout* csButtonRow = new QHBoxLayout();
    m_newBtn = new QPushButton("New");
    m_loadBtn = new QPushButton("Load");
    m_deleteBtn = new QPushButton("Delete");
    m_saveBtn = new QPushButton("Save");

    for (auto* btn : {m_newBtn, m_loadBtn, m_deleteBtn, m_saveBtn}) {
        btn->setFixedHeight(28);
        btn->setStyleSheet(btnStyle);
    }

    csButtonRow->addWidget(m_newBtn);
    csButtonRow->addWidget(m_loadBtn);
    csButtonRow->addWidget(m_deleteBtn);
    csButtonRow->addWidget(m_saveBtn);
    csLayout->addLayout(csButtonRow);

    connect(m_newBtn, &QPushButton::clicked, this, &CameraOffsetTrainerPage::onNewCalibrationSet);
    connect(m_loadBtn, &QPushButton::clicked, this, &CameraOffsetTrainerPage::onLoadCalibrationSet);
    connect(m_deleteBtn, &QPushButton::clicked, this, &CameraOffsetTrainerPage::onDeleteCalibrationSet);
    connect(m_saveBtn, &QPushButton::clicked, this, &CameraOffsetTrainerPage::onSaveCalibrationSet);

    leftLayout->addWidget(csGroup);

    // Camera Selection group
    QGroupBox* camGroup = new QGroupBox("Camera Selection");
    camGroup->setStyleSheet(groupStyle);
    QVBoxLayout* camLayout = new QVBoxLayout(camGroup);

    QLabel* lowMagLabel = new QLabel("Low-Mag Camera:");
    lowMagLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    camLayout->addWidget(lowMagLabel);

    m_lowMagCameraCombo = new QComboBox();
    camLayout->addWidget(m_lowMagCameraCombo);

    QLabel* highMagLabel = new QLabel("High-Mag Camera:");
    highMagLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    camLayout->addWidget(highMagLabel);

    m_highMagCameraCombo = new QComboBox();
    camLayout->addWidget(m_highMagCameraCombo);

    // Populate camera combos from CameraManager
    if (m_context && m_context->cameraManager) {
        auto cameraIds = m_context->cameraManager->GetCameraIds();
        for (const auto& id : cameraIds) {
            QString qid = QString::fromStdString(id);
            m_lowMagCameraCombo->addItem(qid);
            m_highMagCameraCombo->addItem(qid);
        }
    }
    if (m_lowMagCameraCombo->count() == 0) {
        m_lowMagCameraCombo->addItem("(no cameras)");
        m_highMagCameraCombo->addItem("(no cameras)");
    }

    // Camera selection change updates the current set
    connect(m_lowMagCameraCombo, &QComboBox::currentTextChanged,
            this, [this](const QString& text) {
        if (m_hasLoadedSet) {
            m_currentSet.lowMagCameraId = text.toStdString();
        }
    });
    connect(m_highMagCameraCombo, &QComboBox::currentTextChanged,
            this, [this](const QString& text) {
        if (m_hasLoadedSet) {
            m_currentSet.highMagCameraId = text.toStdString();
        }
    });

    leftLayout->addWidget(camGroup);

    // Pneumatic Slide group
    QGroupBox* pneuGroup = new QGroupBox("Pneumatic Slide");
    pneuGroup->setStyleSheet(groupStyle);
    QVBoxLayout* pneuLayout = new QVBoxLayout(pneuGroup);

    QLabel* pneuLabel = new QLabel("High-Mag Lens Tube:");
    pneuLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    pneuLayout->addWidget(pneuLabel);

    m_pneumaticSlideCombo = new QComboBox();
    m_pneumaticSlideCombo->addItem("(None)");
    if (m_context && m_context->pneumaticManager) {
        auto slideNames = m_context->pneumaticManager->GetAllSlideNames();
        for (const auto& name : slideNames) {
            m_pneumaticSlideCombo->addItem(QString::fromStdString(name));
        }
    }
    pneuLayout->addWidget(m_pneumaticSlideCombo);

    connect(m_pneumaticSlideCombo, &QComboBox::currentTextChanged,
            this, [this](const QString& text) {
        if (m_hasLoadedSet) {
            m_currentSet.pneumaticSlideName = (text == "(None)") ? "" : text.toStdString();
        }
    });

    leftLayout->addWidget(pneuGroup);

    // Navigation list (Workflow)
    QGroupBox* navGroup = new QGroupBox("Workflow");
    navGroup->setStyleSheet(groupStyle);
    QVBoxLayout* navLayout = new QVBoxLayout(navGroup);
    m_navList = new QListWidget();
    m_navList->setStyleSheet(
        "QListWidget { background-color: #1a1f2e; color: white; border: none; font-size: 13px; }"
        "QListWidget::item { padding: 10px 8px; border-bottom: 1px solid #2d3e5f; }"
        "QListWidget::item:selected { background-color: #3d5a8c; }"
        "QListWidget::item:hover { background-color: #2d3e5f; }"
    );
    m_navList->addItem("1. Calibrate Offset");
    m_navList->addItem("2. Test Offset");
    connect(m_navList, &QListWidget::currentRowChanged,
            this, &CameraOffsetTrainerPage::onNavItemChanged);
    navLayout->addWidget(m_navList);
    leftLayout->addWidget(navGroup);

    // Status label
    m_statusLabel = new QLabel("No calibration set loaded");
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
        "Welcome to Camera Offset Trainer\n\n"
        "This tool calibrates the XYZ offset between a\n"
        "low-magnification camera and a high-magnification camera,\n"
        "both mounted on the same gantry device.\n\n"
        "1. Create or load a Calibration Set\n"
        "2. Select the low-mag and high-mag cameras\n"
        "3. Calibrate: center each camera on a fiducial\n"
        "4. Test: verify the offset is accurate\n\n"
        "Select a workflow step from the left panel."
    );
    welcomeLabel->setAlignment(Qt::AlignCenter);
    welcomeLabel->setStyleSheet("color: #aaa; font-size: 14px;");
    welcomeLayout->addWidget(welcomeLabel);
    m_contentStack->addWidget(m_welcomeWidget);  // index 0

    // Calibrate widget (index 1)
    m_calibrateWidget = new CalibrateOffsetWidget(m_context, this);
    m_contentStack->addWidget(m_calibrateWidget);  // index 1

    // Test widget (index 2)
    m_testWidget = new TestOffsetWidget(m_context, this);
    m_contentStack->addWidget(m_testWidget);  // index 2

    // Wire widget signals
    connect(m_calibrateWidget, &CalibrateOffsetWidget::calibrationCompleted,
            this, [this](const CameraOffsetCalibrationSet& updatedSet) {
        m_currentSet.lowMagPosition = updatedSet.lowMagPosition;
        m_currentSet.highMagPosition = updatedSet.highMagPosition;
        m_currentSet.offsetX = updatedSet.offsetX;
        m_currentSet.offsetY = updatedSet.offsetY;
        m_currentSet.offsetZ = updatedSet.offsetZ;
        m_currentSet.offsetValid = updatedSet.offsetValid;
        m_currentSet.lowMagVisionProfilePath = updatedSet.lowMagVisionProfilePath;
        m_currentSet.highMagVisionProfilePath = updatedSet.highMagVisionProfilePath;
        m_currentSet.lastModifiedAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
        updateCurrentCalibrationSet();
        // Auto-save after successful calibration
        m_store.SaveCalibrationSet(m_currentSet);
    });

    connect(m_testWidget, &TestOffsetWidget::testCompleted,
            this, [this](const CameraOffsetCalibrationSet::TestResult& result) {
        m_currentSet.lastTestResult = result;
        m_currentSet.lastModifiedAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
        updateCurrentCalibrationSet();
        // Auto-save after successful test
        m_store.SaveCalibrationSet(m_currentSet);
    });

    // Add panels to splitter
    splitter->addWidget(leftPanel);
    splitter->addWidget(m_contentStack);
    splitter->setStretchFactor(0, 3);   // 30%
    splitter->setStretchFactor(1, 7);   // 70%
    splitter->setHandleWidth(3);

    mainLayout->addWidget(splitter, 1);
}

void CameraOffsetTrainerPage::onNavItemChanged(int currentRow)
{
    if (!m_hasLoadedSet && currentRow >= 0) {
        m_statusLabel->setText("Please create or load a calibration set first.");
        m_contentStack->setCurrentIndex(0);
        return;
    }

    // Sync camera selections and pneumatic slide into the current set before passing to widgets
    m_currentSet.lowMagCameraId = m_lowMagCameraCombo->currentText().toStdString();
    m_currentSet.highMagCameraId = m_highMagCameraCombo->currentText().toStdString();
    QString pneuNav = m_pneumaticSlideCombo->currentText();
    m_currentSet.pneumaticSlideName = (pneuNav == "(None)") ? "" : pneuNav.toStdString();

    switch (currentRow) {
    case 0:  // Calibrate Offset
        m_calibrateWidget->SetCalibrationSet(&m_currentSet);
        m_contentStack->setCurrentIndex(1);
        break;
    case 1:  // Test Offset
        m_testWidget->SetCalibrationSet(&m_currentSet);
        m_contentStack->setCurrentIndex(2);
        break;
    default:
        m_contentStack->setCurrentIndex(0);
        break;
    }
}

void CameraOffsetTrainerPage::onNewCalibrationSet()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "New Calibration Set",
        "Enter a name for the calibration set:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    m_currentSet = CameraOffsetCalibrationSet();
    m_currentSet.name = name.trimmed().toStdString();
    m_currentSet.deviceName = "gantry-main";
    m_currentSet.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    m_currentSet.lastModifiedAt = m_currentSet.createdAt;

    // Set camera IDs and pneumatic slide from combos
    m_currentSet.lowMagCameraId = m_lowMagCameraCombo->currentText().toStdString();
    m_currentSet.highMagCameraId = m_highMagCameraCombo->currentText().toStdString();
    QString pneuNew = m_pneumaticSlideCombo->currentText();
    m_currentSet.pneumaticSlideName = (pneuNew == "(None)") ? "" : pneuNew.toStdString();

    setCalibrationSetLoaded(true);

    // Auto-save the empty set
    m_store.SaveCalibrationSet(m_currentSet);

    m_calibrationSetLabel->setText(name.trimmed());
    m_statusLabel->setText(QString("Created: %1\nOffset: not calibrated").arg(name.trimmed()));

    std::cout << "[CameraOffsetTrainerPage] Created new calibration set: " << m_currentSet.name << std::endl;
}

void CameraOffsetTrainerPage::onLoadCalibrationSet()
{
    QString baseDir = QString::fromStdString(
        std::filesystem::absolute(m_store.GetBasePath()).string());

    QString filePath = QFileDialog::getOpenFileName(
        this, "Load Calibration Set", baseDir, "Calibration Set Files (*.json)");

    if (filePath.isEmpty()) {
        return;
    }

    std::string stem = std::filesystem::path(filePath.toStdString()).stem().string();

    auto loaded = m_store.LoadCalibrationSet(stem);
    if (!loaded.has_value()) {
        QMessageBox::warning(this, "Load Failed",
            "Could not load calibration set: " + QString::fromStdString(stem));
        return;
    }

    m_currentSet = loaded.value();
    setCalibrationSetLoaded(true);

    m_calibrationSetLabel->setText(QString::fromStdString(m_currentSet.name));

    // Set camera combos to match loaded data
    int lowIdx = m_lowMagCameraCombo->findText(QString::fromStdString(m_currentSet.lowMagCameraId));
    if (lowIdx >= 0) {
        m_lowMagCameraCombo->setCurrentIndex(lowIdx);
    }
    int highIdx = m_highMagCameraCombo->findText(QString::fromStdString(m_currentSet.highMagCameraId));
    if (highIdx >= 0) {
        m_highMagCameraCombo->setCurrentIndex(highIdx);
    }

    // Set pneumatic slide combo to match loaded data
    if (m_currentSet.pneumaticSlideName.empty()) {
        m_pneumaticSlideCombo->setCurrentIndex(0);  // "(None)"
    } else {
        int pneuIdx = m_pneumaticSlideCombo->findText(QString::fromStdString(m_currentSet.pneumaticSlideName));
        if (pneuIdx >= 0) {
            m_pneumaticSlideCombo->setCurrentIndex(pneuIdx);
        }
    }

    updateCurrentCalibrationSet();
    std::cout << "[CameraOffsetTrainerPage] Loaded calibration set: " << m_currentSet.name << std::endl;
}

void CameraOffsetTrainerPage::onDeleteCalibrationSet()
{
    if (!m_hasLoadedSet) {
        return;
    }
    QString name = QString::fromStdString(m_currentSet.name);
    if (name.isEmpty()) {
        return;
    }

    auto result = QMessageBox::question(this, "Delete Calibration Set",
        "Delete calibration set '" + name + "'?\nThis cannot be undone.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result != QMessageBox::Yes) {
        return;
    }

    // Delete by source filename if available, otherwise by name
    std::string deleteKey = m_currentSet.sourceFilename.empty()
        ? m_currentSet.name : m_currentSet.sourceFilename;
    m_store.DeleteCalibrationSet(deleteKey);

    m_currentSet = CameraOffsetCalibrationSet();
    setCalibrationSetLoaded(false);
    m_calibrationSetLabel->setText("(none loaded)");
    m_contentStack->setCurrentIndex(0);
    std::cout << "[CameraOffsetTrainerPage] Deleted calibration set: " << name.toStdString() << std::endl;
}

void CameraOffsetTrainerPage::onSaveCalibrationSet()
{
    if (!m_hasLoadedSet) {
        QMessageBox::information(this, "Save", "No calibration set to save.");
        return;
    }

    // Sync camera selections
    m_currentSet.lowMagCameraId = m_lowMagCameraCombo->currentText().toStdString();
    m_currentSet.highMagCameraId = m_highMagCameraCombo->currentText().toStdString();
    QString pneuText = m_pneumaticSlideCombo->currentText();
    m_currentSet.pneumaticSlideName = (pneuText == "(None)") ? "" : pneuText.toStdString();
    m_currentSet.lastModifiedAt = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();

    if (m_store.SaveCalibrationSet(m_currentSet)) {
        m_statusLabel->setText(QString("Saved: %1")
            .arg(QString::fromStdString(m_currentSet.name)));
    } else {
        QMessageBox::warning(this, "Save Failed", "Could not save calibration set.");
    }
}

void CameraOffsetTrainerPage::updateCurrentCalibrationSet()
{
    if (!m_hasLoadedSet) return;

    QString offsetStr = m_currentSet.offsetValid
        ? QString("dX=%1  dY=%2  dZ=%3")
            .arg(m_currentSet.offsetX, 0, 'f', 4)
            .arg(m_currentSet.offsetY, 0, 'f', 4)
            .arg(m_currentSet.offsetZ, 0, 'f', 4)
        : "not calibrated";

    QString testStr = m_currentSet.lastTestResult.valid
        ? QString("devX=%1  devY=%2  devZ=%3")
            .arg(m_currentSet.lastTestResult.deviationX, 0, 'f', 4)
            .arg(m_currentSet.lastTestResult.deviationY, 0, 'f', 4)
            .arg(m_currentSet.lastTestResult.deviationZ, 0, 'f', 4)
        : "not tested";

    m_statusLabel->setText(QString("Set: %1\nOffset: %2\nTest: %3")
        .arg(QString::fromStdString(m_currentSet.name))
        .arg(offsetStr)
        .arg(testStr));

    // Push updated data into widgets if they are visible
    if (m_contentStack->currentWidget() == m_calibrateWidget) {
        m_calibrateWidget->SetCalibrationSet(&m_currentSet);
    }
    if (m_contentStack->currentWidget() == m_testWidget) {
        m_testWidget->SetCalibrationSet(&m_currentSet);
    }
}

void CameraOffsetTrainerPage::setCalibrationSetLoaded(bool loaded)
{
    m_hasLoadedSet = loaded;
    m_saveBtn->setEnabled(loaded);
    m_deleteBtn->setEnabled(loaded);

    if (!loaded) {
        m_statusLabel->setText("No calibration set loaded");
        m_navList->clearSelection();
    }
}

void CameraOffsetTrainerPage::updateStoreBasePath()
{
    if (m_context && m_context->productPositionManager &&
        m_context->productPositionManager->HasLoadedProduct()) {
        QString productFilePath = QString::fromStdString(
            m_context->productPositionManager->GetCurrentProductPath());
        QString productDir = QFileInfo(productFilePath).path();
        std::string path = (productDir + "/PR_Camera_Offset").toStdString();
        m_store.SetBasePath(path);
        std::cout << "[CameraOffsetTrainerPage] Store base path set to product: " << path << std::endl;
    } else {
        m_store.SetBasePath("config/SYSTEM/PR_Camera_Offset");
        std::cout << "[CameraOffsetTrainerPage] No product loaded, using default: config/SYSTEM/PR_Camera_Offset" << std::endl;
    }
}

void CameraOffsetTrainerPage::setTopBarVisible(bool visible)
{
    if (m_topBarWidget) {
        m_topBarWidget->setVisible(visible);
    }
}

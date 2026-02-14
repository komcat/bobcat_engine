// VisionProfileTesterWindow.cpp
#include "VisionProfileTesterWindow.h"
#include "VisionManager.h"
#include "DarkTheme.h"

// OpenCV plugins (excludes PatternMatching variants - they depend on VisionManagerPage)
#include "CircleDetectionPlugin.h"
#include "EdgeDetectionPlugin.h"
#include "FiducialDetectorPlugin.h"
#include "LineDetectorPlugin.h"
#include "CornerDetectorPlugin.h"
#include "ImageProcessingPlugin.h"
#include "CalibrationPlatePlugin.h"

#ifdef HAS_HALCON
#include "HalconCircleDetectionPlugin.h"
#include "HalconCircleDetectionRobustPlugin.h"
#include "HalconLineDetectionPlugin.h"
#include "HalconBeamDetectionPlugin.h"
#include "HalconD4SigmaBeamDetectionPlugin.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QToolBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QFont>
#include <QGroupBox>
#include <QComboBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QThread>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <algorithm>

VisionProfileTesterWindow::VisionProfileTesterWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Vision Profile Tester");
    resize(1400, 900);

    registerPlugins();
    setupUI();
    loadSettings();
    updateStatusBar("Ready. Load a vision profile and image folder to begin.");
}

// ============================================================================
// closeEvent
// ============================================================================

void VisionProfileTesterWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

// ============================================================================
// QSettings Persistence
// ============================================================================

void VisionProfileTesterWindow::saveSettings()
{
    QSettings settings("BobcatEngine", "VisionProfileTester");
    settings.setValue("geometry", saveGeometry());
    if (m_mainSplitter) {
        settings.setValue("splitterState", m_mainSplitter->saveState());
    }
    if (!m_loadedProfilePath.isEmpty()) {
        settings.setValue("lastProfilePath", m_loadedProfilePath);
    }
    QString folderText = m_folderPath ? m_folderPath->text() : QString();
    if (!folderText.isEmpty()) {
        settings.setValue("lastFolderPath", folderText);
    }
}

void VisionProfileTesterWindow::loadSettings()
{
    QSettings settings("BobcatEngine", "VisionProfileTester");

    // Restore geometry
    QByteArray geo = settings.value("geometry").toByteArray();
    if (!geo.isEmpty()) {
        restoreGeometry(geo);
    }

    // Restore splitter
    QByteArray splitterState = settings.value("splitterState").toByteArray();
    if (!splitterState.isEmpty() && m_mainSplitter) {
        m_mainSplitter->restoreState(splitterState);
    }

    // Auto-load last profile
    QString lastProfile = settings.value("lastProfilePath").toString();
    if (!lastProfile.isEmpty() && QFileInfo::exists(lastProfile)) {
        loadProfileJson(lastProfile);
    }

    // Auto-load last folder
    QString lastFolder = settings.value("lastFolderPath").toString();
    if (!lastFolder.isEmpty() && QDir(lastFolder).exists()) {
        loadFolder(lastFolder);
    }
}

// ============================================================================
// UI Setup
// ============================================================================

void VisionProfileTesterWindow::setupUI()
{
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // --- Top toolbar row ---
    auto* topRow = new QHBoxLayout();

    topRow->addWidget(new QLabel("Profile:"));
    m_profilePath = new QLineEdit();
    m_profilePath->setReadOnly(true);
    m_profilePath->setPlaceholderText("Select a vision profile JSON...");
    m_profilePath->setMinimumWidth(250);
    topRow->addWidget(m_profilePath, 1);

    auto* browseProfileBtn = new QPushButton("...");
    browseProfileBtn->setFixedWidth(30);
    connect(browseProfileBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onSelectProfile);
    topRow->addWidget(browseProfileBtn);

    topRow->addSpacing(12);

    topRow->addWidget(new QLabel("Folder:"));
    m_folderPath = new QLineEdit();
    m_folderPath->setReadOnly(true);
    m_folderPath->setPlaceholderText("Select image folder...");
    m_folderPath->setMinimumWidth(200);
    topRow->addWidget(m_folderPath, 1);

    auto* browseFolderBtn = new QPushButton("...");
    browseFolderBtn->setFixedWidth(30);
    connect(browseFolderBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onSelectFolder);
    topRow->addWidget(browseFolderBtn);

    mainLayout->addLayout(topRow);

    // --- Button row ---
    auto* buttonRow = new QHBoxLayout();

    m_runAllBtn = new QPushButton("Run All");
    m_runAllBtn->setEnabled(false);
    connect(m_runAllBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onRunAll);
    buttonRow->addWidget(m_runAllBtn);

    m_runSelectedBtn = new QPushButton("Run Selected");
    m_runSelectedBtn->setEnabled(false);
    connect(m_runSelectedBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onRunSelected);
    buttonRow->addWidget(m_runSelectedBtn);

    m_exportCsvBtn = new QPushButton("Export CSV");
    m_exportCsvBtn->setEnabled(false);
    connect(m_exportCsvBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onExportCsv);
    buttonRow->addWidget(m_exportCsvBtn);

    m_clearBtn = new QPushButton("Clear Results");
    m_clearBtn->setEnabled(false);
    connect(m_clearBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onClearResults);
    buttonRow->addWidget(m_clearBtn);

    buttonRow->addSpacing(8);

    // Profile file operation buttons
    m_reloadBtn = new QPushButton("Reload");
    m_reloadBtn->setEnabled(false);
    m_reloadBtn->setToolTip("Reload profile from disk");
    connect(m_reloadBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onReloadProfile);
    buttonRow->addWidget(m_reloadBtn);

    m_saveBtn = new QPushButton("Save");
    m_saveBtn->setEnabled(false);
    m_saveBtn->setToolTip("Save current settings to profile");
    connect(m_saveBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onSaveProfile);
    buttonRow->addWidget(m_saveBtn);

    m_saveAsBtn = new QPushButton("Save As");
    m_saveAsBtn->setEnabled(false);
    m_saveAsBtn->setToolTip("Save current settings to a new file");
    connect(m_saveAsBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onSaveAsProfile);
    buttonRow->addWidget(m_saveAsBtn);

    buttonRow->addStretch();

    // Settings toggle
    m_settingsToggleBtn = new QPushButton("Settings");
    m_settingsToggleBtn->setCheckable(true);
    m_settingsToggleBtn->setChecked(false);
    connect(m_settingsToggleBtn, &QPushButton::toggled, this, &VisionProfileTesterWindow::onToggleSettings);
    buttonRow->addWidget(m_settingsToggleBtn);

    buttonRow->addSpacing(8);

    m_calibrationLabel = new QLabel("Calibration: --");
    m_calibrationLabel->setStyleSheet("color: #a0a0a0;");
    buttonRow->addWidget(m_calibrationLabel);

    mainLayout->addLayout(buttonRow);

    // --- Progress bar ---
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFixedHeight(18);
    mainLayout->addWidget(m_progressBar);

    // --- Main content area (splitter) ---
    m_mainSplitter = new QSplitter(Qt::Horizontal);

    // Left: image list
    m_imageList = new QListWidget();
    m_imageList->setMinimumWidth(220);
    m_imageList->setMaximumWidth(350);
    connect(m_imageList, &QListWidget::currentRowChanged, this, &VisionProfileTesterWindow::onImageSelected);
    m_mainSplitter->addWidget(m_imageList);

    // Center: vertical split for image + results
    auto* rightSplitter = new QSplitter(Qt::Vertical);

    // Image display in scroll area
    m_imageScrollArea = new QScrollArea();
    m_imageScrollArea->setWidgetResizable(true);
    m_imageScrollArea->setAlignment(Qt::AlignCenter);
    m_imageDisplay = new QLabel("No image loaded");
    m_imageDisplay->setAlignment(Qt::AlignCenter);
    m_imageDisplay->setStyleSheet("QLabel { color: #a0a0a0; background-color: #1e1e1e; }");
    m_imageDisplay->setMinimumSize(400, 300);
    m_imageScrollArea->setWidget(m_imageDisplay);
    rightSplitter->addWidget(m_imageScrollArea);

    // Results text
    m_resultsText = new QTextEdit();
    m_resultsText->setReadOnly(true);
    m_resultsText->setFont(QFont("Consolas", 9));
    m_resultsText->setMaximumHeight(250);
    m_resultsText->setPlaceholderText("Select an image to see results...");
    rightSplitter->addWidget(m_resultsText);

    rightSplitter->setStretchFactor(0, 3);
    rightSplitter->setStretchFactor(1, 1);

    m_mainSplitter->addWidget(rightSplitter);

    // Right: settings panel (hidden by default)
    m_settingsPanel = createSettingsPanel();
    m_settingsPanel->setVisible(false);
    m_mainSplitter->addWidget(m_settingsPanel);

    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 0);

    mainLayout->addWidget(m_mainSplitter, 1);

    // --- Status bar ---
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel, 1);
}

// ============================================================================
// Settings Panel Creation
// ============================================================================

QWidget* VisionProfileTesterWindow::createSettingsPanel()
{
    auto* panel = new QWidget();
    panel->setMinimumWidth(320);
    panel->setMaximumWidth(400);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* tabs = new QTabWidget();
    tabs->addTab(createROITab(), "ROI");
    tabs->addTab(createSearchBoxTab(), "Search Box");
    tabs->addTab(createPluginTab(), "Plugin");
    layout->addWidget(tabs);

    return panel;
}

QWidget* VisionProfileTesterWindow::createROITab()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);

    m_roiEnabledCheck = new QCheckBox("Enable ROI");
    layout->addWidget(m_roiEnabledCheck);

    auto* form = new QFormLayout();

    m_roiStartX = new QSpinBox();
    m_roiStartX->setRange(0, 10000);
    m_roiStartX->setSingleStep(10);
    form->addRow("Start X:", m_roiStartX);

    m_roiStartY = new QSpinBox();
    m_roiStartY->setRange(0, 10000);
    m_roiStartY->setSingleStep(10);
    form->addRow("Start Y:", m_roiStartY);

    m_roiEndX = new QSpinBox();
    m_roiEndX->setRange(0, 10000);
    m_roiEndX->setSingleStep(10);
    form->addRow("End X:", m_roiEndX);

    m_roiEndY = new QSpinBox();
    m_roiEndY->setRange(0, 10000);
    m_roiEndY->setSingleStep(10);
    form->addRow("End Y:", m_roiEndY);

    layout->addLayout(form);

    m_roiSizeLabel = new QLabel("Size: 0 x 0 pixels");
    m_roiSizeLabel->setStyleSheet("color: #a0a0a0;");
    layout->addWidget(m_roiSizeLabel);

    // Connect value changes to update size label
    auto updateROI = [this]() { updateROISizeLabel(); };
    connect(m_roiStartX, QOverload<int>::of(&QSpinBox::valueChanged), this, updateROI);
    connect(m_roiStartY, QOverload<int>::of(&QSpinBox::valueChanged), this, updateROI);
    connect(m_roiEndX, QOverload<int>::of(&QSpinBox::valueChanged), this, updateROI);
    connect(m_roiEndY, QOverload<int>::of(&QSpinBox::valueChanged), this, updateROI);

    layout->addStretch();
    return widget;
}

QWidget* VisionProfileTesterWindow::createSearchBoxTab()
{
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(8, 8, 8, 8);

    m_searchEnabledCheck = new QCheckBox("Enable Search Box");
    layout->addWidget(m_searchEnabledCheck);

    auto* form = new QFormLayout();

    m_searchStartX = new QSpinBox();
    m_searchStartX->setRange(0, 10000);
    m_searchStartX->setSingleStep(10);
    form->addRow("Start X:", m_searchStartX);

    m_searchStartY = new QSpinBox();
    m_searchStartY->setRange(0, 10000);
    m_searchStartY->setSingleStep(10);
    form->addRow("Start Y:", m_searchStartY);

    m_searchEndX = new QSpinBox();
    m_searchEndX->setRange(0, 10000);
    m_searchEndX->setSingleStep(10);
    form->addRow("End X:", m_searchEndX);

    m_searchEndY = new QSpinBox();
    m_searchEndY->setRange(0, 10000);
    m_searchEndY->setSingleStep(10);
    form->addRow("End Y:", m_searchEndY);

    layout->addLayout(form);

    m_searchSizeLabel = new QLabel("Size: 0 x 0 pixels");
    m_searchSizeLabel->setStyleSheet("color: #a0a0a0;");
    layout->addWidget(m_searchSizeLabel);

    // Connect value changes to update size label
    auto updateSearch = [this]() { updateSearchBoxSizeLabel(); };
    connect(m_searchStartX, QOverload<int>::of(&QSpinBox::valueChanged), this, updateSearch);
    connect(m_searchStartY, QOverload<int>::of(&QSpinBox::valueChanged), this, updateSearch);
    connect(m_searchEndX, QOverload<int>::of(&QSpinBox::valueChanged), this, updateSearch);
    connect(m_searchEndY, QOverload<int>::of(&QSpinBox::valueChanged), this, updateSearch);

    layout->addStretch();
    return widget;
}

QWidget* VisionProfileTesterWindow::createPluginTab()
{
    auto* widget = new QWidget();
    auto* outerLayout = new QVBoxLayout(widget);
    outerLayout->setContentsMargins(8, 8, 8, 8);

    // Calibration section at top
    auto* calGroup = new QGroupBox("Calibration");
    auto* calForm = new QFormLayout(calGroup);

    m_calXSpin = new QDoubleSpinBox();
    m_calXSpin->setRange(0.0001, 1.0);
    m_calXSpin->setSingleStep(0.00001);
    m_calXSpin->setDecimals(5);
    m_calXSpin->setValue(0.00248);
    m_calXSpin->setSuffix(" mm/px");
    calForm->addRow("Pixel to mm X:", m_calXSpin);

    m_calYSpin = new QDoubleSpinBox();
    m_calYSpin->setRange(0.0001, 1.0);
    m_calYSpin->setSingleStep(0.00001);
    m_calYSpin->setDecimals(5);
    m_calYSpin->setValue(0.00248);
    m_calYSpin->setSuffix(" mm/px");
    calForm->addRow("Pixel to mm Y:", m_calYSpin);

    outerLayout->addWidget(calGroup);

    // Scrollable area for plugin parameters
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContent = new QWidget();
    m_pluginParamsLayout = new QVBoxLayout(scrollContent);
    m_pluginParamsLayout->setContentsMargins(0, 0, 0, 0);
    m_pluginParamsLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    outerLayout->addWidget(scrollArea, 1);

    // Apply button
    auto* applyBtn = new QPushButton("Apply");
    applyBtn->setToolTip("Apply parameter changes and update VisionManager");
    connect(applyBtn, &QPushButton::clicked, this, &VisionProfileTesterWindow::onApplyParams);
    outerLayout->addWidget(applyBtn);

    return widget;
}

void VisionProfileTesterWindow::updateROISizeLabel()
{
    int w = std::abs(m_roiEndX->value() - m_roiStartX->value());
    int h = std::abs(m_roiEndY->value() - m_roiStartY->value());
    m_roiSizeLabel->setText(QString("Size: %1 x %2 pixels").arg(w).arg(h));
}

void VisionProfileTesterWindow::updateSearchBoxSizeLabel()
{
    int w = std::abs(m_searchEndX->value() - m_searchStartX->value());
    int h = std::abs(m_searchEndY->value() - m_searchStartY->value());
    m_searchSizeLabel->setText(QString("Size: %1 x %2 pixels").arg(w).arg(h));
}

// ============================================================================
// Settings Panel Toggle
// ============================================================================

void VisionProfileTesterWindow::onToggleSettings(bool checked)
{
    m_settingsPanel->setVisible(checked);
}

// ============================================================================
// Plugin Parameters UI Generation
// ============================================================================

void VisionProfileTesterWindow::buildPluginParamsUI()
{
    // Clear existing widgets
    m_paramWidgets.clear();
    while (m_pluginParamsLayout->count() > 0) {
        auto* item = m_pluginParamsLayout->takeAt(0);
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    auto plugin = VisionManager::instance().getPlugin(m_pluginName);
    if (!plugin) {
        m_pluginParamsLayout->addWidget(new QLabel("No plugin loaded"));
        m_pluginParamsLayout->addStretch();
        return;
    }

    auto schema = plugin->getParameterSchema();

    // Ungrouped parameters first
    auto ungrouped = schema.getUngroupedParameters();
    if (!ungrouped.empty()) {
        auto* form = new QFormLayout();
        for (const auto& desc : ungrouped) {
            QWidget* w = nullptr;
            switch (desc.type) {
            case ParameterDescriptor::Type::Int: {
                auto* spin = new QSpinBox();
                spin->setRange(static_cast<int>(desc.minValue), static_cast<int>(desc.maxValue));
                spin->setSingleStep(static_cast<int>(desc.step));
                spin->setValue(static_cast<int>(desc.defaultValue));
                if (!desc.unit.empty()) spin->setSuffix(QString(" %1").arg(QString::fromStdString(desc.unit)));
                if (!desc.tooltip.empty()) spin->setToolTip(QString::fromStdString(desc.tooltip));
                w = spin;
                break;
            }
            case ParameterDescriptor::Type::Double: {
                auto* spin = new QDoubleSpinBox();
                spin->setRange(desc.minValue, desc.maxValue);
                spin->setSingleStep(desc.step);
                spin->setDecimals(desc.step < 0.01 ? 5 : (desc.step < 0.1 ? 3 : 2));
                spin->setValue(desc.defaultValue);
                if (!desc.unit.empty()) spin->setSuffix(QString(" %1").arg(QString::fromStdString(desc.unit)));
                if (!desc.tooltip.empty()) spin->setToolTip(QString::fromStdString(desc.tooltip));
                w = spin;
                break;
            }
            case ParameterDescriptor::Type::Bool: {
                auto* check = new QCheckBox();
                check->setChecked(desc.defaultValue > 0.5);
                if (!desc.tooltip.empty()) check->setToolTip(QString::fromStdString(desc.tooltip));
                w = check;
                break;
            }
            case ParameterDescriptor::Type::Choice: {
                auto* combo = new QComboBox();
                for (const auto& choice : desc.choices) {
                    combo->addItem(QString::fromStdString(choice));
                }
                combo->setCurrentIndex(static_cast<int>(desc.defaultValue));
                if (!desc.tooltip.empty()) combo->setToolTip(QString::fromStdString(desc.tooltip));
                w = combo;
                break;
            }
            case ParameterDescriptor::Type::String: {
                auto* edit = new QLineEdit();
                if (!desc.tooltip.empty()) edit->setToolTip(QString::fromStdString(desc.tooltip));
                w = edit;
                break;
            }
            }
            if (w) {
                form->addRow(QString::fromStdString(desc.displayName) + ":", w);
                m_paramWidgets[desc.key] = w;
            }
        }
        auto* container = new QWidget();
        container->setLayout(form);
        m_pluginParamsLayout->addWidget(container);
    }

    // Grouped parameters
    auto groups = schema.getGroups();
    for (const auto& group : groups) {
        auto* groupBox = new QGroupBox(QString::fromStdString(group));
        auto* form = new QFormLayout(groupBox);

        auto params = schema.getParametersByGroup(group);
        for (const auto& desc : params) {
            QWidget* w = nullptr;
            switch (desc.type) {
            case ParameterDescriptor::Type::Int: {
                auto* spin = new QSpinBox();
                spin->setRange(static_cast<int>(desc.minValue), static_cast<int>(desc.maxValue));
                spin->setSingleStep(static_cast<int>(desc.step));
                spin->setValue(static_cast<int>(desc.defaultValue));
                if (!desc.unit.empty()) spin->setSuffix(QString(" %1").arg(QString::fromStdString(desc.unit)));
                if (!desc.tooltip.empty()) spin->setToolTip(QString::fromStdString(desc.tooltip));
                w = spin;
                break;
            }
            case ParameterDescriptor::Type::Double: {
                auto* spin = new QDoubleSpinBox();
                spin->setRange(desc.minValue, desc.maxValue);
                spin->setSingleStep(desc.step);
                spin->setDecimals(desc.step < 0.01 ? 5 : (desc.step < 0.1 ? 3 : 2));
                spin->setValue(desc.defaultValue);
                if (!desc.unit.empty()) spin->setSuffix(QString(" %1").arg(QString::fromStdString(desc.unit)));
                if (!desc.tooltip.empty()) spin->setToolTip(QString::fromStdString(desc.tooltip));
                w = spin;
                break;
            }
            case ParameterDescriptor::Type::Bool: {
                auto* check = new QCheckBox();
                check->setChecked(desc.defaultValue > 0.5);
                if (!desc.tooltip.empty()) check->setToolTip(QString::fromStdString(desc.tooltip));
                w = check;
                break;
            }
            case ParameterDescriptor::Type::Choice: {
                auto* combo = new QComboBox();
                for (const auto& choice : desc.choices) {
                    combo->addItem(QString::fromStdString(choice));
                }
                combo->setCurrentIndex(static_cast<int>(desc.defaultValue));
                if (!desc.tooltip.empty()) combo->setToolTip(QString::fromStdString(desc.tooltip));
                w = combo;
                break;
            }
            case ParameterDescriptor::Type::String: {
                auto* edit = new QLineEdit();
                if (!desc.tooltip.empty()) edit->setToolTip(QString::fromStdString(desc.tooltip));
                w = edit;
                break;
            }
            }
            if (w) {
                form->addRow(QString::fromStdString(desc.displayName) + ":", w);
                m_paramWidgets[desc.key] = w;
            }
        }

        m_pluginParamsLayout->addWidget(groupBox);
    }

    m_pluginParamsLayout->addStretch();
}

nlohmann::json VisionProfileTesterWindow::collectPluginParamsFromUI()
{
    nlohmann::json params = m_pluginParams; // start from current params

    auto plugin = VisionManager::instance().getPlugin(m_pluginName);
    if (!plugin) return params;

    auto schema = plugin->getParameterSchema();

    for (const auto& desc : schema.parameters) {
        auto it = m_paramWidgets.find(desc.key);
        if (it == m_paramWidgets.end()) continue;

        QWidget* w = it->second;
        switch (desc.type) {
        case ParameterDescriptor::Type::Int: {
            auto* spin = qobject_cast<QSpinBox*>(w);
            if (spin) params[desc.key] = spin->value();
            break;
        }
        case ParameterDescriptor::Type::Double: {
            auto* spin = qobject_cast<QDoubleSpinBox*>(w);
            if (spin) params[desc.key] = spin->value();
            break;
        }
        case ParameterDescriptor::Type::Bool: {
            auto* check = qobject_cast<QCheckBox*>(w);
            if (check) params[desc.key] = check->isChecked();
            break;
        }
        case ParameterDescriptor::Type::Choice: {
            auto* combo = qobject_cast<QComboBox*>(w);
            if (combo && !desc.choiceValues.empty() &&
                combo->currentIndex() < static_cast<int>(desc.choiceValues.size())) {
                params[desc.key] = desc.choiceValues[static_cast<size_t>(combo->currentIndex())];
            }
            break;
        }
        case ParameterDescriptor::Type::String: {
            auto* edit = qobject_cast<QLineEdit*>(w);
            if (edit) params[desc.key] = edit->text().toStdString();
            break;
        }
        }
    }

    return params;
}

void VisionProfileTesterWindow::onApplyParams()
{
    if (!m_profileLoaded) return;

    // Update calibration
    m_pixelToMmX = m_calXSpin->value();
    m_pixelToMmY = m_calYSpin->value();
    VisionManager::instance().setPixelToMmNoSave(m_pixelToMmX, m_pixelToMmY);

    // Update ROI
    VisionROI roi(m_roiStartX->value(), m_roiStartY->value(),
                  m_roiEndX->value(), m_roiEndY->value());
    roi.setEnabled(m_roiEnabledCheck->isChecked());
    VisionManager::instance().setROI(roi);
    VisionManager::instance().setROIEnabled(roi.isEnabled());

    // Update plugin params
    m_pluginParams = collectPluginParamsFromUI();

    auto plugin = VisionManager::instance().getPlugin(m_pluginName);
    if (plugin) {
        plugin->setParameters(m_pluginParams);
    }

    // Update profile JSON in memory
    m_profileJson["camera"]["pixelToMmX"] = m_pixelToMmX;
    m_profileJson["camera"]["pixelToMmY"] = m_pixelToMmY;
    m_profileJson["roi"] = roi.toJson();
    m_profileJson["searchBox"]["enabled"] = m_searchEnabledCheck->isChecked();
    m_profileJson["searchBox"]["startX"] = m_searchStartX->value();
    m_profileJson["searchBox"]["startY"] = m_searchStartY->value();
    m_profileJson["searchBox"]["endX"] = m_searchEndX->value();
    m_profileJson["searchBox"]["endY"] = m_searchEndY->value();
    m_profileJson["plugin"]["parameters"] = m_pluginParams;

    m_calibrationLabel->setText(
        QString("Calibration: %1 x %2 mm/px | Plugin: %3")
            .arg(m_pixelToMmX, 0, 'f', 5)
            .arg(m_pixelToMmY, 0, 'f', 5)
            .arg(m_pluginName));

    updateStatusBar("Settings applied.");
}

void VisionProfileTesterWindow::populateSettingsFromProfile()
{
    // Block signals during population to avoid triggering callbacks
    m_roiStartX->blockSignals(true);
    m_roiStartY->blockSignals(true);
    m_roiEndX->blockSignals(true);
    m_roiEndY->blockSignals(true);
    m_searchStartX->blockSignals(true);
    m_searchStartY->blockSignals(true);
    m_searchEndX->blockSignals(true);
    m_searchEndY->blockSignals(true);

    // ROI
    if (m_profileJson.contains("roi")) {
        const auto& roi = m_profileJson["roi"];
        m_roiEnabledCheck->setChecked(roi.value("enabled", false));
        m_roiStartX->setValue(roi.value("startX", 0));
        m_roiStartY->setValue(roi.value("startY", 0));
        m_roiEndX->setValue(roi.value("endX", 0));
        m_roiEndY->setValue(roi.value("endY", 0));
    } else {
        m_roiEnabledCheck->setChecked(false);
        m_roiStartX->setValue(0);
        m_roiStartY->setValue(0);
        m_roiEndX->setValue(0);
        m_roiEndY->setValue(0);
    }

    // SearchBox
    if (m_profileJson.contains("searchBox")) {
        const auto& sb = m_profileJson["searchBox"];
        m_searchEnabledCheck->setChecked(sb.value("enabled", false));
        m_searchStartX->setValue(sb.value("startX", 0));
        m_searchStartY->setValue(sb.value("startY", 0));
        m_searchEndX->setValue(sb.value("endX", 0));
        m_searchEndY->setValue(sb.value("endY", 0));
    } else {
        m_searchEnabledCheck->setChecked(false);
        m_searchStartX->setValue(0);
        m_searchStartY->setValue(0);
        m_searchEndX->setValue(0);
        m_searchEndY->setValue(0);
    }

    // Calibration
    m_calXSpin->setValue(m_pixelToMmX);
    m_calYSpin->setValue(m_pixelToMmY);

    // Unblock signals
    m_roiStartX->blockSignals(false);
    m_roiStartY->blockSignals(false);
    m_roiEndX->blockSignals(false);
    m_roiEndY->blockSignals(false);
    m_searchStartX->blockSignals(false);
    m_searchStartY->blockSignals(false);
    m_searchEndX->blockSignals(false);
    m_searchEndY->blockSignals(false);

    // Update size labels
    updateROISizeLabel();
    updateSearchBoxSizeLabel();

    // Build plugin params UI from schema, then set values from profile
    buildPluginParamsUI();

    // Set param widget values from profile
    if (m_profileJson.contains("plugin") && m_profileJson["plugin"].contains("parameters")) {
        const auto& params = m_profileJson["plugin"]["parameters"];
        auto plugin = VisionManager::instance().getPlugin(m_pluginName);
        if (plugin) {
            auto schema = plugin->getParameterSchema();
            for (const auto& desc : schema.parameters) {
                auto widgetIt = m_paramWidgets.find(desc.key);
                if (widgetIt == m_paramWidgets.end()) continue;
                if (!params.contains(desc.key)) continue;

                QWidget* w = widgetIt->second;
                switch (desc.type) {
                case ParameterDescriptor::Type::Int: {
                    auto* spin = qobject_cast<QSpinBox*>(w);
                    if (spin) spin->setValue(params[desc.key].get<int>());
                    break;
                }
                case ParameterDescriptor::Type::Double: {
                    auto* spin = qobject_cast<QDoubleSpinBox*>(w);
                    if (spin) spin->setValue(params[desc.key].get<double>());
                    break;
                }
                case ParameterDescriptor::Type::Bool: {
                    auto* check = qobject_cast<QCheckBox*>(w);
                    if (check) check->setChecked(params[desc.key].get<bool>());
                    break;
                }
                case ParameterDescriptor::Type::Choice: {
                    auto* combo = qobject_cast<QComboBox*>(w);
                    if (combo && !desc.choiceValues.empty()) {
                        int val = params[desc.key].get<int>();
                        for (size_t i = 0; i < desc.choiceValues.size(); ++i) {
                            if (desc.choiceValues[i] == val) {
                                combo->setCurrentIndex(static_cast<int>(i));
                                break;
                            }
                        }
                    }
                    break;
                }
                case ParameterDescriptor::Type::String: {
                    auto* edit = qobject_cast<QLineEdit*>(w);
                    if (edit) edit->setText(QString::fromStdString(params[desc.key].get<std::string>()));
                    break;
                }
                }
            }
        }
    }
}

// ============================================================================
// Plugin Registration
// ============================================================================

void VisionProfileTesterWindow::registerPlugins()
{
    std::cout << "[VisionProfileTester] Registering vision plugins..." << std::endl;

    auto& vm = VisionManager::instance();

    // OpenCV plugins
    vm.registerPlugin(std::make_shared<CircleDetectionPlugin>());
    vm.registerPlugin(std::make_shared<EdgeDetectionPlugin>());
    vm.registerPlugin(std::make_shared<FiducialDetectorPlugin>());
    vm.registerPlugin(std::make_shared<LineDetectorPlugin>());
    vm.registerPlugin(std::make_shared<CornerDetectorPlugin>());
    vm.registerPlugin(std::make_shared<ImageProcessingPlugin>());
    vm.registerPlugin(std::make_shared<CalibrationPlatePlugin>());

#ifdef HAS_HALCON
    vm.registerPlugin(std::make_shared<HalconCircleDetectionPlugin>());
    vm.registerPlugin(std::make_shared<HalconCircleDetectionRobustPlugin>());
    vm.registerPlugin(std::make_shared<HalconLineDetectionPlugin>());
    vm.registerPlugin(std::make_shared<HalconBeamDetectionPlugin>());
    vm.registerPlugin(std::make_shared<HalconD4SigmaBeamDetectionPlugin>());
#endif

    std::cout << "[VisionProfileTester] Registered " << vm.getPluginCount() << " plugins" << std::endl;
}

// ============================================================================
// Profile Loading
// ============================================================================

void VisionProfileTesterWindow::loadProfileJson(const QString& path)
{
    std::ifstream file(path.toStdString());
    if (!file.is_open()) {
        QMessageBox::warning(this, "Error", "Cannot open profile: " + path);
        return;
    }

    nlohmann::json profileJson;
    try {
        file >> profileJson;
    } catch (const nlohmann::json::parse_error& e) {
        QMessageBox::warning(this, "Parse Error",
            QString("Failed to parse profile JSON:\n%1").arg(e.what()));
        return;
    }

    // Extract plugin name and parameters
    if (profileJson.contains("plugin")) {
        const auto& pluginObj = profileJson["plugin"];
        m_pluginName = QString::fromStdString(pluginObj.value("name", ""));
        m_pluginParams = pluginObj.value("parameters", nlohmann::json::object());
    } else {
        QMessageBox::warning(this, "Error", "Profile JSON missing 'plugin' section");
        return;
    }

    // Verify plugin exists
    auto plugin = VisionManager::instance().getPlugin(m_pluginName);
    if (!plugin) {
        QMessageBox::warning(this, "Plugin Not Found",
            QString("Plugin '%1' is not registered.\n\nAvailable plugins:\n%2")
                .arg(m_pluginName)
                .arg(VisionManager::instance().getAllPluginNames().join("\n")));
        return;
    }

    // Extract calibration
    if (profileJson.contains("camera")) {
        const auto& cam = profileJson["camera"];
        m_pixelToMmX = cam.value("pixelToMmX", 0.00248);
        m_pixelToMmY = cam.value("pixelToMmY", 0.00248);
    }
    VisionManager::instance().setPixelToMmNoSave(m_pixelToMmX, m_pixelToMmY);

    // Extract ROI
    if (profileJson.contains("roi")) {
        auto roi = VisionROI::fromJson(profileJson["roi"]);
        VisionManager::instance().setROI(roi);
        VisionManager::instance().setROIEnabled(roi.isEnabled());
    } else {
        VisionManager::instance().setROIEnabled(false);
    }

    // Store full JSON for round-trip save
    m_profileJson = profileJson;
    m_loadedProfilePath = path;

    m_profileLoaded = true;
    m_profilePath->setText(path);
    m_calibrationLabel->setText(
        QString("Calibration: %1 x %2 mm/px | Plugin: %3")
            .arg(m_pixelToMmX, 0, 'f', 5)
            .arg(m_pixelToMmY, 0, 'f', 5)
            .arg(m_pluginName));

    // Enable buttons
    bool canRun = m_profileLoaded && !m_images.empty();
    m_runAllBtn->setEnabled(canRun);
    m_runSelectedBtn->setEnabled(canRun);
    m_reloadBtn->setEnabled(true);
    m_saveBtn->setEnabled(true);
    m_saveAsBtn->setEnabled(true);

    // Populate settings panel from profile
    populateSettingsFromProfile();

    updateStatusBar(QString("Profile loaded: %1 (%2)")
        .arg(m_pluginName)
        .arg(QFileInfo(path).fileName()));
}

// ============================================================================
// Folder Selection
// ============================================================================

void VisionProfileTesterWindow::onSelectProfile()
{
    // Use last profile directory as starting point
    QString startDir;
    if (!m_loadedProfilePath.isEmpty()) {
        startDir = QFileInfo(m_loadedProfilePath).absolutePath();
    }

    QString path = QFileDialog::getOpenFileName(this, "Select Vision Profile",
        startDir, "JSON Files (*.json);;All Files (*.*)");
    if (path.isEmpty()) return;
    loadProfileJson(path);
}

void VisionProfileTesterWindow::onSelectFolder()
{
    // Use last folder path as starting point
    QString startDir = m_folderPath->text();

    QString dir = QFileDialog::getExistingDirectory(this, "Select Image Folder",
        startDir.isEmpty() ? QString() : startDir);
    if (dir.isEmpty()) return;
    loadFolder(dir);
}

void VisionProfileTesterWindow::loadFolder(const QString& path)
{
    m_folderPath->setText(path);
    m_images.clear();
    m_imageList->clear();

    // Scan for image files
    QDir folder(path);
    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.bmp" << "*.tif" << "*.tiff";
    QStringList files = folder.entryList(filters, QDir::Files, QDir::Name);

    for (const auto& file : files) {
        ImageEntry entry;
        entry.filePath = folder.absoluteFilePath(file);
        m_images.push_back(entry);
        m_imageList->addItem(file);
    }

    bool canRun = m_profileLoaded && !m_images.empty();
    m_runAllBtn->setEnabled(canRun);
    m_runSelectedBtn->setEnabled(canRun);
    m_exportCsvBtn->setEnabled(false);

    updateStatusBar(QString("Loaded %1 images from folder").arg(m_images.size()));
}

// ============================================================================
// Profile File Operations
// ============================================================================

void VisionProfileTesterWindow::onReloadProfile()
{
    if (m_loadedProfilePath.isEmpty()) return;
    loadProfileJson(m_loadedProfilePath);
    updateStatusBar(QString("Profile reloaded from disk: %1")
        .arg(QFileInfo(m_loadedProfilePath).fileName()));
}

void VisionProfileTesterWindow::onSaveProfile()
{
    if (m_loadedProfilePath.isEmpty() || m_profileJson.is_null()) return;

    // Apply current UI settings into m_profileJson before saving
    onApplyParams();

    std::ofstream file(m_loadedProfilePath.toStdString());
    if (!file.is_open()) {
        QMessageBox::warning(this, "Error", "Cannot open file for writing: " + m_loadedProfilePath);
        return;
    }

    file << m_profileJson.dump(4);
    file.close();

    updateStatusBar(QString("Profile saved: %1").arg(QFileInfo(m_loadedProfilePath).fileName()));
}

void VisionProfileTesterWindow::onSaveAsProfile()
{
    if (m_profileJson.is_null()) return;

    QString startDir;
    if (!m_loadedProfilePath.isEmpty()) {
        startDir = QFileInfo(m_loadedProfilePath).absolutePath();
    }

    QString path = QFileDialog::getSaveFileName(this, "Save Profile As",
        startDir, "JSON Files (*.json);;All Files (*.*)");
    if (path.isEmpty()) return;

    // Apply current UI settings into m_profileJson before saving
    onApplyParams();

    std::ofstream file(path.toStdString());
    if (!file.is_open()) {
        QMessageBox::warning(this, "Error", "Cannot open file for writing: " + path);
        return;
    }

    file << m_profileJson.dump(4);
    file.close();

    // Update to new path
    m_loadedProfilePath = path;
    m_profilePath->setText(path);

    updateStatusBar(QString("Profile saved as: %1").arg(QFileInfo(path).fileName()));
}

// ============================================================================
// Processing
// ============================================================================

void VisionProfileTesterWindow::processImage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) return;

    auto& entry = m_images[static_cast<size_t>(index)];

    // Load image from disk
    cv::Mat inputImage = cv::imread(entry.filePath.toStdString(), cv::IMREAD_UNCHANGED);
    if (inputImage.empty()) {
        entry.result.success = false;
        entry.result.errorMessage = "Failed to load image";
        entry.processed = true;
        return;
    }

    // Set input image on VisionManager
    VisionManager::instance().setInputImage(inputImage);

    // Execute plugin with profile parameters
    entry.result = VisionManager::instance().executePlugin(m_pluginName, m_pluginParams);

    // Store the output image (with overlays rendered by VisionManager)
    entry.outputImage = VisionManager::instance().getOutputImage();
    if (entry.outputImage.empty()) {
        entry.outputImage = inputImage.clone();
    }

    entry.processed = true;
}

void VisionProfileTesterWindow::onRunAll()
{
    if (!m_profileLoaded || m_images.empty()) return;

    resetAllListItems();

    m_runAllBtn->setEnabled(false);
    m_runSelectedBtn->setEnabled(false);

    int total = static_cast<int>(m_images.size());
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(0);

    int successCount = 0;

    for (int i = 0; i < total; ++i) {
        updateStatusBar(QString("Processing %1/%2: %3")
            .arg(i + 1).arg(total)
            .arg(QFileInfo(m_images[static_cast<size_t>(i)].filePath).fileName()));

        processImage(i);

        // Update list item with result indicator
        auto* item = m_imageList->item(i);
        if (m_images[static_cast<size_t>(i)].result.success) {
            item->setForeground(QColor("#27ae60"));
            item->setText(QFileInfo(m_images[static_cast<size_t>(i)].filePath).fileName() + "  [OK]");
            ++successCount;
        } else {
            item->setForeground(QColor("#e74c3c"));
            item->setText(QFileInfo(m_images[static_cast<size_t>(i)].filePath).fileName() + "  [FAIL]");
        }

        m_progressBar->setValue(i + 1);

        // Show result image and text for each image so user can observe
        m_imageList->setCurrentRow(i);
        displayResult(i);
        QApplication::processEvents();
        QThread::msleep(300);
    }

    m_progressBar->setVisible(false);
    m_runAllBtn->setEnabled(true);
    m_runSelectedBtn->setEnabled(true);
    m_exportCsvBtn->setEnabled(true);
    m_clearBtn->setEnabled(true);

    updateStatusBar(QString("Done. %1/%2 succeeded (plugin: %3)")
        .arg(successCount).arg(total).arg(m_pluginName));
}

void VisionProfileTesterWindow::onRunSelected()
{
    int row = m_imageList->currentRow();
    if (row < 0 || !m_profileLoaded) {
        QMessageBox::information(this, "No Selection", "Select an image from the list first.");
        return;
    }

    updateStatusBar(QString("Processing: %1")
        .arg(QFileInfo(m_images[static_cast<size_t>(row)].filePath).fileName()));

    processImage(row);

    // Update list item
    auto* item = m_imageList->item(row);
    if (m_images[static_cast<size_t>(row)].result.success) {
        item->setForeground(QColor("#27ae60"));
        item->setText(QFileInfo(m_images[static_cast<size_t>(row)].filePath).fileName() + "  [OK]");
    } else {
        item->setForeground(QColor("#e74c3c"));
        item->setText(QFileInfo(m_images[static_cast<size_t>(row)].filePath).fileName() + "  [FAIL]");
    }

    displayResult(row);
    m_exportCsvBtn->setEnabled(true);
    m_clearBtn->setEnabled(true);

    updateStatusBar(QString("Processed: %1 - %2")
        .arg(QFileInfo(m_images[static_cast<size_t>(row)].filePath).fileName())
        .arg(m_images[static_cast<size_t>(row)].result.success ? "SUCCESS" : "FAILED"));
}

// ============================================================================
// Display
// ============================================================================

void VisionProfileTesterWindow::onImageSelected(int row)
{
    if (row < 0 || row >= static_cast<int>(m_images.size())) {
        m_imageDisplay->setText("No image selected");
        m_resultsText->clear();
        return;
    }

    displayResult(row);
}

void VisionProfileTesterWindow::displayResult(int index)
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) return;

    const auto& entry = m_images[static_cast<size_t>(index)];

    // Display image
    cv::Mat displayMat;
    if (entry.processed && !entry.outputImage.empty()) {
        displayMat = entry.outputImage;
    } else {
        // Load raw image for preview
        displayMat = cv::imread(entry.filePath.toStdString(), cv::IMREAD_UNCHANGED);
    }

    if (!displayMat.empty()) {
        // Convert cv::Mat to QImage
        cv::Mat rgbMat;
        if (displayMat.channels() == 1) {
            cv::cvtColor(displayMat, rgbMat, cv::COLOR_GRAY2RGB);
        } else if (displayMat.channels() == 4) {
            cv::cvtColor(displayMat, rgbMat, cv::COLOR_BGRA2RGB);
        } else {
            cv::cvtColor(displayMat, rgbMat, cv::COLOR_BGR2RGB);
        }

        QImage qimg(rgbMat.data, rgbMat.cols, rgbMat.rows,
                     static_cast<int>(rgbMat.step), QImage::Format_RGB888);

        // Scale to fit the scroll area while maintaining aspect ratio
        QSize availSize = m_imageScrollArea->viewport()->size() - QSize(4, 4);
        QPixmap pixmap = QPixmap::fromImage(qimg.copy());  // deep copy before rgbMat goes out of scope
        if (pixmap.width() > availSize.width() || pixmap.height() > availSize.height()) {
            pixmap = pixmap.scaled(availSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        m_imageDisplay->setPixmap(pixmap);
        m_imageDisplay->setStyleSheet("QLabel { background-color: #1e1e1e; }");
    } else {
        m_imageDisplay->setText("Failed to load image");
    }

    // Display results text
    if (entry.processed) {
        QString text;
        text += QString::fromStdString(entry.result.getSummary());

        // Add file info
        text += "\n--- File ---\n";
        text += "Path: " + entry.filePath + "\n";

        m_resultsText->setText(text);
    } else {
        m_resultsText->setText("Not yet processed. Click 'Run All' or 'Run Selected'.");
    }
}

// ============================================================================
// CSV Export
// ============================================================================

void VisionProfileTesterWindow::onExportCsv()
{
    QString savePath = QFileDialog::getSaveFileName(this, "Export CSV",
        "vision_results.csv", "CSV Files (*.csv);;All Files (*.*)");
    if (savePath.isEmpty()) return;

    std::ofstream csvFile(savePath.toStdString());
    if (!csvFile.is_open()) {
        QMessageBox::warning(this, "Error", "Cannot open file for writing: " + savePath);
        return;
    }

    // Collect all unique measurement keys across all results
    std::vector<std::string> measurementKeys;
    for (const auto& entry : m_images) {
        if (!entry.processed) continue;
        for (const auto& kv : entry.result.measurements) {
            const auto& key = kv.first;
            if (std::find(measurementKeys.begin(), measurementKeys.end(), key) == measurementKeys.end()) {
                measurementKeys.push_back(key);
            }
        }
    }
    std::sort(measurementKeys.begin(), measurementKeys.end());

    // Write header
    csvFile << "filename,success,processing_time_ms,beam_count,point_count,circle_count,line_count";
    for (const auto& key : measurementKeys) {
        csvFile << "," << key;
    }

    // Beam-specific columns (up to first 4 beams)
    int maxBeams = 0;
    for (const auto& entry : m_images) {
        if (entry.processed) {
            int bc = static_cast<int>(entry.result.beams.size());
            if (bc > maxBeams) maxBeams = bc;
        }
    }
    if (maxBeams > 4) maxBeams = 4;  // cap for readability

    for (int b = 0; b < maxBeams; ++b) {
        csvFile << ",beam" << b << "_cx_px,beam" << b << "_cy_px"
                << ",beam" << b << "_fwhm_major_px,beam" << b << "_fwhm_minor_px"
                << ",beam" << b << "_mean_intensity";
    }
    csvFile << "\n";

    // Write data rows
    for (const auto& entry : m_images) {
        if (!entry.processed) continue;

        const auto& r = entry.result;
        csvFile << QFileInfo(entry.filePath).fileName().toStdString()
                << "," << (r.success ? "TRUE" : "FALSE")
                << "," << r.processingTimeMs
                << "," << r.beams.size()
                << "," << r.points.size()
                << "," << r.circles.size()
                << "," << r.lines.size();

        // Measurement columns
        for (const auto& key : measurementKeys) {
            auto it = r.measurements.find(key);
            if (it != r.measurements.end()) {
                csvFile << "," << it->second;
            } else {
                csvFile << ",";
            }
        }

        // Beam detail columns
        for (int b = 0; b < maxBeams; ++b) {
            if (b < static_cast<int>(r.beams.size())) {
                const auto& beam = r.beams[static_cast<size_t>(b)];
                csvFile << "," << beam.centerPx.x << "," << beam.centerPx.y
                        << "," << beam.fwhmMajorPx << "," << beam.fwhmMinorPx
                        << "," << beam.meanIntensity;
            } else {
                csvFile << ",,,,,";
            }
        }

        csvFile << "\n";
    }

    csvFile.close();

    int processedCount = 0;
    for (const auto& entry : m_images) {
        if (entry.processed) ++processedCount;
    }

    updateStatusBar(QString("Exported %1 results to %2").arg(processedCount).arg(savePath));
    QMessageBox::information(this, "Export Complete",
        QString("Exported %1 results to:\n%2").arg(processedCount).arg(savePath));
}

// ============================================================================
// Status Bar
// ============================================================================

void VisionProfileTesterWindow::updateStatusBar(const QString& message)
{
    m_statusLabel->setText(message);
}

// ============================================================================
// Clear / Reset
// ============================================================================

void VisionProfileTesterWindow::resetAllListItems()
{
    for (int i = 0; i < m_imageList->count(); ++i) {
        auto* item = m_imageList->item(i);
        item->setForeground(QColor("#d4d4d4"));  // default text color
        item->setText(QFileInfo(m_images[static_cast<size_t>(i)].filePath).fileName());
    }
}

void VisionProfileTesterWindow::onClearResults()
{
    // Reset all processed state
    for (auto& entry : m_images) {
        entry.processed = false;
        entry.result.clear();
        entry.outputImage.release();
    }

    // Reset list item colors and text
    resetAllListItems();

    // Clear display
    m_imageDisplay->setText("No image loaded");
    m_imageDisplay->setPixmap(QPixmap());
    m_resultsText->clear();

    m_exportCsvBtn->setEnabled(false);
    m_clearBtn->setEnabled(false);

    updateStatusBar("Results cleared.");
}

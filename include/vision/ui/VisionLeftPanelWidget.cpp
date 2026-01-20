// VisionLeftPanelWidget.cpp
#include "VisionLeftPanelWidget.h"
#include <QGroupBox>
#include <QLabel>

VisionLeftPanelWidget::VisionLeftPanelWidget(QWidget* parent)
    : QWidget(parent)
    , m_addToNodeBtn(nullptr)
    , m_pluginTabs(nullptr)
    , m_prList(nullptr)
    , m_measureList(nullptr)
    , m_filterList(nullptr)
    , m_calibrationList(nullptr)
    , m_lightingList(nullptr)
    , m_resultsDisplay(nullptr)
{
    setupUI();
    populateMockupData();
}

void VisionLeftPanelWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // Add to Recipe Node button
    m_addToNodeBtn = new QPushButton("Add to Recipe Node", this);
    m_addToNodeBtn->setToolTip("Add selected plugin to recipe");
    m_addToNodeBtn->setStyleSheet("QPushButton { background-color: #1565C0; color: white; font-weight: bold; padding: 8px; }");
    layout->addWidget(m_addToNodeBtn);

    // Plugin selection tabs
    QLabel* pluginLabel = new QLabel("Available Plugins:", this);
    pluginLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(pluginLabel);

    m_pluginTabs = new QTabWidget(this);

    // PR (Pattern Recognition) tab
    m_prList = new QListWidget();
    m_prList->setToolTip("Pattern Recognition plugins");
    m_pluginTabs->addTab(m_prList, "PR");

    // Measurement tab
    m_measureList = new QListWidget();
    m_measureList->setToolTip("Measurement plugins");
    m_pluginTabs->addTab(m_measureList, "Measure");

    // Filter tab
    m_filterList = new QListWidget();
    m_filterList->setToolTip("Image filter plugins");
    m_pluginTabs->addTab(m_filterList, "Filter");

    // Calibration tab
    m_calibrationList = new QListWidget();
    m_calibrationList->setToolTip("Camera calibration plugins");
    m_pluginTabs->addTab(m_calibrationList, "Calib");

    // Lighting tab
    m_lightingList = new QListWidget();
    m_lightingList->setToolTip("Lighting control commands");
    m_pluginTabs->addTab(m_lightingList, "Light");

    layout->addWidget(m_pluginTabs, 1);

    // Results display
    QLabel* resultsLabel = new QLabel("Results:", this);
    resultsLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(resultsLabel);

    m_resultsDisplay = new QTextEdit(this);
    m_resultsDisplay->setReadOnly(true);
    m_resultsDisplay->setPlaceholderText("Plugin execution results will appear here...");
    m_resultsDisplay->setStyleSheet("QTextEdit { font-family: 'Consolas', 'Courier New', monospace; font-size: 11px; }");
    m_resultsDisplay->setMinimumHeight(150);
    layout->addWidget(m_resultsDisplay, 1);

    // Connect signals
    connect(m_addToNodeBtn, &QPushButton::clicked, this, &VisionLeftPanelWidget::addToNodeClicked);

    auto connectList = [this](QListWidget* list) {
        connect(list, &QListWidget::itemClicked, [this, list](QListWidgetItem* item) {
            emit pluginSelected(item->text());
        });
    };

    connectList(m_prList);
    connectList(m_measureList);
    connectList(m_filterList);
    connectList(m_calibrationList);
    connectList(m_lightingList);

    // Set fixed width
    setFixedWidth(250);
}

void VisionLeftPanelWidget::populateMockupData()
{
    // PR plugins (mockup)
    m_prList->addItem("Circle Detection");
    m_prList->addItem("Template Matching");
    m_prList->addItem("Blob Detection");
    m_prList->addItem("Pattern Find");

    // Measurement plugins (mockup)
    m_measureList->addItem("Distance");
    m_measureList->addItem("Angle");
    m_measureList->addItem("Area");
    m_measureList->addItem("Perimeter");

    // Filter plugins (mockup)
    m_filterList->addItem("Edge Detection");
    m_filterList->addItem("Threshold Binary");
    m_filterList->addItem("Gaussian Blur");
    m_filterList->addItem("Morphology");

    // Lighting commands
    // Format: "Light: <command>" - will be parsed to create sequence actions
    m_lightingList->addItem("Light: Set Intensity");    // Set channel intensity (%)
    m_lightingList->addItem("Light: Turn On");          // Turn channel on
    m_lightingList->addItem("Light: Turn Off");         // Turn channel off
    m_lightingList->addItem("Light: All Off");          // Turn all channels off
    m_lightingList->addItem("Light: All On");           // Turn all channels on
}

void VisionLeftPanelWidget::updatePluginList(const QStringList& plugins)
{
    // Clear current lists
    m_prList->clear();
    m_measureList->clear();
    m_filterList->clear();

    // Add plugins to appropriate tabs
    // (In real implementation, would sort by category)
    for (const QString& plugin : plugins) {
        m_prList->addItem(plugin);
    }
}

void VisionLeftPanelWidget::updatePluginsByCategory(const QStringList& prPlugins,
                                                     const QStringList& measurePlugins,
                                                     const QStringList& filterPlugins,
                                                     const QStringList& calibrationPlugins)
{
    // Clear current lists
    m_prList->clear();
    m_measureList->clear();
    m_filterList->clear();
    m_calibrationList->clear();

    // Populate by category
    for (const QString& plugin : prPlugins) {
        m_prList->addItem(plugin);
    }
    for (const QString& plugin : measurePlugins) {
        m_measureList->addItem(plugin);
    }
    for (const QString& plugin : filterPlugins) {
        m_filterList->addItem(plugin);
    }
    for (const QString& plugin : calibrationPlugins) {
        m_calibrationList->addItem(plugin);
    }
}

void VisionLeftPanelWidget::clearPluginLists()
{
    m_prList->clear();
    m_measureList->clear();
    m_filterList->clear();
    m_calibrationList->clear();
    m_lightingList->clear();
}

void VisionLeftPanelWidget::displayResult(const QString& resultText)
{
    m_resultsDisplay->setPlainText(resultText);
}

QString VisionLeftPanelWidget::getSelectedPlugin() const
{
    // Get selected plugin from current tab
    QListWidget* currentList = nullptr;
    int tabIndex = m_pluginTabs->currentIndex();

    switch (tabIndex) {
    case 0: currentList = m_prList; break;
    case 1: currentList = m_measureList; break;
    case 2: currentList = m_filterList; break;
    case 3: currentList = m_calibrationList; break;
    }

    if (currentList && currentList->currentItem()) {
        return currentList->currentItem()->text();
    }

    return QString();
}

// RunPageLeftPanel.cpp
#include "RunPageLeftPanel.h"

#include "ProgramManager.h"
#include "SlotManagerWidget.h"
#include "Theme.h"
#include "ThemeButtonStyles.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

// Repeated bezelled file-button style (used for the 5 file actions).
const char* kFileButtonStyle =
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #3c3c3c); "
    "  color: #d4d4d4; "
    "  padding: 5px 10px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6a6a6a, stop:1 #4a4a4a); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2c2c2c; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 7px; "
    "  padding-left: 12px; "
    "}";

// Group-box style used by the File / Execute / Recipe / Execute Controls /
// Option group boxes (they're identical).
const char* kGroupBoxStyle =
    "QGroupBox { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 15px; "
    "  color: #d4d4d4; "
    "  font-weight: bold; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  subcontrol-position: top left; "
    "  padding: 2px 5px; "
    "  color: #ffffff; "
    "}";

// Shared checkbox base (only the accent color varies between checkboxes).
QString checkBoxStyle(const QString& accent, int boxSize = 20)
{
    return QString(
        "QCheckBox { "
        "  spacing: 8px; "
        "  padding: 5px; "
        "  color: #d4d4d4; "
        "} "
        "QCheckBox::indicator { "
        "  width: %1px; "
        "  height: %1px; "
        "  border: 2px solid #555555; "
        "  border-radius: 3px; "
        "  background-color: #2d2d30; "
        "} "
        "QCheckBox::indicator:hover { "
        "  border-color: %2; "
        "} "
        "QCheckBox::indicator:checked { "
        "  background-color: %2; "
        "  border-color: %2; "
        "} "
        "QCheckBox::indicator:checked:hover { "
        "  background-color: %2; "
        "  border-color: %2; "
        "} "
        "QCheckBox::indicator:unchecked { "
        "  background-color: #2d2d30; "
        "  border-color: #555555; "
        "}").arg(boxSize).arg(accent);
}

}  // namespace

RunPageLeftPanel::RunPageLeftPanel(Deps deps, QWidget* parent)
    : QWidget(parent)
    , m_deps(std::move(deps))
{
    buildUi();
}

RunPageLeftPanel::~RunPageLeftPanel() = default;

void RunPageLeftPanel::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setSpacing(10);
    outer->setContentsMargins(10, 10, 10, 10);

    // ---- Back button ----
    m_backButton = new QPushButton(QString::fromUtf8("\xE2\x86\x90 Back to Main Menu"));
    m_backButton->setMinimumHeight(35);
    m_backButton->setStyleSheet(Theme::Buttons::bezelPrimary());
    outer->addWidget(m_backButton);

    // ---- Product display + chain/loop indicators ----
    auto* productRow = new QWidget();
    auto* productRowLayout = new QHBoxLayout(productRow);
    productRowLayout->setContentsMargins(0, 0, 0, 0);
    productRowLayout->setSpacing(8);

    m_productLabel = new QLabel("Product: None");
    m_productLabel->setStyleSheet(
        "QLabel {"
        "  color: #cccccc;"
        "  font-weight: bold;"
        "  padding: 5px;"
        "}");
    m_productLabel->setWordWrap(true);
    productRowLayout->addWidget(m_productLabel);

    m_chainIndicator = new QLabel();
    m_chainIndicator->setFixedSize(16, 16);
    m_chainIndicator->setToolTip("Chain execution enabled");
    m_chainIndicator->setVisible(false);
    m_chainIndicator->setStyleSheet(
        "QLabel {"
        "  background-color: #e69500;"
        "  border-radius: 8px;"
        "  border: 2px solid #ffaa00;"
        "}");
    productRowLayout->addWidget(m_chainIndicator);

    m_loopIndicator = new QLabel();
    m_loopIndicator->setFixedSize(16, 16);
    m_loopIndicator->setToolTip("Loop execution enabled");
    m_loopIndicator->setVisible(false);
    m_loopIndicator->setStyleSheet(
        "QLabel {"
        "  background-color: #4ec9b0;"
        "  border-radius: 8px;"
        "  border: 2px solid #5fd9c0;"
        "}");
    productRowLayout->addWidget(m_loopIndicator);
    productRowLayout->addStretch();

    outer->addWidget(productRow);

    // ---- Tabs ----
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { "
        "  border: 1px solid #3f3f46; "
        "  background-color: #1e1e1e; "
        "} "
        "QTabBar::tab { "
        "  background-color: #3c3c3c; "
        "  color: #d4d4d4; "
        "  padding: 8px 16px; "
        "  margin-right: 2px; "
        "  border-top-left-radius: 3px; "
        "  border-top-right-radius: 3px; "
        "} "
        "QTabBar::tab:selected { "
        "  background-color: #0e639c; "
        "  color: white; "
        "  font-weight: bold; "
        "} "
        "QTabBar::tab:hover:!selected { "
        "  background-color: #4a4a4a; "
        "}");

    // ----- File tab -----
    auto* fileTab = new QWidget();
    auto* fileTabLayout = new QVBoxLayout(fileTab);
    fileTabLayout->setContentsMargins(5, 10, 5, 5);
    fileTabLayout->setSpacing(10);

    auto* fileGroup = new QGroupBox("Program File");
    fileGroup->setStyleSheet(kGroupBoxStyle);
    auto* fileLayout = new QVBoxLayout(fileGroup);

    m_programNameLabel = new QLabel("No program loaded");
    m_programNameLabel->setWordWrap(true);
    m_programNameLabel->setStyleSheet("font-weight: bold; color: #2196F3;");
    fileLayout->addWidget(m_programNameLabel);

    m_programPathLabel = new QLabel("");
    m_programPathLabel->setWordWrap(true);
    m_programPathLabel->setStyleSheet("font-size: 9px; color: #888888; font-style: italic;");
    m_programPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileLayout->addWidget(m_programPathLabel);

    auto* fileButtonLayout = new QVBoxLayout();
    fileButtonLayout->setSpacing(5);
    m_newButton    = new QPushButton("New");
    m_loadButton   = new QPushButton("Load");
    m_saveButton   = new QPushButton("Save");
    m_saveAsButton = new QPushButton("Save As");
    m_editButton   = new QPushButton("Edit");
    for (QPushButton* b : {m_newButton, m_loadButton, m_saveButton, m_saveAsButton, m_editButton}) {
        b->setStyleSheet(kFileButtonStyle);
        fileButtonLayout->addWidget(b);
    }
    fileLayout->addLayout(fileButtonLayout);
    fileTabLayout->addWidget(fileGroup);

    // Recent programs list
    auto* recentGroup = new QGroupBox("Recent Programs");
    recentGroup->setStyleSheet(kGroupBoxStyle);
    auto* recentLayout = new QVBoxLayout(recentGroup);
    recentLayout->setContentsMargins(5, 5, 5, 5);
    recentLayout->setSpacing(0);

    m_recentProgramsList = new QListWidget();
    m_recentProgramsList->setStyleSheet(
        "QListWidget { "
        "  background-color: #2d2d30; "
        "  border: 1px solid #3f3f46; "
        "  color: #d4d4d4; "
        "  font-size: 10px; "
        "} "
        "QListWidget::item { "
        "  padding: 4px 6px; "
        "  border-bottom: 1px solid #3f3f46; "
        "} "
        "QListWidget::item:hover { "
        "  background-color: #3c3c3c; "
        "} "
        "QListWidget::item:selected { "
        "  background-color: #0e639c; "
        "  color: white; "
        "}");
    m_recentProgramsList->setMaximumHeight(200);
    m_recentProgramsList->setToolTip("Double-click to load a recent program");
    recentLayout->addWidget(m_recentProgramsList);

    fileTabLayout->addWidget(recentGroup);
    fileTabLayout->addStretch();
    m_tabWidget->addTab(fileTab, "File");

    // ----- Execute tab -----
    auto* executeTab = new QWidget();
    auto* executeTabLayout = new QVBoxLayout(executeTab);
    executeTabLayout->setContentsMargins(5, 10, 5, 5);
    executeTabLayout->setSpacing(10);

    auto* executeGroup = new QGroupBox("Execution");
    executeGroup->setStyleSheet(kGroupBoxStyle);
    auto* executeLayout = new QVBoxLayout(executeGroup);

    m_executeButton = new QPushButton(QString::fromUtf8("\xE2\x96\xB6 Execute Program"));
    m_executeButton->setMinimumHeight(50);
    m_executeButton->setStyleSheet(Theme::Buttons::bezelSuccess());
    executeLayout->addWidget(m_executeButton);

    executeTabLayout->addWidget(executeGroup, 0);

    // Run-side slot widget.
    {
        SlotManagerWidget::Deps slotDeps;
        slotDeps.manager      = m_deps.manager;
        slotDeps.dialogParent = m_deps.dialogParent;
        m_slotWidget = new SlotManagerWidget(SlotManagerWidget::Mode::Run, slotDeps);
        executeTabLayout->addWidget(m_slotWidget, 0);
    }

    // Recipe list.
    auto* recipeGroup = new QGroupBox("Recipes in Program");
    recipeGroup->setStyleSheet(kGroupBoxStyle);
    auto* recipeLayout = new QVBoxLayout(recipeGroup);

    m_recipeListWidget = new QListWidget();
    m_recipeListWidget->setAlternatingRowColors(false);
    m_recipeListWidget->setStyleSheet(
        "QListWidget { "
        "  background-color: #1e1e1e; "
        "  border: 1px solid #3f3f46; "
        "  border-radius: 4px; "
        "  font-size: 16px; "
        "  padding: 2px; "
        "  color: #d4d4d4; "
        "  gridline-color: #3f3f46; "
        "} "
        "QListWidget::item { "
        "  padding: 10px 8px; "
        "  border-bottom: 1px solid #3f3f46; "
        "  background-color: #1e1e1e; "
        "} "
        "QListWidget::item:selected { "
        "  background-color: #0e639c; "
        "  color: white; "
        "} "
        "QListWidget::item:hover { "
        "  background-color: #2a2d2e; "
        "} "
        "QListWidget::item:selected:hover { "
        "  background-color: #0e639c; "
        "  color: white; "
        "}");
    m_recipeListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    recipeLayout->addWidget(m_recipeListWidget);

    executeTabLayout->addWidget(recipeGroup, 1);
    m_tabWidget->addTab(executeTab, "Execute");

    // ----- Option tab -----
    auto* optionTab = new QWidget();
    auto* optionTabLayout = new QVBoxLayout(optionTab);
    optionTabLayout->setContentsMargins(5, 10, 5, 5);
    optionTabLayout->setSpacing(10);

    auto* optionGroup = new QGroupBox("Execution Options");
    optionGroup->setStyleSheet(kGroupBoxStyle);
    auto* optionLayout = new QVBoxLayout(optionGroup);

    m_autoRunNextCheckBox = new QCheckBox("Auto-run next recipe");
    m_autoRunNextCheckBox->setChecked(false);
    m_autoRunNextCheckBox->setStyleSheet(checkBoxStyle("#27ae60"));
    optionLayout->addWidget(m_autoRunNextCheckBox);

    m_useDependenciesCheckBox = new QCheckBox("Use recipe dependencies (advanced)");
    m_useDependenciesCheckBox->setChecked(false);
    m_useDependenciesCheckBox->setToolTip(
        "Enable dependency-based execution where recipes run based on prerequisites");
    m_useDependenciesCheckBox->setStyleSheet(checkBoxStyle("#9b59b6"));
    optionLayout->addWidget(m_useDependenciesCheckBox);

    m_chainExecutionCheckBox = new QCheckBox("Enable chain execution");
    m_chainExecutionCheckBox->setChecked(false);
    m_chainExecutionCheckBox->setToolTip(
        "When enabled, completing a recipe will automatically trigger dependent "
        "recipes if conditions are met");
    m_chainExecutionCheckBox->setStyleSheet(checkBoxStyle("#e67e22"));
    optionLayout->addWidget(m_chainExecutionCheckBox);

    // Toggle the chain indicator dot from the checkbox state.
    connect(m_chainExecutionCheckBox, &QCheckBox::toggled,
            this, [this](bool checked) {
        m_chainIndicator->setVisible(checked);
    });

    // Loop section.
    auto* loopGroup = new QGroupBox("Loop Execution");
    loopGroup->setStyleSheet(
        "QGroupBox { "
        "  color: #4ec9b0; "
        "  font-weight: bold; "
        "  border: 1px solid #3c3c3c; "
        "  border-radius: 5px; "
        "  margin-top: 10px; "
        "  padding-top: 10px; "
        "} "
        "QGroupBox::title { "
        "  subcontrol-origin: margin; "
        "  left: 10px; "
        "  padding: 0 5px; "
        "}");
    auto* loopLayout = new QVBoxLayout(loopGroup);
    loopLayout->setContentsMargins(10, 15, 10, 10);
    loopLayout->setSpacing(8);

    m_loopEnabledCheckBox = new QCheckBox("Enable infinite loop");
    m_loopEnabledCheckBox->setChecked(false);
    m_loopEnabledCheckBox->setToolTip("Run selected recipes in an infinite loop until stopped");
    m_loopEnabledCheckBox->setStyleSheet(checkBoxStyle("#4ec9b0"));
    loopLayout->addWidget(m_loopEnabledCheckBox);

    // Toggle loop indicator dot — only shown when there are configured loop recipes.
    connect(m_loopEnabledCheckBox, &QCheckBox::toggled,
            this, [this](bool checked) {
        m_loopIndicator->setVisible(
            checked && m_deps.manager && m_deps.manager->hasLoopRecipes());
    });

    m_configureLoopButton = new QPushButton("Configure Loop Recipes...");
    m_configureLoopButton->setToolTip("Select which recipes to include in the loop");
    m_configureLoopButton->setStyleSheet(
        "QPushButton { "
        "  background-color: #3c3c3c; "
        "  color: #d4d4d4; "
        "  border: 1px solid #555555; "
        "  padding: 6px 12px; "
        "  border-radius: 4px; "
        "} "
        "QPushButton:hover { "
        "  background-color: #505050; "
        "  border-color: #4ec9b0; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #4ec9b0; "
        "}");
    loopLayout->addWidget(m_configureLoopButton);

    m_loopStatusLabel = new QLabel("No recipes configured for loop");
    m_loopStatusLabel->setStyleSheet("color: #888888; font-size: 11px; padding: 2px 5px;");
    loopLayout->addWidget(m_loopStatusLabel);

    m_ignoreDependenciesCheckBox = new QCheckBox("Ignore dependencies");
    m_ignoreDependenciesCheckBox->setChecked(false);
    m_ignoreDependenciesCheckBox->setToolTip(
        "Force sequential execution without checking dependency conditions");
    m_ignoreDependenciesCheckBox->setStyleSheet(checkBoxStyle("#e8a838", /*boxSize=*/18));
    loopLayout->addWidget(m_ignoreDependenciesCheckBox);

    optionLayout->addWidget(loopGroup);
    optionLayout->addStretch();
    optionTabLayout->addWidget(optionGroup);
    optionTabLayout->addStretch();
    m_tabWidget->addTab(optionTab, "Option");

    // Default to the Execute tab.
    m_tabWidget->setCurrentIndex(1);

    outer->addWidget(m_tabWidget, 1);

    // Width constraints replicate the previous QWidget wrapper.
    setMinimumWidth(500);
    setMaximumWidth(700);
}

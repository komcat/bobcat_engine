// ContextVariablePreviewWidget.cpp
#include "ContextVariablePreviewWidget.h"
#include "RecipeManager.h"
#include "RecipePageStyles.h"
#include <QHBoxLayout>
#include <iostream>

// Parameter keys that indicate context variable definitions (writes to context)
const QStringList ContextVariablePreviewWidget::CONTEXT_PARAM_KEYS = {
  "captureAs",           // Camera capture -> file path storage
  "capture_as",          // Vision process -> result storage
  "context_key",         // Generic context storage (DataStore, Camera)
  "key",                 // Set Context Value -> variable name
  "output_context_key",  // Crop image, transforms -> output storage
  "result_key"           // Measurement results
};

ContextVariablePreviewWidget::ContextVariablePreviewWidget(QWidget* parent)
  : QGroupBox("Context Variables", parent)
  , m_variableList(nullptr)
  , m_countLabel(nullptr)
  , m_collapseButton(nullptr)
  , m_isCollapsed(false)
{
  setupUI();
}

void ContextVariablePreviewWidget::setupUI()
{
  // Style the group box
  setStyleSheet(
    "QGroupBox { "
    "  font-weight: bold; "
    "  color: #4ec9b0; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  margin-top: 8px; "
    "  padding-top: 4px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  left: 10px; "
    "  padding: 0 5px; "
    "}"
  );

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(8, 12, 8, 8);
  mainLayout->setSpacing(4);

  // Header row with count and collapse button
  QHBoxLayout* headerLayout = new QHBoxLayout();
  headerLayout->setSpacing(5);

  m_countLabel = new QLabel("No variables");
  m_countLabel->setStyleSheet(
    "QLabel { "
    "  color: #888888; "
    "  font-size: 11px; "
    "  font-style: italic; "
    "  font-weight: normal; "
    "}"
  );
  headerLayout->addWidget(m_countLabel);

  headerLayout->addStretch();

  m_collapseButton = new QPushButton("-");
  m_collapseButton->setFixedSize(20, 20);
  m_collapseButton->setToolTip("Collapse/Expand");
  m_collapseButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; "
    "  color: #d4d4d4; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4d; "
    "}"
  );
  connect(m_collapseButton, &QPushButton::clicked, [this]() {
    m_isCollapsed = !m_isCollapsed;
    m_variableList->setVisible(!m_isCollapsed);
    m_collapseButton->setText(m_isCollapsed ? "+" : "-");
  });
  headerLayout->addWidget(m_collapseButton);

  mainLayout->addLayout(headerLayout);

  // Variable list
  m_variableList = new QListWidget();
  // No max height - let parent splitter control sizing
  m_variableList->setStyleSheet(
    "QListWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 3px; "
    "  color: #dcdcaa; "
    "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
    "  font-size: 12px; "
    "} "
    "QListWidget::item { "
    "  padding: 3px 6px; "
    "  border-bottom: 1px solid #2d2d30; "
    "} "
    "QListWidget::item:hover { "
    "  background-color: #2a2d2e; "
    "} "
    "QListWidget::item:selected { "
    "  background-color: #0e639c; "
    "  color: white; "
    "}"
  );

  // Connect item click
  connect(m_variableList, &QListWidget::itemClicked, [this](QListWidgetItem* item) {
    if (item) {
      emit variableClicked(item->text());
    }
  });

  mainLayout->addWidget(m_variableList);
}

QSet<QString> ContextVariablePreviewWidget::extractVariablesFromStep(
    const std::map<std::string, std::string>& params)
{
  QSet<QString> variables;

  for (const QString& key : CONTEXT_PARAM_KEYS) {
    auto it = params.find(key.toStdString());
    if (it != params.end() && !it->second.empty()) {
      QString value = QString::fromStdString(it->second);
      // Skip if it's a reference (starts with $) - those are reads, not writes
      if (!value.startsWith("$")) {
        variables.insert(value);
      }
    }
  }

  return variables;
}

void ContextVariablePreviewWidget::updateFromRecipe(
    const std::vector<std::unique_ptr<ProcessInstance>>& steps)
{
  m_variableList->clear();

  QSet<QString> allVariables;

  // Scan all steps for context variable definitions
  for (const auto& step : steps) {
    if (!step || !step->enabled) continue;

    QSet<QString> stepVars = extractVariablesFromStep(step->parameters);
    allVariables.unite(stepVars);
  }

  // Sort and add to list
  QStringList sortedVars = allVariables.values();
  sortedVars.sort(Qt::CaseInsensitive);

  for (const QString& var : sortedVars) {
    QListWidgetItem* item = new QListWidgetItem("$" + var);
    item->setToolTip(QString("Click to copy: $%1").arg(var));
    m_variableList->addItem(item);
  }

  // Update count label
  int count = sortedVars.size();
  if (count == 0) {
    m_countLabel->setText("No variables defined");
    m_countLabel->setStyleSheet(
      "QLabel { color: #888888; font-size: 11px; font-style: italic; font-weight: normal; }"
    );
  } else {
    m_countLabel->setText(QString("%1 variable%2").arg(count).arg(count > 1 ? "s" : ""));
    m_countLabel->setStyleSheet(
      "QLabel { color: #4ec9b0; font-size: 11px; font-weight: normal; }"
    );
  }

  std::cout << "[ContextVariablePreview] Found " << count << " context variables" << std::endl;
}

void ContextVariablePreviewWidget::clear()
{
  m_variableList->clear();
  m_countLabel->setText("No variables");
  m_countLabel->setStyleSheet(
    "QLabel { color: #888888; font-size: 11px; font-style: italic; font-weight: normal; }"
  );
}

QStringList ContextVariablePreviewWidget::getVariableNames() const
{
  QStringList names;
  for (int i = 0; i < m_variableList->count(); ++i) {
    names.append(m_variableList->item(i)->text());
  }
  return names;
}

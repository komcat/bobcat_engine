// RecipeTreeItemFactory.cpp
#include "RecipeTreeItemFactory.h"
#include "RecipePageStyles.h"
#include "RecipeManager.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>

RecipeTreeItemFactory::RecipeTreeItemFactory(
  QTreeWidget* treeWidget,
  RecipeManager* recipeManager,
  BreakpointCallback onBreakpointChanged,
  EnabledCallback onEnabledChanged)
  : m_treeWidget(treeWidget)
  , m_recipeManager(recipeManager)
  , m_onBreakpointChanged(onBreakpointChanged)
  , m_onEnabledChanged(onEnabledChanged)
{
}

QString RecipeTreeItemFactory::getCategorySymbol(const std::string& category)
{
  // Using simple ASCII/Unicode symbols that render reliably on Windows
  if (category == "Motion Control" || category == "Motion") {
    return QString::fromUtf8("\xE2\x9E\xA4");  // ➤ Arrow (motion/movement)
  }
  if (category == "I/O & Pneumatics") {
    return QString::fromUtf8("\xE2\x9A\xA1");  // ⚡ Lightning bolt (I/O)
  }
  if (category == "TEC & Laser") {
    return QString::fromUtf8("\xE2\x98\x80");  // ☀ Sun (thermal/laser)
  }
  if (category == "Power Supply") {
    return QString::fromUtf8("\xE2\x8F\xBB");  // ⏻ Power symbol
  }
  if (category == "SMU Control") {
    return QString::fromUtf8("\xE2\x8C\xA1");  // ⌡ Measurement
  }
  if (category == "Data") {
    return QString::fromUtf8("\xE2\x96\xA3");  // ▣ Data/storage
  }
  if (category == "System") {
    return QString::fromUtf8("\xE2\x9C\x8B");  // ✋ Hand (user interaction)
  }
  if (category == "Control Flow") {
    return QString::fromUtf8("\xE2\x86\xB9");  // ↹ Control flow
  }
  if (category == "Camera" || category == "Vision") {
    return QString::fromUtf8("\xE2\x97\x89");  // ◉ Eye/camera
  }
  if (category == "Calibration") {
    return QString::fromUtf8("\xE2\x9C\x93");  // ✓ Checkmark
  }
  if (category == "Dispensing") {
    return QString::fromUtf8("\xE2\x96\xBC");  // ▼ Dispense
  }
  if (category == "Aux Window") {
    return QString::fromUtf8("\xE2\x96\xA1");  // □ Window
  }
  if (category == "Comment") {
    return QString::fromUtf8("#");  // # Comment marker
  }
  // Default
  return QString::fromUtf8("\xE2\x97\x8F");  // ● Circle (generic)
}

QString RecipeTreeItemFactory::getBackgroundColor(const ProcessInstance* step) const
{
  if (!step->backgroundColor.empty()) {
    return QString::fromStdString(step->backgroundColor);
  }
  return "transparent";
}

QString RecipeTreeItemFactory::getIconPrefix(const ProcessInstance* step) const
{
  bool isComment = (step->processType == "Comment");
  bool isBlockStart = RecipeManager::isBlockStart(step->processType);
  bool isBlockEnd = RecipeManager::isBlockEnd(step->processType);
  bool isElse = (step->processType == "Conditional Else");

  if (isComment) {
    return "### ";
  } else if (isBlockStart) {
    return QString::fromUtf8("\u25BC ");  // ▼ down arrow for block start
  } else if (isBlockEnd) {
    return QString::fromUtf8("\u2514\u2500 ");  // └─ end marker
  } else if (isElse) {
    return QString::fromUtf8("\u251C\u2500 ");  // ├─ else branch marker
  }
  return "";
}

QString RecipeTreeItemFactory::getDisplayText(const ProcessInstance* step) const
{
  QString iconPrefix = getIconPrefix(step);
  bool isComment = (step->processType == "Comment");

  QString displayText;
  if (isComment) {
    // For comments, display the comment text parameter
    auto it = step->parameters.find("text");
    if (it != step->parameters.end()) {
      displayText = iconPrefix + QString::fromStdString(it->second);
    } else {
      displayText = iconPrefix + QString::fromStdString(step->userNickname);
    }
  } else {
    displayText = iconPrefix + QString::fromStdString(step->userNickname);
  }

  if (!step->enabled) {
    displayText = "[DISABLED] " + displayText;
  }

  return displayText;
}

QTreeWidgetItem* RecipeTreeItemFactory::createStepItem(
  const ProcessInstance* step,
  int stepIndex,
  QTreeWidgetItem* parent)
{
  // Create tree item
  QTreeWidgetItem* item;
  if (parent) {
    item = new QTreeWidgetItem(parent);
  } else {
    item = new QTreeWidgetItem(m_treeWidget);
  }

  // Store step index in item data
  item->setData(0, Qt::UserRole, stepIndex);

  // Clear all column text to prevent any default text from showing
  for (int col = 0; col < 5; ++col) {
    item->setText(col, "");
  }

  QString bgColor = getBackgroundColor(step);

  // Column 0: Breakpoint checkbox
  m_treeWidget->setItemWidget(item, 0, createBreakpointWidget(step, stepIndex, bgColor));

  // Column 1: Step number
  m_treeWidget->setItemWidget(item, 1, createStepNumberWidget(stepIndex, bgColor, step->enabled));

  // Column 2: Category symbol
  m_treeWidget->setItemWidget(item, 2, createSymbolWidget(step, bgColor));

  // Column 3: Process name
  m_treeWidget->setItemWidget(item, 3, createProcessNameWidget(step, bgColor));

  // Column 4: Run checkbox
  m_treeWidget->setItemWidget(item, 4, createRunCheckboxWidget(step, stepIndex, bgColor));

  return item;
}

void RecipeTreeItemFactory::updateProcessNameLabel(QTreeWidgetItem* item, const ProcessInstance* step)
{
  if (!item) return;

  // Get the label widget from column 3
  QWidget* widget = m_treeWidget->itemWidget(item, 3);
  QLabel* processLabel = qobject_cast<QLabel*>(widget);
  if (processLabel) {
    processLabel->setText(getDisplayText(step));
  }
}

QWidget* RecipeTreeItemFactory::createBreakpointWidget(
  const ProcessInstance* step,
  int stepIndex,
  const QString& bgColor)
{
  QCheckBox* breakpointChk = new QCheckBox();
  breakpointChk->setChecked(step->breakpoint);
  breakpointChk->setProperty("stepIndex", stepIndex);
  breakpointChk->setCursor(Qt::PointingHandCursor);
  breakpointChk->setToolTip(step->breakpoint ? "Breakpoint set - click to remove" : "Click to set breakpoint");
  breakpointChk->setStyleSheet(RecipeStyles::breakpointCheckbox());

  // Connect callback
  if (m_onBreakpointChanged) {
    QObject::connect(breakpointChk, &QCheckBox::toggled,
      [this, stepIndex](bool checked) {
        m_onBreakpointChanged(stepIndex, checked);
      });
  }

  QWidget* container = new QWidget();
  container->setStyleSheet(RecipeStyles::rowContainer(bgColor));
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->addWidget(breakpointChk);
  layout->setAlignment(Qt::AlignCenter);
  layout->setContentsMargins(0, 0, 0, 0);

  return container;
}

QWidget* RecipeTreeItemFactory::createStepNumberWidget(
  int stepIndex,
  const QString& bgColor,
  bool enabled)
{
  QLabel* label = new QLabel(QString::number(stepIndex + 1));
  label->setAlignment(Qt::AlignCenter);
  label->setStyleSheet(RecipeStyles::stepNumberLabel(bgColor, enabled));
  return label;
}

QWidget* RecipeTreeItemFactory::createSymbolWidget(
  const ProcessInstance* step,
  const QString& bgColor)
{
  std::string category = m_recipeManager->getProcessCategory(step->processType);
  QString symbol = getCategorySymbol(category);

  QLabel* label = new QLabel(symbol);
  label->setAlignment(Qt::AlignCenter);
  label->setStyleSheet(RecipeStyles::symbolLabel(bgColor, step->enabled));
  label->setToolTip(QString::fromStdString(category));

  return label;
}

QWidget* RecipeTreeItemFactory::createProcessNameWidget(
  const ProcessInstance* step,
  const QString& bgColor)
{
  bool isComment = (step->processType == "Comment");
  QString displayText = getDisplayText(step);

  QLabel* label = new QLabel(displayText);
  label->setStyleSheet(RecipeStyles::processNameLabel(bgColor, step->enabled, isComment));

  return label;
}

QWidget* RecipeTreeItemFactory::createRunCheckboxWidget(
  const ProcessInstance* step,
  int stepIndex,
  const QString& bgColor)
{
  QCheckBox* runChk = new QCheckBox();
  runChk->setChecked(step->enabled);
  runChk->setProperty("stepIndex", stepIndex);
  runChk->setCursor(Qt::PointingHandCursor);
  runChk->setToolTip(step->enabled ? "Step enabled - click to disable" : "Step disabled - click to enable");
  runChk->setStyleSheet(RecipeStyles::runCheckbox());

  // Connect callback
  if (m_onEnabledChanged) {
    QObject::connect(runChk, &QCheckBox::toggled,
      [this, stepIndex](bool enabled) {
        m_onEnabledChanged(stepIndex, enabled);
      });
  }

  QWidget* container = new QWidget();
  container->setStyleSheet(RecipeStyles::rowContainer(bgColor));
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->addWidget(runChk);
  layout->setAlignment(Qt::AlignCenter);
  layout->setContentsMargins(0, 0, 0, 0);

  return container;
}

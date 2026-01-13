// LoopConfigDialog.cpp
#include "LoopConfigDialog.h"
#include "ProgramManager.h"
#include <QGroupBox>
#include <QFrame>
#include <QSplitter>
#include <QPainterPath>
#include <QFont>
#include <QMessageBox>
#include <set>
#include <iostream>

LoopConfigDialog::LoopConfigDialog(ProgramManager* programManager, QWidget* parent)
  : QDialog(parent)
  , m_programManager(programManager)
  , m_previewScene(nullptr)
  , m_previewView(nullptr)
{
  setWindowTitle("Configure Loop Recipes");
  setMinimumSize(700, 500);
  resize(800, 550);
  setupUI();
  loadCurrentSettings();
  updatePreview();
}

void LoopConfigDialog::setupUI()
{
  // Apply dark theme
  setStyleSheet(R"(
    QDialog {
      background-color: #252526;
    }
    QGroupBox {
      background-color: #2d2d30;
      border: 1px solid #3c3c3c;
      border-radius: 5px;
      margin-top: 10px;
      padding: 10px;
      color: #cccccc;
      font-weight: bold;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      left: 10px;
      padding: 0 5px;
    }
    QPushButton {
      background-color: #3c3c3c;
      color: #cccccc;
      border: 1px solid #555555;
      padding: 8px 16px;
      border-radius: 4px;
      min-width: 80px;
    }
    QPushButton:hover {
      background-color: #505050;
    }
    QPushButton:pressed {
      background-color: #1177bb;
    }
    QPushButton#okButton {
      background-color: #0e639c;
      border-color: #1177bb;
    }
    QPushButton#okButton:hover {
      background-color: #1177bb;
    }
    QCheckBox {
      color: #d4d4d4;
      spacing: 8px;
      padding: 6px;
    }
    QCheckBox::indicator {
      width: 18px;
      height: 18px;
      border: 2px solid #555555;
      border-radius: 3px;
      background-color: #2d2d30;
    }
    QCheckBox::indicator:checked {
      background-color: #4ec9b0;
      border-color: #5fd9c0;
    }
    QCheckBox::indicator:hover {
      border-color: #4ec9b0;
    }
    QLabel {
      color: #cccccc;
    }
    QScrollArea {
      border: none;
      background-color: #252526;
    }
    QWidget#recipeListContainer {
      background-color: #252526;
    }
    QGraphicsView {
      border: 1px solid #3c3c3c;
      background-color: #1e1e1e;
    }
  )");

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(15);
  mainLayout->setContentsMargins(15, 15, 15, 15);

  // Header
  QLabel* headerLabel = new QLabel(
    "Select recipes to include in the infinite loop. Only selected recipes will run."
  );
  headerLabel->setStyleSheet("color: #888888; font-size: 12px;");
  headerLabel->setWordWrap(true);
  mainLayout->addWidget(headerLabel);

  // Splitter for left (list) and right (preview) panels
  QSplitter* splitter = new QSplitter(Qt::Horizontal);
  splitter->setStyleSheet("QSplitter::handle { background-color: #3c3c3c; width: 3px; }");

  // === LEFT PANEL: Recipe List ===
  QWidget* leftPanel = new QWidget();
  QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(0, 0, 10, 0);

  // Select All / Clear All buttons
  QHBoxLayout* buttonRowLayout = new QHBoxLayout();
  buttonRowLayout->setSpacing(10);

  m_selectAllButton = new QPushButton("Select All");
  m_selectAllButton->setFixedWidth(90);
  connect(m_selectAllButton, &QPushButton::clicked, this, &LoopConfigDialog::onSelectAll);
  buttonRowLayout->addWidget(m_selectAllButton);

  m_clearAllButton = new QPushButton("Clear All");
  m_clearAllButton->setFixedWidth(90);
  connect(m_clearAllButton, &QPushButton::clicked, this, &LoopConfigDialog::onClearAll);
  buttonRowLayout->addWidget(m_clearAllButton);

  buttonRowLayout->addStretch();
  leftLayout->addLayout(buttonRowLayout);

  // Recipe list in scrollable area
  QGroupBox* recipeGroup = new QGroupBox("Recipes");
  QVBoxLayout* groupLayout = new QVBoxLayout(recipeGroup);
  groupLayout->setContentsMargins(5, 15, 5, 5);
  groupLayout->setSpacing(0);

  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  m_recipeListWidget = new QWidget();
  m_recipeListWidget->setObjectName("recipeListContainer");
  QVBoxLayout* listLayout = new QVBoxLayout(m_recipeListWidget);
  listLayout->setContentsMargins(10, 10, 10, 10);
  listLayout->setSpacing(3);

  // Add checkbox for each recipe with dependency info
  int recipeCount = m_programManager->getRecipeCount();
  for (int i = 0; i < recipeCount; i++) {
    const RecipeReference* recipe = m_programManager->getRecipe(i);
    if (!recipe) continue;

    // Build label with dependency info
    QString label = QString("%1. %2")
      .arg(i + 1)
      .arg(QString::fromStdString(recipe->displayName));

    // Add dependency info
    std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(i);
    if (!deps.empty()) {
      QStringList depList;
      for (const auto& dep : deps) {
        depList << QString::number(dep.predecessorIndex + 1);
      }
      label += QString(" [depends on: %1]").arg(depList.join(", "));
    }

    if (!recipe->enabled) {
      label += " (disabled)";
    }

    QCheckBox* checkbox = new QCheckBox(label);
    checkbox->setEnabled(recipe->enabled);

    // Style differently if has dependencies
    if (!deps.empty()) {
      checkbox->setStyleSheet(
        "QCheckBox { color: #9b87f5; }"  // Purple for recipes with dependencies
        "QCheckBox::indicator:checked { background-color: #4ec9b0; border-color: #5fd9c0; }"
      );
    }

    // Connect to update preview
    connect(checkbox, &QCheckBox::toggled, this, &LoopConfigDialog::onRecipeToggled);

    m_recipeCheckboxes.push_back(checkbox);
    listLayout->addWidget(checkbox);
  }

  listLayout->addStretch();
  scrollArea->setWidget(m_recipeListWidget);
  groupLayout->addWidget(scrollArea);

  leftLayout->addWidget(recipeGroup, 1);

  // Summary label
  m_summaryLabel = new QLabel("0 recipes selected for loop");
  m_summaryLabel->setStyleSheet("color: #4ec9b0; font-weight: bold; padding: 5px;");
  leftLayout->addWidget(m_summaryLabel);

  splitter->addWidget(leftPanel);

  // === RIGHT PANEL: Visual Preview ===
  QWidget* rightPanel = new QWidget();
  QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(10, 0, 0, 0);

  m_previewTitle = new QLabel("Loop Execution Flow");
  m_previewTitle->setStyleSheet("color: #4ec9b0; font-weight: bold; font-size: 13px;");
  rightLayout->addWidget(m_previewTitle);

  // Graphics view for preview
  m_previewScene = new QGraphicsScene(this);
  m_previewView = new QGraphicsView(m_previewScene);
  m_previewView->setRenderHint(QPainter::Antialiasing);
  m_previewView->setMinimumWidth(250);
  rightLayout->addWidget(m_previewView, 1);

  // Legend
  QLabel* legendLabel = new QLabel(
    "<span style='color:#4ec9b0;'>■</span> Selected (will run)  "
    "<span style='color:#9b87f5;'>→</span> Dependency"
  );
  legendLabel->setStyleSheet("color: #888888; font-size: 11px; padding: 5px;");
  rightLayout->addWidget(legendLabel);

  splitter->addWidget(rightPanel);

  // Set splitter proportions (60% list, 40% preview)
  splitter->setSizes({400, 300});

  mainLayout->addWidget(splitter, 1);

  // Separator line
  QFrame* separator = new QFrame();
  separator->setFrameShape(QFrame::HLine);
  separator->setStyleSheet("background-color: #3c3c3c;");
  separator->setFixedHeight(1);
  mainLayout->addWidget(separator);

  // OK / Cancel buttons
  QHBoxLayout* bottomLayout = new QHBoxLayout();
  bottomLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  bottomLayout->addWidget(m_cancelButton);

  m_okButton = new QPushButton("OK");
  m_okButton->setObjectName("okButton");
  m_okButton->setDefault(true);
  connect(m_okButton, &QPushButton::clicked, this, &LoopConfigDialog::onAccept);
  bottomLayout->addWidget(m_okButton);

  mainLayout->addLayout(bottomLayout);
}

void LoopConfigDialog::loadCurrentSettings()
{
  // Load current includeInLoop settings from ProgramManager
  for (size_t i = 0; i < m_recipeCheckboxes.size(); i++) {
    const RecipeReference* recipe = m_programManager->getRecipe(static_cast<int>(i));
    if (recipe) {
      m_recipeCheckboxes[i]->setChecked(recipe->includeInLoop);
    }
  }
  updateSummary();
}

void LoopConfigDialog::applySettings()
{
  // Apply checkbox states to ProgramManager
  for (size_t i = 0; i < m_recipeCheckboxes.size(); i++) {
    m_programManager->setRecipeIncludeInLoop(
      static_cast<int>(i),
      m_recipeCheckboxes[i]->isChecked()
    );
  }

  std::cout << "[LoopConfigDialog] Applied loop settings to "
            << m_recipeCheckboxes.size() << " recipes" << std::endl;
}

void LoopConfigDialog::onSelectAll()
{
  for (QCheckBox* cb : m_recipeCheckboxes) {
    if (cb->isEnabled()) {
      cb->setChecked(true);
    }
  }
}

void LoopConfigDialog::onClearAll()
{
  for (QCheckBox* cb : m_recipeCheckboxes) {
    cb->setChecked(false);
  }
}

void LoopConfigDialog::onAccept()
{
  // Validate the configuration before applying
  QStringList warnings;

  // Build set of loop recipe indices
  std::set<int> loopIndices;
  for (size_t i = 0; i < m_recipeCheckboxes.size(); i++) {
    if (m_recipeCheckboxes[i]->isChecked()) {
      loopIndices.insert(static_cast<int>(i));
    }
  }

  // Check for invalid dependencies
  int recipeCount = m_programManager->getRecipeCount();
  for (int i = 0; i < recipeCount; i++) {
    const RecipeReference* recipe = m_programManager->getRecipe(i);
    if (!recipe || !recipe->enabled) continue;

    bool isLoopRecipe = loopIndices.find(i) != loopIndices.end();
    std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(i);

    for (const auto& dep : deps) {
      bool depIsLoopRecipe = loopIndices.find(dep.predecessorIndex) != loopIndices.end();

      // Warning: Non-loop recipe depends on loop recipe
      if (!isLoopRecipe && depIsLoopRecipe) {
        const RecipeReference* depRecipe = m_programManager->getRecipe(dep.predecessorIndex);
        warnings << QString("Recipe %1 (%2) depends on loop recipe %3 (%4).\n"
          "Non-loop recipes run first, so this dependency may not work as expected.")
          .arg(i + 1)
          .arg(QString::fromStdString(recipe->displayName))
          .arg(dep.predecessorIndex + 1)
          .arg(depRecipe ? QString::fromStdString(depRecipe->displayName) : "Unknown");
      }
    }
  }

  // Show warnings if any
  if (!warnings.isEmpty()) {
    QString message = "The following dependency issues were found:\n\n";
    for (const QString& warning : warnings) {
      message += "• " + warning + "\n\n";
    }
    message += "Do you want to continue anyway?";

    QMessageBox::StandardButton reply = QMessageBox::warning(
      this,
      "Dependency Warnings",
      message,
      QMessageBox::Yes | QMessageBox::No,
      QMessageBox::No
    );

    if (reply != QMessageBox::Yes) {
      return;  // Don't accept, let user fix
    }
  }

  applySettings();
  accept();
}

void LoopConfigDialog::onRecipeToggled()
{
  updateSummary();
  updatePreview();
}

void LoopConfigDialog::updateSummary()
{
  int count = 0;
  for (const QCheckBox* cb : m_recipeCheckboxes) {
    if (cb->isChecked()) {
      count++;
    }
  }

  if (count == 0) {
    m_summaryLabel->setText("No recipes selected for loop");
    m_summaryLabel->setStyleSheet("color: #f48771; font-weight: bold; padding: 5px;");
  } else if (count == 1) {
    m_summaryLabel->setText("1 recipe selected for loop");
    m_summaryLabel->setStyleSheet("color: #4ec9b0; font-weight: bold; padding: 5px;");
  } else {
    m_summaryLabel->setText(QString("%1 recipes selected for loop").arg(count));
    m_summaryLabel->setStyleSheet("color: #4ec9b0; font-weight: bold; padding: 5px;");
  }
}

void LoopConfigDialog::updatePreview()
{
  clearPreview();
  drawLoopPreview();
}

void LoopConfigDialog::clearPreview()
{
  m_previewScene->clear();
  m_previewNodes.clear();
}

void LoopConfigDialog::drawLoopPreview()
{
  const int nodeWidth = 140;
  const int nodeHeight = 30;
  const int verticalSpacing = 15;
  const int startX = 20;
  int currentY = 20;

  int recipeCount = m_programManager->getRecipeCount();

  // Collect only loop recipes (non-loop recipes are skipped entirely)
  std::vector<int> loopRecipes;

  for (int i = 0; i < recipeCount; i++) {
    const RecipeReference* recipe = m_programManager->getRecipe(i);
    if (!recipe || !recipe->enabled) continue;

    if (i < static_cast<int>(m_recipeCheckboxes.size()) && m_recipeCheckboxes[i]->isChecked()) {
      loopRecipes.push_back(i);
    }
  }

  QFont nodeFont("Segoe UI", 9);
  QFont sectionFont("Segoe UI", 10, QFont::Bold);

  // Draw header
  if (loopRecipes.empty()) {
    QGraphicsTextItem* emptyText = m_previewScene->addText("No recipes selected", sectionFont);
    emptyText->setDefaultTextColor(QColor(136, 136, 136));
    emptyText->setPos(startX, currentY);
    currentY += 30;

    QGraphicsTextItem* hintText = m_previewScene->addText("Select recipes to include in loop", nodeFont);
    hintText->setDefaultTextColor(QColor(100, 100, 100));
    hintText->setPos(startX, currentY);
  } else {
    QGraphicsTextItem* header = m_previewScene->addText("Loop Execution", sectionFont);
    header->setDefaultTextColor(QColor(78, 201, 176));
    header->setPos(startX, currentY);
    currentY += 25;

    // Draw loop bracket/indicator on the left
    int loopStartY = currentY;
    int loopEndY = currentY;

    // Draw loop recipe nodes
    for (size_t i = 0; i < loopRecipes.size(); i++) {
      int idx = loopRecipes[i];
      const RecipeReference* recipe = m_programManager->getRecipe(idx);
      if (!recipe) continue;

      // Draw rectangle (teal for loop recipes)
      QGraphicsRectItem* rect = m_previewScene->addRect(
        startX + 15, currentY, nodeWidth, nodeHeight,
        QPen(QColor(78, 201, 176), 2),
        QBrush(QColor(40, 70, 65))
      );
      rect->setData(0, idx);

      // Draw text
      QString displayName = QString::fromStdString(recipe->displayName);
      if (displayName.length() > 18) {
        displayName = displayName.left(16) + "...";
      }
      QString label = QString("%1. %2").arg(idx + 1).arg(displayName);

      QGraphicsTextItem* text = m_previewScene->addText(label, nodeFont);
      text->setDefaultTextColor(QColor(78, 201, 176));
      text->setPos(startX + 20, currentY + 5);

      RecipeNode node = {rect, text, idx, true};
      m_previewNodes.push_back(node);

      // Draw arrow to next recipe (except for last)
      if (i < loopRecipes.size() - 1) {
        int arrowX = startX + 15 + nodeWidth / 2;
        m_previewScene->addLine(
          arrowX, currentY + nodeHeight,
          arrowX, currentY + nodeHeight + verticalSpacing,
          QPen(QColor(78, 201, 176), 2)
        );
      }

      currentY += nodeHeight + verticalSpacing;
      loopEndY = currentY - verticalSpacing;
    }

    // Draw loop-back arrow
    if (loopRecipes.size() > 0) {
      int arrowX = startX + 5;
      QPainterPath loopPath;
      loopPath.moveTo(startX + 15 + nodeWidth/2, loopEndY + nodeHeight/2);
      loopPath.lineTo(arrowX, loopEndY + nodeHeight/2);
      loopPath.lineTo(arrowX, loopStartY - 10);
      loopPath.lineTo(startX + 15 + nodeWidth/2, loopStartY - 10);

      QGraphicsPathItem* loopArrow = m_previewScene->addPath(
        loopPath,
        QPen(QColor(78, 201, 176), 2, Qt::DashLine)
      );
      Q_UNUSED(loopArrow);

      // Add "repeat" text
      QGraphicsTextItem* repeatText = m_previewScene->addText("repeat", nodeFont);
      repeatText->setDefaultTextColor(QColor(78, 201, 176));
      repeatText->setPos(arrowX - 5, (loopStartY + loopEndY) / 2);
    }

    // Draw dependencies (only between selected loop recipes)
    for (int idx : loopRecipes) {
      std::vector<DependencyLink> deps = m_programManager->getRecipeDependencies(idx);
      for (const auto& dep : deps) {
        // Only show dependency if both source and target are in loop
        bool sourceInLoop = std::find(loopRecipes.begin(), loopRecipes.end(), dep.predecessorIndex) != loopRecipes.end();
        if (!sourceInLoop) continue;

        // Find source and target nodes
        RecipeNode* sourceNode = nullptr;
        RecipeNode* targetNode = nullptr;

        for (auto& node : m_previewNodes) {
          if (node.recipeIndex == dep.predecessorIndex) sourceNode = &node;
          if (node.recipeIndex == idx) targetNode = &node;
        }

        if (sourceNode && targetNode) {
          // Draw dependency arrow (purple, dashed)
          QPointF sourcePos = sourceNode->rect->rect().center();
          QPointF targetPos = targetNode->rect->rect().center();

          // Offset to the right side
          sourcePos.setX(sourceNode->rect->rect().right() + 5);
          targetPos.setX(targetNode->rect->rect().right() + 5);

          QPainterPath depPath;
          depPath.moveTo(sourcePos);
          depPath.quadTo(
            QPointF(sourcePos.x() + 30, (sourcePos.y() + targetPos.y()) / 2),
            targetPos
          );

          m_previewScene->addPath(
            depPath,
            QPen(QColor(155, 135, 245), 1, Qt::DotLine)
          );
        }
      }
    }
  }

  // Fit view to contents
  m_previewScene->setSceneRect(m_previewScene->itemsBoundingRect().adjusted(-10, -10, 20, 20));
}

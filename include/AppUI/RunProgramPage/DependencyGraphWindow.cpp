// DependencyGraphWindow.cpp
#include "DependencyGraphWindow.h"
#include "DependencyGraphScene.h"
#include "DependencyEdgeItem.h"
#include "RecipeNodeItem.h"
#include "ProgramManager.h"
#include "RecipeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>

// Parameter keys that indicate context variable definitions (writes to context)
const QStringList DependencyGraphWindow::CONTEXT_PARAM_KEYS = {
  "captureAs",           // Camera capture -> file path storage
  "capture_as",          // Vision process -> result storage
  "context_key",         // Generic context storage (DataStore, Camera)
  "key",                 // Set Context Value -> variable name
  "output_context_key",  // Crop image, transforms -> output storage
  "result_key"           // Measurement results
};

// ============================================================================
// DependencyGraphView Implementation
// ============================================================================

DependencyGraphView::DependencyGraphView(QGraphicsScene* scene, QWidget* parent)
  : QGraphicsView(scene, parent)
  , m_isPanning(false)
  , m_currentZoom(1.0)
{
  setRenderHint(QPainter::Antialiasing);
  setRenderHint(QPainter::SmoothPixmapTransform);
  setDragMode(QGraphicsView::RubberBandDrag);
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorUnderMouse);

  // Enable scrollbars
  setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void DependencyGraphView::zoomIn()
{
  if (m_currentZoom < MAX_ZOOM) {
    scale(ZOOM_FACTOR, ZOOM_FACTOR);
    m_currentZoom *= ZOOM_FACTOR;
  }
}

void DependencyGraphView::zoomOut()
{
  if (m_currentZoom > MIN_ZOOM) {
    scale(1.0 / ZOOM_FACTOR, 1.0 / ZOOM_FACTOR);
    m_currentZoom /= ZOOM_FACTOR;
  }
}

void DependencyGraphView::fitToContents()
{
  if (scene()) {
    QRectF bounds = scene()->itemsBoundingRect();
    bounds.adjust(-50, -50, 50, 50);  // Add margin
    fitInView(bounds, Qt::KeepAspectRatio);

    // Update zoom level tracking
    QTransform t = transform();
    m_currentZoom = t.m11();  // Approximate zoom from transform
  }
}

void DependencyGraphView::wheelEvent(QWheelEvent* event)
{
  // Zoom with mouse wheel
  if (event->angleDelta().y() > 0) {
    zoomIn();
  }
  else {
    zoomOut();
  }
  event->accept();
}

void DependencyGraphView::mousePressEvent(QMouseEvent* event)
{
  // Middle mouse button or Alt+Left for panning
  if (event->button() == Qt::MiddleButton ||
      (event->button() == Qt::LeftButton && event->modifiers() & Qt::AltModifier)) {
    m_isPanning = true;
    m_lastPanPoint = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }

  QGraphicsView::mousePressEvent(event);
}

void DependencyGraphView::mouseMoveEvent(QMouseEvent* event)
{
  if (m_isPanning) {
    QPoint delta = event->pos() - m_lastPanPoint;
    m_lastPanPoint = event->pos();

    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());

    event->accept();
    return;
  }

  QGraphicsView::mouseMoveEvent(event);
}

void DependencyGraphView::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::MiddleButton ||
      (event->button() == Qt::LeftButton && m_isPanning)) {
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    event->accept();
    return;
  }

  QGraphicsView::mouseReleaseEvent(event);
}

// ============================================================================
// DependencyGraphWindow Implementation
// ============================================================================

DependencyGraphWindow::DependencyGraphWindow(ProgramManager* programManager, QWidget* parent)
  : QDialog(parent)
  , m_programManager(programManager)
{
  setWindowTitle("Recipe Dependency Graph");
  setMinimumSize(800, 600);
  resize(1000, 700);

  setupUI();
  applyDarkTheme();

  // Load initial graph
  refreshGraph();
}

DependencyGraphWindow::~DependencyGraphWindow()
{
  // Disconnect selection signal before scene cleanup to prevent crash
  // during destruction when clearGraph() triggers selectionChanged
  if (m_scene) {
    disconnect(m_scene, &QGraphicsScene::selectionChanged,
               this, &DependencyGraphWindow::onSelectionChanged);
  }
}

void DependencyGraphWindow::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create toolbar
  setupToolbar();
  mainLayout->addWidget(m_toolbar);

  // Create scene and view
  m_scene = new DependencyGraphScene(m_programManager, this);
  m_graphicsView = new DependencyGraphView(m_scene, this);

  // Create context variable panel
  setupContextVariablePanel();

  // Create splitter with graph view and context panel
  m_splitter = new QSplitter(Qt::Horizontal);
  m_splitter->addWidget(m_graphicsView);
  m_splitter->addWidget(m_contextVarPanel);
  m_splitter->setStretchFactor(0, 3);  // Graph gets 75%
  m_splitter->setStretchFactor(1, 1);  // Context panel gets 25%
  m_splitter->setSizes({750, 250});

  mainLayout->addWidget(m_splitter, 1);

  // Status bar
  QWidget* statusBar = new QWidget();
  QHBoxLayout* statusLayout = new QHBoxLayout(statusBar);
  statusLayout->setContentsMargins(10, 5, 10, 5);

  m_statusLabel = new QLabel("Select 2 nodes and click Link, or drag from right edge to left edge. "
                             "Right-click arrows to edit or delete.");
  m_statusLabel->setStyleSheet("color: #888888;");
  statusLayout->addWidget(m_statusLabel);

  statusLayout->addStretch();

  QPushButton* closeBtn = new QPushButton("Close");
  closeBtn->setFixedWidth(80);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
  statusLayout->addWidget(closeBtn);

  mainLayout->addWidget(statusBar);

  // Connect scene signals
  connect(m_scene, &DependencyGraphScene::dependencyCreated,
          this, &DependencyGraphWindow::onDependencyCreated);
  connect(m_scene, &DependencyGraphScene::dependencyRemoved,
          this, &DependencyGraphWindow::onDependencyRemoved);
  connect(m_scene, &DependencyGraphScene::editConditionRequested,
          this, &DependencyGraphWindow::onEditConditionRequested);
  connect(m_scene, &DependencyGraphScene::nodePositionChanged,
          this, &DependencyGraphWindow::onNodePositionChanged);

  // Connect selection changed signal
  connect(m_scene, &QGraphicsScene::selectionChanged,
          this, &DependencyGraphWindow::onSelectionChanged);
}

void DependencyGraphWindow::setupToolbar()
{
  m_toolbar = new QToolBar();
  m_toolbar->setMovable(false);
  m_toolbar->setIconSize(QSize(20, 20));

  // Auto Layout button
  m_btnAutoLayout = new QPushButton("Auto Layout");
  m_btnAutoLayout->setToolTip("Automatically arrange nodes in layers");
  connect(m_btnAutoLayout, &QPushButton::clicked, this, &DependencyGraphWindow::onAutoLayout);
  m_toolbar->addWidget(m_btnAutoLayout);

  // Link button
  m_btnLink = new QPushButton("Link");
  m_btnLink->setToolTip("Create dependency from first selected node to second selected node");
  connect(m_btnLink, &QPushButton::clicked, this, &DependencyGraphWindow::onLinkSelected);
  m_toolbar->addWidget(m_btnLink);

  m_toolbar->addSeparator();

  // Zoom controls
  m_btnZoomIn = new QPushButton("+");
  m_btnZoomIn->setFixedWidth(30);
  m_btnZoomIn->setToolTip("Zoom In (Mouse Wheel Up)");
  connect(m_btnZoomIn, &QPushButton::clicked, this, &DependencyGraphWindow::onZoomIn);
  m_toolbar->addWidget(m_btnZoomIn);

  m_btnZoomOut = new QPushButton("-");
  m_btnZoomOut->setFixedWidth(30);
  m_btnZoomOut->setToolTip("Zoom Out (Mouse Wheel Down)");
  connect(m_btnZoomOut, &QPushButton::clicked, this, &DependencyGraphWindow::onZoomOut);
  m_toolbar->addWidget(m_btnZoomOut);

  m_btnFitToView = new QPushButton("Fit");
  m_btnFitToView->setToolTip("Fit all nodes in view");
  connect(m_btnFitToView, &QPushButton::clicked, this, &DependencyGraphWindow::onFitToView);
  m_toolbar->addWidget(m_btnFitToView);

  m_toolbar->addSeparator();

  // Help button
  m_btnHelp = new QPushButton("?");
  m_btnHelp->setFixedWidth(30);
  m_btnHelp->setToolTip("Show help");
  connect(m_btnHelp, &QPushButton::clicked, this, &DependencyGraphWindow::onShowHelp);
  m_toolbar->addWidget(m_btnHelp);

  // Spacer
  QWidget* spacer = new QWidget();
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_toolbar->addWidget(spacer);

  // Program name label
  if (m_programManager) {
    QLabel* programLabel = new QLabel(QString::fromStdString(m_programManager->getProgramName()));
    programLabel->setStyleSheet("font-weight: bold; padding-right: 10px;");
    m_toolbar->addWidget(programLabel);
  }
}

void DependencyGraphWindow::applyDarkTheme()
{
  setStyleSheet(R"(
    QDialog {
      background-color: #252526;
    }
    QToolBar {
      background-color: #333333;
      border: none;
      padding: 4px;
      spacing: 4px;
    }
    QPushButton {
      background-color: #3c3c3c;
      color: #cccccc;
      border: 1px solid #555555;
      padding: 5px 10px;
      border-radius: 3px;
    }
    QPushButton:hover {
      background-color: #505050;
    }
    QPushButton:pressed {
      background-color: #1177bb;
    }
    QGraphicsView {
      border: none;
      background-color: #1e1e1e;
    }
    QLabel {
      color: #cccccc;
    }
    QScrollBar:vertical {
      background-color: #2a2a2a;
      width: 12px;
    }
    QScrollBar::handle:vertical {
      background-color: #555555;
      border-radius: 4px;
      min-height: 20px;
    }
    QScrollBar:horizontal {
      background-color: #2a2a2a;
      height: 12px;
    }
    QScrollBar::handle:horizontal {
      background-color: #555555;
      border-radius: 4px;
      min-width: 20px;
    }
  )");
}

void DependencyGraphWindow::refreshGraph()
{
  m_scene->loadFromProgram();

  // Fit to view after loading
  QTimer::singleShot(100, this, &DependencyGraphWindow::onFitToView);
}

void DependencyGraphWindow::onAutoLayout()
{
  // Clear saved positions so auto-layout is used
  if (m_programManager) {
    m_programManager->clearNodePositions();
  }
  m_scene->autoLayout();
}

void DependencyGraphWindow::onLinkSelected()
{
  QString error = m_scene->linkSelectedNodes();

  if (error.isEmpty()) {
    m_statusLabel->setText("Dependency created successfully");
  } else {
    m_statusLabel->setText(error);
  }
}

void DependencyGraphWindow::onZoomIn()
{
  m_graphicsView->zoomIn();
}

void DependencyGraphWindow::onZoomOut()
{
  m_graphicsView->zoomOut();
}

void DependencyGraphWindow::onFitToView()
{
  m_graphicsView->fitToContents();
}

void DependencyGraphWindow::onShowHelp()
{
  QMessageBox::information(this, "Dependency Graph Help",
    "<b>Creating Dependencies:</b><br>"
    "1. <b>Link button:</b> Select 2 nodes (Ctrl+click), then click Link<br>"
    "2. <b>Drag:</b> From <b>right edge</b> of source to <b>left edge</b> of target<br><br>"
    "<b>Editing Dependencies:</b><br>"
    "Right-click on an arrow to edit its condition or delete it.<br><br>"
    "<b>Navigation:</b><br>"
    "- Mouse wheel: Zoom in/out<br>"
    "- Middle mouse button: Pan<br>"
    "- Alt + Left mouse: Pan<br>"
    "- Drag nodes: Reposition<br><br>"
    "<b>Colors:</b><br>"
    "- Green: Enabled recipe<br>"
    "- Gray: Disabled recipe<br>"
    "- Blue border: Selected<br>"
    "- Orange glow: Prerequisite highlight"
  );
}

void DependencyGraphWindow::onDependencyCreated(int sourceIndex, int targetIndex)
{
  if (m_programManager) {
    m_programManager->addRecipeDependency(targetIndex, sourceIndex);
    m_statusLabel->setText(QString("Created dependency: Recipe %1 -> Recipe %2")
                           .arg(sourceIndex + 1).arg(targetIndex + 1));
  }
}

void DependencyGraphWindow::onDependencyRemoved(int sourceIndex, int targetIndex)
{
  if (m_programManager) {
    m_programManager->removeRecipeDependency(targetIndex, sourceIndex);
    m_statusLabel->setText(QString("Removed dependency: Recipe %1 -> Recipe %2")
                           .arg(sourceIndex + 1).arg(targetIndex + 1));
  }
}

void DependencyGraphWindow::onEditConditionRequested(int sourceIndex, int targetIndex)
{
  if (!m_programManager) return;

  QString currentCondition = QString::fromStdString(
    m_programManager->getDependencyCondition(targetIndex, sourceIndex));

  bool ok;
  QString newCondition = QInputDialog::getText(
    this,
    "Edit Dependency Condition",
    QString("Condition for Recipe %1 -> Recipe %2:\n"
            "(Leave empty for unconditional dependency)")
            .arg(sourceIndex + 1).arg(targetIndex + 1),
    QLineEdit::Normal,
    currentCondition,
    &ok
  );

  if (ok) {
    m_programManager->setDependencyCondition(targetIndex, sourceIndex, newCondition.toStdString());

    // Update the edge label
    DependencyEdgeItem* edge = m_scene->findEdge(sourceIndex, targetIndex);
    if (edge) {
      edge->setCondition(newCondition);
    }

    m_statusLabel->setText(QString("Updated condition for dependency: Recipe %1 -> Recipe %2")
                           .arg(sourceIndex + 1).arg(targetIndex + 1));
  }
}

void DependencyGraphWindow::onNodePositionChanged(int recipeIndex, double x, double y)
{
  if (m_programManager) {
    m_programManager->setNodePosition(recipeIndex, x, y);
  }
}

void DependencyGraphWindow::setupContextVariablePanel()
{
  m_contextVarPanel = new QWidget();
  m_contextVarPanel->setMinimumWidth(200);
  m_contextVarPanel->setMaximumWidth(400);

  QVBoxLayout* panelLayout = new QVBoxLayout(m_contextVarPanel);
  panelLayout->setContentsMargins(8, 8, 8, 8);
  panelLayout->setSpacing(8);

  // Title label
  QLabel* titleLabel = new QLabel("Context Variables");
  titleLabel->setStyleSheet(
    "QLabel { "
    "  font-weight: bold; "
    "  font-size: 14px; "
    "  color: #4ec9b0; "
    "  padding: 4px 0; "
    "}"
  );
  panelLayout->addWidget(titleLabel);

  // Selected recipe label
  m_selectedRecipeLabel = new QLabel("Select a recipe to view variables");
  m_selectedRecipeLabel->setStyleSheet(
    "QLabel { "
    "  color: #888888; "
    "  font-style: italic; "
    "  padding: 4px 0; "
    "}"
  );
  m_selectedRecipeLabel->setWordWrap(true);
  panelLayout->addWidget(m_selectedRecipeLabel);

  // Context variable list
  m_contextVarList = new QListWidget();
  m_contextVarList->setStyleSheet(
    "QListWidget { "
    "  background-color: #1e1e1e; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 4px; "
    "  color: #dcdcaa; "
    "  font-family: 'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace; "
    "  font-size: 12px; "
    "} "
    "QListWidget::item { "
    "  padding: 6px 8px; "
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
  m_contextVarList->setToolTip("Left-click: copy with $\nRight-click: copy without $");
  m_contextVarList->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_contextVarList, &QListWidget::itemClicked,
          this, &DependencyGraphWindow::onVariableItemClicked);
  connect(m_contextVarList, &QWidget::customContextMenuRequested,
          this, [this](const QPoint& pos) {
    QListWidgetItem* item = m_contextVarList->itemAt(pos);
    if (!item) return;

    QString varName = item->text();
    if (varName.startsWith("(")) return;  // Skip placeholder item

    QMenu menu;
    QAction* copyWithDollar = menu.addAction("Copy with $ (for conditions)");
    QAction* copyWithoutDollar = menu.addAction("Copy without $ (for keys)");

    QAction* selected = menu.exec(m_contextVarList->mapToGlobal(pos));

    if (selected == copyWithDollar) {
      QApplication::clipboard()->setText(varName);
      m_statusLabel->setText(QString("Copied '%1' to clipboard").arg(varName));
    } else if (selected == copyWithoutDollar) {
      QString nameWithoutDollar = varName.mid(1);  // Remove leading $
      QApplication::clipboard()->setText(nameWithoutDollar);
      m_statusLabel->setText(QString("Copied '%1' to clipboard").arg(nameWithoutDollar));
    }
  });
  panelLayout->addWidget(m_contextVarList, 1);

  // Hint label
  QLabel* hintLabel = new QLabel("Left-click: copy with $\nRight-click: copy without $");
  hintLabel->setStyleSheet(
    "QLabel { "
    "  color: #666666; "
    "  font-size: 11px; "
    "  padding: 4px 0; "
    "}"
  );
  hintLabel->setWordWrap(true);
  panelLayout->addWidget(hintLabel);

  // Set panel background
  m_contextVarPanel->setStyleSheet(
    "QWidget { background-color: #252526; }"
  );
}

void DependencyGraphWindow::onSelectionChanged()
{
  QList<QGraphicsItem*> selectedItems = m_scene->selectedItems();

  // Find selected recipe nodes
  QList<RecipeNodeItem*> selectedNodes;
  for (QGraphicsItem* item : selectedItems) {
    RecipeNodeItem* node = dynamic_cast<RecipeNodeItem*>(item);
    if (node) {
      selectedNodes.append(node);
    }
  }

  if (selectedNodes.size() == 1) {
    // Single node selected - show its context variables
    RecipeNodeItem* node = selectedNodes.first();
    updateContextVariablesForRecipe(node->getRecipeIndex());
  } else {
    // No node or multiple nodes selected - clear the panel
    m_contextVarList->clear();
    if (selectedNodes.isEmpty()) {
      m_selectedRecipeLabel->setText("Select a recipe to view variables");
    } else {
      m_selectedRecipeLabel->setText(QString("%1 recipes selected").arg(selectedNodes.size()));
    }
    m_selectedRecipeLabel->setStyleSheet(
      "QLabel { color: #888888; font-style: italic; padding: 4px 0; }"
    );
  }
}

void DependencyGraphWindow::updateContextVariablesForRecipe(int recipeIndex)
{
  m_contextVarList->clear();

  if (!m_programManager) {
    m_selectedRecipeLabel->setText("No program loaded");
    return;
  }

  const RecipeReference* recipeRef = m_programManager->getRecipe(recipeIndex);
  if (!recipeRef) {
    m_selectedRecipeLabel->setText("Invalid recipe");
    return;
  }

  // Update selected recipe label
  m_selectedRecipeLabel->setText(QString("#%1: %2")
    .arg(recipeIndex + 1)
    .arg(QString::fromStdString(recipeRef->displayName)));
  m_selectedRecipeLabel->setStyleSheet(
    "QLabel { color: #4ec9b0; font-weight: bold; padding: 4px 0; }"
  );

  // Extract context variables from the recipe file
  QStringList variables = extractContextVariablesFromRecipe(recipeRef->recipeFilePath);

  if (variables.isEmpty()) {
    QListWidgetItem* item = new QListWidgetItem("(No context variables defined)");
    item->setForeground(QColor("#888888"));
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    m_contextVarList->addItem(item);
  } else {
    for (const QString& var : variables) {
      QListWidgetItem* item = new QListWidgetItem("$" + var);
      item->setToolTip(QString("Click to copy: $%1").arg(var));
      m_contextVarList->addItem(item);
    }
  }

  std::cout << "[DependencyGraph] Recipe #" << (recipeIndex + 1)
            << " has " << variables.size() << " context variables" << std::endl;
}

QStringList DependencyGraphWindow::extractContextVariablesFromRecipe(const std::string& recipeFilePath)
{
  QSet<QString> variables;

  // Load the recipe JSON file
  QFile file(QString::fromStdString(recipeFilePath));
  if (!file.open(QIODevice::ReadOnly)) {
    std::cerr << "[DependencyGraph] Failed to open recipe file: " << recipeFilePath << std::endl;
    return {};
  }

  QByteArray jsonData = file.readAll();
  file.close();

  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
  if (parseError.error != QJsonParseError::NoError) {
    std::cerr << "[DependencyGraph] JSON parse error: " << parseError.errorString().toStdString() << std::endl;
    return {};
  }

  QJsonObject root = doc.object();
  QJsonArray steps = root["steps"].toArray();

  // Scan all steps for context variable definitions
  for (const QJsonValue& stepVal : steps) {
    QJsonObject step = stepVal.toObject();

    // Skip disabled steps
    if (!step["enabled"].toBool(true)) {
      continue;
    }

    QJsonObject params = step["parameters"].toObject();

    // Check each parameter key that could define a context variable
    for (const QString& key : CONTEXT_PARAM_KEYS) {
      if (params.contains(key)) {
        QString value = params[key].toString();
        // Skip empty values and references (starting with $)
        if (!value.isEmpty() && !value.startsWith("$")) {
          variables.insert(value);
        }
      }
    }
  }

  // Sort and return
  QStringList sortedVars = variables.values();
  sortedVars.sort(Qt::CaseInsensitive);
  return sortedVars;
}

void DependencyGraphWindow::onVariableItemClicked(QListWidgetItem* item)
{
  if (!item) return;

  QString varName = item->text();
  if (varName.startsWith("(")) {
    // This is the "(No context variables)" item, don't copy
    return;
  }

  // Copy to clipboard
  QClipboard* clipboard = QApplication::clipboard();
  clipboard->setText(varName);

  // Update status
  m_statusLabel->setText(QString("Copied '%1' to clipboard").arg(varName));
}

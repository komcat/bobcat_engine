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
  , m_scene(nullptr)
  , m_graphicsView(nullptr)
  , m_setTabWidget(nullptr)
  , m_currentSetIndex(0)
{
  setWindowTitle("Recipe Dependency Graph");
  setMinimumSize(800, 600);
  resize(1000, 700);

  setupUI();
  applyDarkTheme();

  // Initialize with current active set
  if (m_programManager) {
    m_currentSetIndex = m_programManager->getActiveDependencySetIndex();
  }

  // Load initial graph
  refreshSetTabs();
}

DependencyGraphWindow::~DependencyGraphWindow()
{
  // Disconnect selection signals before scene cleanup to prevent crash
  // during destruction when clearGraph() triggers selectionChanged
  for (auto scene : m_setScenes.values()) {
    if (scene) {
      disconnect(scene, &QGraphicsScene::selectionChanged,
                 this, &DependencyGraphWindow::onSelectionChanged);
    }
  }
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

  // Create set management bar
  setupSetManagementBar();
  mainLayout->addWidget(m_setManagementBar);

  // Create tab widget for dependency sets
  m_setTabWidget = new QTabWidget();
  m_setTabWidget->setTabPosition(QTabWidget::North);
  m_setTabWidget->setDocumentMode(true);
  connect(m_setTabWidget, &QTabWidget::currentChanged,
          this, &DependencyGraphWindow::onSetTabChanged);

  // Create context variable panel
  setupContextVariablePanel();

  // Create splitter with tab widget and context panel
  m_splitter = new QSplitter(Qt::Horizontal);
  m_splitter->addWidget(m_setTabWidget);
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

void DependencyGraphWindow::setupSetManagementBar()
{
  m_setManagementBar = new QWidget();
  QHBoxLayout* barLayout = new QHBoxLayout(m_setManagementBar);
  barLayout->setContentsMargins(10, 4, 10, 4);
  barLayout->setSpacing(8);

  // Add Set button
  m_btnAddSet = new QPushButton("+ New Set");
  m_btnAddSet->setToolTip("Create a new dependency set");
  connect(m_btnAddSet, &QPushButton::clicked, this, &DependencyGraphWindow::onAddSet);
  barLayout->addWidget(m_btnAddSet);

  // Duplicate Set button
  m_btnDuplicateSet = new QPushButton("Duplicate");
  m_btnDuplicateSet->setToolTip("Duplicate the current dependency set");
  connect(m_btnDuplicateSet, &QPushButton::clicked, this, &DependencyGraphWindow::onDuplicateSet);
  barLayout->addWidget(m_btnDuplicateSet);

  // Delete Set button
  m_btnDeleteSet = new QPushButton("Delete");
  m_btnDeleteSet->setToolTip("Delete the current dependency set (cannot delete POR set)");
  connect(m_btnDeleteSet, &QPushButton::clicked, this, &DependencyGraphWindow::onDeleteSet);
  barLayout->addWidget(m_btnDeleteSet);

  // Rename Set button
  m_btnRenameSet = new QPushButton("Rename");
  m_btnRenameSet->setToolTip("Rename the current dependency set");
  connect(m_btnRenameSet, &QPushButton::clicked, this, &DependencyGraphWindow::onRenameSet);
  barLayout->addWidget(m_btnRenameSet);

  barLayout->addSpacing(20);

  // Set as POR button
  m_btnSetAsPOR = new QPushButton("Set as Executing");
  m_btnSetAsPOR->setToolTip("Mark this set as the Plan of Record (used for actual execution)");
  m_btnSetAsPOR->setStyleSheet(
    "QPushButton { background-color: #2e7d32; border: 1px solid #1b5e20; }"
    "QPushButton:hover { background-color: #388e3c; }"
    "QPushButton:disabled { background-color: #3c3c3c; border: 1px solid #555555; color: #888888; }"
  );
  connect(m_btnSetAsPOR, &QPushButton::clicked, this, &DependencyGraphWindow::onSetAsPOR);
  barLayout->addWidget(m_btnSetAsPOR);

  barLayout->addStretch();

  m_setManagementBar->setStyleSheet("background-color: #2d2d2d;");
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
  // Refresh the current tab's scene
  if (m_scene) {
    m_scene->loadFromProgram();
  }

  // Fit to view after loading
  QTimer::singleShot(100, this, &DependencyGraphWindow::onFitToView);
}

void DependencyGraphWindow::refreshSetTabs()
{
  if (!m_programManager || !m_setTabWidget) return;

  // Block signals during rebuild
  m_setTabWidget->blockSignals(true);

  // Clear existing tabs
  while (m_setTabWidget->count() > 0) {
    QWidget* widget = m_setTabWidget->widget(0);
    m_setTabWidget->removeTab(0);
    // Don't delete - we manage these separately
  }

  // Clear scene/view maps
  for (auto scene : m_setScenes.values()) {
    if (scene) {
      disconnect(scene, nullptr, this, nullptr);
      delete scene;
    }
  }
  m_setScenes.clear();

  for (auto view : m_setViews.values()) {
    if (view) delete view;
  }
  m_setViews.clear();

  int setCount = m_programManager->getDependencySetCount();
  int activeIndex = m_programManager->getActiveDependencySetIndex();

  // Create tabs for each dependency set
  for (int i = 0; i < setCount; ++i) {
    QString setName = QString::fromStdString(m_programManager->getDependencySetName(i));

    // Create scene for this set
    DependencyGraphScene* scene = new DependencyGraphScene(m_programManager, this);
    scene->setDependencySetIndex(i);
    m_setScenes[i] = scene;

    // Create view for this set
    DependencyGraphView* view = new DependencyGraphView(scene, this);
    m_setViews[i] = view;

    // Connect scene signals
    connect(scene, &DependencyGraphScene::dependencyCreated,
            this, &DependencyGraphWindow::onDependencyCreated);
    connect(scene, &DependencyGraphScene::dependencyRemoved,
            this, &DependencyGraphWindow::onDependencyRemoved);
    connect(scene, &DependencyGraphScene::editConditionRequested,
            this, &DependencyGraphWindow::onEditConditionRequested);
    connect(scene, &DependencyGraphScene::nodePositionChanged,
            this, &DependencyGraphWindow::onNodePositionChanged);
    connect(scene, &QGraphicsScene::selectionChanged,
            this, &DependencyGraphWindow::onSelectionChanged);

    // Add tab with POR suffix for active set
    QString tabTitle = (i == activeIndex) ? setName + " [POR]" : setName;
    m_setTabWidget->addTab(view, tabTitle);

    // Load the graph
    scene->loadFromProgram();
  }

  // Set current tab
  m_currentSetIndex = activeIndex;
  if (m_currentSetIndex >= 0 && m_currentSetIndex < m_setTabWidget->count()) {
    m_setTabWidget->setCurrentIndex(m_currentSetIndex);
  }

  // Update current scene reference
  if (m_setScenes.contains(m_currentSetIndex)) {
    m_scene = m_setScenes[m_currentSetIndex];
    m_graphicsView = m_setViews[m_currentSetIndex];
  }

  m_setTabWidget->blockSignals(false);

  // Update button states and tab styles
  updateSetButtonStates();
  updateTabStyles();

  std::cout << "[DependencyGraph] Refreshed " << setCount << " dependency set tabs" << std::endl;
}

void DependencyGraphWindow::onSetTabChanged(int tabIndex)
{
  if (tabIndex < 0 || !m_programManager) return;

  m_currentSetIndex = tabIndex;

  // Update current scene and view references
  if (m_setScenes.contains(tabIndex)) {
    m_scene = m_setScenes[tabIndex];
  }
  if (m_setViews.contains(tabIndex)) {
    m_graphicsView = m_setViews[tabIndex];
  }

  // Update button states
  updateSetButtonStates();

  m_statusLabel->setText(QString("Viewing dependency set: %1")
    .arg(QString::fromStdString(m_programManager->getDependencySetName(tabIndex))));
}

void DependencyGraphWindow::onAddSet()
{
  if (!m_programManager) return;

  // Check if we can add more sets
  if (m_programManager->getDependencySetCount() >= MAX_DEPENDENCY_SETS) {
    QMessageBox::warning(this, "Cannot Add Set",
      QString("Maximum of %1 dependency sets allowed.").arg(MAX_DEPENDENCY_SETS));
    return;
  }

  bool ok;
  QString name = QInputDialog::getText(this, "New Dependency Set",
    "Enter name for the new dependency set:",
    QLineEdit::Normal, "New Set", &ok);

  if (ok && !name.isEmpty()) {
    int newIndex = m_programManager->createDependencySet(name.toStdString());
    if (newIndex >= 0) {
      refreshSetTabs();
      m_setTabWidget->setCurrentIndex(newIndex);
      m_statusLabel->setText(QString("Created dependency set: %1").arg(name));
    }
  }
}

void DependencyGraphWindow::onDeleteSet()
{
  if (!m_programManager) return;

  int currentIndex = m_setTabWidget->currentIndex();
  int activeIndex = m_programManager->getActiveDependencySetIndex();

  // Check if trying to delete active set
  if (currentIndex == activeIndex) {
    QMessageBox::warning(this, "Cannot Delete",
      "Cannot delete the active (POR) dependency set.\n"
      "Please set another set as executing first.");
    return;
  }

  // Check if only one set remains
  if (m_programManager->getDependencySetCount() <= 1) {
    QMessageBox::warning(this, "Cannot Delete",
      "Cannot delete the last dependency set.");
    return;
  }

  QString setName = QString::fromStdString(m_programManager->getDependencySetName(currentIndex));

  QMessageBox::StandardButton reply = QMessageBox::question(this, "Delete Dependency Set",
    QString("Are you sure you want to delete '%1'?\n\n"
            "This will permanently remove all dependencies in this set.")
    .arg(setName),
    QMessageBox::Yes | QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    if (m_programManager->deleteDependencySet(currentIndex)) {
      refreshSetTabs();
      m_statusLabel->setText(QString("Deleted dependency set: %1").arg(setName));
    }
  }
}

void DependencyGraphWindow::onDuplicateSet()
{
  if (!m_programManager) return;

  // Check if we can add more sets
  if (m_programManager->getDependencySetCount() >= MAX_DEPENDENCY_SETS) {
    QMessageBox::warning(this, "Cannot Duplicate",
      QString("Maximum of %1 dependency sets allowed.").arg(MAX_DEPENDENCY_SETS));
    return;
  }

  int currentIndex = m_setTabWidget->currentIndex();
  QString currentName = QString::fromStdString(m_programManager->getDependencySetName(currentIndex));

  bool ok;
  QString name = QInputDialog::getText(this, "Duplicate Dependency Set",
    QString("Enter name for the copy of '%1':").arg(currentName),
    QLineEdit::Normal, currentName + " (Copy)", &ok);

  if (ok && !name.isEmpty()) {
    int newIndex = m_programManager->duplicateDependencySet(currentIndex, name.toStdString());
    if (newIndex >= 0) {
      refreshSetTabs();
      m_setTabWidget->setCurrentIndex(newIndex);
      m_statusLabel->setText(QString("Duplicated dependency set as: %1").arg(name));
    }
  }
}

void DependencyGraphWindow::onSetAsPOR()
{
  if (!m_programManager) return;

  int currentIndex = m_setTabWidget->currentIndex();

  if (currentIndex == m_programManager->getActiveDependencySetIndex()) {
    // Already the active set
    return;
  }

  QString setName = QString::fromStdString(m_programManager->getDependencySetName(currentIndex));

  if (m_programManager->setActiveDependencySet(currentIndex)) {
    refreshSetTabs();
    m_statusLabel->setText(QString("'%1' is now the executing dependency set").arg(setName));
  }
}

void DependencyGraphWindow::onRenameSet()
{
  if (!m_programManager) return;

  int currentIndex = m_setTabWidget->currentIndex();
  QString currentName = QString::fromStdString(m_programManager->getDependencySetName(currentIndex));

  bool ok;
  QString name = QInputDialog::getText(this, "Rename Dependency Set",
    "Enter new name:",
    QLineEdit::Normal, currentName, &ok);

  if (ok && !name.isEmpty() && name != currentName) {
    if (m_programManager->renameDependencySet(currentIndex, name.toStdString())) {
      refreshSetTabs();
      m_statusLabel->setText(QString("Renamed dependency set to: %1").arg(name));
    }
  }
}

void DependencyGraphWindow::updateSetButtonStates()
{
  if (!m_programManager) return;

  int currentIndex = m_setTabWidget->currentIndex();
  int activeIndex = m_programManager->getActiveDependencySetIndex();
  int setCount = m_programManager->getDependencySetCount();

  // Add button - disabled if max sets reached
  m_btnAddSet->setEnabled(setCount < MAX_DEPENDENCY_SETS);

  // Delete button - disabled if only 1 set or current is POR
  m_btnDeleteSet->setEnabled(setCount > 1 && currentIndex != activeIndex);

  // Duplicate button - disabled if max sets reached
  m_btnDuplicateSet->setEnabled(setCount < MAX_DEPENDENCY_SETS);

  // Set as POR button - disabled if already POR
  m_btnSetAsPOR->setEnabled(currentIndex != activeIndex);
}

void DependencyGraphWindow::updateTabStyles()
{
  if (!m_programManager || !m_setTabWidget) return;

  int activeIndex = m_programManager->getActiveDependencySetIndex();

  // Update tab bar styling to highlight POR
  QString tabStyle = QString(
    "QTabBar::tab { "
    "  background-color: #3c3c3c; "
    "  color: #cccccc; "
    "  padding: 8px 16px; "
    "  margin-right: 2px; "
    "  border-top-left-radius: 4px; "
    "  border-top-right-radius: 4px; "
    "} "
    "QTabBar::tab:selected { "
    "  background-color: #1e1e1e; "
    "} "
    "QTabBar::tab:hover { "
    "  background-color: #505050; "
    "} "
    "QTabWidget::pane { "
    "  border: none; "
    "  background-color: #1e1e1e; "
    "}"
  );

  m_setTabWidget->setStyleSheet(tabStyle);

  // Update tab texts with POR indicator
  for (int i = 0; i < m_setTabWidget->count(); ++i) {
    QString name = QString::fromStdString(m_programManager->getDependencySetName(i));
    if (i == activeIndex) {
      m_setTabWidget->setTabText(i, name + " [POR]");
      // Use stylesheet for green color on POR tab
    } else {
      m_setTabWidget->setTabText(i, name);
    }
  }
}

void DependencyGraphWindow::onAutoLayout()
{
  // Clear saved positions so auto-layout is used
  if (m_programManager) {
    m_programManager->clearNodePositions();
  }
  if (m_scene) {
    m_scene->autoLayout();
  }
}

void DependencyGraphWindow::onLinkSelected()
{
  if (!m_scene) return;

  QString error = m_scene->linkSelectedNodes();

  if (error.isEmpty()) {
    m_statusLabel->setText("Dependency created successfully");
  } else {
    m_statusLabel->setText(error);
  }
}

void DependencyGraphWindow::onZoomIn()
{
  if (m_graphicsView) {
    m_graphicsView->zoomIn();
  }
}

void DependencyGraphWindow::onZoomOut()
{
  if (m_graphicsView) {
    m_graphicsView->zoomOut();
  }
}

void DependencyGraphWindow::onFitToView()
{
  if (m_graphicsView) {
    m_graphicsView->fitToContents();
  }
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
    // Use the current set index
    m_programManager->addRecipeDependency(m_currentSetIndex, targetIndex, sourceIndex);
    m_statusLabel->setText(QString("Created dependency in '%1': Recipe %2 -> Recipe %3")
                           .arg(QString::fromStdString(m_programManager->getDependencySetName(m_currentSetIndex)))
                           .arg(sourceIndex + 1).arg(targetIndex + 1));
  }
}

void DependencyGraphWindow::onDependencyRemoved(int sourceIndex, int targetIndex)
{
  if (m_programManager) {
    // Use the current set index
    m_programManager->removeRecipeDependency(m_currentSetIndex, targetIndex, sourceIndex);
    m_statusLabel->setText(QString("Removed dependency from '%1': Recipe %2 -> Recipe %3")
                           .arg(QString::fromStdString(m_programManager->getDependencySetName(m_currentSetIndex)))
                           .arg(sourceIndex + 1).arg(targetIndex + 1));
  }
}

void DependencyGraphWindow::onEditConditionRequested(int sourceIndex, int targetIndex)
{
  if (!m_programManager) return;

  // Use the current set index
  QString currentCondition = QString::fromStdString(
    m_programManager->getDependencyCondition(m_currentSetIndex, targetIndex, sourceIndex));

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
    m_programManager->setDependencyCondition(m_currentSetIndex, targetIndex, sourceIndex, newCondition.toStdString());

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
  if (!m_scene) return;

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

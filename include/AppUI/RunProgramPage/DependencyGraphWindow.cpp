// DependencyGraphWindow.cpp
#include "DependencyGraphWindow.h"
#include "DependencyGraphScene.h"
#include "DependencyEdgeItem.h"
#include "ProgramManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>

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
  mainLayout->addWidget(m_graphicsView, 1);

  // Status bar
  QWidget* statusBar = new QWidget();
  QHBoxLayout* statusLayout = new QHBoxLayout(statusBar);
  statusLayout->setContentsMargins(10, 5, 10, 5);

  m_statusLabel = new QLabel("Drag from right edge of a recipe to left edge of another to create dependency. "
                             "Right-click on arrows to edit or delete.");
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
  m_scene->autoLayout();
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
    "Drag from the <b>right edge</b> of a source recipe to the <b>left edge</b> of a target recipe.<br><br>"
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

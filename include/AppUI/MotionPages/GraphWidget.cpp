// GraphWidget.cpp
#include "GraphWidget.h"
#include "../../products/product_position_manager.h"
#include "../../graph/MotionGraphManager.h"
#include "../../motions/MotionTypes.h"
#include "../../motions/unified_motion_manager.h"
#include "../../sequence/SequenceEngine.h"
#include "../../sequence/SequenceBuilder.h"
#include "../../sequence/SequenceContext.h"
#include "../../sequence/SequenceWorker.h"
#include "../DarkTheme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenu>
#include <QGraphicsSceneMouseEvent>
#include <QLabel>
#include <QFont>
#include <QFrame>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QtMath>
#include <iostream>
#include <cmath>
#include <limits>

// ============================================================================
// CustomGraphicsView Implementation
// ============================================================================

CustomGraphicsView::CustomGraphicsView(QGraphicsScene* scene, QWidget* parent)
  : QGraphicsView(scene, parent)
  , m_isPanning(false)
  , m_currentZoom(1.0)
{
  setDragMode(QGraphicsView::RubberBandDrag);
  setRenderHint(QPainter::Antialiasing);
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
}

void CustomGraphicsView::zoomIn()
{
  if (m_currentZoom * ZOOM_FACTOR <= MAX_ZOOM) {
    scale(ZOOM_FACTOR, ZOOM_FACTOR);
    m_currentZoom *= ZOOM_FACTOR;
  }
}

void CustomGraphicsView::zoomOut()
{
  if (m_currentZoom / ZOOM_FACTOR >= MIN_ZOOM) {
    scale(1.0 / ZOOM_FACTOR, 1.0 / ZOOM_FACTOR);
    m_currentZoom /= ZOOM_FACTOR;
  }
}

void CustomGraphicsView::resetZoom()
{
  // Reset to 1:1 scale
  scale(1.0 / m_currentZoom, 1.0 / m_currentZoom);
  m_currentZoom = 1.0;
}

void CustomGraphicsView::wheelEvent(QWheelEvent* event)
{
  // Zoom in/out with mouse wheel
  const qreal WHEEL_ZOOM_FACTOR = 1.15;

  if (event->angleDelta().y() > 0) {
    // Zoom in
    if (m_currentZoom * WHEEL_ZOOM_FACTOR <= MAX_ZOOM) {
      scale(WHEEL_ZOOM_FACTOR, WHEEL_ZOOM_FACTOR);
      m_currentZoom *= WHEEL_ZOOM_FACTOR;
    }
  } else {
    // Zoom out
    if (m_currentZoom / WHEEL_ZOOM_FACTOR >= MIN_ZOOM) {
      scale(1.0 / WHEEL_ZOOM_FACTOR, 1.0 / WHEEL_ZOOM_FACTOR);
      m_currentZoom /= WHEEL_ZOOM_FACTOR;
    }
  }

  event->accept();
}

void CustomGraphicsView::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::MiddleButton) {
    // Start panning with middle mouse button
    m_isPanning = true;
    m_lastPanPoint = event->pos();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
  } else {
    QGraphicsView::mousePressEvent(event);
  }
}

void CustomGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
  if (m_isPanning) {
    // Pan the view
    QPoint delta = event->pos() - m_lastPanPoint;
    m_lastPanPoint = event->pos();

    // Move the scrollbars
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());

    event->accept();
  } else {
    QGraphicsView::mouseMoveEvent(event);
  }
}

void CustomGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::MiddleButton && m_isPanning) {
    // Stop panning
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    event->accept();
  } else {
    QGraphicsView::mouseReleaseEvent(event);
  }
}

// ============================================================================
// GraphWidget Implementation
// ============================================================================

GraphWidget::GraphWidget(const QString& deviceName,
                         ProductPositionManager* positionManager,
                         QWidget* parent)
  : QWidget(parent)
  , m_deviceName(deviceName)
  , m_currentGraphName("")
  , m_positionManager(positionManager)
  , m_motionManager(nullptr)
  , m_sequenceEngine(nullptr)
  , m_nodeCounter(0)
  , m_edgeCounter(0)
  , m_isModified(false)
  , m_isExecutingPath(false)
  , m_goalPositionName("")
  , m_sequenceWorker(nullptr)
{
  // Initialize color palette with distinct colors
  m_colorPalette << "#27ae60"  // Green
                 << "#3498db"  // Blue
                 << "#e74c3c"  // Red
                 << "#f39c12"  // Orange
                 << "#9b59b6"  // Purple
                 << "#1abc9c"  // Turquoise
                 << "#e67e22"  // Carrot
                 << "#2ecc71"  // Emerald
                 << "#34495e"  // Wet Asphalt
                 << "#f1c40f"; // Yellow

  setupUI();
  applyDarkTheme();
  updateGraphList();

  // Setup position update timer (500ms refresh)
  m_positionUpdateTimer = new QTimer(this);
  m_positionUpdateTimer->setInterval(500);
  connect(m_positionUpdateTimer, &QTimer::timeout, this, &GraphWidget::updateCurrentPosition, Qt::UniqueConnection);
  m_positionUpdateTimer->start();

  // Load default graph if exists
  QList<QString> graphs = m_positionManager->GetGraphManager().getGraphNames(m_deviceName);
  if (!graphs.isEmpty()) {
    loadGraph(graphs.first());
  } else {
    // Create default graph
    m_positionManager->GetGraphManager().createGraph(m_deviceName, "default");
    loadGraph("default");
  }
}

void GraphWidget::cleanup()
{
  // Stop and cleanup sequence worker if running
  if (m_sequenceWorker) {
    m_sequenceWorker->requestStop();
    m_sequenceWorker->wait(3000);  // Wait up to 3 seconds
    m_sequenceWorker->deleteLater();
    m_sequenceWorker = nullptr;
  }

  // Stop timer immediately to prevent any more callbacks
  if (m_positionUpdateTimer) {
    m_positionUpdateTimer->stop();
    disconnect(m_positionUpdateTimer, &QTimer::timeout, this, &GraphWidget::updateCurrentPosition);
  }

  // Save if modified
  if (m_isModified) {
    saveCurrentGraph();
  }
}

GraphWidget::~GraphWidget()
{
  // Final cleanup in case cleanup() wasn't called
  if (m_sequenceWorker) {
    m_sequenceWorker->requestStop();
    m_sequenceWorker->wait(1000);
    delete m_sequenceWorker;
    m_sequenceWorker = nullptr;
  }

  // Disconnect all signals to prevent callbacks during destruction
  // This prevents crashes when child widgets emit signals during their deletion
  if (m_scene) {
    disconnect(m_scene, nullptr, this, nullptr);
  }
  if (m_graphSelector) {
    disconnect(m_graphSelector, nullptr, this, nullptr);
  }
  if (m_goalNodeSelector) {
    disconnect(m_goalNodeSelector, nullptr, this, nullptr);
  }

  // Stop timer to prevent final timeout during destruction
  if (m_positionUpdateTimer) {
    m_positionUpdateTimer->stop();
  }
}

// ============================================================================
// UI SETUP
// ============================================================================

void GraphWidget::setupUI()
{
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(0);

  setupToolbar();

  // Create horizontal layout for scene + legend
  QHBoxLayout* contentLayout = new QHBoxLayout();
  contentLayout->setSpacing(0);
  contentLayout->setContentsMargins(0, 0, 0, 0);

  setupScene();
  contentLayout->addWidget(m_graphicsView, 1);  // Scene takes most space

  // Create legend panel
  m_legendWidget = new QWidget(this);
  m_legendWidget->setMinimumWidth(150);
  m_legendWidget->setMaximumWidth(200);
  m_legendWidget->setStyleSheet(
    "QWidget { "
    "  background-color: #2b2b2b; "
    "  border-left: 1px solid #444; "
    "}"
  );
  m_legendLayout = new QVBoxLayout(m_legendWidget);
  m_legendLayout->setContentsMargins(10, 10, 10, 10);
  m_legendLayout->setSpacing(10);

  QLabel* legendTitle = new QLabel("Device Legend", m_legendWidget);
  QFont titleFont;
  titleFont.setBold(true);
  titleFont.setPointSize(10);
  legendTitle->setFont(titleFont);
  legendTitle->setStyleSheet("color: white; border: none;");
  m_legendLayout->addWidget(legendTitle);

  // Add separator line
  QFrame* separator = new QFrame(m_legendWidget);
  separator->setFrameShape(QFrame::HLine);
  separator->setStyleSheet("background-color: #444; border: none;");
  separator->setFixedHeight(1);
  m_legendLayout->addWidget(separator);

  m_legendLayout->addStretch();  // Push items to top

  // Add execution panel to legend widget
  setupExecutionPanel();
  m_legendLayout->addWidget(m_executionPanel);

  contentLayout->addWidget(m_legendWidget);

  m_mainLayout->addLayout(contentLayout);

  setLayout(m_mainLayout);
}

void GraphWidget::setupToolbar()
{
  m_toolbar = new QToolBar(this);
  m_toolbar->setMovable(false);
  m_toolbar->setIconSize(QSize(16, 16));

  // Graph selector
  m_toolbar->addWidget(new QLabel("Graph: "));
  m_graphSelector = new QComboBox(this);
  m_graphSelector->setMinimumWidth(150);
  connect(m_graphSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &GraphWidget::onGraphSelected);
  m_toolbar->addWidget(m_graphSelector);

  m_toolbar->addSeparator();

  // Graph management
  m_btnNewGraph = new QPushButton("New Graph", this);
  m_btnNewGraph->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5dade2, stop:1 #3498db); "
    "  color: white; "
    "  padding: 6px 12px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6dbdf2, stop:1 #5dade2); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2874a6; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 14px; "
    "}");
  connect(m_btnNewGraph, &QPushButton::clicked, this, &GraphWidget::onNewGraph);
  m_toolbar->addWidget(m_btnNewGraph);

  m_btnRenameGraph = new QPushButton("Rename", this);
  m_btnRenameGraph->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5dade2, stop:1 #3498db); "
    "  color: white; "
    "  padding: 6px 12px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6dbdf2, stop:1 #5dade2); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2874a6; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 14px; "
    "}");
  connect(m_btnRenameGraph, &QPushButton::clicked, this, &GraphWidget::onRenameGraph);
  m_toolbar->addWidget(m_btnRenameGraph);

  m_btnDeleteGraph = new QPushButton("Delete Graph", this);
  m_btnDeleteGraph->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e85d5d, stop:1 #e74c3c); "
    "  color: white; "
    "  padding: 6px 12px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f86d6d, stop:1 #e85d5d); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #c0392b; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 14px; "
    "}");
  connect(m_btnDeleteGraph, &QPushButton::clicked, this, &GraphWidget::onDeleteGraph);
  m_toolbar->addWidget(m_btnDeleteGraph);

  m_toolbar->addSeparator();

  // Node/Edge creation
  m_btnCreateNode = new QPushButton("Create Node", this);
  m_btnCreateNode->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5cbf60, stop:1 #4CAF50); "
    "  color: white; "
    "  padding: 6px 12px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6ccf70, stop:1 #5cbf60); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #3d9041; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 14px; "
    "}");
  connect(m_btnCreateNode, &QPushButton::clicked, this, &GraphWidget::onCreateNode);
  m_toolbar->addWidget(m_btnCreateNode);

  m_btnCreateEdge = new QPushButton("Create Edge", this);
  m_btnCreateEdge->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5cbf60, stop:1 #4CAF50); "
    "  color: white; "
    "  padding: 6px 12px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6ccf70, stop:1 #5cbf60); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #3d9041; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 14px; "
    "}");
  connect(m_btnCreateEdge, &QPushButton::clicked, this, &GraphWidget::onCreateEdge);
  m_toolbar->addWidget(m_btnCreateEdge);

  m_btnDeleteSelected = new QPushButton("Delete Selected", this);
  m_btnDeleteSelected->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e85d5d, stop:1 #e74c3c); "
    "  color: white; "
    "  padding: 6px 12px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f86d6d, stop:1 #e85d5d); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #c0392b; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 14px; "
    "}");
  connect(m_btnDeleteSelected, &QPushButton::clicked, this, &GraphWidget::onDeleteSelected);
  m_toolbar->addWidget(m_btnDeleteSelected);

  m_toolbar->addSeparator();

  // Zoom controls
  m_btnZoomIn = new QPushButton("Zoom +", this);
  m_btnZoomIn->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7f8c8d, stop:1 #6c7a7b); "
    "  color: white; "
    "  padding: 6px 12px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8f9c9d, stop:1 #7f8c8d); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #5a6667; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 14px; "
    "}");
  connect(m_btnZoomIn, &QPushButton::clicked, this, &GraphWidget::onZoomIn);
  m_toolbar->addWidget(m_btnZoomIn);

  m_btnZoomOut = new QPushButton("Zoom -", this);
  m_btnZoomOut->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7f8c8d, stop:1 #6c7a7b); "
    "  color: white; "
    "  padding: 6px 12px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8f9c9d, stop:1 #7f8c8d); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #5a6667; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 14px; "
    "}");
  connect(m_btnZoomOut, &QPushButton::clicked, this, &GraphWidget::onZoomOut);
  m_toolbar->addWidget(m_btnZoomOut);

  m_btnResetZoom = new QPushButton("Reset Zoom", this);
  m_btnResetZoom->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #7f8c8d, stop:1 #6c7a7b); "
    "  color: white; "
    "  padding: 6px 12px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #8f9c9d, stop:1 #7f8c8d); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #5a6667; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 8px; "
    "  padding-left: 14px; "
    "}");
  connect(m_btnResetZoom, &QPushButton::clicked, this, &GraphWidget::onResetZoom);
  m_toolbar->addWidget(m_btnResetZoom);

  m_mainLayout->addWidget(m_toolbar);
}

void GraphWidget::setupScene()
{
  m_scene = new QGraphicsScene(this);
  m_scene->setSceneRect(-2000, -2000, 4000, 4000);
  m_scene->setBackgroundBrush(QBrush(QColor(30, 30, 30)));  // Dark background

  m_graphicsView = new CustomGraphicsView(m_scene, this);

  connect(m_scene, &QGraphicsScene::selectionChanged,
          this, &GraphWidget::onSceneSelectionChanged);

  m_mainLayout->addWidget(m_graphicsView);
}

void GraphWidget::applyDarkTheme()
{
  setStyleSheet(DarkTheme::getStyleSheet());
  m_toolbar->setStyleSheet(DarkTheme::getStyleSheet());
}

// ============================================================================
// LOAD / SAVE
// ============================================================================

void GraphWidget::loadGraph(const QString& graphName)
{
  if (m_isModified && !m_currentGraphName.isEmpty()) {
    saveCurrentGraph();
  }

  m_currentGraphName = graphName;
  clearScene();

  MotionGraph* graph = getCurrentGraph();
  if (!graph) {
    return;
  }

  // Create node items and register device colors
  for (const MotionNode& node : graph->getAllNodes()) {
    // Ensure device color is registered
    if (!node.device.isEmpty()) {
      getDeviceColor(node.device);
    }

    MotionNodeItem* nodeItem = createNodeItem(node);
    m_scene->addItem(nodeItem);
    m_nodeItems[node.id] = nodeItem;
  }

  // Create edge items
  for (const MotionEdge& edge : graph->getAllEdges()) {
    MotionEdgeItem* edgeItem = createEdgeItem(edge);
    m_scene->addItem(edgeItem);
    m_edgeItems[edge.id] = edgeItem;
  }

  // ✅ FIX: Update counters based on loaded IDs to prevent duplicate IDs
  // Find highest node counter
  m_nodeCounter = 0;
  for (const MotionNode& node : graph->getAllNodes()) {
    // Extract number from node_X format
    if (node.id.startsWith("node_")) {
      QString numStr = node.id.mid(5); // Skip "node_"
      bool ok;
      int num = numStr.toInt(&ok);
      if (ok && num > m_nodeCounter) {
        m_nodeCounter = num;
      }
    }
  }

  // Find highest edge counter
  m_edgeCounter = 0;
  for (const MotionEdge& edge : graph->getAllEdges()) {
    // Extract number from edge_X format
    if (edge.id.startsWith("edge_")) {
      QString numStr = edge.id.mid(5); // Skip "edge_"
      bool ok;
      int num = numStr.toInt(&ok);
      if (ok && num > m_edgeCounter) {
        m_edgeCounter = num;
      }
    }
  }

  m_isModified = false;

  // Update node selectors for execution panel
  updateNodeSelectors();

  emit graphSwitched(graphName);
}

void GraphWidget::saveCurrentGraph()
{
  if (m_currentGraphName.isEmpty()) {
    return;
  }

  MotionGraph* graph = getCurrentGraph();
  if (!graph) {
    return;
  }

  // Clear existing graph data
  graph->clear();

  // Save nodes from visual items
  for (auto it = m_nodeItems.begin(); it != m_nodeItems.end(); ++it) {
    MotionNodeItem* nodeItem = it.value();

    MotionNode node;
    node.id = nodeItem->getNodeId();
    node.label = nodeItem->getLabel();
    node.device = nodeItem->getDeviceName();
    node.position = nodeItem->getPositionName();
    node.uiPosition = nodeItem->pos();
    node.color = getDeviceColor(node.device).name();  // Save device color

    graph->addNode(node);
  }

  // Save edges from visual items
  for (auto it = m_edgeItems.begin(); it != m_edgeItems.end(); ++it) {
    MotionEdgeItem* edgeItem = it.value();

    MotionEdge edge;
    edge.id = edgeItem->getEdgeId();
    edge.fromNodeId = edgeItem->getFromNode()->getNodeId();
    edge.toNodeId = edgeItem->getToNode()->getNodeId();
    edge.maxSpeed = edgeItem->getMaxSpeed();
    edge.label = "";
    edge.color = "#ffffff";  // Default white

    graph->addEdge(edge);
  }

  m_isModified = false;
  emit graphModified();
}

// ============================================================================
// TOOLBAR ACTIONS
// ============================================================================

void GraphWidget::onCreateNode()
{
  // Use the current device from the page-level device selector
  // (Device is selected via the device button stack on the left panel)
  QString selectedDevice = m_deviceName;

  if (selectedDevice.isEmpty()) {
    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("No device selected.");
    msgBox.setInformativeText("Please select a device from the left panel first.");
    msgBox.exec();
    return;
  }

  // Get available positions for current device
  std::map<std::string, PositionStruct> positions =
    m_positionManager->GetAllPositions(selectedDevice.toStdString());

  if (positions.empty()) {
    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("No positions available for this device.");
    msgBox.setInformativeText("Please create positions first in the Actions tab.");
    msgBox.exec();
    return;
  }

  // Build position list
  QStringList positionNames;
  for (const auto& pair : positions) {
    positionNames << QString::fromStdString(pair.first);
  }

  // Show position selection dialog
  QInputDialog positionDialog(this);
  positionDialog.setStyleSheet(DarkTheme::getStyleSheet());
  positionDialog.setComboBoxItems(positionNames);
  positionDialog.setWindowTitle("Create Node");
  positionDialog.setLabelText("Select position:");

  if (positionDialog.exec() == QDialog::Accepted) {
    QString selectedPosition = positionDialog.textValue();

    // Create new node with device-specific color
    MotionNode node;
    node.id = generateNodeId();
    node.label = selectedPosition;
    node.device = selectedDevice;
    node.position = selectedPosition;
    node.uiPosition = QPointF(0, 0);  // Center of scene
    node.color = getDeviceColor(selectedDevice).name();  // Use device color

    // Create visual item
    MotionNodeItem* nodeItem = createNodeItem(node);
    m_scene->addItem(nodeItem);
    m_nodeItems[node.id] = nodeItem;

    m_isModified = true;

    // Update node selectors
    updateNodeSelectors();

    // ✅ Auto-save graph immediately after creating node
    saveCurrentGraph();

    std::cout << "[GraphWidget] Created node: " << node.id.toStdString()
              << " (" << node.label.toStdString() << ")" << std::endl;
  }
}

void GraphWidget::onCreateEdge()
{
  // Get selected nodes
  QList<QGraphicsItem*> selectedItems = m_scene->selectedItems();
  QList<MotionNodeItem*> selectedNodes;

  for (QGraphicsItem* item : selectedItems) {
    MotionNodeItem* node = dynamic_cast<MotionNodeItem*>(item);
    if (node) {
      selectedNodes.append(node);
    }
  }

  if (selectedNodes.size() != 2) {
    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("Please select exactly 2 nodes to connect.");
    msgBox.exec();
    return;
  }

  // Ask for max speed
  bool ok;
  QInputDialog inputDialog(this);
  inputDialog.setStyleSheet(DarkTheme::getStyleSheet());
  inputDialog.setWindowTitle("Create Edge");
  inputDialog.setLabelText("Max Speed (mm/s, 0 = unlimited):");
  inputDialog.setDoubleValue(0.0);
  inputDialog.setDoubleMinimum(0.0);
  inputDialog.setDoubleMaximum(10000.0);
  inputDialog.setDoubleDecimals(1);

  if (inputDialog.exec() == QDialog::Accepted) {
    double maxSpeed = inputDialog.doubleValue();

    // Create edge
    MotionEdge edge;
    edge.id = generateEdgeId();
    edge.fromNodeId = selectedNodes[0]->getNodeId();
    edge.toNodeId = selectedNodes[1]->getNodeId();
    edge.maxSpeed = maxSpeed;
    edge.label = "";
    edge.color = "#ffffff";

    // Create visual item
    MotionEdgeItem* edgeItem = createEdgeItem(edge);
    m_scene->addItem(edgeItem);
    m_edgeItems[edge.id] = edgeItem;

    m_isModified = true;

    // ✅ Auto-save graph immediately after creating edge
    saveCurrentGraph();

    std::cout << "[GraphWidget] Created edge: " << edge.id.toStdString()
              << " from " << edge.fromNodeId.toStdString()
              << " to " << edge.toNodeId.toStdString() << std::endl;
  }
}

void GraphWidget::onDeleteSelected()
{
  QList<QGraphicsItem*> selectedItems = m_scene->selectedItems();

  if (selectedItems.isEmpty()) {
    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("No items selected.");
    msgBox.exec();
    return;
  }

  // Confirm deletion
  QMessageBox msgBox(this);
  msgBox.setStyleSheet(DarkTheme::getStyleSheet());
  msgBox.setIcon(QMessageBox::Question);
  msgBox.setText(QString("Delete %1 selected item(s)?").arg(selectedItems.size()));
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);

  if (msgBox.exec() == QMessageBox::Yes) {
    // Collect nodes and edges to delete (with their IDs)
    QList<MotionNodeItem*> nodesToDelete;
    QMap<QString, MotionEdgeItem*> edgesToDelete;  // edgeId -> edge

    for (QGraphicsItem* item : selectedItems) {
      MotionNodeItem* node = dynamic_cast<MotionNodeItem*>(item);
      if (node) {
        nodesToDelete.append(node);
        continue;
      }

      MotionEdgeItem* edge = dynamic_cast<MotionEdgeItem*>(item);
      if (edge) {
        edgesToDelete[edge->getEdgeId()] = edge;  // Store ID now while edge is valid
      }
    }

    // Delete nodes and their connected edges
    for (MotionNodeItem* node : nodesToDelete) {
      QString nodeId = node->getNodeId();

      // Find and delete all edges connected to this node
      QList<QString> connectedEdgeIds;
      for (auto it = m_edgeItems.begin(); it != m_edgeItems.end(); ++it) {
        MotionEdgeItem* edge = it.value();
        if (edge->getFromNode() == node || edge->getToNode() == node) {
          connectedEdgeIds.append(it.key());
        }
      }

      for (const QString& edgeId : connectedEdgeIds) {
        if (m_edgeItems.contains(edgeId)) {
          MotionEdgeItem* edge = m_edgeItems[edgeId];
          m_scene->removeItem(edge);
          m_edgeItems.remove(edgeId);
          delete edge;
        }
      }

      // Delete the node
      if (m_nodeItems.contains(nodeId)) {
        m_nodeItems.remove(nodeId);
        m_scene->removeItem(node);
        delete node;
      }
    }

    // Delete standalone edges (those not already deleted with nodes)
    for (auto it = edgesToDelete.begin(); it != edgesToDelete.end(); ++it) {
      QString edgeId = it.key();
      MotionEdgeItem* edge = it.value();

      if (m_edgeItems.contains(edgeId)) {  // Check if not already deleted with a node
        m_edgeItems.remove(edgeId);
        m_scene->removeItem(edge);
        delete edge;
      }
    }

    m_isModified = true;

    // Update node selectors after deletion
    updateNodeSelectors();

    // ✅ Auto-save graph immediately after deleting items
    saveCurrentGraph();

    std::cout << "[GraphWidget] Deleted " << nodesToDelete.size() << " nodes and "
              << edgesToDelete.size() << " edges" << std::endl;
  }
}

void GraphWidget::onGraphSelected(int index)
{
  if (index < 0) {
    return;
  }

  QString graphName = m_graphSelector->itemText(index);
  if (graphName != m_currentGraphName) {
    loadGraph(graphName);
  }
}

void GraphWidget::onNewGraph()
{
  bool ok;
  QInputDialog inputDialog(this);
  inputDialog.setStyleSheet(DarkTheme::getStyleSheet());
  inputDialog.setWindowTitle("New Graph");
  inputDialog.setLabelText("Graph name:");

  if (inputDialog.exec() == QDialog::Accepted) {
    QString graphName = inputDialog.textValue().trimmed();

    if (graphName.isEmpty()) {
      QMessageBox msgBox(this);
      msgBox.setStyleSheet(DarkTheme::getStyleSheet());
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("Graph name cannot be empty.");
      msgBox.exec();
      return;
    }

    if (m_positionManager->GetGraphManager().hasGraph(m_deviceName, graphName)) {
      QMessageBox msgBox(this);
      msgBox.setStyleSheet(DarkTheme::getStyleSheet());
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("A graph with this name already exists.");
      msgBox.exec();
      return;
    }

    m_positionManager->GetGraphManager().createGraph(m_deviceName, graphName);
    updateGraphList();

    // Switch to new graph
    int index = m_graphSelector->findText(graphName);
    if (index >= 0) {
      m_graphSelector->setCurrentIndex(index);
    }
  }
}

void GraphWidget::onRenameGraph()
{
  if (m_currentGraphName.isEmpty()) {
    return;
  }

  bool ok;
  QInputDialog inputDialog(this);
  inputDialog.setStyleSheet(DarkTheme::getStyleSheet());
  inputDialog.setWindowTitle("Rename Graph");
  inputDialog.setLabelText("New name:");
  inputDialog.setTextValue(m_currentGraphName);

  if (inputDialog.exec() == QDialog::Accepted) {
    QString newName = inputDialog.textValue().trimmed();

    if (newName.isEmpty()) {
      QMessageBox msgBox(this);
      msgBox.setStyleSheet(DarkTheme::getStyleSheet());
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("Graph name cannot be empty.");
      msgBox.exec();
      return;
    }

    if (newName == m_currentGraphName) {
      return;  // No change
    }

    if (m_positionManager->GetGraphManager().hasGraph(m_deviceName, newName)) {
      QMessageBox msgBox(this);
      msgBox.setStyleSheet(DarkTheme::getStyleSheet());
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.setText("A graph with this name already exists.");
      msgBox.exec();
      return;
    }

    // Save current graph first
    saveCurrentGraph();

    // Rename
    if (m_positionManager->GetGraphManager().renameGraph(m_deviceName, m_currentGraphName, newName)) {
      m_currentGraphName = newName;
      updateGraphList();

      // Select renamed graph
      int index = m_graphSelector->findText(newName);
      if (index >= 0) {
        m_graphSelector->setCurrentIndex(index);
      }
    }
  }
}

void GraphWidget::onDeleteGraph()
{
  if (m_currentGraphName.isEmpty()) {
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setStyleSheet(DarkTheme::getStyleSheet());
  msgBox.setIcon(QMessageBox::Question);
  msgBox.setText(QString("Delete graph '%1'?").arg(m_currentGraphName));
  msgBox.setInformativeText("This action cannot be undone.");
  msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  msgBox.setDefaultButton(QMessageBox::No);

  if (msgBox.exec() == QMessageBox::Yes) {
    QString graphToDelete = m_currentGraphName;

    // Switch to another graph first
    QList<QString> graphs = m_positionManager->GetGraphManager().getGraphNames(m_deviceName);
    QString nextGraph;
    for (const QString& graph : graphs) {
      if (graph != graphToDelete) {
        nextGraph = graph;
        break;
      }
    }

    // Delete the graph
    m_positionManager->GetGraphManager().removeGraph(m_deviceName, graphToDelete);

    // If no other graphs exist, create default
    if (nextGraph.isEmpty()) {
      m_positionManager->GetGraphManager().createGraph(m_deviceName, "default");
      nextGraph = "default";
    }

    updateGraphList();
    loadGraph(nextGraph);
  }
}

void GraphWidget::onZoomIn()
{
  m_graphicsView->zoomIn();
}

void GraphWidget::onZoomOut()
{
  m_graphicsView->zoomOut();
}

void GraphWidget::onResetZoom()
{
  m_graphicsView->resetZoom();
}

// ============================================================================
// SCENE EVENTS
// ============================================================================

void GraphWidget::onSceneSelectionChanged()
{
  // Update button states based on selection
  QList<QGraphicsItem*> selectedItems = m_scene->selectedItems();
  m_btnDeleteSelected->setEnabled(!selectedItems.isEmpty());

  // If exactly one node is selected, set it as goal and calculate path
  if (selectedItems.size() == 1) {
    QGraphicsItem* item = selectedItems.first();

    // Check if it's a node (not an edge)
    MotionNodeItem* nodeItem = dynamic_cast<MotionNodeItem*>(item);
    if (nodeItem) {
      QString nodeId = nodeItem->getNodeId();
      QString positionName = nodeItem->getPositionName();

      std::cout << "[GraphWidget] Node selected: id=" << nodeId.toStdString()
                << ", position=" << positionName.toStdString() << std::endl;

      // Find the node in goal selector and select it
      bool found = false;
      for (int i = 0; i < m_goalNodeSelector->count(); ++i) {
        QString itemNodeId = m_goalNodeSelector->itemData(i).toString();

        if (itemNodeId == nodeId) {
          // Block signals temporarily to avoid triggering onGoalNodeChanged
          m_goalNodeSelector->blockSignals(true);
          m_goalNodeSelector->setCurrentIndex(i);
          m_goalNodeSelector->blockSignals(false);

          std::cout << "[GraphWidget] Updated dropdown to index " << i
                    << ": " << m_goalNodeSelector->currentText().toStdString() << std::endl;

          // Automatically calculate path
          onCalculatePath();
          found = true;
          break;
        }
      }

      if (!found) {
        std::cout << "[GraphWidget] ERROR: Could not find node in dropdown! NodeId="
                  << nodeId.toStdString() << std::endl;
        std::cout << "[GraphWidget] Dropdown has " << m_goalNodeSelector->count() << " items:" << std::endl;
        for (int i = 0; i < m_goalNodeSelector->count(); ++i) {
          std::cout << "  [" << i << "] text='" << m_goalNodeSelector->itemText(i).toStdString()
                    << "' data='" << m_goalNodeSelector->itemData(i).toString().toStdString() << "'" << std::endl;
        }
      }
    }
  }
}

void GraphWidget::onNodeMoved(MotionNodeItem* node)
{
  // Update connected edges
  for (auto it = m_edgeItems.begin(); it != m_edgeItems.end(); ++it) {
    MotionEdgeItem* edge = it.value();
    if (edge->getFromNode() == node || edge->getToNode() == node) {
      edge->updatePosition();
    }
  }

  m_isModified = true;
}

// ============================================================================
// HELPERS
// ============================================================================

void GraphWidget::clearScene()
{
  m_scene->clear();
  m_nodeItems.clear();
  m_edgeItems.clear();
}

void GraphWidget::updateGraphList()
{
  m_graphSelector->blockSignals(true);
  m_graphSelector->clear();

  QList<QString> graphs = m_positionManager->GetGraphManager().getGraphNames(m_deviceName);
  for (const QString& graphName : graphs) {
    m_graphSelector->addItem(graphName);
  }

  // Select current graph
  if (!m_currentGraphName.isEmpty()) {
    int index = m_graphSelector->findText(m_currentGraphName);
    if (index >= 0) {
      m_graphSelector->setCurrentIndex(index);
    }
  }

  m_graphSelector->blockSignals(false);
}

MotionGraph* GraphWidget::getCurrentGraph()
{
  return m_positionManager->GetGraphManager().getGraph(m_deviceName, m_currentGraphName);
}

MotionNodeItem* GraphWidget::createNodeItem(const MotionNode& node)
{
  MotionNodeItem* nodeItem = new MotionNodeItem(
    node.id,
    node.label,
    node.device,
    node.position,
    QColor(node.color)
  );

  nodeItem->setPos(node.uiPosition);
  nodeItem->setGraphWidget(this);  // Set parent for movement notifications

  return nodeItem;
}

MotionEdgeItem* GraphWidget::createEdgeItem(const MotionEdge& edge)
{
  MotionNodeItem* fromNode = findNodeItem(edge.fromNodeId);
  MotionNodeItem* toNode = findNodeItem(edge.toNodeId);

  if (!fromNode || !toNode) {
    return nullptr;
  }

  MotionEdgeItem* edgeItem = new MotionEdgeItem(
    edge.id,
    fromNode,
    toNode,
    edge.maxSpeed,
    QColor(edge.color)
  );

  return edgeItem;
}

MotionNodeItem* GraphWidget::findNodeItem(const QString& nodeId)
{
  auto it = m_nodeItems.find(nodeId);
  if (it != m_nodeItems.end()) {
    return it.value();
  }
  return nullptr;
}

QString GraphWidget::generateNodeId()
{
  return QString("node_%1").arg(++m_nodeCounter);
}

QString GraphWidget::generateEdgeId()
{
  return QString("edge_%1").arg(++m_edgeCounter);
}

QColor GraphWidget::getDeviceColor(const QString& deviceName)
{
  // If color already assigned, return it
  if (m_deviceColors.contains(deviceName)) {
    return m_deviceColors[deviceName];
  }

  // Assign new color from palette
  int colorIndex = m_deviceColors.size() % m_colorPalette.size();
  QColor deviceColor(m_colorPalette[colorIndex]);
  m_deviceColors[deviceName] = deviceColor;

  // Update legend when new device is added
  updateLegend();

  return deviceColor;
}

void GraphWidget::updateLegend()
{
  // Clear existing legend items (keep title, separator, and stretch)
  while (m_legendLayout->count() > 3) {  // Keep title, separator, and stretch
    QLayoutItem* item = m_legendLayout->takeAt(2);  // Remove after separator
    if (item->widget()) {
      delete item->widget();
    }
    delete item;
  }

  // Add legend entry for each device
  for (auto it = m_deviceColors.begin(); it != m_deviceColors.end(); ++it) {
    QString deviceName = it.key();
    QColor deviceColor = it.value();

    // Create legend entry widget
    QWidget* entryWidget = new QWidget(m_legendWidget);
    entryWidget->setStyleSheet("border: none;");
    QHBoxLayout* entryLayout = new QHBoxLayout(entryWidget);
    entryLayout->setContentsMargins(0, 0, 0, 0);
    entryLayout->setSpacing(8);

    // Color indicator (circle)
    QLabel* colorBox = new QLabel(entryWidget);
    colorBox->setFixedSize(20, 20);
    colorBox->setStyleSheet(QString(
      "background-color: %1; "
      "border: 2px solid %2; "
      "border-radius: 10px;"
    ).arg(deviceColor.name()).arg(deviceColor.darker(120).name()));

    // Device name label
    QLabel* nameLabel = new QLabel(deviceName, entryWidget);
    nameLabel->setStyleSheet("color: white; border: none;");
    QFont nameFont;
    nameFont.setPointSize(9);
    nameLabel->setFont(nameFont);

    entryLayout->addWidget(colorBox);
    entryLayout->addWidget(nameLabel);
    entryLayout->addStretch();

    // Insert before the stretch at the end
    m_legendLayout->insertWidget(m_legendLayout->count() - 1, entryWidget);
  }
}

// ============================================================================
// EXECUTION PANEL
// ============================================================================

void GraphWidget::setupExecutionPanel()
{
  m_executionPanel = new QWidget(m_legendWidget);
  m_executionPanel->setStyleSheet("QWidget { background: transparent; border: none; }");

  QVBoxLayout* layout = new QVBoxLayout(m_executionPanel);
  layout->setContentsMargins(0, 15, 0, 0);
  layout->setSpacing(10);

  // Title
  QLabel* title = new QLabel("Graph Execution");
  QFont titleFont;
  titleFont.setBold(true);
  titleFont.setPointSize(10);
  title->setFont(titleFont);
  title->setStyleSheet("color: white; border: none;");
  layout->addWidget(title);

  // Separator
  QFrame* separator = new QFrame();
  separator->setFrameShape(QFrame::HLine);
  separator->setStyleSheet("background-color: #444; border: none;");
  separator->setFixedHeight(1);
  layout->addWidget(separator);

  // Current position (read-only, auto-detected)
  QLabel* currentLabel = new QLabel("Current Position:");
  currentLabel->setStyleSheet("color: #aaa; font-size: 10px; border: none;");
  layout->addWidget(currentLabel);

  m_currentPositionLabel = new QLabel("Unknown");
  m_currentPositionLabel->setStyleSheet(
    "color: #f39c12; "
    "font-size: 11px; "
    "font-weight: bold; "
    "border: 1px solid #555; "
    "border-radius: 3px; "
    "padding: 5px; "
    "background-color: #3a3a3a;");
  m_currentPositionLabel->setWordWrap(true);
  layout->addWidget(m_currentPositionLabel);

  // Goal node selector
  QLabel* goalLabel = new QLabel("Goal Node:");
  goalLabel->setStyleSheet("color: #aaa; font-size: 10px; border: none;");
  layout->addWidget(goalLabel);

  m_goalNodeSelector = new QComboBox();
  m_goalNodeSelector->setStyleSheet(DarkTheme::getStyleSheet());
  connect(m_goalNodeSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &GraphWidget::onGoalNodeChanged);
  layout->addWidget(m_goalNodeSelector);

  // Calculate path button
  m_btnCalculatePath = new QPushButton("Calculate Path");
  m_btnCalculatePath->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5dade2, stop:1 #3498db); "
    "  color: white; "
    "  padding: 8px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6dbdf2, stop:1 #5dade2); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2874a6; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 10px; "
    "  padding-left: 10px; "
    "}");
  connect(m_btnCalculatePath, &QPushButton::clicked, this, &GraphWidget::onCalculatePath);
  layout->addWidget(m_btnCalculatePath);

  // Path info label
  m_pathInfoLabel = new QLabel("No path calculated");
  m_pathInfoLabel->setWordWrap(true);
  m_pathInfoLabel->setStyleSheet("color: #888; font-size: 9px; border: none;");
  layout->addWidget(m_pathInfoLabel);

  // Execute button
  m_btnExecutePath = new QPushButton("Execute Movement");
  m_btnExecutePath->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5cbf60, stop:1 #4CAF50); "
    "  color: white; "
    "  padding: 8px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6ccf70, stop:1 #5cbf60); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #3d9041; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 10px; "
    "  padding-left: 10px; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #7f8c8d; "
    "  color: #bdc3c7; "
    "  border-top: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-left: 1px solid rgba(255, 255, 255, 0.2); "
    "  border-bottom: 1px solid rgba(0, 0, 0, 0.3); "
    "  border-right: 1px solid rgba(0, 0, 0, 0.3); "
    "}");
  m_btnExecutePath->setEnabled(false);
  connect(m_btnExecutePath, &QPushButton::clicked, this, &GraphWidget::onExecutePath);
  layout->addWidget(m_btnExecutePath);

  // Initially populate selectors
  updateNodeSelectors();
  // Note: Don't call updateCurrentPosition() here - timer hasn't been created yet!
  // It will be called automatically when the timer starts in the constructor.
}

void GraphWidget::updateNodeSelectors()
{
  // Block signals during update
  m_goalNodeSelector->blockSignals(true);

  m_goalNodeSelector->clear();

  // ✅ FIX: Read from visual items (m_nodeItems) instead of saved graph
  // This ensures newly created nodes appear in dropdown immediately
  for (auto it = m_nodeItems.begin(); it != m_nodeItems.end(); ++it) {
    MotionNodeItem* nodeItem = it.value();
    QString displayName = QString("%1 (%2)").arg(nodeItem->getLabel()).arg(nodeItem->getPositionName());
    m_goalNodeSelector->addItem(displayName, nodeItem->getNodeId());
  }

  std::cout << "[GraphWidget] Updated node selectors with " << m_nodeItems.size() << " nodes" << std::endl;

  m_goalNodeSelector->blockSignals(false);
}

void GraphWidget::updateCurrentPosition()
{
  // Safety check: Don't update if widget is being destroyed or timer is stopped
  if (!m_positionUpdateTimer || !m_positionUpdateTimer->isActive()) {
    return;
  }

  if (!m_motionManager) {
    m_currentPositionLabel->setText("Unknown (no motion manager)");
    m_currentPositionLabel->setStyleSheet(
      "color: #e74c3c; "
      "font-size: 11px; "
      "font-weight: bold; "
      "border: 1px solid #555; "
      "border-radius: 3px; "
      "padding: 5px; "
      "background-color: #3a3a3a;");
    return;
  }

  // Get ACTUAL physical position from motor
  PositionStruct currentPos = m_motionManager->GetCurrentPosition(m_deviceName.toStdString());

  // Get all named positions for this device
  std::map<std::string, PositionStruct> allPositions =
    m_positionManager->GetAllPositions(m_deviceName.toStdString());

  // Find which named position the motor is actually at (within tolerance)
  const double POSITION_TOLERANCE = 0.1;  // 0.1mm tolerance
  std::string matchedPosition;
  double closestDistance = std::numeric_limits<double>::max();

  for (const auto& pair : allPositions) {
    const std::string& posName = pair.first;
    const PositionStruct& savedPos = pair.second;

    // Calculate distance (simple Euclidean in XYZ)
    double dx = currentPos.x - savedPos.x;
    double dy = currentPos.y - savedPos.y;
    double dz = currentPos.z - savedPos.z;
    double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

    if (distance < closestDistance) {
      closestDistance = distance;
      if (distance < POSITION_TOLERANCE) {
        matchedPosition = posName;
      }
    }
  }

  if (matchedPosition.empty()) {
    // Not at any named position - show coordinates
    m_currentPositionLabel->setText(QString("Unknown (X:%1 Y:%2 Z:%3)")
      .arg(currentPos.x, 0, 'f', 2)
      .arg(currentPos.y, 0, 'f', 2)
      .arg(currentPos.z, 0, 'f', 2));
    m_currentPositionLabel->setStyleSheet(
      "color: #e74c3c; "
      "font-size: 11px; "
      "font-weight: bold; "
      "border: 1px solid #555; "
      "border-radius: 3px; "
      "padding: 5px; "
      "background-color: #3a3a3a;");
  } else {
    // At a known position - show name
    m_currentPositionLabel->setText(QString::fromStdString(matchedPosition));
    m_currentPositionLabel->setStyleSheet(
      "color: #2ecc71; "
      "font-size: 11px; "
      "font-weight: bold; "
      "border: 1px solid #27ae60; "
      "border-radius: 3px; "
      "padding: 5px; "
      "background-color: #2a4a2e;");

    // Check if we've reached the goal position
    if (m_isExecutingPath && !m_goalPositionName.isEmpty()) {
      if (QString::fromStdString(matchedPosition) == m_goalPositionName) {
        // Reached goal! Clear "Movement in progress..." message
        m_isExecutingPath = false;
        m_goalPositionName.clear();

        // Remove the "Movement in progress..." text from path info
        QString pathInfo = m_pathInfoLabel->text();
        pathInfo.replace("<br><font color='#f39c12'><b>Movement in progress...</b></font>", "");
        pathInfo += "<br><font color='#2ecc71'><b>Movement completed!</b></font>";
        m_pathInfoLabel->setText(pathInfo);

        std::cout << "[GraphWidget] Path execution completed - reached goal position" << std::endl;
      }
    }
  }
}

void GraphWidget::onGoalNodeChanged(int index)
{
  // Clear cached path when selection changes
  m_currentPath = GraphPath();
  m_pathInfoLabel->setText("No path calculated");
  m_btnExecutePath->setEnabled(false);

  // Clear execution tracking
  m_isExecutingPath = false;
  m_goalPositionName.clear();
}

void GraphWidget::onCalculatePath()
{
  // Clear any previous execution tracking
  m_isExecutingPath = false;
  m_goalPositionName.clear();

  // Update current position first
  updateCurrentPosition();

  if (m_goalNodeSelector->currentIndex() < 0) {
    m_pathInfoLabel->setText("Please select a goal node");
    m_btnExecutePath->setEnabled(false);
    return;
  }

  if (!m_motionManager) {
    m_pathInfoLabel->setText("<font color='#e74c3c'>Motion manager not connected</font>");
    m_btnExecutePath->setEnabled(false);
    return;
  }

  // Get ACTUAL physical position from motor
  PositionStruct currentPos = m_motionManager->GetCurrentPosition(m_deviceName.toStdString());

  // Get all named positions for this device
  std::map<std::string, PositionStruct> allPositions =
    m_positionManager->GetAllPositions(m_deviceName.toStdString());

  // Find which named position the motor is actually at (within tolerance)
  const double POSITION_TOLERANCE = 0.1;  // 0.1mm tolerance
  std::string currentPosName;

  for (const auto& pair : allPositions) {
    const std::string& posName = pair.first;
    const PositionStruct& savedPos = pair.second;

    // Calculate distance (simple Euclidean in XYZ)
    double dx = currentPos.x - savedPos.x;
    double dy = currentPos.y - savedPos.y;
    double dz = currentPos.z - savedPos.z;
    double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

    if (distance < POSITION_TOLERANCE) {
      currentPosName = posName;
      break;
    }
  }

  if (currentPosName.empty()) {
    m_pathInfoLabel->setText(QString(
      "<font color='#e74c3c'>Current position (X:%1 Y:%2 Z:%3) not at any named position - move to a node first</font>")
      .arg(currentPos.x, 0, 'f', 2)
      .arg(currentPos.y, 0, 'f', 2)
      .arg(currentPos.z, 0, 'f', 2));
    m_btnExecutePath->setEnabled(false);
    return;
  }

  QString currentPosition = QString::fromStdString(currentPosName);

  // Find node ID for current position in the graph
  MotionGraph* graph = getCurrentGraph();
  if (!graph) {
    m_pathInfoLabel->setText("<font color='#e74c3c'>No graph loaded</font>");
    m_btnExecutePath->setEnabled(false);
    return;
  }

  QString startNodeId;
  QList<MotionNode> allNodes = graph->getAllNodes();
  for (const MotionNode& node : allNodes) {
    if (node.position == currentPosition) {
      startNodeId = node.id;
      break;
    }
  }

  if (startNodeId.isEmpty()) {
    m_pathInfoLabel->setText(QString(
      "<font color='#e74c3c'>Current position '%1' not found in graph - please add it as a node first</font>")
      .arg(currentPosition));
    m_btnExecutePath->setEnabled(false);
    return;
  }

  QString goalNodeId = m_goalNodeSelector->currentData().toString();

  // Calculate path using MotionGraphManager
  m_currentPath = m_positionManager->GetGraphManager().findPath(
    m_deviceName,
    m_currentGraphName,
    startNodeId,
    goalNodeId
  );

  if (!m_currentPath.isValid) {
    m_pathInfoLabel->setText(QString("<font color='#e74c3c'>Error: %1</font>")
      .arg(m_currentPath.errorMessage));
    m_btnExecutePath->setEnabled(false);
    return;
  }

  // Display path info
  QString pathText = "<b>Path:</b><br>";
  for (int i = 0; i < m_currentPath.positionNames.size(); ++i) {
    pathText += m_currentPath.positionNames[i];
    if (i < m_currentPath.positionNames.size() - 1) {
      pathText += " → ";
    }
  }

  pathText += QString("<br><b>Steps:</b> %1").arg(m_currentPath.nodeIds.size());

  if (m_currentPath.maxSpeed > 0) {
    pathText += QString("<br><b>Max Speed:</b> %1 mm/s").arg(m_currentPath.maxSpeed, 0, 'f', 1);
  }

  m_pathInfoLabel->setText(pathText);
  m_btnExecutePath->setEnabled(true);
}

void GraphWidget::onExecutePath()
{
  if (!m_currentPath.isValid) {
    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("No valid path calculated");
    msgBox.exec();
    return;
  }

  if (!m_motionManager) {
    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText("Motion manager not connected");
    msgBox.exec();
    return;
  }

  if (!m_sequenceEngine) {
    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText("Sequence engine not connected");
    msgBox.exec();
    return;
  }

  // Check if already executing
  if (m_sequenceWorker) {
    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("Path execution already in progress");
    msgBox.exec();
    return;
  }

  // Execute movement along the path using SequenceWorker (async)
  std::cout << "[GraphWidget] Executing path with " << m_currentPath.positionNames.size() << " waypoints using sequence worker" << std::endl;

  // Get ACTUAL physical position and all named positions
  PositionStruct currentPos = m_motionManager->GetCurrentPosition(m_deviceName.toStdString());
  std::map<std::string, PositionStruct> allPositions =
    m_positionManager->GetAllPositions(m_deviceName.toStdString());

  const double POSITION_TOLERANCE = 0.1;  // 0.1mm tolerance

  // Build sequence with all moves using SequenceEngine factory
  SequenceBuilder* sequence = m_sequenceEngine->CreateSequence("GraphPathExecution");
  int movesAdded = 0;

  for (const QString& posName : m_currentPath.positionNames) {
    if (allPositions.count(posName.toStdString()) == 0) {
      std::cout << "  Warning: Position '" << posName.toStdString() << "' not found, skipping" << std::endl;
      continue;
    }

    // Check if we're already physically at this position
    const PositionStruct& targetPos = allPositions[posName.toStdString()];
    double dx = currentPos.x - targetPos.x;
    double dy = currentPos.y - targetPos.y;
    double dz = currentPos.z - targetPos.z;
    double distance = std::sqrt(dx*dx + dy*dy + dz*dz);

    if (distance < POSITION_TOLERANCE) {
      std::cout << "  Skipping waypoint (already at " << posName.toStdString() << ")" << std::endl;
      continue;
    }

    std::cout << "  Adding to sequence: " << posName.toStdString() << std::endl;

    // Add MoveToNamedPosition action
    sequence->AddAction(m_deviceName.toStdString(), "MoveToNamedPosition", {
      {"positionName", posName.toStdString()}
    });

    // Add WaitForMotionComplete action (ensures move finishes before next starts)
    sequence->AddAction(m_deviceName.toStdString(), "WaitForMotionComplete", {});

    movesAdded++;
  }

  // Check if any moves needed
  if (movesAdded == 0) {
    delete sequence;  // Clean up unused sequence
    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setText("Already at goal position - no movement needed");
    msgBox.exec();
    return;
  }

  // Show info about multi-step execution
  if (movesAdded > 1) {
    std::cout << "[GraphWidget] Multi-step path: " << movesAdded << " moves will execute sequentially" << std::endl;
  }

  // Track execution for completion detection
  m_isExecutingPath = true;
  m_goalPositionName = m_currentPath.positionNames.last();  // Last position in path is the goal

  // Update path info to show movement in progress
  m_pathInfoLabel->setText(m_pathInfoLabel->text() + "<br><font color='#f39c12'><b>Movement in progress...</b></font>");

  // Create worker and connect signals
  m_sequenceWorker = new SequenceWorker(m_sequenceEngine, sequence, this);

  connect(m_sequenceWorker, &SequenceWorker::executionCompleted, this, &GraphWidget::onSequenceExecutionCompleted);
  connect(m_sequenceWorker, &SequenceWorker::finished, m_sequenceWorker, &QObject::deleteLater);

  // Start execution in background thread (UI stays responsive!)
  m_sequenceWorker->start();

  std::cout << "[GraphWidget] Sequence worker started - UI remains responsive" << std::endl;
}

void GraphWidget::onSequenceExecutionCompleted(bool success, QString errorMessage)
{
  std::cout << "[GraphWidget] Sequence execution completed - success: " << success << std::endl;

  // Clear worker pointer (will be deleted by deleteLater)
  m_sequenceWorker = nullptr;

  // Update path info label
  QString pathInfo = m_pathInfoLabel->text();
  pathInfo.replace("<br><font color='#f39c12'><b>Movement in progress...</b></font>", "");

  if (success) {
    pathInfo += "<br><font color='#2ecc71'><b>Path execution completed!</b></font>";
    m_pathInfoLabel->setText(pathInfo);

    std::cout << "[GraphWidget] Path execution completed successfully" << std::endl;
  } else {
    pathInfo += QString("<br><font color='#e74c3c'><b>Path execution failed: %1</b></font>").arg(errorMessage);
    m_pathInfoLabel->setText(pathInfo);

    QMessageBox msgBox(this);
    msgBox.setStyleSheet(DarkTheme::getStyleSheet());
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setText("Path execution failed");
    msgBox.setInformativeText(errorMessage);
    msgBox.exec();

    // Clear execution tracking
    m_isExecutingPath = false;
    m_goalPositionName.clear();

    std::cout << "[GraphWidget] Path execution failed: " << errorMessage.toStdString() << std::endl;
  }

  // Note: Keep m_isExecutingPath = true for successful completion
  // The updateCurrentPosition() timer will detect arrival and clear it
}

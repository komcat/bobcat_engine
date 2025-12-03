// GraphWidget.h
#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QComboBox>
#include <QPushButton>
#include <QToolBar>
#include <QVBoxLayout>
#include <QLabel>
#include <QString>
#include <QMap>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSlider>

#include "../../graph/MotionGraph.h"
#include "../../graph/MotionGraphManager.h"
#include "MotionNodeItem.h"
#include "MotionEdgeItem.h"

class ProductPositionManager;
class UnifiedMotionManager;
class SequenceEngine;
class SequenceWorker;

/**
 * @brief Custom QGraphicsView with pan and zoom support
 *
 * Features:
 * - Middle mouse button hold to pan
 * - Mouse wheel scroll to zoom in/out
 */
class CustomGraphicsView : public QGraphicsView
{
  Q_OBJECT

public:
  explicit CustomGraphicsView(QGraphicsScene* scene, QWidget* parent = nullptr);

  void zoomIn();
  void zoomOut();
  void resetZoom();

protected:
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

private:
  bool m_isPanning;
  QPoint m_lastPanPoint;
  qreal m_currentZoom;

  static constexpr qreal ZOOM_FACTOR = 1.2;
  static constexpr qreal MIN_ZOOM = 0.1;
  static constexpr qreal MAX_ZOOM = 10.0;
};

/**
 * @brief Widget for visualizing and editing motion graphs
 *
 * Features:
 * - Interactive canvas with draggable nodes and edges
 * - Create nodes from existing positions
 * - Connect nodes with bidirectional edges
 * - Multiple named graphs per device
 * - Graph management (create/rename/delete graphs)
 * - Dark theme styling
 */
class GraphWidget : public QWidget
{
  Q_OBJECT

public:
  explicit GraphWidget(const QString& deviceName,
                       ProductPositionManager* positionManager,
                       QWidget* parent = nullptr);
  ~GraphWidget();

  // Load/Save
  void loadGraph(const QString& graphName);
  void saveCurrentGraph();

  // Current graph info
  QString getCurrentGraphName() const { return m_currentGraphName; }

  // Called by MotionNodeItem when node is moved (for edge updates)
  void onNodeMoved(MotionNodeItem* node);

  // Set motion manager for execution
  void setMotionManager(UnifiedMotionManager* motionManager) {
    m_motionManager = motionManager;
    refreshSpeedControl();  // Update speed control now that we have the motion manager
  }

  // Set sequence engine for multi-step path execution
  void setSequenceEngine(SequenceEngine* sequenceEngine) { m_sequenceEngine = sequenceEngine; }

  // Refresh speed control panel (call after motion manager is set)
  void refreshSpeedControl();

  // Cleanup - call before deleting widget
  void cleanup();

signals:
  void graphModified();
  void graphSwitched(const QString& graphName);

private slots:
  // Toolbar actions
  void onCreateNode();
  void onCreateEdge();
  void onDeleteSelected();
  void onGraphSelected(int index);
  void onNewGraph();
  void onRenameGraph();
  void onDeleteGraph();
  void onZoomIn();
  void onZoomOut();
  void onResetZoom();

  // Scene events
  void onSceneSelectionChanged();

  // Execution panel
  void onExecutePath();
  void onGoalNodeChanged(int index);
  void onCalculatePath();
  void updateCurrentPosition();

  // Speed control
  void onSpeedSliderChanged(int value);
  void onSpeedPresetClicked();

  // Sequence worker slots
  void onSequenceExecutionCompleted(bool success, QString errorMessage);

private:
  // UI setup
  void setupUI();
  void setupToolbar();
  void setupScene();
  void applyDarkTheme();

  // Graph operations
  void clearScene();
  void updateGraphList();
  MotionGraph* getCurrentGraph();

  // Node/Edge creation helpers
  MotionNodeItem* createNodeItem(const MotionNode& node);
  MotionEdgeItem* createEdgeItem(const MotionEdge& edge);
  MotionNodeItem* findNodeItem(const QString& nodeId);

  // Generate unique IDs
  QString generateNodeId();
  QString generateEdgeId();

  // Device color management
  QColor getDeviceColor(const QString& deviceName);
  void updateLegend();

  // Execution panel
  void setupExecutionPanel();
  void setupSpeedControl(QVBoxLayout* layout);
  void updateNodeSelectors();

  // Members
  QString m_deviceName;
  QString m_currentGraphName;
  ProductPositionManager* m_positionManager;
  UnifiedMotionManager* m_motionManager;
  SequenceEngine* m_sequenceEngine;

  // UI Components
  QVBoxLayout* m_mainLayout;
  QToolBar* m_toolbar;
  CustomGraphicsView* m_graphicsView;
  QGraphicsScene* m_scene;

  // Toolbar widgets
  QComboBox* m_graphSelector;
  QPushButton* m_btnCreateNode;
  QPushButton* m_btnCreateEdge;
  QPushButton* m_btnDeleteSelected;
  QPushButton* m_btnNewGraph;
  QPushButton* m_btnRenameGraph;
  QPushButton* m_btnDeleteGraph;
  QPushButton* m_btnZoomIn;
  QPushButton* m_btnZoomOut;
  QPushButton* m_btnResetZoom;

  // Legend widget
  QWidget* m_legendWidget;
  QVBoxLayout* m_legendLayout;

  // Execution panel widgets
  QWidget* m_executionPanel;
  QLabel* m_currentPositionLabel;
  QComboBox* m_goalNodeSelector;
  QPushButton* m_btnCalculatePath;
  QPushButton* m_btnExecutePath;
  QLabel* m_pathInfoLabel;

  // Speed control widgets
  QWidget* m_speedControlPanel;
  QLabel* m_speedValueLabel;
  QSlider* m_speedSlider;
  QPushButton* m_speedPreset1;
  QPushButton* m_speedPreset2;
  QPushButton* m_speedPreset3;

  // Item tracking
  QMap<QString, MotionNodeItem*> m_nodeItems;  // nodeId -> visual item
  QMap<QString, MotionEdgeItem*> m_edgeItems;  // edgeId -> visual item

  // Device color mapping
  QMap<QString, QColor> m_deviceColors;
  QStringList m_colorPalette;

  // State
  int m_nodeCounter;
  int m_edgeCounter;
  bool m_isModified;

  // Cached path
  GraphPath m_currentPath;

  // Position update timer
  QTimer* m_positionUpdateTimer;

  // Movement tracking
  bool m_isExecutingPath;
  QString m_goalPositionName;

  // Async sequence execution
  SequenceWorker* m_sequenceWorker;
};

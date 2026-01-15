// DependencyGraphWindow.h
#pragma once

#include <QDialog>
#include <QGraphicsView>
#include <QPushButton>
#include <QToolBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QListWidget>
#include <QSplitter>

class ProgramManager;
class DependencyGraphScene;
class RecipeManager;

/**
 * @brief Custom QGraphicsView with pan and zoom support
 *
 * Features:
 * - Middle mouse button or Alt+Left to pan
 * - Mouse wheel to zoom in/out
 */
class DependencyGraphView : public QGraphicsView
{
  Q_OBJECT

public:
  explicit DependencyGraphView(QGraphicsScene* scene, QWidget* parent = nullptr);

  void zoomIn();
  void zoomOut();
  void fitToContents();

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
  static constexpr qreal MAX_ZOOM = 5.0;
};

/**
 * @brief Popup window displaying the dependency graph
 *
 * Features:
 * - Interactive DAG visualization of recipe dependencies
 * - Drag from node to node to create dependencies
 * - Right-click edges to edit conditions or delete
 * - Auto-layout, zoom controls, fit to view
 * - Real-time sync with ProgramManager
 */
class DependencyGraphWindow : public QDialog
{
  Q_OBJECT

public:
  explicit DependencyGraphWindow(ProgramManager* programManager, QWidget* parent = nullptr);
  ~DependencyGraphWindow();

public slots:
  // Refresh the graph from ProgramManager
  void refreshGraph();

private slots:
  // Toolbar actions
  void onAutoLayout();
  void onLinkSelected();
  void onZoomIn();
  void onZoomOut();
  void onFitToView();
  void onShowHelp();

  // Scene events
  void onDependencyCreated(int sourceIndex, int targetIndex);
  void onDependencyRemoved(int sourceIndex, int targetIndex);
  void onEditConditionRequested(int sourceIndex, int targetIndex);
  void onNodePositionChanged(int recipeIndex, double x, double y);

  // Selection events
  void onSelectionChanged();
  void onVariableItemClicked(QListWidgetItem* item);

private:
  void setupUI();
  void setupToolbar();
  void setupContextVariablePanel();
  void applyDarkTheme();
  void updateContextVariablesForRecipe(int recipeIndex);
  QStringList extractContextVariablesFromRecipe(const std::string& recipeFilePath);

  ProgramManager* m_programManager;
  DependencyGraphScene* m_scene;
  DependencyGraphView* m_graphicsView;

  // Toolbar widgets
  QToolBar* m_toolbar;
  QPushButton* m_btnAutoLayout;
  QPushButton* m_btnLink;
  QPushButton* m_btnZoomIn;
  QPushButton* m_btnZoomOut;
  QPushButton* m_btnFitToView;
  QPushButton* m_btnHelp;

  // Status bar
  QLabel* m_statusLabel;

  // Context variable panel
  QWidget* m_contextVarPanel;
  QLabel* m_selectedRecipeLabel;
  QListWidget* m_contextVarList;
  QSplitter* m_splitter;

  // Parameter keys that define context variables
  static const QStringList CONTEXT_PARAM_KEYS;
};

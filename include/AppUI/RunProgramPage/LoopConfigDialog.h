// LoopConfigDialog.h
// Dialog for configuring which recipes are included in infinite loop execution
#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QGraphicsPathItem>
#include <vector>

class ProgramManager;

/**
 * @brief Dialog for selecting which recipes to include in loop execution
 *
 * Displays all recipes with checkboxes and a visual preview of the loop flow.
 * Shows dependencies between recipes and highlights the execution order.
 */
class LoopConfigDialog : public QDialog
{
  Q_OBJECT

public:
  explicit LoopConfigDialog(ProgramManager* programManager, QWidget* parent = nullptr);
  ~LoopConfigDialog() = default;

private slots:
  void onSelectAll();
  void onClearAll();
  void onAccept();
  void onRecipeToggled();

private:
  void setupUI();
  void loadCurrentSettings();
  void applySettings();
  void updateSummary();
  void updatePreview();

  // Visual preview helpers
  void drawLoopPreview();
  void clearPreview();

  ProgramManager* m_programManager;
  std::vector<QCheckBox*> m_recipeCheckboxes;

  // Left panel (recipe list)
  QPushButton* m_selectAllButton;
  QPushButton* m_clearAllButton;
  QPushButton* m_okButton;
  QPushButton* m_cancelButton;
  QLabel* m_summaryLabel;
  QWidget* m_recipeListWidget;

  // Right panel (visual preview)
  QGraphicsScene* m_previewScene;
  QGraphicsView* m_previewView;
  QLabel* m_previewTitle;

  // Node tracking for preview
  struct RecipeNode {
    QGraphicsRectItem* rect;
    QGraphicsTextItem* text;
    int recipeIndex;
    bool inLoop;
  };
  std::vector<RecipeNode> m_previewNodes;
};

// RecipeNodeItem.h
#pragma once

#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QString>
#include <QColor>

class DependencyGraphScene;

/**
 * @brief Rectangle node representing a recipe in the dependency graph
 *
 * Visual: Rounded rectangle showing recipe index and name
 * Features:
 * - Color-coded: Green=enabled, Gray=disabled
 * - Selection highlight (blue border)
 * - Draggable for repositioning
 * - Connection points on left (incoming) and right (outgoing) edges
 */
class RecipeNodeItem : public QGraphicsRectItem
{
public:
  explicit RecipeNodeItem(int recipeIndex,
                          const QString& recipeName,
                          bool enabled = true);

  // Getters
  int getRecipeIndex() const { return m_recipeIndex; }
  QString getRecipeName() const { return m_recipeName; }
  bool isEnabled() const { return m_enabled; }

  // Setters
  void setRecipeName(const QString& name);
  void setEnabled(bool enabled);
  void setSelected(bool selected);
  void setHighlightAsPrerequisite(bool highlight);
  void setGraphScene(DependencyGraphScene* scene) { m_graphScene = scene; }

  // Connection points
  QPointF getOutputConnectionPoint() const;  // Right edge center
  QPointF getInputConnectionPoint() const;   // Left edge center

  // Visual constants
  static constexpr qreal NODE_WIDTH = 140.0;
  static constexpr qreal NODE_HEIGHT = 50.0;
  static constexpr qreal CORNER_RADIUS = 8.0;
  static constexpr qreal BORDER_WIDTH = 2.0;

protected:
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
  void updateAppearance();
  QColor getNodeColor() const;

  int m_recipeIndex;
  QString m_recipeName;
  bool m_enabled;
  bool m_isSelected;
  bool m_isPrerequisiteHighlight;

  QGraphicsTextItem* m_indexLabel;  // Shows recipe index
  QGraphicsTextItem* m_nameLabel;   // Shows recipe name
  DependencyGraphScene* m_graphScene;
};

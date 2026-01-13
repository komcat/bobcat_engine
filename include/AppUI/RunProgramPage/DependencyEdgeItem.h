// DependencyEdgeItem.h
#pragma once

#include <QGraphicsPathItem>
#include <QGraphicsTextItem>
#include <QString>
#include <QColor>

class RecipeNodeItem;

/**
 * @brief Curved arrow representing a dependency between two recipes
 *
 * Visual: Bezier curve with arrow head at target end
 * Features:
 * - Shows condition label at midpoint (if condition exists)
 * - Selectable with highlight
 * - Click to select for context menu operations
 */
class DependencyEdgeItem : public QGraphicsPathItem
{
public:
  explicit DependencyEdgeItem(RecipeNodeItem* sourceNode,
                              RecipeNodeItem* targetNode,
                              const QString& condition = "");

  // Getters
  RecipeNodeItem* getSourceNode() const { return m_sourceNode; }
  RecipeNodeItem* getTargetNode() const { return m_targetNode; }
  QString getCondition() const { return m_condition; }

  // Setters
  void setCondition(const QString& condition);
  void setEdgeSelected(bool selected);

  // Update path when nodes move
  void updatePath();

  // Visual constants
  static constexpr qreal EDGE_WIDTH = 2.0;
  static constexpr qreal EDGE_WIDTH_SELECTED = 4.0;
  static constexpr qreal ARROW_SIZE = 12.0;
  static constexpr qreal CURVE_CONTROL_OFFSET = 60.0;

protected:
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
  QPainterPath shape() const override;

private:
  void drawArrowHead(QPainter* painter, const QPointF& tip, qreal angle);
  void updateConditionLabel();

  RecipeNodeItem* m_sourceNode;
  RecipeNodeItem* m_targetNode;
  QString m_condition;
  bool m_isSelected;

  QGraphicsTextItem* m_conditionLabel;

  // Cached path endpoints for arrow drawing
  QPointF m_endPoint;
  qreal m_endAngle;
};

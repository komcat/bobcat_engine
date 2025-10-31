// MotionEdgeItem.h
#pragma once

#include <QGraphicsLineItem>
#include <QGraphicsTextItem>
#include <QString>
#include <QColor>

class MotionNodeItem;

/**
 * @brief Custom QGraphicsItem representing a bidirectional edge between two nodes
 *
 * Visual: Line with arrows at both ends (bidirectional)
 * Features:
 * - Connects two MotionNodeItem objects
 * - Shows speed limit label (if max_speed > 0)
 * - Selectable (highlights on selection)
 * - Automatically updates when nodes move
 */
class MotionEdgeItem : public QGraphicsLineItem
{
public:
  explicit MotionEdgeItem(const QString& edgeId,
                          MotionNodeItem* fromNode,
                          MotionNodeItem* toNode,
                          double maxSpeed = 0.0,
                          const QColor& color = QColor("#ffffff"));

  // Getters
  QString getEdgeId() const { return m_edgeId; }
  MotionNodeItem* getFromNode() const { return m_fromNode; }
  MotionNodeItem* getToNode() const { return m_toNode; }
  double getMaxSpeed() const { return m_maxSpeed; }

  // Setters
  void setMaxSpeed(double maxSpeed);
  void setEdgeColor(const QColor& color);
  void setLabel(const QString& label);

  // Update line position based on node positions
  void updatePosition();

  // Visual constants
  static constexpr qreal EDGE_WIDTH = 2.0;
  static constexpr qreal EDGE_WIDTH_SELECTED = 4.0;
  static constexpr qreal ARROW_SIZE = 12.0;

protected:
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
  QPainterPath shape() const override;
  QRectF boundingRect() const override;

private:
  void drawArrow(QPainter* painter, const QPointF& start, const QPointF& end);
  QString formatSpeedLabel() const;

  QString m_edgeId;
  MotionNodeItem* m_fromNode;
  MotionNodeItem* m_toNode;
  double m_maxSpeed;
  QColor m_color;
  QString m_label;

  QGraphicsTextItem* m_speedLabel;  // Shows max speed
};

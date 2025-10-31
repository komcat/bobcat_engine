// MotionNodeItem.h
#pragma once

#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QString>
#include <QColor>

// Forward declaration
class GraphWidget;

/**
 * @brief Custom QGraphicsItem representing a motion node in the graph
 *
 * Visual: Circular node with label displaying position name
 * Features:
 * - Draggable (updates node position in data model)
 * - Selectable (highlights on selection)
 * - Color customizable
 * - Shows position name as label
 */
class MotionNodeItem : public QGraphicsEllipseItem
{
public:
  explicit MotionNodeItem(const QString& nodeId,
                          const QString& label,
                          const QString& deviceName,
                          const QString& positionName,
                          const QColor& color = QColor("#27ae60"));

  // Getters
  QString getNodeId() const { return m_nodeId; }
  QString getLabel() const { return m_label; }
  QString getDeviceName() const { return m_deviceName; }
  QString getPositionName() const { return m_positionName; }

  // Setters
  void setLabel(const QString& label);
  void setNodeColor(const QColor& color);
  void setSelected(bool selected);
  void setGraphWidget(GraphWidget* widget) { m_graphWidget = widget; }

  // Visual constants
  static constexpr qreal NODE_RADIUS = 30.0;
  static constexpr qreal NODE_BORDER_WIDTH = 2.0;

protected:
  // Override for custom appearance
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

  // Mouse events for drag notification
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
  void updateAppearance();

  QString m_nodeId;
  QString m_label;
  QString m_deviceName;
  QString m_positionName;
  QColor m_color;
  bool m_isSelected;

  QGraphicsTextItem* m_labelItem;  // Child text item for label
  GraphWidget* m_graphWidget;      // Parent widget for notifications
};

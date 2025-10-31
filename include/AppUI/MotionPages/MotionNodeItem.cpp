// MotionNodeItem.cpp
#include "MotionNodeItem.h"
#include "GraphWidget.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QFont>

MotionNodeItem::MotionNodeItem(const QString& nodeId,
                               const QString& label,
                               const QString& deviceName,
                               const QString& positionName,
                               const QColor& color)
  : QGraphicsEllipseItem(-NODE_RADIUS, -NODE_RADIUS, NODE_RADIUS * 2, NODE_RADIUS * 2)
  , m_nodeId(nodeId)
  , m_label(label)
  , m_deviceName(deviceName)
  , m_positionName(positionName)
  , m_color(color)
  , m_isSelected(false)
  , m_graphWidget(nullptr)
{
  // Make node draggable and selectable
  setFlag(QGraphicsItem::ItemIsMovable, true);
  setFlag(QGraphicsItem::ItemIsSelectable, true);
  setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);

  // Create label text item as child
  m_labelItem = new QGraphicsTextItem(this);
  m_labelItem->setPlainText(label);

  QFont font;
  font.setPointSize(9);
  font.setBold(true);
  m_labelItem->setFont(font);
  m_labelItem->setDefaultTextColor(Qt::white);

  // Center text below the node
  QRectF textRect = m_labelItem->boundingRect();
  m_labelItem->setPos(-textRect.width() / 2, NODE_RADIUS + 5);

  // Set initial appearance
  updateAppearance();
}

void MotionNodeItem::setLabel(const QString& label)
{
  m_label = label;
  m_labelItem->setPlainText(label);

  // Re-center text
  QRectF textRect = m_labelItem->boundingRect();
  m_labelItem->setPos(-textRect.width() / 2, NODE_RADIUS + 5);
}

void MotionNodeItem::setNodeColor(const QColor& color)
{
  m_color = color;
  updateAppearance();
}

void MotionNodeItem::setSelected(bool selected)
{
  m_isSelected = selected;
  QGraphicsEllipseItem::setSelected(selected);
  updateAppearance();
}

void MotionNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  Q_UNUSED(option);
  Q_UNUSED(widget);

  painter->setRenderHint(QPainter::Antialiasing);

  // Draw shadow if selected
  if (m_isSelected || isSelected()) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(14, 99, 156, 100));  // Semi-transparent blue
    painter->drawEllipse(rect().adjusted(-4, -4, 4, 4));
  }

  // Draw node circle
  painter->setPen(QPen(m_color.darker(120), NODE_BORDER_WIDTH));
  painter->setBrush(m_color);
  painter->drawEllipse(rect());

  // Draw inner highlight
  QRadialGradient gradient(0, -NODE_RADIUS / 3, NODE_RADIUS);
  gradient.setColorAt(0, m_color.lighter(130));
  gradient.setColorAt(1, m_color);
  painter->setBrush(gradient);
  painter->setPen(Qt::NoPen);
  painter->drawEllipse(rect());

  // Draw selection ring
  if (m_isSelected || isSelected()) {
    painter->setPen(QPen(QColor(14, 99, 156), 3));
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(rect().adjusted(-2, -2, 2, 2));
  }
}

QVariant MotionNodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
  if (change == ItemPositionHasChanged && m_graphWidget) {
    // Notify GraphWidget that this node has moved so it can update connected edges
    m_graphWidget->onNodeMoved(this);
  }

  return QGraphicsEllipseItem::itemChange(change, value);
}

void MotionNodeItem::updateAppearance()
{
  update();  // Trigger repaint
}

// MotionEdgeItem.cpp
#include "MotionEdgeItem.h"
#include "MotionNodeItem.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <QFont>

MotionEdgeItem::MotionEdgeItem(const QString& edgeId,
                               MotionNodeItem* fromNode,
                               MotionNodeItem* toNode,
                               double maxSpeed,
                               const QColor& color)
  : QGraphicsLineItem()
  , m_edgeId(edgeId)
  , m_fromNode(fromNode)
  , m_toNode(toNode)
  , m_maxSpeed(maxSpeed)
  , m_color(color)
  , m_label("")
{
  setFlag(QGraphicsItem::ItemIsSelectable, true);
  setZValue(-1);  // Draw edges below nodes

  // Create speed label
  m_speedLabel = new QGraphicsTextItem(this);
  QFont font;
  font.setPointSize(8);
  m_speedLabel->setFont(font);
  m_speedLabel->setDefaultTextColor(QColor(200, 200, 200));
  m_speedLabel->setPlainText(formatSpeedLabel());

  updatePosition();
}

void MotionEdgeItem::setMaxSpeed(double maxSpeed)
{
  m_maxSpeed = maxSpeed;
  m_speedLabel->setPlainText(formatSpeedLabel());
  updatePosition();
}

void MotionEdgeItem::setEdgeColor(const QColor& color)
{
  m_color = color;
  update();
}

void MotionEdgeItem::setLabel(const QString& label)
{
  m_label = label;
  update();
}

void MotionEdgeItem::updatePosition()
{
  if (!m_fromNode || !m_toNode) {
    return;
  }

  QPointF p1 = m_fromNode->pos();
  QPointF p2 = m_toNode->pos();

  setLine(QLineF(p1, p2));

  // Position speed label at midpoint
  QPointF midPoint = (p1 + p2) / 2.0;
  QRectF labelRect = m_speedLabel->boundingRect();
  m_speedLabel->setPos(midPoint.x() - labelRect.width() / 2,
                       midPoint.y() - labelRect.height() / 2 - 10);
}

void MotionEdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  Q_UNUSED(option);
  Q_UNUSED(widget);

  if (!m_fromNode || !m_toNode) {
    return;
  }

  painter->setRenderHint(QPainter::Antialiasing);

  QLineF l = line();
  if (l.length() == 0) {
    return;
  }

  // Adjust line to start/end at node boundaries
  qreal nodeRadius = MotionNodeItem::NODE_RADIUS;
  QLineF adjustedLine = l;

  // Shorten line from both ends
  QPointF offset1 = (l.p2() - l.p1());
  qreal len = qSqrt(offset1.x() * offset1.x() + offset1.y() * offset1.y());
  offset1 = offset1 / len * nodeRadius;

  QPointF offset2 = (l.p1() - l.p2());
  offset2 = offset2 / len * nodeRadius;

  adjustedLine.setP1(l.p1() + offset1);
  adjustedLine.setP2(l.p2() + offset2);

  // Draw line
  qreal lineWidth = isSelected() ? EDGE_WIDTH_SELECTED : EDGE_WIDTH;
  QPen pen(m_color, lineWidth);
  if (isSelected()) {
    pen.setColor(QColor(14, 99, 156));  // Blue when selected
  }
  painter->setPen(pen);
  painter->drawLine(adjustedLine);

  // Draw bidirectional arrows
  drawArrow(painter, adjustedLine.p1(), adjustedLine.p2());  // Arrow at end
  drawArrow(painter, adjustedLine.p2(), adjustedLine.p1());  // Arrow at start (reversed)
}

QPainterPath MotionEdgeItem::shape() const
{
  QPainterPath path;
  QLineF l = line();

  if (l.length() > 0) {
    // Create a thick line for easier selection
    QPainterPathStroker stroker;
    stroker.setWidth(10);
    path.moveTo(l.p1());
    path.lineTo(l.p2());
    return stroker.createStroke(path);
  }

  return path;
}

QRectF MotionEdgeItem::boundingRect() const
{
  QLineF l = line();
  qreal extra = ARROW_SIZE + EDGE_WIDTH_SELECTED;

  return QRectF(l.p1(), l.p2()).normalized()
    .adjusted(-extra, -extra, extra, extra);
}

void MotionEdgeItem::drawArrow(QPainter* painter, const QPointF& start, const QPointF& end)
{
  QLineF line(start, end);
  qreal angle = ::atan2(-line.dy(), line.dx());

  QPointF arrowP1 = end - QPointF(sin(angle + M_PI / 3) * ARROW_SIZE,
                                   cos(angle + M_PI / 3) * ARROW_SIZE);
  QPointF arrowP2 = end - QPointF(sin(angle + M_PI - M_PI / 3) * ARROW_SIZE,
                                   cos(angle + M_PI - M_PI / 3) * ARROW_SIZE);

  QPolygonF arrowHead;
  arrowHead << end << arrowP1 << arrowP2;

  painter->setBrush(m_color);
  if (isSelected()) {
    painter->setBrush(QColor(14, 99, 156));
  }
  painter->drawPolygon(arrowHead);
}

QString MotionEdgeItem::formatSpeedLabel() const
{
  if (m_maxSpeed > 0) {
    return QString("%1 mm/s").arg(m_maxSpeed, 0, 'f', 1);
  }
  return "";  // No speed limit
}

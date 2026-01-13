// DependencyEdgeItem.cpp
#include "DependencyEdgeItem.h"
#include "RecipeNodeItem.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <QFont>

DependencyEdgeItem::DependencyEdgeItem(RecipeNodeItem* sourceNode,
                                       RecipeNodeItem* targetNode,
                                       const QString& condition)
  : QGraphicsPathItem()
  , m_sourceNode(sourceNode)
  , m_targetNode(targetNode)
  , m_condition(condition)
  , m_isSelected(false)
  , m_endAngle(0)
{
  setFlag(QGraphicsItem::ItemIsSelectable, true);
  setZValue(-1);  // Draw edges below nodes

  // Create condition label
  m_conditionLabel = new QGraphicsTextItem(this);
  QFont font;
  font.setPointSize(8);
  font.setItalic(true);
  m_conditionLabel->setFont(font);
  m_conditionLabel->setDefaultTextColor(QColor(200, 200, 200));

  updatePath();
  updateConditionLabel();
}

void DependencyEdgeItem::setCondition(const QString& condition)
{
  m_condition = condition;
  updateConditionLabel();
}

void DependencyEdgeItem::setEdgeSelected(bool selected)
{
  m_isSelected = selected;
  QGraphicsPathItem::setSelected(selected);
  update();
}

void DependencyEdgeItem::updatePath()
{
  if (!m_sourceNode || !m_targetNode) {
    return;
  }

  // Get connection points
  QPointF p1 = m_sourceNode->getOutputConnectionPoint();
  QPointF p2 = m_targetNode->getInputConnectionPoint();

  // Calculate control points for bezier curve
  // Horizontal offset creates smooth S-curve
  QPointF c1 = p1 + QPointF(CURVE_CONTROL_OFFSET, 0);
  QPointF c2 = p2 - QPointF(CURVE_CONTROL_OFFSET, 0);

  // Create bezier path
  QPainterPath path;
  path.moveTo(p1);
  path.cubicTo(c1, c2, p2);

  setPath(path);

  // Store endpoint info for arrow drawing
  m_endPoint = p2;

  // Calculate angle at the end of the curve (from c2 to p2)
  QLineF endLine(c2, p2);
  m_endAngle = qDegreesToRadians(-endLine.angle());

  // Position condition label at curve midpoint
  QPointF midPoint = path.pointAtPercent(0.5);
  QRectF labelRect = m_conditionLabel->boundingRect();
  m_conditionLabel->setPos(midPoint.x() - labelRect.width() / 2,
                           midPoint.y() - labelRect.height() - 5);
}

void DependencyEdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  Q_UNUSED(option);
  Q_UNUSED(widget);

  if (!m_sourceNode || !m_targetNode) {
    return;
  }

  painter->setRenderHint(QPainter::Antialiasing);

  // Determine colors based on selection state
  QColor edgeColor = (m_isSelected || isSelected()) ? QColor("#1177bb") : QColor("#888888");
  qreal lineWidth = (m_isSelected || isSelected()) ? EDGE_WIDTH_SELECTED : EDGE_WIDTH;

  // Draw the bezier curve
  QPen pen(edgeColor, lineWidth);
  pen.setCapStyle(Qt::RoundCap);
  painter->setPen(pen);
  painter->setBrush(Qt::NoBrush);
  painter->drawPath(path());

  // Draw arrow head at the end
  drawArrowHead(painter, m_endPoint, m_endAngle);
}

void DependencyEdgeItem::drawArrowHead(QPainter* painter, const QPointF& tip, qreal angle)
{
  QColor edgeColor = (m_isSelected || isSelected()) ? QColor("#1177bb") : QColor("#888888");

  // Calculate arrow head points
  QPointF arrowP1 = tip - QPointF(qCos(angle - M_PI / 6) * ARROW_SIZE,
                                   qSin(angle - M_PI / 6) * ARROW_SIZE);
  QPointF arrowP2 = tip - QPointF(qCos(angle + M_PI / 6) * ARROW_SIZE,
                                   qSin(angle + M_PI / 6) * ARROW_SIZE);

  // Draw filled arrow head
  QPolygonF arrowHead;
  arrowHead << tip << arrowP1 << arrowP2;

  painter->setPen(Qt::NoPen);
  painter->setBrush(edgeColor);
  painter->drawPolygon(arrowHead);
}

QPainterPath DependencyEdgeItem::shape() const
{
  // Create a thick stroke path for easier selection
  QPainterPathStroker stroker;
  stroker.setWidth(12);  // Wide hit area for easier clicking
  return stroker.createStroke(path());
}

void DependencyEdgeItem::updateConditionLabel()
{
  if (m_condition.isEmpty()) {
    m_conditionLabel->setVisible(false);
  }
  else {
    m_conditionLabel->setPlainText(m_condition);
    m_conditionLabel->setVisible(true);

    // Re-position after text change
    if (!path().isEmpty()) {
      QPointF midPoint = path().pointAtPercent(0.5);
      QRectF labelRect = m_conditionLabel->boundingRect();
      m_conditionLabel->setPos(midPoint.x() - labelRect.width() / 2,
                               midPoint.y() - labelRect.height() - 5);
    }
  }
}

// PositionLabel.cpp
#include "PositionLabel.h"
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>

PositionLabel::PositionLabel(const QString& name, double anchorX, double anchorY,
  double offsetX, double offsetY)
  : QObject()  // Initialize QObject part
  , QGraphicsEllipseItem()  // Initialize QGraphicsEllipseItem part
  , m_name(name)
  , m_anchorX(anchorX)
  , m_anchorY(anchorY)
  , m_connectionLine(nullptr)
  , m_isSelected(false)
  , m_isDragging(false)
{
  // Create circle
  setRect(-LABEL_RADIUS, -LABEL_RADIUS, LABEL_RADIUS * 2, LABEL_RADIUS * 2);

  // Position at offset from anchor
  setPos(anchorX + offsetX, anchorY + offsetY);

  // Visual style
  setPen(QPen(QColor(52, 152, 219), 2));  // Blue outline
  setBrush(QBrush(QColor(52, 152, 219, 150)));  // Semi-transparent blue

  // Enable dragging and selection
  setFlag(QGraphicsItem::ItemIsMovable, true);
  setFlag(QGraphicsItem::ItemIsSelectable, true);
  setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);  // IMPORTANT!

  setAcceptHoverEvents(true);
  setCursor(Qt::OpenHandCursor);

  setZValue(10);

  // Create text label
  m_textItem = new QGraphicsTextItem(name, this);
  m_textItem->setDefaultTextColor(Qt::white);
  QFont font = m_textItem->font();
  font.setPointSize(9);
  font.setBold(true);
  m_textItem->setFont(font);

  updateTextPosition();
}

double PositionLabel::getOffsetX() const
{
  return pos().x() - m_anchorX;
}

double PositionLabel::getOffsetY() const
{
  return pos().y() - m_anchorY;
}

void PositionLabel::setSelected(bool selected)
{
  m_isSelected = selected;

  if (selected) {
    setPen(QPen(QColor(46, 204, 113), 3));  // Green when selected
    setBrush(QBrush(QColor(46, 204, 113, 180)));
    setZValue(15);
  }
  else {
    setPen(QPen(QColor(52, 152, 219), 2));
    setBrush(QBrush(QColor(52, 152, 219, 150)));
    setZValue(10);
  }
}

void PositionLabel::setConnectionLine(QGraphicsLineItem* line)
{
  m_connectionLine = line;
  updateConnectionLine();
}

void PositionLabel::updateConnectionLine()
{
  if (m_connectionLine) {
    m_connectionLine->setLine(m_anchorX, m_anchorY, pos().x(), pos().y());
  }
}

void PositionLabel::updateTextPosition()
{
  QRectF textRect = m_textItem->boundingRect();
  m_textItem->setPos(-textRect.width() / 2.0, -textRect.height() / 2.0);
}

// ADD THIS METHOD - This is the key!
QVariant PositionLabel::itemChange(GraphicsItemChange change, const QVariant& value)
{
  if (change == ItemPositionHasChanged) {
    // Update connection line
    updateConnectionLine();

    // Emit signal for saving
    emit positionChanged(m_name, getOffsetX(), getOffsetY());
  }

  return QGraphicsEllipseItem::itemChange(change, value);
}

void PositionLabel::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    m_isDragging = true;
    setCursor(Qt::ClosedHandCursor);
  }
  QGraphicsEllipseItem::mousePressEvent(event);
}

void PositionLabel::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
  QGraphicsEllipseItem::mouseMoveEvent(event);
  // Connection line is updated automatically via itemChange
}

void PositionLabel::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  if (event->button() == Qt::LeftButton) {
    m_isDragging = false;
    setCursor(Qt::OpenHandCursor);
  }
  QGraphicsEllipseItem::mouseReleaseEvent(event);
}

void PositionLabel::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
  if (!m_isDragging) {
    setCursor(Qt::OpenHandCursor);
    setBrush(QBrush(QColor(52, 152, 219, 200)));
  }
  QGraphicsEllipseItem::hoverEnterEvent(event);
}

void PositionLabel::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
  if (!m_isSelected) {
    setBrush(QBrush(QColor(52, 152, 219, 150)));
  }
  QGraphicsEllipseItem::hoverLeaveEvent(event);
}
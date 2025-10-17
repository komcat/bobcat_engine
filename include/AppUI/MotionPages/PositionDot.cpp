// PositionDot.cpp
#include "PositionDot.h"
#include <QBrush>
#include <QPen>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QColor>
#include <QFont>
#include <cmath>

PositionDot::PositionDot(const QString& name, double x, double y, double z,
  double zMin, double zMax, QGraphicsItem* parent)
  : QGraphicsEllipseItem(parent)
  , m_name(name)
  , m_x(x)
  , m_y(y)
  , m_z(z)
  , m_baseScale(1.0)
  , m_showZValue(false)  // NEW - Initially hide Z value
{
  // Create circle (radius 10)
  setRect(-10, -10, 20, 20);

  // Make the item ignore transformations for its size
  setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

  // Set color based on Z value
  updateColor(zMin, zMax);

  // Enable hover and selection
  setAcceptHoverEvents(true);
  setFlag(QGraphicsItem::ItemIsSelectable, true);

  // Create label
  m_label = new QGraphicsTextItem(this);
  m_label->setDefaultTextColor(Qt::white);
  m_label->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

  QFont font = m_label->font();
  font.setPointSize(9);
  m_label->setFont(font);

  // Initialize label text
  updateLabel();

  // Set position in scene coordinates
  setPos(x, y);
}

void PositionDot::updateColor(double zMin, double zMax)
{
  QColor color = calculateColorFromZ(m_z, zMin, zMax);
  setBrush(QBrush(color));
  setPen(QPen(Qt::white, 2));
}

void PositionDot::setSelected(bool selected)
{
  if (selected) {
    setPen(QPen(Qt::yellow, 3));
    setZValue(100); // Bring to front
    setShowZValue(true);  // NEW - Show Z when selected
  }
  else {
    setPen(QPen(Qt::white, 2));
    setZValue(0);
    setShowZValue(false);  // NEW - Hide Z when deselected
  }
  QGraphicsEllipseItem::setSelected(selected);
}

void PositionDot::updateScale(qreal viewScale)
{
  // This method is now optional since we use ItemIgnoresTransformations
  // Keep it for future fine-tuning if needed
  m_baseScale = viewScale;
}

void PositionDot::setShowZValue(bool show)
{
  m_showZValue = show;
  updateLabel();
}

void PositionDot::updateLabel()
{
  QString labelText = m_name;

  if (m_showZValue) {
    // Add Z value on second line
    labelText += QString("\nZ: %1").arg(m_z, 0, 'f', 3);
  }

  m_label->setPlainText(labelText);

  // Reposition label to center vertically
  QRectF textRect = m_label->boundingRect();
  m_label->setPos(15, -textRect.height() / 2);
}

QColor PositionDot::calculateColorFromZ(double z, double zMin, double zMax)
{
  // Normalize Z to 0-1 range
  double normalized = 0.5; // Default middle
  if (std::abs(zMax - zMin) > 0.001) {
    normalized = (z - zMin) / (zMax - zMin);
  }

  // Veritis theme: Blue (low Z) -> Cyan -> Green -> Yellow -> Red (high Z)
  // HSV: Hue 240 (blue) to 0 (red)
  int hue = static_cast<int>(240 - (normalized * 240));

  return QColor::fromHsv(hue, 200, 220); // Veritis-style vibrant colors
}

void PositionDot::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
  // Highlight on hover
  setPen(QPen(Qt::cyan, 3));
  setShowZValue(true);  // NEW - Show Z value on hover
  QGraphicsEllipseItem::hoverEnterEvent(event);
}

void PositionDot::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
  // Restore normal appearance (unless selected)
  if (!isSelected()) {
    setPen(QPen(Qt::white, 2));
    setShowZValue(false);  // NEW - Hide Z value when not hovering
  }
  QGraphicsEllipseItem::hoverLeaveEvent(event);
}

void PositionDot::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  // Handle selection
  QGraphicsEllipseItem::mousePressEvent(event);
}
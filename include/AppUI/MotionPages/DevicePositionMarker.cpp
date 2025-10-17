// DevicePositionMarker.cpp
#include "DevicePositionMarker.h"
#include <QPen>
#include <QBrush>
#include <QFont>

DevicePositionMarker::DevicePositionMarker(const QString& deviceName, QGraphicsItem* parent)
  : QGraphicsItemGroup(parent)
  , m_deviceName(deviceName)
  , m_crossH(nullptr)
  , m_crossV(nullptr)
  , m_labelBox(nullptr)
  , m_labelText(nullptr)
{
  createMarker();
  createLabel();

  // Make the marker ignore transformations (stay same size when zooming)
  setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

  setZValue(50); // Draw above dots but below selection highlights
}

void DevicePositionMarker::createMarker()
{
  // Create yellow cross marker
  QPen crossPen(QColor(255, 215, 0), 3); // Gold/Yellow color
  crossPen.setCapStyle(Qt::RoundCap);

  // Horizontal line
  m_crossH = new QGraphicsLineItem(-CROSS_SIZE, 0, CROSS_SIZE, 0, this);
  m_crossH->setPen(crossPen);
  m_crossH->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
  addToGroup(m_crossH);

  // Vertical line
  m_crossV = new QGraphicsLineItem(0, -CROSS_SIZE, 0, CROSS_SIZE, this);
  m_crossV->setPen(crossPen);
  m_crossV->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
  addToGroup(m_crossV);
}

void DevicePositionMarker::createLabel()
{
  // Create label background box FIRST (so it's behind text)
  m_labelBox = new QGraphicsRectItem(this);

  // 70% transparent yellow background (180 out of 255)
  QColor bgColor(255, 215, 0, 180);  // RGBA - 70% opacity
  m_labelBox->setBrush(QBrush(bgColor));
  m_labelBox->setPen(QPen(QColor(200, 170, 0), 2));  // Darker border
  m_labelBox->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

  // Create label text SECOND (so it's on top)
  m_labelText = new QGraphicsTextItem(m_deviceName, this);
  m_labelText->setDefaultTextColor(Qt::black);
  m_labelText->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);

  QFont font = m_labelText->font();
  font.setPointSize(11);
  font.setBold(true);
  m_labelText->setFont(font);

  // Calculate box size based on text
  QRectF textRect = m_labelText->boundingRect();
  qreal padding = 6.0;

  m_labelBox->setRect(
    textRect.adjusted(-padding, -padding, padding, padding)
  );

  // Position box and text above the cross
  m_labelBox->setPos(-textRect.width() / 2 - padding, LABEL_OFFSET_Y);
  m_labelText->setPos(-textRect.width() / 2, LABEL_OFFSET_Y);

  // IMPORTANT: Add box BEFORE text to ensure proper z-order
  addToGroup(m_labelBox);
  addToGroup(m_labelText);

  // Ensure text is on top
  m_labelText->setZValue(1);
  m_labelBox->setZValue(0);
}

void DevicePositionMarker::updatePosition(double x, double y)
{
  setPos(x, y);
}

void DevicePositionMarker::setDeviceName(const QString& name)
{
  m_deviceName = name;
  if (m_labelText) {
    m_labelText->setPlainText(name);

    // Recalculate label box size
    QRectF textRect = m_labelText->boundingRect();
    qreal padding = 6.0;

    if (m_labelBox) {
      m_labelBox->setRect(textRect.adjusted(-padding, -padding, padding, padding));
      m_labelBox->setPos(-textRect.width() / 2 - padding, LABEL_OFFSET_Y);
    }

    m_labelText->setPos(-textRect.width() / 2, LABEL_OFFSET_Y);
  }
}

void DevicePositionMarker::setVisible(bool visible)
{
  QGraphicsItemGroup::setVisible(visible);
}
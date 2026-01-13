// RecipeNodeItem.cpp
#include "RecipeNodeItem.h"
#include "DependencyGraphScene.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QFont>
#include <QLinearGradient>

RecipeNodeItem::RecipeNodeItem(int recipeIndex,
                               const QString& recipeName,
                               bool enabled)
  : QGraphicsRectItem(-NODE_WIDTH / 2, -NODE_HEIGHT / 2, NODE_WIDTH, NODE_HEIGHT)
  , m_recipeIndex(recipeIndex)
  , m_recipeName(recipeName)
  , m_enabled(enabled)
  , m_isSelected(false)
  , m_isPrerequisiteHighlight(false)
  , m_graphScene(nullptr)
{
  // Make node draggable and selectable
  setFlag(QGraphicsItem::ItemIsMovable, true);
  setFlag(QGraphicsItem::ItemIsSelectable, true);
  setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);

  // Create index label (bold, at top)
  m_indexLabel = new QGraphicsTextItem(this);
  m_indexLabel->setPlainText(QString("#%1").arg(recipeIndex));

  QFont indexFont;
  indexFont.setPointSize(10);
  indexFont.setBold(true);
  m_indexLabel->setFont(indexFont);
  m_indexLabel->setDefaultTextColor(Qt::white);

  // Create name label (below index)
  m_nameLabel = new QGraphicsTextItem(this);
  m_nameLabel->setPlainText(recipeName);

  QFont nameFont;
  nameFont.setPointSize(8);
  m_nameLabel->setFont(nameFont);
  m_nameLabel->setDefaultTextColor(Qt::white);

  // Position labels centered horizontally within the node
  QRectF indexRect = m_indexLabel->boundingRect();
  m_indexLabel->setPos(-indexRect.width() / 2, -NODE_HEIGHT / 2 + 4);

  QRectF nameRect = m_nameLabel->boundingRect();
  m_nameLabel->setPos(-nameRect.width() / 2, -NODE_HEIGHT / 2 + 22);

  // Set initial appearance
  updateAppearance();
}

void RecipeNodeItem::setRecipeName(const QString& name)
{
  m_recipeName = name;
  m_nameLabel->setPlainText(name);

  // Re-center name label
  QRectF nameRect = m_nameLabel->boundingRect();
  m_nameLabel->setPos(-nameRect.width() / 2, -NODE_HEIGHT / 2 + 22);
}

void RecipeNodeItem::setEnabled(bool enabled)
{
  m_enabled = enabled;
  updateAppearance();
}

void RecipeNodeItem::setSelected(bool selected)
{
  m_isSelected = selected;
  QGraphicsRectItem::setSelected(selected);
  updateAppearance();
}

void RecipeNodeItem::setHighlightAsPrerequisite(bool highlight)
{
  m_isPrerequisiteHighlight = highlight;
  updateAppearance();
}

QPointF RecipeNodeItem::getOutputConnectionPoint() const
{
  return pos() + QPointF(NODE_WIDTH / 2, 0);
}

QPointF RecipeNodeItem::getInputConnectionPoint() const
{
  return pos() + QPointF(-NODE_WIDTH / 2, 0);
}

QColor RecipeNodeItem::getNodeColor() const
{
  if (m_enabled) {
    return QColor("#2d5a2d");  // Dark green
  } else {
    return QColor("#4a4a4a");  // Gray
  }
}

void RecipeNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  Q_UNUSED(option);
  Q_UNUSED(widget);

  painter->setRenderHint(QPainter::Antialiasing);

  QRectF nodeRect = rect();
  QColor baseColor = getNodeColor();

  // Draw prerequisite highlight glow (orange) if highlighted
  if (m_isPrerequisiteHighlight) {
    painter->setPen(Qt::NoPen);
    QColor glowColor("#ff9800");
    glowColor.setAlpha(120);
    painter->setBrush(glowColor);
    painter->drawRoundedRect(nodeRect.adjusted(-6, -6, 6, 6), CORNER_RADIUS + 4, CORNER_RADIUS + 4);
  }

  // Draw selection shadow if selected
  if (m_isSelected || isSelected()) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(17, 119, 187, 100));  // Semi-transparent blue
    painter->drawRoundedRect(nodeRect.adjusted(-4, -4, 4, 4), CORNER_RADIUS + 2, CORNER_RADIUS + 2);
  }

  // Draw node rectangle with border
  painter->setPen(QPen(baseColor.darker(120), BORDER_WIDTH));
  painter->setBrush(baseColor);
  painter->drawRoundedRect(nodeRect, CORNER_RADIUS, CORNER_RADIUS);

  // Draw gradient overlay for inner highlight
  QLinearGradient gradient(nodeRect.topLeft(), nodeRect.bottomLeft());
  gradient.setColorAt(0, baseColor.lighter(130));
  gradient.setColorAt(0.4, baseColor);
  gradient.setColorAt(1, baseColor.darker(110));
  painter->setBrush(gradient);
  painter->setPen(Qt::NoPen);
  painter->drawRoundedRect(nodeRect, CORNER_RADIUS, CORNER_RADIUS);

  // Draw selection ring
  if (m_isSelected || isSelected()) {
    painter->setPen(QPen(QColor("#1177bb"), 3));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(nodeRect.adjusted(-2, -2, 2, 2), CORNER_RADIUS + 1, CORNER_RADIUS + 1);
  }
}

QVariant RecipeNodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
  if (change == ItemPositionHasChanged && m_graphScene) {
    // Notify DependencyGraphScene that this node has moved so it can update connected edges
    m_graphScene->onNodeMoved(this);
  }

  return QGraphicsRectItem::itemChange(change, value);
}

void RecipeNodeItem::updateAppearance()
{
  update();  // Trigger repaint
}

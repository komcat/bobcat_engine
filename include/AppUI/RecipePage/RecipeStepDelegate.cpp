// RecipeStepDelegate.cpp
#include "RecipeStepDelegate.h"
#include "RecipeManager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>

RecipeStepDelegate::RecipeStepDelegate(RecipeManager* recipeManager, QObject* parent)
  : QStyledItemDelegate(parent)
  , m_recipeManager(recipeManager)
  , m_textPrimary(0xd4, 0xd4, 0xd4)
  , m_textSecondary(0x99, 0x99, 0x99)
  , m_textComment(0x6a, 0x99, 0x55)
  , m_bgSelected(0x0e, 0x63, 0x9c)
  , m_checkboxChecked(0x4c, 0xaf, 0x50)
  , m_checkboxUnchecked(0x55, 0x55, 0x55)
{
}

QString RecipeStepDelegate::getCategorySymbol(const std::string& category)
{
  if (category == "Motion Control" || category == "Motion") {
    return QString::fromUtf8("\xE2\x9E\xA4");  // >
  }
  if (category == "I/O & Pneumatics") {
    return QString::fromUtf8("\xE2\x9A\xA1");  // Lightning
  }
  if (category == "TEC & Laser") {
    return QString::fromUtf8("\xE2\x98\x80");  // Sun
  }
  if (category == "Power Supply") {
    return QString::fromUtf8("\xE2\x8F\xBB");  // Power
  }
  if (category == "SMU Control") {
    return QString::fromUtf8("\xE2\x8C\xA1");  // Measurement
  }
  if (category == "Data") {
    return QString::fromUtf8("\xE2\x96\xA3");  // Data
  }
  if (category == "System") {
    return QString::fromUtf8("\xE2\x9C\x8B");  // Hand
  }
  if (category == "Control Flow") {
    return QString::fromUtf8("\xE2\x86\xB9");  // Control
  }
  if (category == "Camera" || category == "Vision") {
    return QString::fromUtf8("\xE2\x97\x89");  // Eye
  }
  if (category == "Calibration") {
    return QString::fromUtf8("\xE2\x9C\x93");  // Check
  }
  if (category == "Dispensing") {
    return QString::fromUtf8("\xE2\x96\xBC");  // Down
  }
  if (category == "Aux Window") {
    return QString::fromUtf8("\xE2\x96\xA1");  // Window
  }
  if (category == "Comment") {
    return QString::fromUtf8("#");
  }
  return QString::fromUtf8("\xE2\x97\x8F");  // Circle
}

void RecipeStepDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
  painter->save();

  // Get data from model
  int stepIndex = index.data(StepIndexRole).toInt();
  bool enabled = index.data(EnabledRole).toBool();
  QString displayText = index.data(DisplayTextRole).toString();
  QString category = index.data(CategoryRole).toString();
  QString bgColorStr = index.data(BackgroundColorRole).toString();
  bool isComment = index.data(ProcessTypeRole).toString() == "Comment";

  bool selected = option.state & QStyle::State_Selected;

  // Draw background
  QColor bgColor;
  if (selected) {
    bgColor = m_bgSelected;
  } else if (!bgColorStr.isEmpty() && bgColorStr != "transparent") {
    bgColor = QColor(bgColorStr);
  } else {
    bgColor = QColor(0x1e, 0x1e, 0x1e);  // Default dark background
  }
  painter->fillRect(option.rect, bgColor);

  // Draw based on column
  int column = index.column();
  switch (column) {
    case 0:  // Step number
      paintStepNumber(painter, option.rect, stepIndex, enabled, selected);
      break;
    case 1:  // Category symbol
      paintSymbol(painter, option.rect, getCategorySymbol(category.toStdString()), enabled, selected);
      break;
    case 2:  // Process name
      paintProcessName(painter, option.rect, displayText, enabled, isComment, selected);
      break;
    case 3:  // Run checkbox
      paintRunCheckbox(painter, option.rect, enabled, selected);
      break;
  }

  painter->restore();
}

QSize RecipeStepDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
  Q_UNUSED(option);
  int column = index.column();
  switch (column) {
    case 0: return QSize(35, 28);   // Step number
    case 1: return QSize(30, 28);   // Symbol
    case 2: return QSize(200, 28);  // Process name (will stretch)
    case 3: return QSize(50, 28);   // Checkbox
    default: return QSize(50, 28);
  }
}

bool RecipeStepDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index)
{
  // Only handle checkbox column
  if (index.column() != 3) {
    return QStyledItemDelegate::editorEvent(event, model, option, index);
  }

  if (event->type() == QEvent::MouseButtonRelease) {
    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
    QRect checkboxRect = getCheckboxRect(option.rect);

    if (checkboxRect.contains(mouseEvent->pos())) {
      int stepIndex = index.data(StepIndexRole).toInt();
      bool currentEnabled = index.data(EnabledRole).toBool();

      // Toggle enabled state
      if (m_onEnabledChanged) {
        m_onEnabledChanged(stepIndex, !currentEnabled);
      }
      return true;
    }
  }

  return QStyledItemDelegate::editorEvent(event, model, option, index);
}

void RecipeStepDelegate::paintStepNumber(QPainter* painter, const QRect& rect,
                                          int stepIndex, bool enabled, bool selected) const
{
  QFont font = painter->font();
  font.setBold(true);
  font.setPointSize(10);
  painter->setFont(font);

  QColor textColor = enabled ? m_textPrimary : m_textSecondary;
  if (selected) textColor = Qt::white;
  painter->setPen(textColor);

  painter->drawText(rect, Qt::AlignCenter, QString::number(stepIndex + 1));
}

void RecipeStepDelegate::paintSymbol(QPainter* painter, const QRect& rect,
                                      const QString& symbol, bool enabled, bool selected) const
{
  QFont font = painter->font();
  font.setPointSize(11);
  painter->setFont(font);

  QColor textColor = enabled ? m_textPrimary : m_textSecondary;
  if (selected) textColor = Qt::white;
  painter->setPen(textColor);

  painter->drawText(rect, Qt::AlignCenter, symbol);
}

void RecipeStepDelegate::paintProcessName(QPainter* painter, const QRect& rect,
                                           const QString& text, bool enabled,
                                           bool isComment, bool selected) const
{
  QFont font = painter->font();
  font.setPointSize(10);
  if (isComment && enabled) {
    font.setItalic(true);
  }
  painter->setFont(font);

  QColor textColor;
  if (!enabled) {
    textColor = m_textSecondary;
  } else if (isComment) {
    textColor = m_textComment;
  } else {
    textColor = m_textPrimary;
  }
  if (selected) textColor = Qt::white;
  painter->setPen(textColor);

  // Draw with padding
  QRect textRect = rect.adjusted(8, 0, -4, 0);
  QString displayText = text;

  // Strikethrough for disabled
  if (!enabled) {
    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(displayText);
    int y = rect.center().y();
    int x = textRect.left();
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, displayText);
    painter->drawLine(x, y, x + qMin(textWidth, textRect.width()), y);
  } else {
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, displayText);
  }
}

void RecipeStepDelegate::paintRunCheckbox(QPainter* painter, const QRect& rect,
                                           bool checked, bool selected) const
{
  Q_UNUSED(selected);

  QRect checkboxRect = getCheckboxRect(rect);

  // Draw checkbox background
  QColor bgColor = checked ? m_checkboxChecked : m_checkboxUnchecked;
  painter->setBrush(bgColor);
  painter->setPen(Qt::NoPen);
  painter->drawRoundedRect(checkboxRect, 3, 3);

  // Draw checkmark if checked
  if (checked) {
    painter->setPen(QPen(Qt::white, 2));
    int cx = checkboxRect.center().x();
    int cy = checkboxRect.center().y();
    // Draw checkmark
    painter->drawLine(cx - 4, cy, cx - 1, cy + 3);
    painter->drawLine(cx - 1, cy + 3, cx + 4, cy - 3);
  }
}

QRect RecipeStepDelegate::getCheckboxRect(const QRect& cellRect) const
{
  int size = 20;
  int x = cellRect.center().x() - size / 2;
  int y = cellRect.center().y() - size / 2;
  return QRect(x, y, size, size);
}

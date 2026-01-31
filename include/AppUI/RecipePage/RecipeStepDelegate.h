// RecipeStepDelegate.h
// High-performance delegate for painting recipe step items
// Uses direct painting instead of embedded widgets for better performance
#pragma once

#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <functional>

class RecipeManager;
struct ProcessInstance;

/**
 * @brief High-performance delegate for recipe step items
 *
 * Replaces setItemWidget() approach with direct painting for much better
 * performance during scrolling and resizing.
 */
class RecipeStepDelegate : public QStyledItemDelegate
{
  Q_OBJECT

public:
  // Custom data roles for storing step data
  enum DataRole {
    StepIndexRole = Qt::UserRole,
    EnabledRole = Qt::UserRole + 1,
    ProcessTypeRole = Qt::UserRole + 2,
    DisplayTextRole = Qt::UserRole + 3,
    CategoryRole = Qt::UserRole + 4,
    BackgroundColorRole = Qt::UserRole + 5
  };

  using EnabledCallback = std::function<void(int stepIndex, bool enabled)>;

  explicit RecipeStepDelegate(RecipeManager* recipeManager, QObject* parent = nullptr);

  void setEnabledCallback(EnabledCallback callback) { m_onEnabledChanged = callback; }

  // QStyledItemDelegate overrides
  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
  bool editorEvent(QEvent* event, QAbstractItemModel* model,
                   const QStyleOptionViewItem& option,
                   const QModelIndex& index) override;

  // Get category symbol for a process type
  static QString getCategorySymbol(const std::string& category);

private:
  void paintStepNumber(QPainter* painter, const QRect& rect,
                       int stepIndex, bool enabled, bool selected) const;
  void paintSymbol(QPainter* painter, const QRect& rect,
                   const QString& symbol, bool enabled, bool selected) const;
  void paintProcessName(QPainter* painter, const QRect& rect,
                        const QString& text, bool enabled, bool isComment, bool selected) const;
  void paintRunCheckbox(QPainter* painter, const QRect& rect,
                        bool checked, bool selected) const;

  QRect getCheckboxRect(const QRect& cellRect) const;

  RecipeManager* m_recipeManager;
  EnabledCallback m_onEnabledChanged;

  // Cached colors for performance
  QColor m_textPrimary;
  QColor m_textSecondary;
  QColor m_textComment;
  QColor m_bgSelected;
  QColor m_checkboxChecked;
  QColor m_checkboxUnchecked;
};

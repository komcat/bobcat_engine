// ContextViewerWindow.h
#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <map>
#include <string>

class SequenceContext;

/**
 * @brief Independent window for viewing SequenceContext values
 *
 * Always-on-top window that displays context variables in a table.
 * Shows: Item #, Variable Key, Value
 */
class ContextViewerWindow : public QWidget
{
  Q_OBJECT

public:
  explicit ContextViewerWindow(QWidget* parent = nullptr);
  virtual ~ContextViewerWindow() = default;

  // Update the table with current context values
  void updateFromContext(SequenceContext* context);

  // Clear the table
  void clearContext();

  // Get the number of items displayed
  int getItemCount() const;

public slots:
  // Refresh button clicked
  void onRefreshClicked();

  // Clear button clicked
  void onClearClicked();

  // Copy to clipboard
  void onCopyClicked();

signals:
  // Emitted when user requests refresh
  void refreshRequested();

private:
  void setupUI();
  void applyStyles();

  QTableWidget* m_table;
  QLabel* m_statusLabel;
  QPushButton* m_refreshButton;
  QPushButton* m_clearButton;
  QPushButton* m_copyButton;
  int m_itemCount;
};

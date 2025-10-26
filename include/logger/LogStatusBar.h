// LogStatusBar.h - Bottom status bar with expand/collapse functionality
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <deque>
#include "LogManager.h"

/**
 * @brief Bottom status bar showing last log entry with expandable history
 */
class LogStatusBar : public QWidget
{
  Q_OBJECT

public:
  explicit LogStatusBar(LogManager* logManager, QWidget* parent = nullptr);
  ~LogStatusBar() = default;

  void UpdateFromLog(const LogManager::LogEntry& entry);

signals:
  void openFullLogViewer();

private slots:
  void onOpenFullViewer();
  void onToggleExpand();

private:
  void setupUI();
  void updateStatusLabel();
  void updateExpandedView();
  QString formatLogEntry(const LogManager::LogEntry& entry, bool compact = false);

  LogManager* m_logManager;

  // Status bar components
  QLabel* m_statusIcon;
  QLabel* m_statusText;
  QLabel* m_errorWarningCount;
  QPushButton* m_expandButton;
  QPushButton* m_viewerButton;

  // Expanded view
  QTextEdit* m_expandedLogView;
  bool m_isExpanded;

  // Track last messages (up to 5)
  std::deque<LogManager::LogEntry> m_recentEntries;
  static constexpr size_t MAX_RECENT_ENTRIES = 5;

  // Height constants
  static constexpr int COLLAPSED_HEIGHT = 35;
  static constexpr int EXPANDED_HEIGHT = 180;
};
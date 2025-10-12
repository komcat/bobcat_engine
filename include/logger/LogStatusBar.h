// LogStatusBar.h - Bottom status bar with expandable log drawer
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include "LogManager.h"

/**
 * @brief Bottom status bar showing last log + expandable drawer
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
  void onToggleDrawer();
  void onOpenFullViewer();

private:
  void setupUI();
  void updateStatusLabel();
  void addToDrawer(const LogManager::LogEntry& entry);
  QString formatLogEntry(const LogManager::LogEntry& entry);

  LogManager* m_logManager;

  // Status bar (always visible)
  QLabel* m_statusIcon;
  QLabel* m_statusText;
  QLabel* m_errorWarningCount;
  QPushButton* m_expandButton;
  QPushButton* m_viewerButton;

  // Expandable drawer
  QWidget* m_drawer;
  QTextEdit* m_drawerLog;
  bool m_drawerExpanded;

  // Track last message
  LogManager::LogEntry m_lastEntry;
};
// LogStatusBar.h - Bottom status bar (compact view only)
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include "LogManager.h"

/**
 * @brief Bottom status bar showing last log entry
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

private:
  void setupUI();
  void updateStatusLabel();

  LogManager* m_logManager;

  // Status bar components
  QLabel* m_statusIcon;
  QLabel* m_statusText;
  QLabel* m_errorWarningCount;
  QPushButton* m_viewerButton;

  // Track last message
  LogManager::LogEntry m_lastEntry;
};
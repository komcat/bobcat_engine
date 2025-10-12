// LogViewerPage.h
#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include "LogManager.h"

class LogViewerPage : public QWidget
{
  Q_OBJECT

public:
  explicit LogViewerPage(LogManager* logManager, QWidget* parent = nullptr);
  ~LogViewerPage() = default;

  void RefreshLogs();

signals:
  void backRequested();  // ✅ NEW - Signal to go back

private slots:
  void onBackClicked();  // ✅ NEW
  void onFilterChanged();
  void onSearchChanged();
  void onClearClicked();
  void onSaveClicked();
  void onCopyClicked();
  void onRefreshClicked();
  void onAutoScrollToggled(bool checked);

private:
  void setupUI();
  void displayLogs(const std::vector<LogManager::LogEntry>& entries);
  QString formatLogEntry(const LogManager::LogEntry& entry);
  bool matchesFilter(const LogManager::LogEntry& entry);

  LogManager* m_logManager;

  QPushButton* m_backButton;  // ✅ NEW
  QTextEdit* m_logDisplay;
  QComboBox* m_levelFilter;
  QLineEdit* m_searchBox;
  QPushButton* m_clearButton;
  QPushButton* m_saveButton;
  QPushButton* m_copyButton;
  QPushButton* m_refreshButton;
  QCheckBox* m_autoScrollCheck;
  QLabel* m_statsLabel;

  bool m_autoScroll;
};
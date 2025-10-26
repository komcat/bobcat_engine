// LogStatusBar.cpp
#include "LogStatusBar.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollBar>

LogStatusBar::LogStatusBar(LogManager* logManager, QWidget* parent)
  : QWidget(parent)
  , m_logManager(logManager)
  , m_isExpanded(false)
{
  setupUI();
}

void LogStatusBar::setupUI()
{
  setFixedHeight(COLLAPSED_HEIGHT);

  // Background color
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(43, 43, 43));  // #2b2b2b
  setAutoFillBackground(true);
  setPalette(pal);

  // Main vertical layout (collapsed bar + expanded view)
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Collapsed status bar (always visible)
  QWidget* collapsedBar = new QWidget();
  collapsedBar->setFixedHeight(COLLAPSED_HEIGHT);
  QHBoxLayout* barLayout = new QHBoxLayout(collapsedBar);
  barLayout->setContentsMargins(10, 0, 10, 0);
  barLayout->setSpacing(10);

  // Status icon (increased from 16px to 18px)
  m_statusIcon = new QLabel("●");
  m_statusIcon->setStyleSheet("QLabel { color: #00ff00; font-size: 18px; }");
  barLayout->addWidget(m_statusIcon);

  // Status text (increased from 12px to 14px)
  m_statusText = new QLabel("Ready");
  m_statusText->setStyleSheet("QLabel { color: #cccccc; font-size: 14px; }");
  barLayout->addWidget(m_statusText, 1);

  // Error/warning count (increased from 11px to 13px)
  m_errorWarningCount = new QLabel("⚠ 0  ✗ 0");
  m_errorWarningCount->setStyleSheet("QLabel { color: #ffaa00; font-size: 13px; font-weight: bold; }");
  barLayout->addWidget(m_errorWarningCount);

  // Expand/collapse button
  m_expandButton = new QPushButton("▼");
  m_expandButton->setFixedSize(30, 25);
  m_expandButton->setToolTip("Expand to show recent logs");
  m_expandButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3a3a3a; "
    "  color: white; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  font-size: 12px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4a; "
    "  border-color: #00aaff; "
    "}");
  connect(m_expandButton, &QPushButton::clicked, this, &LogStatusBar::onToggleExpand);
  barLayout->addWidget(m_expandButton);

  // Full viewer button (increased font from 11px to 12px)
  m_viewerButton = new QPushButton("📋 Full Log");
  m_viewerButton->setFixedSize(90, 25);
  m_viewerButton->setToolTip("Open Full Log Viewer");
  m_viewerButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3a3a3a; "
    "  color: white; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "  font-size: 12px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4a; "
    "  border-color: #00aaff; "
    "}");
  connect(m_viewerButton, &QPushButton::clicked, this, &LogStatusBar::onOpenFullViewer);
  barLayout->addWidget(m_viewerButton);

  mainLayout->addWidget(collapsedBar);

  // Expanded log view (initially hidden)
  m_expandedLogView = new QTextEdit();
  m_expandedLogView->setReadOnly(true);
  m_expandedLogView->setFixedHeight(EXPANDED_HEIGHT - COLLAPSED_HEIGHT);
  m_expandedLogView->setStyleSheet(
    "QTextEdit { "
    "  background-color: #2b2b2b; "
    "  color: #cccccc; "
    "  border: none; "
    "  border-top: 1px solid #555555; "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  font-size: 13px; "
    "  padding: 5px; "
    "}");
  m_expandedLogView->hide();
  mainLayout->addWidget(m_expandedLogView);
}

void LogStatusBar::UpdateFromLog(const LogManager::LogEntry& entry)
{
  // Add to recent entries (keep last 5)
  m_recentEntries.push_back(entry);
  if (m_recentEntries.size() > MAX_RECENT_ENTRIES) {
    m_recentEntries.pop_front();
  }

  updateStatusLabel();

  // Update counts
  size_t errors = m_logManager->GetErrorCount();
  size_t warnings = m_logManager->GetWarningCount();
  m_errorWarningCount->setText(QString("⚠ %1  ✗ %2").arg(warnings).arg(errors));

  // Update expanded view if visible
  if (m_isExpanded) {
    updateExpandedView();
  }
}

void LogStatusBar::updateStatusLabel()
{
  if (m_recentEntries.empty()) {
    return;
  }

  const auto& entry = m_recentEntries.back();

  // Update icon color based on level (increased from 16px to 18px)
  QString color = QString::fromStdString(
    LogManager::LevelToColorCode(entry.level));
  m_statusIcon->setStyleSheet(QString("QLabel { color: %1; font-size: 18px; }").arg(color));

  // Update text
  QString levelStr = QString::fromStdString(LogManager::LevelToString(entry.level));
  QString msg = QString::fromStdString(entry.message);

  // Truncate if too long
  if (msg.length() > 80) {
    msg = msg.left(77) + "...";
  }

  m_statusText->setText(QString("[%1] %2").arg(levelStr).arg(msg));
}

void LogStatusBar::updateExpandedView()
{
  m_expandedLogView->clear();

  // Show recent entries in reverse order (most recent at bottom)
  for (const auto& entry : m_recentEntries) {
    m_expandedLogView->append(formatLogEntry(entry, true));
  }

  // Scroll to bottom
  m_expandedLogView->verticalScrollBar()->setValue(
    m_expandedLogView->verticalScrollBar()->maximum());
}

QString LogStatusBar::formatLogEntry(const LogManager::LogEntry& entry, bool compact)
{
  QString color = QString::fromStdString(LogManager::LevelToColorCode(entry.level));
  QString level = QString::fromStdString(LogManager::LevelToString(entry.level));
  QString timestamp = QString::fromStdString(entry.timestamp);
  QString message = QString::fromStdString(entry.message);
  QString source = QString::fromStdString(entry.source);

  if (compact) {
    // Compact format for expanded view (no timestamp, shorter)
    QString formatted = QString("<span style='color: %1; font-weight: bold;'>[%2]</span> ")
      .arg(color)
      .arg(level);

    if (!source.isEmpty()) {
      formatted += QString("<span style='color: #0066cc;'>[%1]</span> ").arg(source);
    }

    formatted += QString("<span style='color: #cccccc;'>%1</span>").arg(message);
    return formatted;
  }
  else {
    // Full format with timestamp
    QString formatted = QString("<span style='color: #888888;'>[%1]</span> "
      "<span style='color: %2; font-weight: bold;'>[%3]</span> ")
      .arg(timestamp)
      .arg(color)
      .arg(level);

    if (!source.isEmpty()) {
      formatted += QString("<span style='color: #0066cc;'>[%1]</span> ").arg(source);
    }

    formatted += QString("<span style='color: #cccccc;'>%1</span>").arg(message);
    return formatted;
  }
}

void LogStatusBar::onToggleExpand()
{
  m_isExpanded = !m_isExpanded;

  if (m_isExpanded) {
    // Expand
    m_expandButton->setText("▲");
    m_expandButton->setToolTip("Collapse");
    setFixedHeight(EXPANDED_HEIGHT);
    updateExpandedView();
    m_expandedLogView->show();
  }
  else {
    // Collapse
    m_expandButton->setText("▼");
    m_expandButton->setToolTip("Expand to show recent logs");
    setFixedHeight(COLLAPSED_HEIGHT);
    m_expandedLogView->hide();
  }
}

void LogStatusBar::onOpenFullViewer()
{
  emit openFullLogViewer();
}
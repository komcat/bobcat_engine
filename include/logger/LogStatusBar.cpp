// LogStatusBar.cpp
#include "LogStatusBar.h"
#include <QHBoxLayout>
#include <QScrollBar>

LogStatusBar::LogStatusBar(LogManager* logManager, QWidget* parent)
  : QWidget(parent)
  , m_logManager(logManager)
  , m_drawerExpanded(false)
{
  setupUI();
}

void LogStatusBar::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Status bar (always visible)
  QWidget* statusBar = new QWidget();
  statusBar->setFixedHeight(30);
  statusBar->setStyleSheet("QWidget { background-color: #2b2b2b; border-top: 1px solid #555555; }");

  QHBoxLayout* statusLayout = new QHBoxLayout(statusBar);
  statusLayout->setContentsMargins(10, 0, 10, 0);
  statusLayout->setSpacing(10);

  // Status icon
  m_statusIcon = new QLabel("●");
  m_statusIcon->setStyleSheet("QLabel { color: #00ff00; font-size: 16px; }");
  statusLayout->addWidget(m_statusIcon);

  // Status text
  m_statusText = new QLabel("Ready");
  m_statusText->setStyleSheet("QLabel { color: #cccccc; font-size: 12px; }");
  statusLayout->addWidget(m_statusText, 1);

  // Error/warning count
  m_errorWarningCount = new QLabel("⚠ 0  ✗ 0");
  m_errorWarningCount->setStyleSheet("QLabel { color: #888888; font-size: 11px; }");
  statusLayout->addWidget(m_errorWarningCount);

  // Expand button
  m_expandButton = new QPushButton("▲");
  m_expandButton->setFixedSize(25, 25);
  m_expandButton->setStyleSheet(
    "QPushButton { background-color: #3a3a3a; color: white; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #4a4a4a; }");
  connect(m_expandButton, &QPushButton::clicked, this, &LogStatusBar::onToggleDrawer);
  statusLayout->addWidget(m_expandButton);

  // Full viewer button
  m_viewerButton = new QPushButton("📋");
  m_viewerButton->setFixedSize(25, 25);
  m_viewerButton->setToolTip("Open Full Log Viewer");
  m_viewerButton->setStyleSheet(
    "QPushButton { background-color: #3a3a3a; color: white; border: none; border-radius: 3px; }"
    "QPushButton:hover { background-color: #4a4a4a; }");
  connect(m_viewerButton, &QPushButton::clicked, this, &LogStatusBar::onOpenFullViewer);
  statusLayout->addWidget(m_viewerButton);

  mainLayout->addWidget(statusBar);

  // Expandable drawer (hidden by default)
  m_drawer = new QWidget();
  m_drawer->setFixedHeight(150);
  m_drawer->setVisible(false);
  m_drawer->setStyleSheet("QWidget { background-color: #1e1e1e; }");

  QVBoxLayout* drawerLayout = new QVBoxLayout(m_drawer);
  drawerLayout->setContentsMargins(5, 5, 5, 5);

  m_drawerLog = new QTextEdit();
  m_drawerLog->setReadOnly(true);
  m_drawerLog->setStyleSheet(
    "QTextEdit { "
    "  background-color: #1e1e1e; "
    "  color: #cccccc; "
    "  border: none; "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  font-size: 11px; "
    "}");
  drawerLayout->addWidget(m_drawerLog);

  mainLayout->addWidget(m_drawer);

  setLayout(mainLayout);
}

void LogStatusBar::UpdateFromLog(const LogManager::LogEntry& entry)
{
  m_lastEntry = entry;
  updateStatusLabel();
  addToDrawer(entry);

  // Update counts
  size_t errors = m_logManager->GetErrorCount();
  size_t warnings = m_logManager->GetWarningCount();
  m_errorWarningCount->setText(QString("⚠ %1  ✗ %2").arg(warnings).arg(errors));
}

void LogStatusBar::updateStatusLabel()
{
  // Update icon color based on level
  QString color = QString::fromStdString(
    LogManager::LevelToColorCode(m_lastEntry.level));
  m_statusIcon->setStyleSheet(QString("QLabel { color: %1; font-size: 16px; }").arg(color));

  // Update text
  QString levelStr = QString::fromStdString(LogManager::LevelToString(m_lastEntry.level));
  QString msg = QString::fromStdString(m_lastEntry.message);

  // Truncate if too long
  if (msg.length() > 80) {
    msg = msg.left(77) + "...";
  }

  m_statusText->setText(QString("[%1] %2").arg(levelStr).arg(msg));
}

void LogStatusBar::addToDrawer(const LogManager::LogEntry& entry)
{
  if (!m_drawerExpanded) {
    return; // Don't update if not visible
  }

  QString formatted = formatLogEntry(entry);
  m_drawerLog->append(formatted);

  // Auto-scroll to bottom
  QScrollBar* scrollBar = m_drawerLog->verticalScrollBar();
  scrollBar->setValue(scrollBar->maximum());
}

QString LogStatusBar::formatLogEntry(const LogManager::LogEntry& entry)
{
  QString color = QString::fromStdString(LogManager::LevelToColorCode(entry.level));
  QString level = QString::fromStdString(LogManager::LevelToString(entry.level));
  QString timestamp = QString::fromStdString(entry.timestamp);
  QString message = QString::fromStdString(entry.message);
  QString source = QString::fromStdString(entry.source);

  QString formatted = QString("<span style='color: #888888;'>[%1]</span> "
    "<span style='color: %2;'>[%3]</span> ")
    .arg(timestamp)
    .arg(color)
    .arg(level);

  if (!source.isEmpty()) {
    formatted += QString("<span style='color: #00aaff;'>[%1]</span> ").arg(source);
  }

  formatted += QString("<span style='color: #cccccc;'>%1</span>").arg(message);

  return formatted;
}

void LogStatusBar::onToggleDrawer()
{
  m_drawerExpanded = !m_drawerExpanded;
  m_drawer->setVisible(m_drawerExpanded);
  m_expandButton->setText(m_drawerExpanded ? "▼" : "▲");

  if (m_drawerExpanded) {
    // Load recent entries when opening
    m_drawerLog->clear();
    auto recent = m_logManager->GetRecentEntries(10);
    for (const auto& entry : recent) {
      m_drawerLog->append(formatLogEntry(entry));
    }
  }
}

void LogStatusBar::onOpenFullViewer()
{
  emit openFullLogViewer();
}
// LogStatusBar.cpp
#include "LogStatusBar.h"
#include <QHBoxLayout>

LogStatusBar::LogStatusBar(LogManager* logManager, QWidget* parent)
  : QWidget(parent)
  , m_logManager(logManager)
{
  setupUI();
}

void LogStatusBar::setupUI()
{
  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(10, 0, 10, 0);
  mainLayout->setSpacing(10);

  setFixedHeight(30);
  setStyleSheet("QWidget { background-color: #2b2b2b; border-top: 1px solid #555555; }");

  // Status icon
  m_statusIcon = new QLabel("●");
  m_statusIcon->setStyleSheet("QLabel { color: #00ff00; font-size: 16px; }");
  mainLayout->addWidget(m_statusIcon);

  // Status text
  m_statusText = new QLabel("Ready");
  m_statusText->setStyleSheet("QLabel { color: #cccccc; font-size: 12px; }");
  mainLayout->addWidget(m_statusText, 1);

  // Error/warning count
  m_errorWarningCount = new QLabel("⚠ 0  ✗ 0");
  m_errorWarningCount->setStyleSheet("QLabel { color: #888888; font-size: 11px; }");
  mainLayout->addWidget(m_errorWarningCount);

  // Full viewer button
  m_viewerButton = new QPushButton("📋 Full Log");
  m_viewerButton->setFixedSize(80, 25);
  m_viewerButton->setToolTip("Open Full Log Viewer");
  m_viewerButton->setStyleSheet(
    "QPushButton { background-color: #3a3a3a; color: white; border: none; border-radius: 3px; font-size: 11px; }"
    "QPushButton:hover { background-color: #4a4a4a; }");
  connect(m_viewerButton, &QPushButton::clicked, this, &LogStatusBar::onOpenFullViewer);
  mainLayout->addWidget(m_viewerButton);
}

void LogStatusBar::UpdateFromLog(const LogManager::LogEntry& entry)
{
  m_lastEntry = entry;
  updateStatusLabel();

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

void LogStatusBar::onOpenFullViewer()
{
  emit openFullLogViewer();
}
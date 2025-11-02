// LogViewerPage.cpp
#include "LogViewerPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QScrollBar>
#include <Qshortcut>
#include <fstream>

// LogViewerPage.cpp - Add to constructor
LogViewerPage::LogViewerPage(LogManager* logManager, QWidget* parent)
  : QWidget(parent)
  , m_logManager(logManager)
  , m_autoScroll(true)
{
  setupUI();
  RefreshLogs();

  // ✅ Add ESC key shortcut
  QShortcut* escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  connect(escShortcut, &QShortcut::activated, this, &LogViewerPage::onBackClicked);
}

// LogViewerPage.cpp - Add to setupUI() method
void LogViewerPage::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // Header with back button and title
  QHBoxLayout* headerLayout = new QHBoxLayout();

  m_backButton = new QPushButton("← Back");
  m_backButton->setFixedWidth(100);
  m_backButton->setStyleSheet(
    "QPushButton { "
    "  padding: 8px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "}");
  connect(m_backButton, &QPushButton::clicked, this, &LogViewerPage::onBackClicked);
  headerLayout->addWidget(m_backButton);

  // Title
  QLabel* title = new QLabel("System Log Viewer");
  title->setStyleSheet("QLabel { font-size: 20px; font-weight: bold; padding: 5px; }");
  headerLayout->addWidget(title);

  headerLayout->addStretch();
  mainLayout->addLayout(headerLayout);

  // Toolbar
  QHBoxLayout* toolbarLayout = new QHBoxLayout();

  // Level filter
  QLabel* filterLabel = new QLabel("Level:");
  filterLabel->setStyleSheet("QLabel { font-weight: bold; }");
  toolbarLayout->addWidget(filterLabel);

  m_levelFilter = new QComboBox();
  m_levelFilter->addItem("All", -1);
  m_levelFilter->addItem("DEBUG", static_cast<int>(LogManager::Level::DEBUG));
  m_levelFilter->addItem("INFO", static_cast<int>(LogManager::Level::INFO));
  m_levelFilter->addItem("WARNING", static_cast<int>(LogManager::Level::WARNING));
  m_levelFilter->addItem("FAILURE", static_cast<int>(LogManager::Level::FAILURE));
  m_levelFilter->addItem("CRITICAL", static_cast<int>(LogManager::Level::CRITICAL));
  connect(m_levelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &LogViewerPage::onFilterChanged);
  toolbarLayout->addWidget(m_levelFilter);

  // Search box
  QLabel* searchLabel = new QLabel("Search:");
  searchLabel->setStyleSheet("QLabel { font-weight: bold; }");
  toolbarLayout->addWidget(searchLabel);

  m_searchBox = new QLineEdit();
  m_searchBox->setPlaceholderText("Filter messages...");
  connect(m_searchBox, &QLineEdit::textChanged, this, &LogViewerPage::onSearchChanged);
  toolbarLayout->addWidget(m_searchBox);

  toolbarLayout->addStretch();

  // Auto-scroll checkbox
  m_autoScrollCheck = new QCheckBox("Auto-scroll");
  m_autoScrollCheck->setChecked(true);
  m_autoScrollCheck->setStyleSheet("QCheckBox { font-weight: bold; }");
  connect(m_autoScrollCheck, &QCheckBox::toggled, this, &LogViewerPage::onAutoScrollToggled);
  toolbarLayout->addWidget(m_autoScrollCheck);

  // Buttons
  m_refreshButton = new QPushButton("🔄 Refresh");
  connect(m_refreshButton, &QPushButton::clicked, this, &LogViewerPage::onRefreshClicked);
  toolbarLayout->addWidget(m_refreshButton);

  m_copyButton = new QPushButton("📋 Copy");
  connect(m_copyButton, &QPushButton::clicked, this, &LogViewerPage::onCopyClicked);
  toolbarLayout->addWidget(m_copyButton);

  m_saveButton = new QPushButton("💾 Save");
  connect(m_saveButton, &QPushButton::clicked, this, &LogViewerPage::onSaveClicked);
  toolbarLayout->addWidget(m_saveButton);

  m_clearButton = new QPushButton("🗑 Clear");
  m_clearButton->setStyleSheet(
    "QPushButton { background-color: #dc3545; border: 1px solid #c82333; }"
    "QPushButton:hover { background-color: #c82333; }");
  connect(m_clearButton, &QPushButton::clicked, this, &LogViewerPage::onClearClicked);
  toolbarLayout->addWidget(m_clearButton);

  mainLayout->addLayout(toolbarLayout);

  // Stats label
  m_statsLabel = new QLabel("Total: 0 | Warnings: 0 | Errors: 0");
  m_statsLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 12px; "
    "  font-weight: bold; "
    "  background-color: #1e1e1e; "
    "  padding: 8px; "
    "  border: 1px solid #555555; "
    "  border-radius: 3px; "
    "}");
  mainLayout->addWidget(m_statsLabel);

  // Log display
  m_logDisplay = new QTextEdit();
  m_logDisplay->setReadOnly(true);
  m_logDisplay->setStyleSheet(
    "QTextEdit { "
    "  font-family: 'Consolas', 'Courier New', monospace; "
    "  font-size: 13px; "
    "}");
  mainLayout->addWidget(m_logDisplay);
}


// ✅ NEW - Add this method at the end of the file
void LogViewerPage::onBackClicked()
{
  emit backRequested();
}

// ... rest of the file stays the same


void LogViewerPage::RefreshLogs()
{
  auto entries = m_logManager->GetAllEntries();
  displayLogs(entries);

  // Update stats
  size_t total = m_logManager->GetTotalCount();
  size_t warnings = m_logManager->GetWarningCount();
  size_t errors = m_logManager->GetErrorCount();
  m_statsLabel->setText(QString("Total: %1 | Warnings: %2 | Errors: %3")
    .arg(total).arg(warnings).arg(errors));
}

void LogViewerPage::displayLogs(const std::vector<LogManager::LogEntry>& entries)
{
  m_logDisplay->clear();

  for (const auto& entry : entries) {
    if (matchesFilter(entry)) {
      m_logDisplay->append(formatLogEntry(entry));
    }
  }

  if (m_autoScroll) {
    QScrollBar* scrollBar = m_logDisplay->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
  }
}

QString LogViewerPage::formatLogEntry(const LogManager::LogEntry& entry)
{
  QString color = QString::fromStdString(LogManager::LevelToColorCode(entry.level));
  QString level = QString::fromStdString(LogManager::LevelToString(entry.level));
  QString timestamp = QString::fromStdString(entry.timestamp);
  QString message = QString::fromStdString(entry.message);
  QString source = QString::fromStdString(entry.source);

  QString formatted = QString("<span style='color: #a0a0a0;'>[%1]</span> "
    "<span style='color: %2; font-weight: bold;'>[%3]</span> ")
    .arg(timestamp)
    .arg(color)
    .arg(level);

  if (!source.isEmpty()) {
    formatted += QString("<span style='color: #4fc3f7;'>[%1]</span> ").arg(source);
  }

  formatted += QString("<span style='color: #d4d4d4;'>%1</span>").arg(message);

  return formatted;
}


bool LogViewerPage::matchesFilter(const LogManager::LogEntry& entry)
{
  // Level filter
  int filterLevel = m_levelFilter->currentData().toInt();
  if (filterLevel >= 0 && static_cast<int>(entry.level) != filterLevel) {
    return false;
  }

  // Search filter
  QString searchText = m_searchBox->text().toLower();
  if (!searchText.isEmpty()) {
    QString message = QString::fromStdString(entry.message).toLower();
    QString source = QString::fromStdString(entry.source).toLower();
    if (!message.contains(searchText) && !source.contains(searchText)) {
      return false;
    }
  }

  return true;
}

void LogViewerPage::onFilterChanged()
{
  RefreshLogs();
}

void LogViewerPage::onSearchChanged()
{
  RefreshLogs();
}

void LogViewerPage::onClearClicked()
{
  auto reply = QMessageBox::question(this, "Clear Logs",
    "Are you sure you want to clear all log history?",
    QMessageBox::Yes | QMessageBox::No);

  if (reply == QMessageBox::Yes) {
    m_logManager->ClearHistory();
    RefreshLogs();
  }
}

void LogViewerPage::onSaveClicked()
{
  QString fileName = QFileDialog::getSaveFileName(this,
    "Save Log File", "bobcat_log.txt", "Text Files (*.txt);;All Files (*)");

  if (fileName.isEmpty()) {
    return;
  }

  try {
    std::ofstream file(fileName.toStdString());
    if (!file.is_open()) {
      QMessageBox::critical(this, "Error", "Failed to open file for writing");
      return;
    }

    auto entries = m_logManager->GetAllEntries();
    for (const auto& entry : entries) {
      file << "[" << entry.timestamp << "] "
        << "[" << LogManager::LevelToString(entry.level) << "] ";
      if (!entry.source.empty()) {
        file << "[" << entry.source << "] ";
      }
      file << entry.message << "\n";
    }

    file.close();
    QMessageBox::information(this, "Success",
      QString("Log saved to:\n%1").arg(fileName));
  }
  catch (const std::exception& e) {
    QMessageBox::critical(this, "Error",
      QString("Failed to save log: %1").arg(e.what()));
  }
}

void LogViewerPage::onCopyClicked()
{
  QString text = m_logDisplay->toPlainText();
  QApplication::clipboard()->setText(text);
  QMessageBox::information(this, "Copied", "Log contents copied to clipboard");
}

void LogViewerPage::onRefreshClicked()
{
  RefreshLogs();
}

void LogViewerPage::onAutoScrollToggled(bool checked)
{
  m_autoScroll = checked;
}
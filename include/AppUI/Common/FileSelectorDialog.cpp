// FileSelectorDialog.cpp
#include "FileSelectorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <QGroupBox>
#include <QBrush>
#include <algorithm>

FileSelectorDialog::FileSelectorDialog(
  const QString& folderPath,
  const QString& filePattern,
  const QString& dialogTitle,
  const QString& fileTypeDescription,
  bool recursive,
  QWidget* parent)
  : QDialog(parent)
  , m_folderPath(folderPath)
  , m_filePattern(filePattern)
  , m_dialogTitle(dialogTitle)
  , m_fileTypeDescription(fileTypeDescription)
  , m_selectedFilePath("")
  , m_recursive(recursive)
{
  setupUI();
  loadFiles();
  populateFileTable();
}

QString FileSelectorDialog::getOpenFileName(
  QWidget* parent,
  const QString& folderPath,
  const QString& filePattern,
  const QString& dialogTitle,
  const QString& fileTypeDescription,
  bool recursive)
{
  FileSelectorDialog dialog(folderPath, filePattern, dialogTitle, fileTypeDescription, recursive, parent);

  if (dialog.exec() == QDialog::Accepted) {
    return dialog.getSelectedFilePath();
  }

  return QString();
}

void FileSelectorDialog::setupUI()
{
  setWindowTitle(m_dialogTitle);
  setMinimumSize(700, 500);
  resize(800, 600);

  // Set comprehensive light theme for the entire dialog
  setStyleSheet(
    "QDialog { "
    "  background-color: white; "
    "} "
    "QGroupBox { "
    "  background-color: white; "
    "  color: #2c3e50; "
    "  border: 1px solid #ddd; "
    "  border-radius: 4px; "
    "  margin-top: 10px; "
    "  padding-top: 10px; "
    "} "
    "QGroupBox::title { "
    "  subcontrol-origin: margin; "
    "  subcontrol-position: top left; "
    "  padding: 2px 5px; "
    "  background-color: white; "
    "  color: #2c3e50; "
    "} "
    "QLabel { "
    "  background-color: transparent; "
    "  color: #2c3e50; "
    "} "
    "QLineEdit { "
    "  background-color: white; "
    "  color: #2c3e50; "
    "  border: 1px solid #bdc3c7; "
    "}"
  );

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // ============================================================================
  // Header section
  // ============================================================================
  QGroupBox* headerGroup = new QGroupBox();
  QVBoxLayout* headerLayout = new QVBoxLayout(headerGroup);

  m_folderLabel = new QLabel(QString("📁 Folder: <b>%1</b>").arg(m_folderPath));
  m_folderLabel->setStyleSheet("font-size: 12px; padding: 5px;");
  headerLayout->addWidget(m_folderLabel);

  m_fileCountLabel = new QLabel();
  m_fileCountLabel->setStyleSheet("font-size: 11px; color: #666; padding: 2px 5px;");
  headerLayout->addWidget(m_fileCountLabel);

  mainLayout->addWidget(headerGroup);

  // ============================================================================
  // Search bar
  // ============================================================================
  QHBoxLayout* searchLayout = new QHBoxLayout();
  QLabel* searchLabel = new QLabel("🔍 Search:");
  searchLabel->setStyleSheet("font-size: 11px;");
  searchLayout->addWidget(searchLabel);

  m_searchEdit = new QLineEdit();
  m_searchEdit->setPlaceholderText("Type to filter files...");
  m_searchEdit->setStyleSheet("padding: 5px; font-size: 11px;");
  connect(m_searchEdit, &QLineEdit::textChanged, this, &FileSelectorDialog::onSearchTextChanged);
  searchLayout->addWidget(m_searchEdit);

  m_refreshButton = new QPushButton("🔄 Refresh");
  m_refreshButton->setStyleSheet(
    "QPushButton { background-color: #95a5a6; color: white; font-weight: bold; padding: 5px 15px; border-radius: 3px; }"
    "QPushButton:hover { background-color: #7f8c8d; }"
  );
  connect(m_refreshButton, &QPushButton::clicked, this, &FileSelectorDialog::onRefresh);
  searchLayout->addWidget(m_refreshButton);

  mainLayout->addLayout(searchLayout);

  // ============================================================================
  // File table
  // ============================================================================
  m_fileTable = new QTableWidget();

  // Conditional column count based on recursive mode
  if (m_recursive) {
    m_fileTable->setColumnCount(4);
    m_fileTable->setHorizontalHeaderLabels({"File Name", "Location", "Date Modified", "Size"});
  } else {
    m_fileTable->setColumnCount(3);
    m_fileTable->setHorizontalHeaderLabels({"File Name", "Date Modified", "Size"});
  }

  m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_fileTable->setSelectionMode(QAbstractItemView::SingleSelection);
  m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_fileTable->horizontalHeader()->setStretchLastSection(false);
  m_fileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

  if (m_recursive) {
    m_fileTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_fileTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fileTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  } else {
    m_fileTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_fileTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  }

  m_fileTable->setAlternatingRowColors(true);
  m_fileTable->setStyleSheet(
    "QTableWidget { "
    "  background-color: white; "
    "  alternate-background-color: #f5f5f5; "
    "  gridline-color: #ddd; "
    "  font-size: 11px; "
    "} "
    "QTableWidget::item:selected { "
    "  background-color: #3498db; "
    "  color: white; "
    "}"
  );

  connect(m_fileTable, &QTableWidget::cellDoubleClicked,
    this, &FileSelectorDialog::onFileDoubleClicked);

  mainLayout->addWidget(m_fileTable);

  // ============================================================================
  // Bottom buttons
  // ============================================================================
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_selectButton = new QPushButton("✓ Select File");
  m_selectButton->setMinimumHeight(35);
  m_selectButton->setMinimumWidth(120);
  m_selectButton->setStyleSheet(
    "QPushButton { background-color: #27ae60; color: white; font-weight: bold; font-size: 12px; border-radius: 3px; }"
    "QPushButton:hover { background-color: #229954; }"
    "QPushButton:disabled { background-color: #bdc3c7; }"
  );
  m_selectButton->setEnabled(false);
  connect(m_selectButton, &QPushButton::clicked, this, &FileSelectorDialog::onFileSelected);
  buttonLayout->addWidget(m_selectButton);

  m_cancelButton = new QPushButton("✗ Cancel");
  m_cancelButton->setMinimumHeight(35);
  m_cancelButton->setMinimumWidth(120);
  m_cancelButton->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; font-weight: bold; font-size: 12px; border-radius: 3px; }"
    "QPushButton:hover { background-color: #c0392b; }"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &FileSelectorDialog::onCancel);
  buttonLayout->addWidget(m_cancelButton);

  mainLayout->addLayout(buttonLayout);

  // Enable select button when row is selected
  connect(m_fileTable, &QTableWidget::itemSelectionChanged, this, [this]() {
    m_selectButton->setEnabled(m_fileTable->currentRow() >= 0);
  });
}

void FileSelectorDialog::loadFiles()
{
  m_allFiles.clear();

  QDir baseDir(m_folderPath);
  if (!baseDir.exists()) {
    // Try to create the directory
    if (!baseDir.mkpath(".")) {
      QMessageBox::warning(this, "Folder Not Found",
        QString("The folder '%1' does not exist and could not be created.").arg(m_folderPath));
      return;
    }
  }

  // Set name filters
  QStringList filters;
  filters << m_filePattern;

  if (m_recursive) {
    // Recursive search - find all matching files in subdirectories
    QDirIterator it(m_folderPath, filters, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
      QString filePath = it.next();
      QFileInfo fileInfo(filePath);

      FileInfo info;
      info.fileName = fileInfo.fileName();
      info.fullPath = fileInfo.absoluteFilePath();

      // Calculate relative path from base folder
      QString relativePath = baseDir.relativeFilePath(fileInfo.absolutePath());
      info.relativePath = relativePath;

      info.dateModified = fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss");
      info.fileSize = fileInfo.size();

      m_allFiles.append(info);
    }

    // Sort by relative path, then by filename
    std::sort(m_allFiles.begin(), m_allFiles.end(),
      [](const FileInfo& a, const FileInfo& b) {
        if (a.relativePath != b.relativePath) {
          return a.relativePath < b.relativePath;
        }
        return a.fileName < b.fileName;
      });
  } else {
    // Non-recursive - only files in the specified folder
    baseDir.setNameFilters(filters);
    baseDir.setFilter(QDir::Files);
    baseDir.setSorting(QDir::Time | QDir::Reversed); // Most recent first

    QFileInfoList fileList = baseDir.entryInfoList();

    for (const QFileInfo& fileInfo : fileList) {
      FileInfo info;
      info.fileName = fileInfo.fileName();
      info.fullPath = fileInfo.absoluteFilePath();
      info.relativePath = ".";  // Current directory
      info.dateModified = fileInfo.lastModified().toString("yyyy-MM-dd hh:mm:ss");
      info.fileSize = fileInfo.size();

      m_allFiles.append(info);
    }
  }

  m_filteredFiles = m_allFiles;
}

void FileSelectorDialog::populateFileTable()
{
  m_fileTable->setRowCount(0);

  for (int i = 0; i < m_filteredFiles.size(); ++i) {
    const FileInfo& file = m_filteredFiles[i];

    m_fileTable->insertRow(i);

    int colIndex = 0;

    // File name
    QTableWidgetItem* nameItem = new QTableWidgetItem(file.fileName);
    nameItem->setData(Qt::UserRole, file.fullPath);
    m_fileTable->setItem(i, colIndex++, nameItem);

    // Location (only in recursive mode)
    if (m_recursive) {
      QString displayPath = file.relativePath;
      if (displayPath == ".") {
        displayPath = "(root)";
      }
      QTableWidgetItem* locationItem = new QTableWidgetItem(displayPath);
      locationItem->setForeground(QBrush(QColor("#666")));
      m_fileTable->setItem(i, colIndex++, locationItem);
    }

    // Date modified
    QTableWidgetItem* dateItem = new QTableWidgetItem(file.dateModified);
    m_fileTable->setItem(i, colIndex++, dateItem);

    // File size
    QString sizeStr;
    if (file.fileSize < 1024) {
      sizeStr = QString("%1 B").arg(file.fileSize);
    } else if (file.fileSize < 1024 * 1024) {
      sizeStr = QString("%1 KB").arg(file.fileSize / 1024.0, 0, 'f', 1);
    } else {
      sizeStr = QString("%1 MB").arg(file.fileSize / (1024.0 * 1024.0), 0, 'f', 2);
    }
    QTableWidgetItem* sizeItem = new QTableWidgetItem(sizeStr);
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_fileTable->setItem(i, colIndex++, sizeItem);
  }

  // Update folder label to show actual file location
  if (m_recursive && !m_filteredFiles.isEmpty()) {
    // Check if all files are in the same subdirectory
    QString commonPath = m_filteredFiles.first().relativePath;
    bool allSameFolder = true;

    for (const FileInfo& file : m_filteredFiles) {
      if (file.relativePath != commonPath) {
        allSameFolder = false;
        break;
      }
    }

    if (allSameFolder && commonPath != ".") {
      // All files in same subfolder - show the full path
      m_folderLabel->setText(QString("📁 Folder: <b>%1/%2</b>").arg(m_folderPath).arg(commonPath));
    } else if (!allSameFolder) {
      // Files in multiple folders - indicate recursive search
      m_folderLabel->setText(QString("📁 Folder: <b>%1</b> (+ subdirectories)").arg(m_folderPath));
    } else {
      // All files in root folder
      m_folderLabel->setText(QString("📁 Folder: <b>%1</b>").arg(m_folderPath));
    }
  }

  // Update file count label
  if (m_searchEdit->text().isEmpty()) {
    m_fileCountLabel->setText(QString("Found <b>%1</b> %2")
      .arg(m_filteredFiles.size())
      .arg(m_fileTypeDescription));
  } else {
    m_fileCountLabel->setText(QString("Showing <b>%1</b> of <b>%2</b> %3")
      .arg(m_filteredFiles.size())
      .arg(m_allFiles.size())
      .arg(m_fileTypeDescription));
  }
}

void FileSelectorDialog::applySearchFilter()
{
  QString searchText = m_searchEdit->text().trimmed();

  if (searchText.isEmpty()) {
    m_filteredFiles = m_allFiles;
  } else {
    m_filteredFiles.clear();
    for (const FileInfo& file : m_allFiles) {
      if (file.fileName.contains(searchText, Qt::CaseInsensitive)) {
        m_filteredFiles.append(file);
      }
    }
  }

  populateFileTable();
}

void FileSelectorDialog::onFileDoubleClicked(int row, int column)
{
  Q_UNUSED(column);

  if (row >= 0 && row < m_fileTable->rowCount()) {
    QTableWidgetItem* item = m_fileTable->item(row, 0);
    if (item) {
      m_selectedFilePath = item->data(Qt::UserRole).toString();
      accept();
    }
  }
}

void FileSelectorDialog::onFileSelected()
{
  int currentRow = m_fileTable->currentRow();
  if (currentRow >= 0) {
    QTableWidgetItem* item = m_fileTable->item(currentRow, 0);
    if (item) {
      m_selectedFilePath = item->data(Qt::UserRole).toString();
      accept();
    }
  }
}

void FileSelectorDialog::onCancel()
{
  m_selectedFilePath.clear();
  reject();
}

void FileSelectorDialog::onRefresh()
{
  loadFiles();
  applySearchFilter();
}

void FileSelectorDialog::onSearchTextChanged(const QString& searchText)
{
  Q_UNUSED(searchText);
  applySearchFilter();
}

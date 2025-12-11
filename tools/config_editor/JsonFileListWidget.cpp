// JsonFileListWidget.cpp
#include "JsonFileListWidget.h"

#include <QVBoxLayout>
#include <QFileInfo>

JsonFileListWidget::JsonFileListWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void JsonFileListWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // Folder path label
    m_folderLabel = new QLabel("No folder selected", this);
    m_folderLabel->setWordWrap(true);
    layout->addWidget(m_folderLabel);

    // Filter input
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Filter files...");
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &JsonFileListWidget::onFilterChanged);
    layout->addWidget(m_filterEdit);

    // File list
    m_fileList = new QListWidget(this);
    connect(m_fileList, &QListWidget::itemClicked, this, &JsonFileListWidget::onFileClicked);
    connect(m_fileList, &QListWidget::itemDoubleClicked, this, &JsonFileListWidget::onFileDoubleClicked);
    layout->addWidget(m_fileList, 1);

    // File count label
    QLabel* countLabel = new QLabel("0 files", this);
    countLabel->setObjectName("fileCountLabel");
    layout->addWidget(countLabel);
}

void JsonFileListWidget::setFolder(const QString& folderPath)
{
    m_folderPath = folderPath;

    // Update folder label
    QFileInfo info(folderPath);
    m_folderLabel->setText("Folder: " + info.fileName());
    m_folderLabel->setToolTip(folderPath);

    refreshFileList();
}

void JsonFileListWidget::refreshFileList()
{
    m_allFiles.clear();
    m_fileList->clear();

    if (m_folderPath.isEmpty()) {
        return;
    }

    QDir dir(m_folderPath);
    if (!dir.exists()) {
        return;
    }

    // Get all JSON files
    QStringList filters;
    filters << "*.json";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    for (const QFileInfo& fileInfo : files) {
        m_allFiles.append(fileInfo.fileName());
    }

    applyFilter(m_filterEdit->text());
}

void JsonFileListWidget::applyFilter(const QString& filter)
{
    m_fileList->clear();

    int count = 0;
    for (const QString& fileName : m_allFiles) {
        if (filter.isEmpty() || fileName.contains(filter, Qt::CaseInsensitive)) {
            QListWidgetItem* item = new QListWidgetItem(fileName);
            item->setData(Qt::UserRole, m_folderPath + "/" + fileName);
            m_fileList->addItem(item);
            count++;
        }
    }

    // Update count label
    QLabel* countLabel = findChild<QLabel*>("fileCountLabel");
    if (countLabel) {
        countLabel->setText(QString::number(count) + " file" + (count != 1 ? "s" : ""));
    }
}

void JsonFileListWidget::onFilterChanged(const QString& text)
{
    applyFilter(text);
}

void JsonFileListWidget::onFileClicked(QListWidgetItem* item)
{
    if (!item) return;

    QString filePath = item->data(Qt::UserRole).toString();
    emit fileSelected(filePath);
}

void JsonFileListWidget::onFileDoubleClicked(QListWidgetItem* item)
{
    // Same behavior as single click for now
    onFileClicked(item);
}

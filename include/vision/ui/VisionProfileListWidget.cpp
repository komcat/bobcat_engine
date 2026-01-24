// VisionProfileListWidget.cpp
#include "VisionProfileListWidget.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QInputDialog>
#include <QAction>
#include <QRegularExpression>
#include <iostream>

VisionProfileListWidget::VisionProfileListWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_profileList(nullptr)
    , m_titleLabel(nullptr)
    , m_watcher(new QFileSystemWatcher(this))
{
    setupUI();

    // Connect file system watcher
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &VisionProfileListWidget::onDirectoryChanged);
}

void VisionProfileListWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 10, 5, 10);
    layout->setSpacing(5);

    // Title
    m_titleLabel = new QLabel("Vision Profiles:", this);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(m_titleLabel);

    // Profile list
    m_profileList = new QListWidget(this);
    m_profileList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_profileList->setToolTip("Double-click to load profile\nRight-click for options");
    m_profileList->setStyleSheet(R"(
        QListWidget {
            background-color: #2d2d2d;
            border: 1px solid #555;
            border-radius: 3px;
        }
        QListWidget::item {
            padding: 5px;
            border-bottom: 1px solid #444;
        }
        QListWidget::item:selected {
            background-color: #1565C0;
        }
        QListWidget::item:hover {
            background-color: #3d3d3d;
        }
    )");
    layout->addWidget(m_profileList, 1);

    // Connect signals
    connect(m_profileList, &QListWidget::itemDoubleClicked,
            this, &VisionProfileListWidget::onItemDoubleClicked);
    connect(m_profileList, &QListWidget::customContextMenuRequested,
            this, &VisionProfileListWidget::onCustomContextMenu);

    // Set fixed width
    setFixedWidth(180);
}

void VisionProfileListWidget::setVisionDirectory(const QString& dir)
{
    // Remove old directory from watcher
    if (!m_visionDir.isEmpty()) {
        m_watcher->removePath(m_visionDir);
    }

    m_visionDir = dir;

    // Add new directory to watcher if it exists
    if (!m_visionDir.isEmpty() && QDir(m_visionDir).exists()) {
        m_watcher->addPath(m_visionDir);
    }

    refreshProfileList();
}

void VisionProfileListWidget::refreshProfileList()
{
    m_profileList->clear();

    // Get vision directory
    QString visionDir = m_visionDir;
    if (visionDir.isEmpty()) {
        if (m_context && m_context->productPositionManager &&
            m_context->productPositionManager->HasLoadedProduct()) {
            QString productFilePath = QString::fromStdString(
                m_context->productPositionManager->GetCurrentProductPath());
            QString productDir = QFileInfo(productFilePath).path();
            visionDir = productDir + "/vision";
        } else {
            visionDir = "config/vision";
        }
    }

    QDir dir(visionDir);
    if (!dir.exists()) {
        m_profileList->addItem("(No profiles found)");
        m_profileList->item(0)->setFlags(Qt::NoItemFlags);
        return;
    }

    // Get all JSON files (excluding .recipe.json)
    QStringList filters;
    filters << "*.json";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name);

    QFileInfoList files = dir.entryInfoList();
    int count = 0;
    for (const QFileInfo& fileInfo : files) {
        // Skip recipe files
        if (fileInfo.fileName().endsWith(".recipe.json")) {
            continue;
        }

        QListWidgetItem* item = new QListWidgetItem(fileInfo.baseName());
        item->setData(Qt::UserRole, fileInfo.absoluteFilePath());
        item->setToolTip(fileInfo.absoluteFilePath());
        m_profileList->addItem(item);
        count++;
    }

    if (count == 0) {
        m_profileList->addItem("(No profiles found)");
        m_profileList->item(0)->setFlags(Qt::NoItemFlags);
    }

    std::cout << "[VisionProfileList] Loaded " << count << " profiles from: "
              << visionDir.toStdString() << std::endl;
}

QString VisionProfileListWidget::getCurrentProfilePath() const
{
    QListWidgetItem* item = m_profileList->currentItem();
    if (item && item->flags() != Qt::NoItemFlags) {
        return item->data(Qt::UserRole).toString();
    }
    return QString();
}

void VisionProfileListWidget::onItemDoubleClicked(QListWidgetItem* item)
{
    if (!item || item->flags() == Qt::NoItemFlags) {
        return;
    }

    QString filePath = item->data(Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        loadProfile(filePath);
    }
}

void VisionProfileListWidget::onCustomContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_profileList->itemAt(pos);
    if (!item || item->flags() == Qt::NoItemFlags) {
        return;
    }

    QString filePath = item->data(Qt::UserRole).toString();

    QMenu contextMenu(this);

    QAction* loadAction = contextMenu.addAction("Load");
    loadAction->setIcon(QIcon::fromTheme("document-open"));

    contextMenu.addSeparator();

    QAction* renameAction = contextMenu.addAction("Rename");
    renameAction->setIcon(QIcon::fromTheme("edit-rename"));

    QAction* deleteAction = contextMenu.addAction("Delete");
    deleteAction->setIcon(QIcon::fromTheme("edit-delete"));

    QAction* selectedAction = contextMenu.exec(m_profileList->mapToGlobal(pos));

    if (selectedAction == loadAction) {
        loadProfile(filePath);
    } else if (selectedAction == renameAction) {
        renameProfile(filePath);
    } else if (selectedAction == deleteAction) {
        deleteProfile(filePath);
    }
}

void VisionProfileListWidget::onDirectoryChanged(const QString& path)
{
    Q_UNUSED(path);
    refreshProfileList();
}

void VisionProfileListWidget::loadProfile(const QString& filePath)
{
    std::cout << "[VisionProfileList] Load requested: " << filePath.toStdString() << std::endl;
    emit profileLoadRequested(filePath);
}

void VisionProfileListWidget::deleteProfile(const QString& filePath)
{
    QFileInfo fileInfo(filePath);

    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "Delete Profile",
        QString("Are you sure you want to delete?\n\n%1").arg(fileInfo.fileName()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    QFile file(filePath);
    if (file.remove()) {
        std::cout << "[VisionProfileList] Deleted: " << filePath.toStdString() << std::endl;
        emit profileDeleted(filePath);
        refreshProfileList();
    } else {
        QMessageBox::critical(this, "Error",
            QString("Failed to delete file:\n%1").arg(filePath));
    }
}

void VisionProfileListWidget::renameProfile(const QString& filePath)
{
    QFileInfo fileInfo(filePath);
    QString oldName = fileInfo.baseName();

    bool ok;
    QString newName = QInputDialog::getText(this, "Rename Profile",
        "Enter new name:", QLineEdit::Normal, oldName, &ok);

    if (!ok || newName.isEmpty() || newName == oldName) {
        return;
    }

    // Validate new name (no special characters)
    if (newName.contains(QRegularExpression("[\\\\/:*?\"<>|]"))) {
        QMessageBox::warning(this, "Invalid Name",
            "Profile name cannot contain: \\ / : * ? \" < > |");
        return;
    }

    QString newPath = fileInfo.path() + "/" + newName + ".json";

    // Check if new name already exists
    if (QFile::exists(newPath)) {
        QMessageBox::warning(this, "Name Exists",
            QString("A profile named '%1' already exists.").arg(newName));
        return;
    }

    QFile file(filePath);
    if (file.rename(newPath)) {
        std::cout << "[VisionProfileList] Renamed: " << oldName.toStdString()
                  << " -> " << newName.toStdString() << std::endl;
        emit profileRenamed(filePath, newPath);
        refreshProfileList();
    } else {
        QMessageBox::critical(this, "Error",
            QString("Failed to rename file:\n%1").arg(file.errorString()));
    }
}

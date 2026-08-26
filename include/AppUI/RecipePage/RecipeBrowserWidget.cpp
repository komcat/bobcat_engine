// RecipeBrowserWidget.cpp
#include "RecipeBrowserWidget.h"

#include "ApplicationContext.h"
#include "RecipeManager.h"
#include "RecipePageStyles.h"
#include "Theme.h"
#include "product_position_manager.h"

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QSettings>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <filesystem>
#include <iostream>
#include <system_error>

namespace {

// Compact action button QSS used by the 4 small folder/recipe buttons.
QString CompactPrimaryQss()
{
  return QString(
    "QPushButton { background-color: %1; color: white; border: none; "
    "border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
    "QPushButton:hover { background-color: %2; }"
    "QPushButton:disabled { background-color: %3; color: %4; }"
  ).arg(QLatin1String(Theme::Color::primary),
        QLatin1String(Theme::Color::primaryHover),
        QLatin1String(Theme::Color::disabledBg),
        QLatin1String(Theme::Color::textDisabled));
}

QString CompactNeutralQss()
{
  return QString(
    "QPushButton { background-color: %1; color: %2; border: none; "
    "border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
    "QPushButton:hover { background-color: %3; }"
    "QPushButton:disabled { background-color: %4; color: %5; }"
  ).arg(QLatin1String(Theme::Color::bgSurface),
        QLatin1String(Theme::Color::textPrimary),
        QLatin1String(Theme::Color::bgHover),
        QLatin1String(Theme::Color::disabledBg),
        QLatin1String(Theme::Color::textDisabled));
}

QString CompactDangerQss()
{
  return QString(
    "QPushButton { background-color: %1; color: white; border: none; "
    "border-radius: 3px; padding: 2px 8px; font-size: 11px; font-weight: bold; }"
    "QPushButton:hover { background-color: %2; }"
    "QPushButton:disabled { background-color: %3; color: %4; }"
  ).arg(QLatin1String(Theme::Color::danger),
        QLatin1String(Theme::Color::dangerHover),
        QLatin1String(Theme::Color::disabledBg),
        QLatin1String(Theme::Color::textDisabled));
}

QString TreeWidgetQss()
{
  return QString(
    "QTreeWidget { "
    "  background-color: %1; "
    "  color: %2; "
    "  border: 1px solid %3; "
    "  border-radius: 3px; "
    "  font-size: 14px; "
    "} "
    "QTreeWidget::item { "
    "  padding: 7px 8px; "
    "  border-bottom: 1px solid %4; "
    "} "
    "QTreeWidget::item:selected { "
    "  background-color: %5; "
    "  color: %6; "
    "} "
    "QTreeWidget::item:hover { "
    "  background-color: %7; "
    "} "
    "QTreeWidget::branch { "
    "  background-color: %1; "
    "} "
    "QTreeWidget::branch:has-siblings:!adjoins-item { "
    "  border-image: none; "
    "} "
    "QTreeWidget::branch:open:has-children { "
    "  image: url(none); "
    "} "
    "QTreeWidget::branch:closed:has-children { "
    "  image: url(none); "
    "}"
  ).arg(QLatin1String(Theme::Color::bgDeep),
        QLatin1String(Theme::Color::textPrimary),
        QLatin1String(Theme::Color::borderSubtle),
        QLatin1String(Theme::Color::bgPrimary),
        QLatin1String(Theme::Color::primary),
        QLatin1String(Theme::Color::textOnPrimary),
        QLatin1String(Theme::Color::bgHover));
}

QString ContextMenuQss()
{
  return QString(
    "QMenu { "
    "  background-color: %1; "
    "  border: 1px solid %2; "
    "  padding: 4px; "
    "} "
    "QMenu::item { "
    "  background-color: transparent; "
    "  padding: 6px 20px; "
    "  color: %3; "
    "} "
    "QMenu::item:selected { "
    "  background-color: %4; "
    "  color: %5; "
    "} "
    "QMenu::separator { "
    "  height: 1px; "
    "  background: %2; "
    "  margin: 4px 10px; "
    "}"
  ).arg(QLatin1String(Theme::Color::bgDeep),
        QLatin1String(Theme::Color::borderNormal),
        QLatin1String(Theme::Color::textPrimary),
        QLatin1String(Theme::Color::primary),
        QLatin1String(Theme::Color::textOnPrimary));
}

}  // namespace

RecipeBrowserWidget::RecipeBrowserWidget(ApplicationContext* context,
                                         RecipeManager* recipeManager,
                                         QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_recipeManager(recipeManager)
{
  setupUI();
}

void RecipeBrowserWidget::setupUI()
{
  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(5);

  // Header row: count label + Refresh button
  QHBoxLayout* headerLayout = new QHBoxLayout();
  m_recipeCountLabel = new QLabel("0 recipe(s)");
  m_recipeCountLabel->setStyleSheet(QString("font-size: 12px; color: %1;")
                                      .arg(QLatin1String(Theme::Color::textSecondary)));
  headerLayout->addWidget(m_recipeCountLabel);
  headerLayout->addStretch();

  m_refreshRecipeListButton = new QPushButton("Refresh");
  m_refreshRecipeListButton->setToolTip("Refresh recipe list");
  m_refreshRecipeListButton->setFixedSize(70, 28);
  m_refreshRecipeListButton->setStyleSheet(RecipeStyles::smallToolButton());
  connect(m_refreshRecipeListButton, &QPushButton::clicked, this,
          &RecipeBrowserWidget::onRefreshClicked);
  headerLayout->addWidget(m_refreshRecipeListButton);

  layout->addLayout(headerLayout);

  // Filter/search box
  m_recipeFilterEdit = new QLineEdit();
  m_recipeFilterEdit->setPlaceholderText("Filter recipes...");
  m_recipeFilterEdit->setClearButtonEnabled(true);
  m_recipeFilterEdit->setStyleSheet(RecipeStyles::searchBox());
  connect(m_recipeFilterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
    for (int i = 0; i < m_recipeListWidget->topLevelItemCount(); ++i) {
      QTreeWidgetItem* topItem = m_recipeListWidget->topLevelItem(i);
      if (!topItem) continue;
      QString itemType = topItem->data(0, Qt::UserRole + 1).toString();
      if (itemType == "folder") {
        bool anyVisible = false;
        for (int j = 0; j < topItem->childCount(); ++j) {
          QTreeWidgetItem* child = topItem->child(j);
          if (!child) continue;
          bool match = text.isEmpty() || child->text(0).contains(text, Qt::CaseInsensitive);
          child->setHidden(!match);
          if (match) anyVisible = true;
        }
        topItem->setHidden(!text.isEmpty() && !anyVisible);
      } else {
        bool match = text.isEmpty() || topItem->text(0).contains(text, Qt::CaseInsensitive);
        topItem->setHidden(!match);
      }
    }
  });
  layout->addWidget(m_recipeFilterEdit);

  // Recipe tree widget
  m_recipeListWidget = new QTreeWidget();
  m_recipeListWidget->setColumnCount(1);
  m_recipeListWidget->setHeaderHidden(true);
  m_recipeListWidget->setStyleSheet(TreeWidgetQss());
  m_recipeListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
  m_recipeListWidget->setDragDropMode(QAbstractItemView::InternalMove);
  m_recipeListWidget->setDefaultDropAction(Qt::MoveAction);
  m_recipeListWidget->setAcceptDrops(true);
  m_recipeListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
  m_recipeListWidget->viewport()->installEventFilter(this);
  connect(m_recipeListWidget, &QTreeWidget::itemDoubleClicked, this,
          &RecipeBrowserWidget::onItemDoubleClicked);
  connect(m_recipeListWidget, &QTreeWidget::customContextMenuRequested, this,
          &RecipeBrowserWidget::onContextMenu);
  connect(m_recipeListWidget, &QTreeWidget::currentItemChanged, this,
      [this](QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
          if (!current) {
              m_removeFolderButton->setEnabled(false);
              m_copyRecipeButton->setEnabled(false);
              m_deleteRecipeButton->setEnabled(false);
              return;
          }
          bool isFolder = current->data(0, Qt::UserRole + 1).toString() == "folder";
          bool isRecipe = current->data(0, Qt::UserRole + 1).toString() == "recipe";
          m_removeFolderButton->setEnabled(isFolder);
          m_copyRecipeButton->setEnabled(isRecipe);
          m_deleteRecipeButton->setEnabled(isRecipe);
      });
  layout->addWidget(m_recipeListWidget, 1);

  // === Action buttons bar ===
  QHBoxLayout* actionBar = new QHBoxLayout();
  actionBar->setSpacing(4);

  m_createFolderButton = new QPushButton("+ Folder");
  m_createFolderButton->setFixedHeight(26);
  m_createFolderButton->setStyleSheet(CompactPrimaryQss());
  connect(m_createFolderButton, &QPushButton::clicked, this,
          &RecipeBrowserWidget::onCreateFolder);

  m_removeFolderButton = new QPushButton("- Folder");
  m_removeFolderButton->setFixedHeight(26);
  m_removeFolderButton->setEnabled(false);
  m_removeFolderButton->setStyleSheet(CompactNeutralQss());
  connect(m_removeFolderButton, &QPushButton::clicked, this, [this]() {
      QTreeWidgetItem* item = m_recipeListWidget->currentItem();
      if (item && item->data(0, Qt::UserRole + 1).toString() == "folder") {
          onDeleteFolder(item);
      }
  });

  m_copyRecipeButton = new QPushButton("Copy");
  m_copyRecipeButton->setFixedHeight(26);
  m_copyRecipeButton->setEnabled(false);
  m_copyRecipeButton->setStyleSheet(CompactNeutralQss());
  connect(m_copyRecipeButton, &QPushButton::clicked, this,
          &RecipeBrowserWidget::onCopyRecipe);

  m_deleteRecipeButton = new QPushButton("Delete");
  m_deleteRecipeButton->setFixedHeight(26);
  m_deleteRecipeButton->setEnabled(false);
  m_deleteRecipeButton->setStyleSheet(CompactDangerQss());
  connect(m_deleteRecipeButton, &QPushButton::clicked, this,
          &RecipeBrowserWidget::onDeleteSelectedRecipe);

  actionBar->addWidget(m_createFolderButton);
  actionBar->addWidget(m_removeFolderButton);
  actionBar->addStretch();
  actionBar->addWidget(m_copyRecipeButton);
  actionBar->addWidget(m_deleteRecipeButton);

  layout->addLayout(actionBar);

  // Help text
  QLabel* helpLabel = new QLabel("Double-click to load | Right-click for options | Drag to move");
  helpLabel->setStyleSheet(QString("font-size: 10px; color: %1; font-style: italic;")
                             .arg(QLatin1String(Theme::Color::textSecondary)));
  helpLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(helpLabel);
}

void RecipeBrowserWidget::applyTheme()
{
  if (m_recipeListWidget) m_recipeListWidget->setStyleSheet(TreeWidgetQss());
  if (m_createFolderButton) m_createFolderButton->setStyleSheet(CompactPrimaryQss());
  if (m_removeFolderButton) m_removeFolderButton->setStyleSheet(CompactNeutralQss());
  if (m_copyRecipeButton) m_copyRecipeButton->setStyleSheet(CompactNeutralQss());
  if (m_deleteRecipeButton) m_deleteRecipeButton->setStyleSheet(CompactDangerQss());
  if (m_recipeFilterEdit) m_recipeFilterEdit->setStyleSheet(RecipeStyles::searchBox());
  if (m_refreshRecipeListButton) m_refreshRecipeListButton->setStyleSheet(RecipeStyles::smallToolButton());
  if (m_recipeCountLabel) {
    m_recipeCountLabel->setStyleSheet(QString("font-size: 12px; color: %1;")
                                        .arg(QLatin1String(Theme::Color::textSecondary)));
  }
}

QString RecipeBrowserWidget::getRecipeFolderPath() const
{
  if (!m_context || !m_context->productPositionManager ||
      !m_context->productPositionManager->HasLoadedProduct()) {
    return QString();
  }

  QString productName = QString::fromStdString(m_context->productPositionManager->GetCurrentProductName());
  return QString("config/%1/recipes").arg(productName);
}

void RecipeBrowserWidget::refresh()
{
  if (!m_recipeListWidget) return;

  m_recipeListWidget->clear();

  QString folderPath = getRecipeFolderPath();
  if (folderPath.isEmpty()) {
    m_recipeCountLabel->setText("No product loaded");
    return;
  }

  QDir().mkpath(folderPath);

  QDir rootDir(folderPath);
  if (!rootDir.exists()) {
    m_recipeCountLabel->setText("0 recipe(s)");
    return;
  }

  // One QSettings for the whole refresh, positioned in the cache group, so
  // recipeDisplayName() does not construct one per recipe file.
  QSettings nameCache("Fabrinet", "BobcatEngine");
  nameCache.beginGroup("RecipeNameCache");

  auto readRecipeName = [this, &nameCache](const QFileInfo& fi) -> QString {
    return recipeDisplayName(fi, nameCache);
  };

  auto makeRecipeItem = [&](const QFileInfo& fi) -> QTreeWidgetItem* {
    QString recipeName = readRecipeName(fi);
    QTreeWidgetItem* recipeItem = new QTreeWidgetItem();
    recipeItem->setText(0, recipeName);
    recipeItem->setData(0, Qt::UserRole, fi.absoluteFilePath());
    recipeItem->setData(0, Qt::UserRole + 1, QString("recipe"));
    recipeItem->setToolTip(0, QString("%1\nModified: %2")
      .arg(fi.fileName())
      .arg(fi.lastModified().toString("yyyy-MM-dd hh:mm")));
    recipeItem->setFlags(recipeItem->flags() | Qt::ItemIsDragEnabled);
    recipeItem->setFlags(recipeItem->flags() & ~Qt::ItemIsDropEnabled);
    return recipeItem;
  };

  int totalRecipes = 0;

  // 1. Scan for subdirectories -> create folder items
  QDir subdirScanner(folderPath);
  subdirScanner.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
  subdirScanner.setSorting(QDir::Name);
  QFileInfoList subDirs = subdirScanner.entryInfoList();

  for (const QFileInfo& dirInfo : subDirs) {
    QDir subDir(dirInfo.absoluteFilePath());
    subDir.setNameFilters(QStringList() << "*.json");
    subDir.setFilter(QDir::Files);
    subDir.setSorting(QDir::Time | QDir::Reversed);
    QFileInfoList subFiles = subDir.entryInfoList();

    QTreeWidgetItem* folderItem = new QTreeWidgetItem();
    folderItem->setText(0, QString("%1  (%2)").arg(dirInfo.fileName()).arg(subFiles.size()));
    folderItem->setData(0, Qt::UserRole, dirInfo.absoluteFilePath());
    folderItem->setData(0, Qt::UserRole + 1, QString("folder"));
    folderItem->setFlags((folderItem->flags() | Qt::ItemIsDropEnabled) & ~Qt::ItemIsDragEnabled);
    QFont folderFont = folderItem->font(0);
    folderFont.setBold(true);
    folderItem->setFont(0, folderFont);
    folderItem->setForeground(0, QColor(QLatin1String(Theme::Color::warning)));

    for (const QFileInfo& fi : subFiles) {
      QTreeWidgetItem* recipeItem = makeRecipeItem(fi);
      folderItem->addChild(recipeItem);
      ++totalRecipes;
    }

    m_recipeListWidget->addTopLevelItem(folderItem);
    folderItem->setExpanded(true);
  }

  // 2. Scan root dir for *.json (uncategorized recipes)
  rootDir.setNameFilters(QStringList() << "*.json");
  rootDir.setFilter(QDir::Files);
  rootDir.setSorting(QDir::Time | QDir::Reversed);
  QFileInfoList rootFiles = rootDir.entryInfoList();

  for (const QFileInfo& fi : rootFiles) {
    QTreeWidgetItem* recipeItem = makeRecipeItem(fi);
    m_recipeListWidget->addTopLevelItem(recipeItem);
    ++totalRecipes;
  }

  m_recipeCountLabel->setText(QString("%1 recipe(s)").arg(totalRecipes));

  // Re-apply current filter text
  if (m_recipeFilterEdit && !m_recipeFilterEdit->text().isEmpty()) {
    QString text = m_recipeFilterEdit->text();
    for (int i = 0; i < m_recipeListWidget->topLevelItemCount(); ++i) {
      QTreeWidgetItem* topItem = m_recipeListWidget->topLevelItem(i);
      if (!topItem) continue;
      QString itemType = topItem->data(0, Qt::UserRole + 1).toString();
      if (itemType == "folder") {
        bool anyVisible = false;
        for (int j = 0; j < topItem->childCount(); ++j) {
          QTreeWidgetItem* child = topItem->child(j);
          if (!child) continue;
          bool match = child->text(0).contains(text, Qt::CaseInsensitive);
          child->setHidden(!match);
          if (match) anyVisible = true;
        }
        topItem->setHidden(!anyVisible);
      } else {
        topItem->setHidden(!topItem->text(0).contains(text, Qt::CaseInsensitive));
      }
    }
  }
}

void RecipeBrowserWidget::onRefreshClicked()
{
  refresh();
}

void RecipeBrowserWidget::onItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
  if (!item) return;
  if (item->data(0, Qt::UserRole + 1).toString() == "folder") return;
  onLoadSelectedRecipe();
}

void RecipeBrowserWidget::onContextMenu(const QPoint& pos)
{
  QTreeWidgetItem* item = m_recipeListWidget->itemAt(pos);

  const QString menuStyle = ContextMenuQss();

  QMenu contextMenu(this);
  contextMenu.setStyleSheet(menuStyle);

  if (!item) {
    QAction* newFolderAction = contextMenu.addAction("New Folder...");
    QAction* selected = contextMenu.exec(m_recipeListWidget->mapToGlobal(pos));
    if (selected == newFolderAction) {
      onCreateFolder();
    }
    return;
  }

  QString itemType = item->data(0, Qt::UserRole + 1).toString();

  if (itemType == "folder") {
    QAction* newFolderAction  = contextMenu.addAction("New Folder...");
    QAction* renameFolderAction = contextMenu.addAction("Rename Folder...");
    contextMenu.addSeparator();
    QAction* deleteFolderAction = contextMenu.addAction("Delete Folder");

    QAction* selected = contextMenu.exec(m_recipeListWidget->mapToGlobal(pos));
    if (selected == newFolderAction) {
      onCreateFolder();
    } else if (selected == renameFolderAction) {
      onRenameFolder(item);
    } else if (selected == deleteFolderAction) {
      onDeleteFolder(item);
    }
  } else {
    QAction* loadAction   = contextMenu.addAction("Load Recipe");
    contextMenu.addSeparator();
    QAction* renameAction = contextMenu.addAction("Rename...");
    QAction* deleteAction = contextMenu.addAction("Delete");
    contextMenu.addSeparator();

    QMenu* moveMenu = contextMenu.addMenu("Move to");
    moveMenu->setStyleSheet(menuStyle);

    QStringList folderNames;
    for (int i = 0; i < m_recipeListWidget->topLevelItemCount(); ++i) {
      QTreeWidgetItem* top = m_recipeListWidget->topLevelItem(i);
      if (top && top->data(0, Qt::UserRole + 1).toString() == "folder") {
        QString displayText = top->text(0);
        int parenIdx = displayText.lastIndexOf("  (");
        QString folderName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;
        folderNames << folderName;
      }
    }

    QList<QAction*> moveActions;
    for (const QString& fn : folderNames) {
      moveActions << moveMenu->addAction(fn);
    }
    if (!moveActions.isEmpty()) moveMenu->addSeparator();
    QAction* moveToRootAction = moveMenu->addAction("Uncategorized (root)");

    QAction* selected = contextMenu.exec(m_recipeListWidget->mapToGlobal(pos));

    if (selected == loadAction) {
      onLoadSelectedRecipe();
    } else if (selected == renameAction) {
      onRenameSelectedRecipe();
    } else if (selected == deleteAction) {
      onDeleteSelectedRecipe();
    } else if (selected == moveToRootAction) {
      QString srcPath = item->data(0, Qt::UserRole).toString();
      QString recipeName = item->text(0);
      if (confirmRecipeMove(recipeName, "Uncategorized (root)")) {
        moveRecipeToFolder(srcPath, QString());
      }
    } else {
      for (int i = 0; i < moveActions.size(); ++i) {
        if (selected == moveActions[i]) {
          QString srcPath = item->data(0, Qt::UserRole).toString();
          QString recipeName = item->text(0);
          if (confirmRecipeMove(recipeName, folderNames[i])) {
            moveRecipeToFolder(srcPath, folderNames[i]);
          }
          break;
        }
      }
    }
  }
}

void RecipeBrowserWidget::onLoadSelectedRecipe()
{
  QTreeWidgetItem* item = m_recipeListWidget->currentItem();
  if (!item) return;
  if (item->data(0, Qt::UserRole + 1).toString() == "folder") return;

  QString filePath = item->data(0, Qt::UserRole).toString();
  if (filePath.isEmpty()) return;

  emit loadRecipeRequested(filePath);
}

void RecipeBrowserWidget::onDeleteSelectedRecipe()
{
  QTreeWidgetItem* item = m_recipeListWidget->currentItem();
  if (!item) return;
  if (item->data(0, Qt::UserRole + 1).toString() == "folder") return;

  QString filePath = item->data(0, Qt::UserRole).toString();
  QString recipeName = item->text(0);

  QStringList programUsages = findRecipeUsageInPrograms(filePath);
  QStringList subRecipeUsages = findRecipeUsageAsSubRecipe(filePath);

  QString message = QString("Are you sure you want to delete '%1'?\n").arg(recipeName);

  if (!programUsages.isEmpty()) {
    message += "\nThis recipe is used in the following program(s):\n";
    for (const auto& usage : programUsages) {
      message += "  - " + usage + "\n";
    }
  }

  if (!subRecipeUsages.isEmpty()) {
    message += "\nThis recipe is used as a Sub-Recipe in:\n";
    for (const auto& usage : subRecipeUsages) {
      message += "  - " + usage + "\n";
    }
  }

  if (!programUsages.isEmpty() || !subRecipeUsages.isEmpty()) {
    message += "\nDeleting it will break these references.";
  }

  message += "\nThis action cannot be undone.";

  QMessageBox::StandardButton reply = QMessageBox::question(
    this,
    "Delete Recipe",
    message,
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::No
  );

  if (reply != QMessageBox::Yes) return;

  if (QFile::remove(filePath)) {
    refresh();
    std::cout << "[RecipeBrowser] Deleted recipe: " << filePath.toStdString() << std::endl;
  }
  else {
    QMessageBox::critical(this, "Error", "Failed to delete recipe file.");
  }
}

QStringList RecipeBrowserWidget::findRecipeUsageInPrograms(const QString& recipeFilePath) const
{
  QStringList usages;
  if (!m_context || !m_context->productPositionManager ||
      !m_context->productPositionManager->HasLoadedProduct()) {
    return usages;
  }

  QString productName = QString::fromStdString(
    m_context->productPositionManager->GetCurrentProductName());
  QString programsPath = QString("config/%1/programs").arg(productName);

  QDir programsDir(programsPath);
  if (!programsDir.exists()) return usages;

  QStringList filters;
  filters << "*.json";
  QFileInfoList programFiles = programsDir.entryInfoList(filters, QDir::Files);

  QFileInfo targetInfo(recipeFilePath);
  QString targetCanonical = targetInfo.canonicalFilePath();

  for (const auto& progFileInfo : programFiles) {
    QFile file(progFileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) continue;

    QJsonObject root = doc.object();
    QString programName = root["program_name"].toString(progFileInfo.baseName());

    QJsonArray slotsArray = root["recipe_slots"].toArray();
    bool foundInProgram = false;
    for (int si = 0; si < slotsArray.size() && !foundInProgram; ++si) {
      QJsonObject slotObj = slotsArray[si].toObject();
      QJsonArray recipes = slotObj["recipes"].toArray();
      for (int ri = 0; ri < recipes.size(); ++ri) {
        QJsonObject recipeObj = recipes[ri].toObject();
        QString refPath = recipeObj["recipe_file_path"].toString();
        QFileInfo refInfo(refPath);
        if (refInfo.canonicalFilePath() == targetCanonical ||
            refInfo.absoluteFilePath() == targetInfo.absoluteFilePath()) {
          usages.append(programName);
          foundInProgram = true;
          break;
        }
      }
    }
  }

  return usages;
}

QStringList RecipeBrowserWidget::findRecipeUsageAsSubRecipe(const QString& recipeFilePath) const
{
  QStringList usages;
  QString recipeFolderPath = getRecipeFolderPath();
  if (recipeFolderPath.isEmpty()) return usages;

  QFileInfo targetInfo(recipeFilePath);
  QString targetAbsolute = targetInfo.absoluteFilePath();

  QDirIterator it(recipeFolderPath, QStringList() << "*.json",
    QDir::Files, QDirIterator::Subdirectories);

  for (int scanCount = 0; it.hasNext() && scanCount < 500; ++scanCount) {
    it.next();
    if (QFileInfo(it.filePath()).absoluteFilePath() == targetAbsolute) continue;

    QFile file(it.filePath());
    if (!file.open(QIODevice::ReadOnly)) continue;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) continue;

    QJsonObject root = doc.object();
    QString recipeName = root["recipe_name"].toString(QFileInfo(it.filePath()).baseName());

    QJsonArray steps = root["steps"].toArray();
    for (int si = 0; si < steps.size(); ++si) {
      QJsonObject stepObj = steps[si].toObject();
      if (stepObj["type"].toString() == "Sub-Recipe") {
        QJsonObject params = stepObj["parameters"].toObject();
        QString subRecipePath = params["recipe_file"].toString();
        if (!subRecipePath.isEmpty()) {
          QFileInfo subInfo(subRecipePath);
          if (subInfo.absoluteFilePath() == targetAbsolute ||
              subInfo.canonicalFilePath() == targetInfo.canonicalFilePath()) {
            usages.append(recipeName);
            break;
          }
        }
      }
    }
  }

  return usages;
}

void RecipeBrowserWidget::onRenameSelectedRecipe()
{
  QTreeWidgetItem* item = m_recipeListWidget->currentItem();
  if (!item) return;
  if (item->data(0, Qt::UserRole + 1).toString() == "folder") return;

  QString filePath = item->data(0, Qt::UserRole).toString();
  QString currentName = item->text(0);

  bool ok;
  QString newName = QInputDialog::getText(
    this,
    "Rename Recipe",
    "Enter new recipe name:",
    QLineEdit::Normal,
    currentName,
    &ok
  );

  if (!ok || newName.isEmpty() || newName == currentName) return;

  QString invalidChars = "<>:\"/\\|?*";
  for (QChar c : invalidChars) {
    if (newName.contains(c)) {
      QMessageBox::warning(this, "Invalid Name",
        "Recipe name contains invalid characters.\n\nInvalid characters: < > : \" / \\ | ? *");
      return;
    }
  }

  QFileInfo oldFileInfo(filePath);
  QString newFilePath = oldFileInfo.absolutePath() + "/" + newName + ".json";

  if (QFile::exists(newFilePath)) {
    QMessageBox::warning(this, "Name Exists",
      QString("A recipe named '%1' already exists.").arg(newName));
    return;
  }

  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    QByteArray fileData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(fileData);
    if (doc.isObject()) {
      QJsonObject obj = doc.object();
      obj["recipe_name"] = newName;

      QFile newFile(newFilePath);
      if (newFile.open(QIODevice::WriteOnly)) {
        newFile.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        newFile.close();

        QFile::remove(filePath);

        refresh();
        std::cout << "[RecipeBrowser] Renamed recipe: " << currentName.toStdString()
                  << " -> " << newName.toStdString() << std::endl;
        return;
      }
    }
  }

  QMessageBox::critical(this, "Error", "Failed to rename recipe file.");
}

void RecipeBrowserWidget::onCopyRecipe()
{
  QTreeWidgetItem* item = m_recipeListWidget->currentItem();
  if (!item || item->data(0, Qt::UserRole + 1).toString() != "recipe") return;

  QString srcPath = item->data(0, Qt::UserRole).toString();
  QFileInfo fi(srcPath);
  QString dstPath = fi.absolutePath() + "/" + fi.baseName() + "_copy." + fi.suffix();

  int counter = 1;
  for (int i = 0; i < 100; ++i) {
    if (!QFile::exists(dstPath)) break;
    ++counter;
    dstPath = fi.absolutePath() + "/" + fi.baseName() + "_copy" + QString::number(counter) + "." + fi.suffix();
  }

  if (QFile::copy(srcPath, dstPath)) {
    std::cout << "[RecipeBrowser] Copied recipe to: " << dstPath.toStdString() << std::endl;
    refresh();
  } else {
    QMessageBox::warning(this, "Copy Failed", "Failed to copy recipe file.");
  }
}

void RecipeBrowserWidget::onCreateFolder()
{
  bool ok = false;
  QString name = QInputDialog::getText(this, "New Folder", "Folder name:",
                                       QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) return;

  name = name.trimmed();
  QRegularExpression invalidChars("[<>:\"/\\\\|?*]");
  if (name.contains(invalidChars)) {
    QMessageBox::warning(this, "Invalid Name",
      "Folder name contains invalid characters:\n< > : \" / \\ | ? *");
    return;
  }

  QString folderPath = getRecipeFolderPath() + "/" + name;
  std::error_code ec;
  std::filesystem::create_directory(folderPath.toStdString(), ec);
  if (ec) {
    QMessageBox::warning(this, "Error",
      QString("Failed to create folder: %1").arg(QString::fromStdString(ec.message())));
    return;
  }

  std::cout << "[RecipeBrowser] Created folder: " << folderPath.toStdString() << std::endl;
  refresh();
}

void RecipeBrowserWidget::onRenameFolder(QTreeWidgetItem* folderItem)
{
  if (!folderItem) return;

  QString displayText = folderItem->text(0);
  int parenIdx = displayText.lastIndexOf("  (");
  QString currentName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;

  bool ok = false;
  QString newName = QInputDialog::getText(this, "Rename Folder", "New folder name:",
    QLineEdit::Normal, currentName, &ok);
  if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == currentName) return;

  newName = newName.trimmed();
  QRegularExpression invalidChars("[<>:\"/\\\\|?*]");
  if (newName.contains(invalidChars)) {
    QMessageBox::warning(this, "Invalid Name",
      "Folder name contains invalid characters:\n< > : \" / \\ | ? *");
    return;
  }

  QString basePath = getRecipeFolderPath();
  QString oldPath  = basePath + "/" + currentName;
  QString newPath  = basePath + "/" + newName;

  if (QDir(newPath).exists()) {
    QMessageBox::warning(this, "Name Exists",
      QString("A folder named '%1' already exists.").arg(newName));
    return;
  }

  std::error_code ec;
  std::filesystem::rename(oldPath.toStdString(), newPath.toStdString(), ec);
  if (ec) {
    QMessageBox::warning(this, "Error",
      QString("Failed to rename folder: %1").arg(QString::fromStdString(ec.message())));
    return;
  }

  std::cout << "[RecipeBrowser] Renamed folder: " << currentName.toStdString()
            << " -> " << newName.toStdString() << std::endl;
  refresh();
}

void RecipeBrowserWidget::onDeleteFolder(QTreeWidgetItem* folderItem)
{
  if (!folderItem) return;

  if (folderItem->childCount() > 0) {
    QMessageBox::warning(this, "Folder Not Empty",
      "Cannot delete a folder that contains recipes.\n\nMove or delete all recipes first.");
    return;
  }

  QString displayText = folderItem->text(0);
  int parenIdx = displayText.lastIndexOf("  (");
  QString folderName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;

  QMessageBox::StandardButton reply = QMessageBox::question(this,
    "Delete Folder",
    QString("Are you sure you want to delete the empty folder '%1'?").arg(folderName),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (reply != QMessageBox::Yes) return;

  QString folderPath = getRecipeFolderPath() + "/" + folderName;
  std::error_code ec;
  std::filesystem::remove(folderPath.toStdString(), ec);
  if (ec) {
    QMessageBox::warning(this, "Error",
      QString("Failed to delete folder: %1").arg(QString::fromStdString(ec.message())));
    return;
  }

  std::cout << "[RecipeBrowser] Deleted folder: " << folderPath.toStdString() << std::endl;
  refresh();
}

void RecipeBrowserWidget::moveRecipeToFolder(const QString& srcPath, const QString& targetFolderName)
{
  QFileInfo srcInfo(srcPath);
  if (!srcInfo.exists()) {
    QMessageBox::warning(this, "Error", "Source recipe file not found.");
    return;
  }

  QString destDir = getRecipeFolderPath();
  if (!targetFolderName.isEmpty()) {
    destDir += "/" + targetFolderName;
    QDir().mkpath(destDir);
  }

  QString destPath = destDir + "/" + srcInfo.fileName();

  if (QFile::exists(destPath)) {
    QMessageBox::warning(this, "Name Conflict",
      QString("A recipe file named '%1' already exists in the target folder.").arg(srcInfo.fileName()));
    return;
  }

  std::error_code ec;
  std::filesystem::rename(srcPath.toStdString(), destPath.toStdString(), ec);
  if (ec) {
    QMessageBox::warning(this, "Error",
      QString("Failed to move recipe: %1").arg(QString::fromStdString(ec.message())));
    return;
  }

  std::cout << "[RecipeBrowser] Moved recipe: " << srcPath.toStdString()
            << " -> " << destPath.toStdString() << std::endl;
  refresh();
}

bool RecipeBrowserWidget::confirmRecipeMove(const QString& recipeName, const QString& targetFolder)
{
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Move Recipe?");
  msgBox.setIcon(QMessageBox::Warning);
  msgBox.setText(QString("Move \"%1\" to folder \"%2\"?").arg(recipeName, targetFolder));
  msgBox.setInformativeText(
    "Caution: If this recipe is referenced in any Run Program,\n"
    "you will need to update the program list on the Run Process Page.");
  msgBox.setStandardButtons(QMessageBox::Cancel);
  QPushButton* moveButton = msgBox.addButton("Move", QMessageBox::AcceptRole);
  msgBox.setDefaultButton(QMessageBox::Cancel);
  msgBox.exec();
  return (msgBox.clickedButton() == moveButton);
}

bool RecipeBrowserWidget::eventFilter(QObject* watched, QEvent* event)
{
  if (m_recipeListWidget && watched == m_recipeListWidget->viewport() &&
      event->type() == QEvent::Drop) {
    QDropEvent* dropEvent = static_cast<QDropEvent*>(event);

    QTreeWidgetItem* draggedItem = m_recipeListWidget->currentItem();
    if (!draggedItem) return true;

    if (draggedItem->data(0, Qt::UserRole + 1).toString() != "recipe") return true;

    QString srcPath = draggedItem->data(0, Qt::UserRole).toString();
    QString recipeName = draggedItem->text(0);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QTreeWidgetItem* targetItem = m_recipeListWidget->itemAt(dropEvent->position().toPoint());
#else
    QTreeWidgetItem* targetItem = m_recipeListWidget->itemAt(dropEvent->pos());
#endif

    QString targetFolderName;

    if (targetItem) {
      QString targetType = targetItem->data(0, Qt::UserRole + 1).toString();
      if (targetType == "folder") {
        QString displayText = targetItem->text(0);
        int parenIdx = displayText.lastIndexOf("  (");
        targetFolderName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;
      } else if (targetType == "recipe") {
        QTreeWidgetItem* parent = targetItem->parent();
        if (parent && parent->data(0, Qt::UserRole + 1).toString() == "folder") {
          QString displayText = parent->text(0);
          int parenIdx = displayText.lastIndexOf("  (");
          targetFolderName = (parenIdx >= 0) ? displayText.left(parenIdx) : displayText;
        }
      }
    }

    QFileInfo srcInfo(srcPath);
    QString currentFolder = srcInfo.dir().canonicalPath();
    QString targetDir = QDir(getRecipeFolderPath()).canonicalPath();
    if (!targetFolderName.isEmpty()) {
      targetDir += "/" + targetFolderName;
    }
    if (QDir(currentFolder) == QDir(targetDir)) {
      return true;
    }

    QString targetLabel = targetFolderName.isEmpty() ? "Uncategorized (root)" : targetFolderName;
    if (confirmRecipeMove(recipeName, targetLabel)) {
      moveRecipeToFolder(srcPath, targetFolderName);
    }

    dropEvent->accept();
    return true;
  }
  return QWidget::eventFilter(watched, event);
}

// ===========================================================================
// Recipe display-name lookup (cached)
// ===========================================================================

QString RecipeBrowserWidget::readRecipeNameFromHead(const QString& absoluteFilePath)
{
  // Only the first chunk is read. Recipe JSON is serialised with sorted keys,
  // so "recipe_name" sits ahead of the large "steps" array (byte ~56 in
  // practice). Reading 1 KB instead of up to 17 KB keeps the cache-miss path
  // cheap without needing to parse the whole document.
  constexpr qint64 kHeadBytes = 1024;

  QFile file(absoluteFilePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  const QByteArray head = file.read(kHeadBytes);
  file.close();

  // Wrapping the head in braces would not produce valid JSON (the object is
  // cut mid-way), so extract the value directly rather than parsing.
  const QByteArray key = "\"recipe_name\"";
  const int keyPos = head.indexOf(key);
  if (keyPos < 0) {
    return QString();
  }

  const int colon = head.indexOf(':', keyPos + key.size());
  if (colon < 0) {
    return QString();
  }

  const int openQuote = head.indexOf('"', colon + 1);
  if (openQuote < 0) {
    return QString();
  }

  // Find the closing quote, honouring backslash escapes.
  int i = openQuote + 1;
  QByteArray raw;
  for (; i < head.size(); ++i) {
    const char c = head.at(i);
    if (c == '\\') {
      if (i + 1 >= head.size()) return QString();  // escape truncated by head
      raw.append(head.at(i + 1));
      ++i;
      continue;
    }
    if (c == '"') {
      break;
    }
    raw.append(c);
  }
  if (i >= head.size()) {
    return QString();  // string ran past the head - fall back to a full parse
  }

  return QString::fromUtf8(raw);
}

QString RecipeBrowserWidget::recipeDisplayName(const QFileInfo& fileInfo,
                                              QSettings& cache) const
{
  const QString absolutePath = fileInfo.absoluteFilePath();

  // Identity of the file contents as far as this cache is concerned. Size is
  // included alongside the timestamp because some filesystems have coarse
  // modification-time resolution.
  const QString stamp = QString::number(fileInfo.lastModified().toMSecsSinceEpoch())
                      + "/" + QString::number(fileInfo.size());

  // QSettings keys cannot contain '/' (it denotes a group), so the path is
  // hashed rather than used directly. `cache` is already positioned in the
  // RecipeNameCache group by the caller - one QSettings for the whole refresh
  // rather than one per file.
  const QString cacheKey = QString::fromLatin1(
    QCryptographicHash::hash(absolutePath.toUtf8(), QCryptographicHash::Md5).toHex());

  const QStringList cached = cache.value(cacheKey).toString().split('\x1f');
  if (cached.size() == 2 && cached.at(0) == stamp) {
    return cached.at(1);  // hit: the file was never opened
  }

  // Miss: read the name, preferring the cheap head-only path.
  QString name = readRecipeNameFromHead(absolutePath);

  if (name.isEmpty()) {
    // Head did not contain it (unusual key order, or a very long name):
    // fall back to the original full read + parse.
    QFile file(absolutePath);
    if (file.open(QIODevice::ReadOnly)) {
      const QByteArray data = file.readAll();
      file.close();
      const QJsonDocument doc = QJsonDocument::fromJson(data);
      if (doc.isObject()) {
        name = doc.object().value("recipe_name").toString();
      }
    }
  }

  if (name.isEmpty()) {
    name = fileInfo.baseName();
  }

  cache.setValue(cacheKey, stamp + '\x1f' + name);

  return name;
}

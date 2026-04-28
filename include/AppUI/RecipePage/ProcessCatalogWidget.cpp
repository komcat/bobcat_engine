// ProcessCatalogWidget.cpp
#include "ProcessCatalogWidget.h"

#include "ProcessVisibilityDialog.h"
#include "RecipeManager.h"
#include "RecipePageStyles.h"
#include "Theme.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <iostream>
#include <vector>

ProcessCatalogWidget::ProcessCatalogWidget(QWidget* parent)
  : QWidget(parent)
{
  // Load the hidden process set BEFORE setupUI so the initial tree
  // populate already excludes them.
  loadHiddenProcessesFromDisk();
  setupUI();
}

void ProcessCatalogWidget::loadHiddenProcessesFromDisk()
{
  m_hiddenProcesses = ProcessVisibilityDialog::loadHiddenFromDisk();
  std::cout << "[ProcessCatalog] Loaded " << m_hiddenProcesses.size()
            << " hidden processes" << std::endl;
}

void ProcessCatalogWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(5, 5, 5, 5);
  mainLayout->setSpacing(5);

  QLabel* title = new QLabel("Available Processes");
  title->setStyleSheet(RecipeStyles::sectionLabel());
  mainLayout->addWidget(title);

  // Search box + gear button to open the visibility manager
  QHBoxLayout* searchRow = new QHBoxLayout();
  searchRow->setSpacing(4);
  searchRow->setContentsMargins(0, 0, 0, 0);

  QLineEdit* searchBox = new QLineEdit();
  searchBox->setPlaceholderText("Search processes...");
  searchBox->setStyleSheet(RecipeStyles::searchBox());
  searchRow->addWidget(searchBox, 1);

  QPushButton* visibilityBtn = new QPushButton(QString::fromUtf8("\xE2\x9A\x99"));  // ⚙ gear
  visibilityBtn->setFixedSize(28, 28);
  visibilityBtn->setToolTip("Manage process visibility (hide/show old or experimental processes)");
  visibilityBtn->setStyleSheet(RecipeStyles::smallToolButton());
  connect(visibilityBtn, &QPushButton::clicked,
    this, &ProcessCatalogWidget::onShowVisibilityDialog);
  searchRow->addWidget(visibilityBtn);

  mainLayout->addLayout(searchRow);

  // Quick tip for keyboard shortcut
  QLabel* quickTip = new QLabel("Tip: Press Ctrl+B for quick add");
  quickTip->setStyleSheet(QString(
    "QLabel { "
    "  color: %1; "
    "  font-size: 10px; "
    "  font-style: italic; "
    "  padding: 2px 0px; "
    "}"
  ).arg(QLatin1String(Theme::Color::textSecondary)));
  mainLayout->addWidget(quickTip);

  // ========== Selected process label + Add button row ==========
  QHBoxLayout* selectedRow = new QHBoxLayout();
  selectedRow->setSpacing(5);

  m_selectedProcessLabel = new QLabel("Selected: (none)");
  m_selectedProcessLabel->setFixedHeight(50);
  m_selectedProcessLabel->setStyleSheet(RecipeStyles::selectedProcessLabel());
  m_selectedProcessLabel->setWordWrap(true);
  m_selectedProcessLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  selectedRow->addWidget(m_selectedProcessLabel, 1);  // Stretch to fill

  // Add button next to selected label
  m_addButton = new QPushButton("»");
  m_addButton->setFixedSize(35, 50);  // Match height of selected label
  m_addButton->setToolTip("Add selected process to recipe (or double-click item)");
  m_addButton->setStyleSheet(RecipeStyles::addProcessButton());
  connect(m_addButton, &QPushButton::clicked, this, &ProcessCatalogWidget::onAddButtonClicked);
  selectedRow->addWidget(m_addButton);

  mainLayout->addLayout(selectedRow);

  // ========== Process tree ==========
  m_processTemplateList = new QTreeWidget();
  m_processTemplateList->setHeaderHidden(true);
  m_processTemplateList->setStyleSheet(RecipeStyles::processTemplateTree());
  m_processTemplateList->setRootIsDecorated(true);  // Show expand/collapse arrows
  m_processTemplateList->setIndentation(20);        // Indentation for child items

  rebuildTreeFromCurrentState();

  connect(m_processTemplateList, &QTreeWidget::itemClicked,
    this, &ProcessCatalogWidget::onProcessTemplateSelected);
  connect(m_processTemplateList, &QTreeWidget::itemDoubleClicked,
    this, [this](QTreeWidgetItem* item, int) {
      if (item && item->parent()) { // Only process items (not category headers)
        emit addProcessRequested(item->text(0));
      }
    });

  mainLayout->addWidget(m_processTemplateList, 1);  // Add with stretch factor

  // Connect search box to filter tree
  connect(searchBox, &QLineEdit::textChanged, this, [this](const QString& searchText) {
    QString lowerSearch = searchText.toLower();

    for (int i = 0; i < m_processTemplateList->topLevelItemCount(); ++i) {
      QTreeWidgetItem* categoryItem = m_processTemplateList->topLevelItem(i);
      bool categoryHasVisibleChildren = false;

      for (int j = 0; j < categoryItem->childCount(); ++j) {
        QTreeWidgetItem* processItem = categoryItem->child(j);
        QString processName = processItem->text(0).toLower();

        bool matches = lowerSearch.isEmpty() || processName.contains(lowerSearch);
        processItem->setHidden(!matches);

        if (matches) {
          categoryHasVisibleChildren = true;
        }
      }

      // Hide category if no children are visible. Note: categories that
      // were already hidden by rebuildTreeFromCurrentState (because every
      // child was filtered out) stay hidden too.
      categoryItem->setHidden(!categoryHasVisibleChildren);
    }
  });
}

void ProcessCatalogWidget::rebuildTreeFromCurrentState()
{
  if (!m_processTemplateList) {
    return;
  }

  m_processTemplateList->clear();

  for (const auto& category : RecipeManager::getProcessCategories()) {
    QTreeWidgetItem* categoryItem = new QTreeWidgetItem(m_processTemplateList);
    categoryItem->setText(0, QString::fromStdString(category));
    categoryItem->setFlags(categoryItem->flags() & ~Qt::ItemIsSelectable);

    QFont font = categoryItem->font(0);
    font.setBold(true);
    categoryItem->setFont(0, font);
    categoryItem->setForeground(0, QColor(RecipeStyles::Colors::TEXT_CATEGORY()));
    categoryItem->setBackground(0, QColor(RecipeStyles::Colors::BG_MEDIUM()));

    for (const auto& processType : RecipeManager::getProcessesByCategory(category)) {
      if (m_hiddenProcesses.count(processType) > 0) {
        continue;  // Hidden by user via the visibility manager
      }
      QTreeWidgetItem* processItem = new QTreeWidgetItem(categoryItem);
      processItem->setText(0, QString::fromStdString(processType));
      processItem->setData(0, Qt::UserRole, QString::fromStdString(processType));
    }

    if (categoryItem->childCount() == 0) {
      // Every process in this category is hidden — collapse the header too.
      categoryItem->setHidden(true);
    } else {
      categoryItem->setExpanded(false);
    }
  }
}

void ProcessCatalogWidget::refresh()
{
  loadHiddenProcessesFromDisk();
  rebuildTreeFromCurrentState();
}

void ProcessCatalogWidget::onShowVisibilityDialog()
{
  // Snapshot the registry into a flat process-name list + a category lookup
  std::vector<std::string> allTypes;
  for (const auto& cat : RecipeManager::getProcessCategories()) {
    for (const auto& p : RecipeManager::getProcessesByCategory(cat)) {
      allTypes.push_back(p);
    }
  }
  auto categoryLookup = [](const std::string& proc) -> std::string {
    return RecipeManager::getProcessCategory(proc);
  };

  ProcessVisibilityDialog dlg(allTypes, m_hiddenProcesses, categoryLookup, this);

  // Live refresh of the tree behind the modal dialog as the user clicks
  connect(&dlg, &ProcessVisibilityDialog::visibilityChanged,
    this, [this](const std::unordered_set<std::string>& newHidden) {
      m_hiddenProcesses = newHidden;
      rebuildTreeFromCurrentState();
    });

  dlg.exec();

  // Defensive: in case the dialog closed without firing the signal, sync
  // the final state.
  m_hiddenProcesses = dlg.hiddenProcesses();
  rebuildTreeFromCurrentState();
}

void ProcessCatalogWidget::onProcessTemplateSelected(QTreeWidgetItem* item, int /*column*/)
{
  if (item && item->parent()) { // Only process items (not category headers)
    QString processName = item->text(0);
    std::cout << "[ProcessCatalog] Selected template: " << processName.toStdString() << std::endl;

    if (m_selectedProcessLabel) {
      m_selectedProcessLabel->setText("Selected: " + processName);
    }
  } else {
    // Category header selected, clear selection
    if (m_selectedProcessLabel) {
      m_selectedProcessLabel->setText("Selected: (none)");
    }
  }
}

void ProcessCatalogWidget::onAddButtonClicked()
{
  QTreeWidgetItem* item = m_processTemplateList->currentItem();
  if (!item) {
    QMessageBox::warning(this, "No Selection", "Please select a process template to add.");
    return;
  }

  // Only add if it's a process item (not a category header)
  if (!item->parent()) {
    QMessageBox::warning(this, "Invalid Selection", "Please select a process, not a category.");
    return;
  }

  emit addProcessRequested(item->text(0));
}

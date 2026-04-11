// ProcessVisibilityDialog.cpp
#include "ProcessVisibilityDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

namespace {
const char* kLocalConfigPath   = "config/hidden_processes.local.json";
const char* kDefaultConfigPath = "config/hidden_processes.json";

// Returns "<process> (<category>)" used as the user-visible label in
// each list item.  The bare process name is stored separately in
// Qt::UserRole for stable lookup independent of display formatting.
QString formatItem(const std::string& processName,
                   const std::string& category)
{
  return QString::fromStdString(processName) +
         QStringLiteral(" (") +
         QString::fromStdString(category) +
         QStringLiteral(")");
}
}  // namespace

// =======================================================================
// Static helper - shared by RecipePage startup loader
// =======================================================================
std::unordered_set<std::string> ProcessVisibilityDialog::loadHiddenFromDisk()
{
  std::unordered_set<std::string> result;

  // Try .local.json first, fall back to default
  std::string pathToLoad = kLocalConfigPath;
  std::ifstream test(pathToLoad);
  if (!test.good()) {
    pathToLoad = kDefaultConfigPath;
  }
  test.close();

  std::ifstream file(pathToLoad);
  if (!file.is_open()) {
    return result;
  }

  try {
    nlohmann::json j;
    file >> j;
    if (j.contains("hidden") && j["hidden"].is_array()) {
      for (const auto& name : j["hidden"]) {
        if (name.is_string()) {
          result.insert(name.get<std::string>());
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "[ProcessVisibilityDialog] Failed to parse "
              << pathToLoad << ": " << e.what() << std::endl;
  }
  return result;
}

// =======================================================================
// Construction
// =======================================================================
ProcessVisibilityDialog::ProcessVisibilityDialog(
  const std::vector<std::string>& allProcessTypes,
  std::unordered_set<std::string> hiddenSet,
  std::function<std::string(const std::string&)> categoryLookup,
  QWidget* parent)
  : QDialog(parent)
  , m_allProcessTypes(allProcessTypes)
  , m_hidden(std::move(hiddenSet))
  , m_categoryLookup(std::move(categoryLookup))
{
  setWindowTitle(tr("Manage Process Visibility"));
  resize(750, 520);
  setModal(true);

  // Sort the process list once for stable display
  std::sort(m_allProcessTypes.begin(), m_allProcessTypes.end());

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(10, 10, 10, 10);
  root->setSpacing(8);

  // -------- Search row --------
  auto* searchRow = new QHBoxLayout();
  auto* searchLabel = new QLabel(tr("Search:"));
  m_searchBox = new QLineEdit();
  m_searchBox->setPlaceholderText(tr("Filter both columns by process name or category..."));
  searchRow->addWidget(searchLabel);
  searchRow->addWidget(m_searchBox, 1);
  root->addLayout(searchRow);

  // -------- Three-column shuttle --------
  auto* shuttleRow = new QHBoxLayout();
  shuttleRow->setSpacing(8);

  // Left column: visible
  auto* leftCol = new QVBoxLayout();
  m_visibleHeader = new QLabel();
  QFont headerFont = m_visibleHeader->font();
  headerFont.setBold(true);
  m_visibleHeader->setFont(headerFont);
  m_visibleList = new QListWidget();
  m_visibleList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  leftCol->addWidget(m_visibleHeader);
  leftCol->addWidget(m_visibleList, 1);

  // Middle column: move buttons
  auto* midCol = new QVBoxLayout();
  midCol->addStretch(1);
  auto* moveToHiddenBtn = new QPushButton(QStringLiteral(">>"));
  moveToHiddenBtn->setToolTip(tr("Hide selected processes"));
  moveToHiddenBtn->setFixedSize(50, 32);
  auto* moveToVisibleBtn = new QPushButton(QStringLiteral("<<"));
  moveToVisibleBtn->setToolTip(tr("Show selected processes"));
  moveToVisibleBtn->setFixedSize(50, 32);
  midCol->addWidget(moveToHiddenBtn);
  midCol->addSpacing(6);
  midCol->addWidget(moveToVisibleBtn);
  midCol->addStretch(1);

  // Right column: hidden
  auto* rightCol = new QVBoxLayout();
  m_hiddenHeader = new QLabel();
  m_hiddenHeader->setFont(headerFont);
  m_hiddenList = new QListWidget();
  m_hiddenList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  rightCol->addWidget(m_hiddenHeader);
  rightCol->addWidget(m_hiddenList, 1);

  shuttleRow->addLayout(leftCol, 1);
  shuttleRow->addLayout(midCol);
  shuttleRow->addLayout(rightCol, 1);
  root->addLayout(shuttleRow, 1);

  // -------- Footer (just Close) --------
  auto* footer = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(footer, &QDialogButtonBox::rejected, this, &QDialog::accept);
  root->addWidget(footer);

  // Dark theme to match RecipePage
  setStyleSheet(
    "QDialog { background-color: #2d2d30; color: #d4d4d4; }"
    "QLabel { color: #d4d4d4; }"
    "QLineEdit { background-color: #1e1e1e; color: #d4d4d4; "
    "  border: 1px solid #3f3f46; border-radius: 3px; padding: 4px; }"
    "QListWidget { background-color: #1e1e1e; color: #d4d4d4; "
    "  border: 1px solid #3f3f46; border-radius: 3px; }"
    "QListWidget::item { padding: 4px 6px; }"
    "QListWidget::item:selected { background-color: #0e639c; }"
    "QPushButton { background-color: #3c3c3c; color: #d4d4d4; "
    "  border: 1px solid #555555; border-radius: 3px; "
    "  font-weight: bold; }"
    "QPushButton:hover { background-color: #4a4a4d; }"
    "QPushButton:pressed { background-color: #2a2a2a; }"
  );

  // Wire up signals
  connect(moveToHiddenBtn, &QPushButton::clicked,
    this, &ProcessVisibilityDialog::onMoveToHidden);
  connect(moveToVisibleBtn, &QPushButton::clicked,
    this, &ProcessVisibilityDialog::onMoveToVisible);
  connect(m_searchBox, &QLineEdit::textChanged,
    this, &ProcessVisibilityDialog::onSearchChanged);

  rebuildLists();
}

// =======================================================================
// List rebuild + filter
// =======================================================================
void ProcessVisibilityDialog::rebuildLists()
{
  m_visibleList->clear();
  m_hiddenList->clear();

  const QString filterLower = m_currentFilter.toLower();

  int visibleCount = 0;
  int hiddenCount = 0;

  for (const auto& processName : m_allProcessTypes) {
    const std::string category = m_categoryLookup ? m_categoryLookup(processName) : "";
    const QString display = formatItem(processName, category);

    const bool matchesFilter =
      filterLower.isEmpty() || display.toLower().contains(filterLower);

    const bool isHidden = m_hidden.count(processName) > 0;

    if (isHidden) {
      ++hiddenCount;
    } else {
      ++visibleCount;
    }

    if (!matchesFilter) {
      continue;
    }

    auto* item = new QListWidgetItem(display);
    item->setData(Qt::UserRole, QString::fromStdString(processName));
    if (isHidden) {
      m_hiddenList->addItem(item);
    } else {
      m_visibleList->addItem(item);
    }
  }

  m_visibleHeader->setText(tr("Visible (%1)").arg(visibleCount));
  m_hiddenHeader->setText(tr("Hidden (%1)").arg(hiddenCount));
}

// =======================================================================
// Move handlers
// =======================================================================
void ProcessVisibilityDialog::onMoveToHidden()
{
  const auto selected = m_visibleList->selectedItems();
  if (selected.isEmpty()) {
    return;
  }
  for (const QListWidgetItem* item : selected) {
    const std::string name = item->data(Qt::UserRole).toString().toStdString();
    m_hidden.insert(name);
  }
  saveToJson();
  rebuildLists();
  emit visibilityChanged(m_hidden);
}

void ProcessVisibilityDialog::onMoveToVisible()
{
  const auto selected = m_hiddenList->selectedItems();
  if (selected.isEmpty()) {
    return;
  }
  for (const QListWidgetItem* item : selected) {
    const std::string name = item->data(Qt::UserRole).toString().toStdString();
    m_hidden.erase(name);
  }
  saveToJson();
  rebuildLists();
  emit visibilityChanged(m_hidden);
}

void ProcessVisibilityDialog::onSearchChanged(const QString& text)
{
  m_currentFilter = text;
  rebuildLists();
}

// =======================================================================
// JSON save (always to .local.json)
// =======================================================================
void ProcessVisibilityDialog::saveToJson()
{
  nlohmann::json j;
  j["hidden"] = nlohmann::json::array();

  // Sort for stable diffs
  std::vector<std::string> sorted(m_hidden.begin(), m_hidden.end());
  std::sort(sorted.begin(), sorted.end());
  for (const auto& name : sorted) {
    j["hidden"].push_back(name);
  }

  std::ofstream out(kLocalConfigPath);
  if (!out.is_open()) {
    std::cerr << "[ProcessVisibilityDialog] Failed to write "
              << kLocalConfigPath << std::endl;
    return;
  }
  out << j.dump(2);
  std::cout << "[ProcessVisibilityDialog] Saved " << sorted.size()
            << " hidden processes to " << kLocalConfigPath << std::endl;
}

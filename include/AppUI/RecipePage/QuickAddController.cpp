// QuickAddController.cpp
#include "QuickAddController.h"

#include "ProcessCatalogWidget.h"
#include "RecipeManager.h"
#include "Theme.h"

#include <QKeyEvent>
#include <QLabel>
#include <QWidget>

#include <iostream>
#include <string>
#include <vector>

QuickAddController::QuickAddController(QWidget* pageWidget,
                                       RecipeManager* recipeManager,
                                       ProcessCatalogWidget* processCatalog,
                                       QObject* parent)
  : QObject(parent)
  , m_pageWidget(pageWidget)
  , m_recipeManager(recipeManager)
  , m_processCatalog(processCatalog)
{
}

void QuickAddController::ensureOverlay()
{
  if (m_overlay) return;

  m_overlay = new QLabel(m_pageWidget);
  m_overlay->setAlignment(Qt::AlignCenter);
  m_overlay->setStyleSheet(QString(
    "QLabel { "
    "  background-color: %1; "
    "  color: white; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border: 2px solid %2; "
    "  border-radius: 8px; "
    "  padding: 15px; "
    "}"
  ).arg(QLatin1String(Theme::Color::primary),
        QLatin1String(Theme::Color::primaryHover)));
}

void QuickAddController::Activate()
{
  m_active = true;
  m_selectedCategory = -1;

  ensureOverlay();
  updateOverlay();
  m_overlay->show();
  m_overlay->raise();

  // Make sure the page can receive key events.
  if (m_pageWidget) m_pageWidget->setFocus();

  std::cout << "[QuickAdd] Mode activated - press category number (1-9)" << std::endl;
}

void QuickAddController::Deactivate()
{
  m_active = false;
  m_selectedCategory = -1;

  if (m_overlay) m_overlay->hide();

  std::cout << "[QuickAdd] Mode deactivated" << std::endl;
}

bool QuickAddController::HandleKeyPress(QKeyEvent* event)
{
  // Ctrl+B toggles the mode.
  if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_B) {
    if (!m_active) {
      Activate();
    } else {
      Deactivate();
    }
    return true;
  }

  if (!m_active) return false;

  const int key = event->key();

  if (key == Qt::Key_Escape) {
    Deactivate();
    return true;
  }

  // Number keys 1-9 and 0 (0 maps to 10).
  if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    const int num = (key == Qt::Key_0) ? 10 : (key - Qt::Key_0);

    if (m_selectedCategory < 0) {
      handleCategoryKey(num);
    } else {
      handleProcessKey(num);
    }
    return true;
  }

  return false;
}

void QuickAddController::handleCategoryKey(int key)
{
  auto categories = RecipeManager::getProcessCategories();
  const int categoryIndex = key - 1;  // 1-based to 0-based

  if (categoryIndex < 0 || categoryIndex >= static_cast<int>(categories.size())) {
    std::cout << "[QuickAdd] Invalid category key: " << key << std::endl;
    return;
  }

  m_selectedCategory = categoryIndex;
  updateOverlay();

  std::cout << "[QuickAdd] Selected category: " << categories[categoryIndex]
            << " - press process number (1-9)" << std::endl;
}

void QuickAddController::handleProcessKey(int key)
{
  auto categories = RecipeManager::getProcessCategories();

  if (m_selectedCategory < 0 ||
      m_selectedCategory >= static_cast<int>(categories.size())) {
    Deactivate();
    return;
  }

  std::string selectedCategory = categories[m_selectedCategory];
  auto allProcesses = RecipeManager::getProcessesByCategory(selectedCategory);

  // Filter out hidden processes so the index matches what the user sees.
  std::vector<std::string> processes;
  processes.reserve(allProcesses.size());
  for (const auto& p : allProcesses) {
    if (m_processCatalog && m_processCatalog->hiddenProcesses().count(p) == 0) {
      processes.push_back(p);
    }
  }

  const int processIndex = key - 1;  // 1-based to 0-based

  if (processIndex < 0 || processIndex >= static_cast<int>(processes.size())) {
    std::cout << "[QuickAdd] Invalid process key: " << key << std::endl;
    Deactivate();
    return;
  }

  const std::string processType = processes[processIndex];

  std::cout << "[QuickAdd] Selected process: " << processType << std::endl;

  // Page handles the actual recipe insertion + UI refresh.
  emit processSelected(QString::fromStdString(processType));

  Deactivate();
}

void QuickAddController::updateOverlay()
{
  if (!m_overlay) return;

  QString text;
  auto categories = RecipeManager::getProcessCategories();

  if (m_selectedCategory < 0) {
    // Category list.
    text = "<b>QUICK ADD MODE</b> (Ctrl+B)<br><br>";
    text += "<b>Select Category:</b><br><br>";

    int num = 1;
    for (const auto& category : categories) {
      if (num <= 9) {
        text += QString("<b>%1</b> - %2<br>")
          .arg(num)
          .arg(QString::fromStdString(category));
      }
      num++;
      if (num > 9) break;
    }

    text += "<br><i>Press ESC to cancel</i>";
  } else {
    // Process list for the selected category (filtered by hidden set).
    std::string selectedCategory = categories[m_selectedCategory];
    auto allProcesses = RecipeManager::getProcessesByCategory(selectedCategory);
    std::vector<std::string> processes;
    processes.reserve(allProcesses.size());
    for (const auto& p : allProcesses) {
      if (m_processCatalog && m_processCatalog->hiddenProcesses().count(p) == 0) {
        processes.push_back(p);
      }
    }

    text = QString("<b>QUICK ADD: %1</b><br><br>")
      .arg(QString::fromStdString(selectedCategory));
    text += "<b>Select Process:</b><br><br>";

    int num = 1;
    for (const auto& process : processes) {
      if (num <= 9) {
        text += QString("<b>%1</b> - %2<br>")
          .arg(num)
          .arg(QString::fromStdString(process));
      }
      num++;
      if (num > 9) break;
    }

    if (processes.size() > 9) {
      text += QString("<i>... and %1 more (use mouse)</i><br>").arg(processes.size() - 9);
    }

    text += "<br><i>Press ESC to cancel</i>";
  }

  m_overlay->setText(text);

  // Center the overlay on the page widget.
  m_overlay->adjustSize();
  if (m_pageWidget) {
    const int x = (m_pageWidget->width() - m_overlay->width()) / 2;
    const int y = (m_pageWidget->height() - m_overlay->height()) / 2;
    m_overlay->move(x, y);
  }
}

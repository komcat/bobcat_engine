// MainWindow.cpp
#include "MainWindow.h"
#include "ApplicationContext.h"
#include "MainMenuPage.h"
#include "HardwareInstrumentPage.h"
#include "LogStatusBar.h"      // ✅ NEW
#include "LogViewerPage.h"     // ✅ NEW
#include "LogManager.h"        // ✅ NEW
#include "ConfigEditorPage.h"     // NEW - Add this include

#include <QVBoxLayout>
#include <iostream>

MainWindow::MainWindow(ApplicationContext* context, QWidget* parent)
  : QMainWindow(parent)
  , m_context(context)
  , m_pageStack(nullptr)
  , m_logStatusBar(nullptr)
  , m_logViewerPage(nullptr)
  , m_mainMenuPage(nullptr)
  , m_hardwarePage(nullptr)
{
  setupUI();
  createPages();
  setupLogging();  // ✅ NEW - Setup logging AFTER pages are created

  // Show main menu first
  m_pageStack->setCurrentWidget(m_mainMenuPage);

  // Log window creation
  if (m_context->logManager) {
    m_context->logManager->Info("Main window ready", "MainWindow");
  }
}

void MainWindow::setupUI()
{
  // Create central widget with layout
  QWidget* centralWidget = new QWidget(this);
  QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Page stack (main content area)
  m_pageStack = new QStackedWidget();
  mainLayout->addWidget(m_pageStack, 1);  // Stretch factor 1 (takes remaining space)

  // ✅ Log status bar at bottom (always visible)
  m_logStatusBar = new LogStatusBar(m_context->logManager, this);
  connect(m_logStatusBar, &LogStatusBar::openFullLogViewer,
    this, &MainWindow::onOpenLogViewer);
  mainLayout->addWidget(m_logStatusBar, 0);  // No stretch (fixed size)

  setCentralWidget(centralWidget);
  resize(800, 600);
}

// MainWindow.cpp - Update createPages() method
void MainWindow::createPages()
{
  std::cout << "[MainWindow] Creating pages..." << std::endl;

  // Main menu page
  m_mainMenuPage = new MainMenuPage(this);
  connect(m_mainMenuPage, &MainMenuPage::processClicked,
    this, &MainWindow::onMainMenuProcess);
  connect(m_mainMenuPage, &MainMenuPage::recipeClicked,
    this, &MainWindow::onMainMenuRecipe);
  connect(m_mainMenuPage, &MainMenuPage::motionClicked,
    this, &MainWindow::onMainMenuMotion);
  connect(m_mainMenuPage, &MainMenuPage::hardwareClicked,
    this, &MainWindow::onMainMenuHardware);
  m_pageStack->addWidget(m_mainMenuPage);
  // NEW connections
  connect(m_mainMenuPage, &MainMenuPage::editConfigClicked,
    this, &MainWindow::onMainMenuEditConfig);
  connect(m_mainMenuPage, &MainMenuPage::futureUpdateClicked,
    this, &MainWindow::onMainMenuFutureUpdate);

  m_pageStack->addWidget(m_mainMenuPage);

  // Hardware & Instrument page
  m_hardwarePage = new HardwareInstrumentPage(m_context, this);
  connect(m_hardwarePage, &HardwareInstrumentPage::backToMainMenu,
    this, &MainWindow::onBackToMainMenu);
  m_pageStack->addWidget(m_hardwarePage);

  // Config Editor page - NEW
  m_configEditorPage = new ConfigEditorPage(m_context, this);
  connect(m_configEditorPage, &ConfigEditorPage::backToMainMenu,
    this, &MainWindow::onBackToMainMenu);
  m_pageStack->addWidget(m_configEditorPage);

  // Log viewer page
  m_logViewerPage = new LogViewerPage(m_context->logManager, this);

  // ✅ NEW - Connect back button
  connect(m_logViewerPage, &LogViewerPage::backRequested,
    this, &MainWindow::onBackToMainMenu);

  m_pageStack->addWidget(m_logViewerPage);

  std::cout << "[MainWindow] Pages created" << std::endl;
}


// MainWindow.cpp - COMPLETE SAFE VERSION
void MainWindow::setupLogging()
{
  if (!m_context->logManager) {
    std::cerr << "[MainWindow] LogManager not available!" << std::endl;
    return;
  }

  // ✅ Thread-safe UI callback using Qt's queued connection
  m_context->logManager->SetUICallback([this](const LogManager::LogEntry& entry) {
    // This lambda captures entry by value (safe)
    // QMetaObject::invokeMethod ensures UI thread execution
    QMetaObject::invokeMethod(
      this,
      [this, entry]() {
      if (m_logStatusBar) {
        m_logStatusBar->UpdateFromLog(entry);
      }
    },
      Qt::QueuedConnection
    );
  });

  std::cout << "[MainWindow] Logging UI configured with thread-safe callbacks" << std::endl;
}

void MainWindow::onMainMenuProcess()
{
  std::cout << "[MainWindow] Process clicked (not implemented)" << std::endl;
  m_context->logManager->Warning("Process page not implemented yet", "MainWindow");
}

void MainWindow::onMainMenuRecipe()
{
  std::cout << "[MainWindow] Recipe clicked (not implemented)" << std::endl;
  m_context->logManager->Warning("Recipe page not implemented yet", "MainWindow");
}

void MainWindow::onMainMenuMotion()
{
  std::cout << "[MainWindow] Motion clicked (not implemented)" << std::endl;
  m_context->logManager->Warning("Motion page not implemented yet", "MainWindow");
}

void MainWindow::onMainMenuHardware()
{
  std::cout << "[MainWindow] Hardware clicked" << std::endl;
  m_context->logManager->Info("Navigating to Hardware & Instrument page", "MainWindow");
  m_pageStack->setCurrentWidget(m_hardwarePage);
}

void MainWindow::onBackToMainMenu()
{
  std::cout << "[MainWindow] Back to main menu" << std::endl;
  m_context->logManager->Debug("Returning to main menu", "MainWindow");
  m_pageStack->setCurrentWidget(m_mainMenuPage);
}

void MainWindow::onOpenLogViewer()
{
  std::cout << "[MainWindow] Opening log viewer" << std::endl;
  m_context->logManager->Info("Opening full log viewer", "MainWindow");

  // Refresh logs before showing
  m_logViewerPage->RefreshLogs();
  m_pageStack->setCurrentWidget(m_logViewerPage);
}

// NEW slot implementations - add these methods:

void MainWindow::onMainMenuEditConfig()
{
  std::cout << "[MainWindow] Edit Config clicked" << std::endl;
  m_context->logManager->Info("Navigating to Config Editor page", "MainWindow");
  m_pageStack->setCurrentWidget(m_configEditorPage);  // <- Navigate to the page
}

void MainWindow::onMainMenuFutureUpdate()
{
  std::cout << "[MainWindow] Future Update clicked (not implemented)" << std::endl;
  m_context->logManager->Info("Future update button clicked", "MainWindow");
}
// MainWindow.cpp
#include "MainWindow.h"
#include "ApplicationContext.h"
#include "MainMenuPage.h"
#include "HardwareInstrumentPage.h"
#include "LogStatusBar.h"      // ✅ NEW
#include "LogViewerPage.h"     // ✅ NEW
#include "LogManager.h"        // ✅ NEW
#include "ConfigEditorPage.h"     // NEW - Add this include
// ADD THESE:
#include "JogPanelWindow.h"
#include "JogKeyBindingHandler.h"
#include "TransformationMatrixLoader.h"

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
  , m_jogButton(nullptr)  // ADD THIS
  , m_jogPanel(nullptr)  // ADD THIS
  , m_jogKeyHandler(nullptr)  // ADD THIS
  , m_matrixLoader(nullptr)  // ADD THIS
{
  setupUI();
  createPages();
  setupLogging();  // ✅ NEW - Setup logging AFTER pages are created
  setupJogPanel();  // ADD THIS - Setup jog panel AFTER pages are created

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
  mainLayout->addWidget(m_pageStack, 1);

  // Log status bar at bottom (always visible)
  m_logStatusBar = new LogStatusBar(m_context->logManager, this);
  connect(m_logStatusBar, &LogStatusBar::openFullLogViewer,
    this, &MainWindow::onOpenLogViewer);
  mainLayout->addWidget(m_logStatusBar, 0);

  // ADD THIS: Create orange JOG button (floating, top-right)
  m_jogButton = new QPushButton("JOG", this);
  m_jogButton->setFixedSize(80, 40);
  m_jogButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #ff8c00; "  // Dark orange
    "  color: white; "
    "  border: none; "
    "  border-radius: 5px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #ff9d2e; "  // Lighter orange on hover
    "} "
    "QPushButton:pressed { "
    "  background-color: #e67e00; "  // Darker orange when pressed
    "}");

  connect(m_jogButton, &QPushButton::clicked, this, &MainWindow::onToggleJogPanel);

  // Position button at top-right corner
  m_jogButton->move(this->width() - 90, 10);
  m_jogButton->raise();  // Ensure it's on top

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


void MainWindow::setupJogPanel()
{
  std::cout << "[MainWindow] Setting up jog panel..." << std::endl;

  // Load transformation matrices
  m_matrixLoader = new TransformationMatrixLoader();

  // Try to load from config directory
  std::string matrixPath = "config/transformation_matrix.json";
  std::cout << "[MainWindow] Loading matrix from: " << matrixPath << std::endl;

  if (!m_matrixLoader->LoadFromFile(matrixPath)) {
    std::cerr << "[MainWindow] Failed to load transformation matrix from: " << matrixPath << std::endl;

    if (m_context->logManager) {
      m_context->logManager->Error(
        "Failed to load transformation matrix. Jog panel will not be available.",
        "MainWindow"
      );
    }

    // Disable JOG button
    if (m_jogButton) {
      m_jogButton->setEnabled(false);
      m_jogButton->setToolTip("Jog panel unavailable - transformation matrix not found");
    }

    delete m_matrixLoader;
    m_matrixLoader = nullptr;
    return;  // EARLY EXIT
  }

  std::cout << "[MainWindow] Matrix loaded successfully" << std::endl;

  // Create jog panel
  m_jogPanel = new JogPanelWindow(m_context, m_matrixLoader, this);
  std::cout << "[MainWindow] Jog panel created" << std::endl;

  // Create key binding handler
  m_jogKeyHandler = new JogKeyBindingHandler(m_jogPanel, this);
  m_jogKeyHandler->installOn(m_jogPanel);
  std::cout << "[MainWindow] Key handler installed" << std::endl;

  // Connect visibility to key bindings
  connect(m_jogPanel, &JogPanelWindow::visibilityChanged,
    m_jogKeyHandler, &JogKeyBindingHandler::setEnabled);

  std::cout << "[MainWindow] Jog panel setup complete!" << std::endl;

  if (m_context->logManager) {
    m_context->logManager->Info("Jog panel initialized successfully", "MainWindow");
  }
}


void MainWindow::onToggleJogPanel()
{
  if (!m_jogPanel) {
    std::cerr << "[MainWindow] Jog panel not initialized!" << std::endl;
    return;
  }

  if (m_jogPanel->isPanelVisible()) {
    m_jogPanel->hidePanel();
    std::cout << "[MainWindow] Jog panel hidden" << std::endl;
  }
  else {
    m_jogPanel->showPanel();
    std::cout << "[MainWindow] Jog panel shown" << std::endl;
  }
}



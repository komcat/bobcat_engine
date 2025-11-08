// MainWindow.cpp
#include "MainWindow.h"
#include "ApplicationContext.h"
#include "MainMenuPage.h"
#include "HardwareInstrumentPage.h"
#include "ConfigEditorPage.h"
#include "MotionPage.h"           // NEW - Add this include
#include "TopBar.h"               // ✅ NEW: Fixed top bar
#include "LogStatusBar.h"
#include "LogViewerPage.h"
#include "LogManager.h"
#include "JogPanelWindow.h"
#include "JogKeyBindingHandler.h"
#include "TransformationMatrixLoader.h"
#include "AuxWindowManager.h"
#include "RecipePage.h"           // ✅ ADD THIS
#include "RunProgramPage.h"
#include "DarkTheme.h"            // ✅ ADD THIS - Centralized theme
#include <QVBoxLayout>
#include <iostream>

MainWindow::MainWindow(ApplicationContext* context, QWidget* parent)
  : QMainWindow(parent)
  , m_context(context)
  , m_pageStack(nullptr)
  , m_topBar(nullptr)
  , m_logStatusBar(nullptr)
  , m_logViewerPage(nullptr)
  , m_previousPage(nullptr)
  , m_mainMenuPage(nullptr)
  , m_hardwarePage(nullptr)
  , m_configEditorPage(nullptr)
  , m_motionPage(nullptr)
  , m_recipePage(nullptr)        // ✅ ADD THIS
  , m_runProgramPage(nullptr)
  , m_jogPanel(nullptr)
  , m_jogKeyHandler(nullptr)
  , m_matrixLoader(nullptr)
  , m_shortcutF1(nullptr)
  , m_shortcutF2(nullptr)
  , m_shortcutF3(nullptr)
  , m_shortcutF4(nullptr)
  , m_shortcutF5(nullptr)
  , m_shortcutF6(nullptr)
  , m_shortcutF7(nullptr)
  , m_shortcutF8(nullptr)
  , m_shortcutCtrlJ(nullptr)
  , m_shortcutCtrlK(nullptr)
{
  // Note: ProductPositionManager is created in Application.cpp and available via context

  setupUI();
  setupJogPanel();        // ✅ MUST be before createPages() - RunProgramPage needs m_matrixLoader!
  createPages();          // Now pages can use productPositionManager from context
  setupLogging();
  setupKeyboardShortcuts();  // ✅ NEW: Setup F1-F8 keyboard shortcuts

  // Update TopBar with current product info
  updateProductInfo();

  // Show main menu first
  m_pageStack->setCurrentWidget(m_mainMenuPage);

  // Log window creation
  if (m_context->logManager) {
    m_context->logManager->Info("Main window ready", "MainWindow");
  }
}


// MainWindow.cpp - Add destructor implementation
MainWindow::~MainWindow()
{
  // ✅ Clear the UI callback before MainWindow is destroyed
  if (m_context && m_context->logManager) {
    m_context->logManager->ClearUICallback();
  }

  std::cout << "[MainWindow] Destroyed" << std::endl;
}


void MainWindow::setupUI()
{
  // ✅ Apply centralized dark theme globally to all widgets
  setStyleSheet(DarkTheme::getStyleSheet());

  // Create central widget with layout
  QWidget* centralWidget = new QWidget(this);
  QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // ✅ NEW: Fixed top bar (height: 100px, always visible)
  m_topBar = new TopBar(m_context, this);
  connect(m_topBar, &TopBar::jogButtonClicked, this, &MainWindow::onToggleJogPanel);
  connect(m_topBar, &TopBar::auxWindowButtonClicked, this, &MainWindow::onToggleAuxWindow);
  mainLayout->addWidget(m_topBar, 0);

  // Page stack (main content area)
  m_pageStack = new QStackedWidget();
  mainLayout->addWidget(m_pageStack, 1);

  // Log status bar at bottom (always visible)
  m_logStatusBar = new LogStatusBar(m_context->logManager, this);
  connect(m_logStatusBar, &LogStatusBar::openFullLogViewer,
    this, &MainWindow::onOpenLogViewer);
  mainLayout->addWidget(m_logStatusBar, 0);

  setCentralWidget(centralWidget);

  // Set initial window size (width x height)
  resize(1024, 768);  // Reasonable desktop size

  // Optional: Set size constraints
  setMinimumSize(800, 600);   // Prevent too small
  // setMaximumSize(1920, 1080); // Prevent too large (uncomment if needed)
}

// MainWindow.cpp - Update createPages()
void MainWindow::createPages()
{
  std::cout << "[MainWindow] Creating pages..." << std::endl;

  // Main menu page
  m_mainMenuPage = new MainMenuPage(m_context, this);
  connect(m_mainMenuPage, &MainMenuPage::processClicked,
    this, &MainWindow::onMainMenuProcess);
  connect(m_mainMenuPage, &MainMenuPage::recipeClicked,
    this, &MainWindow::onMainMenuRecipe);
  connect(m_mainMenuPage, &MainMenuPage::motionClicked,
    this, &MainWindow::onMainMenuMotion);
  connect(m_mainMenuPage, &MainMenuPage::hardwareClicked,
    this, &MainWindow::onMainMenuHardware);
  connect(m_mainMenuPage, &MainMenuPage::editConfigClicked,
    this, &MainWindow::onMainMenuEditConfig);
  connect(m_mainMenuPage, &MainMenuPage::futureUpdateClicked,
    this, &MainWindow::onMainMenuFutureUpdate);
  connect(m_mainMenuPage, &MainMenuPage::productChanged,
    this, &MainWindow::updateProductInfo);
  m_pageStack->addWidget(m_mainMenuPage);

  // Hardware & Instrument page
  m_hardwarePage = new HardwareInstrumentPage(m_context, this);
  connect(m_hardwarePage, &HardwareInstrumentPage::backToMainMenu,
    this, &MainWindow::onBackToMainMenu);
  m_pageStack->addWidget(m_hardwarePage);

  // Config Editor page
  m_configEditorPage = new ConfigEditorPage(m_context, this);
  connect(m_configEditorPage, &ConfigEditorPage::backToMainMenu,
    this, &MainWindow::onBackToMainMenu);
  m_pageStack->addWidget(m_configEditorPage);

  // Log Viewer Page
  m_logViewerPage = new LogViewerPage(m_context->logManager, this);
  connect(m_logViewerPage, &LogViewerPage::backRequested,
    this, &MainWindow::onBackToMainMenu);
  m_pageStack->addWidget(m_logViewerPage);

  // Motion page
  m_motionPage = new MotionPage(
    m_context->motionManager,
    m_context->productPositionManager,
    m_matrixLoader,
    m_context->dataStore,
    m_context->coordinateSystemManager,
    m_context->sequenceEngine,
    this
  );
  connect(m_motionPage, &MotionPage::backToMainMenu,
    this, &MainWindow::onBackToMainMenu);
  // Connect product change notifications from MainMenu to MotionPage
  connect(m_mainMenuPage, &MainMenuPage::productChanged,
    m_motionPage, &MotionPage::onProductChanged);
  m_pageStack->addWidget(m_motionPage);

  //  Recipe page
  m_recipePage = new RecipePage(m_context, m_matrixLoader, this);
  connect(m_recipePage, &RecipePage::backToMainMenu,
    this, &MainWindow::onBackToMainMenu);
  // Connect product change notifications from MainMenu to RecipePage
  connect(m_mainMenuPage, &MainMenuPage::productChanged,
    m_recipePage, &RecipePage::onProductChanged);
  m_pageStack->addWidget(m_recipePage);


  //  Run Program page
  m_runProgramPage = new RunProgramPage(m_context, m_matrixLoader, this);
  connect(m_runProgramPage, &RunProgramPage::backToMainMenu,
    this, &MainWindow::onBackToMainMenu);
  // Connect product change notifications from MainMenu to RunProgramPage
  connect(m_mainMenuPage, &MainMenuPage::productChanged,
    m_runProgramPage, &RunProgramPage::onProductChanged);
  m_pageStack->addWidget(m_runProgramPage);


  std::cout << "[MainWindow] Pages created successfully" << std::endl;
}


void MainWindow::setupLogging()
{
  if (!m_context->logManager) {
    std::cerr << "[MainWindow] LogManager not available!" << std::endl;
    return;
  }

  m_context->logManager->SetUICallback([this](const LogManager::LogEntry& entry) {
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

void MainWindow::setupJogPanel()
{
  std::cout << "[MainWindow] Setting up jog panel..." << std::endl;

  m_matrixLoader = new TransformationMatrixLoader();
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

    if (m_topBar && m_topBar->getJogButton()) {
      m_topBar->getJogButton()->setEnabled(false);
      m_topBar->getJogButton()->setToolTip("Jog panel unavailable - transformation matrix not found");
    }

    delete m_matrixLoader;
    m_matrixLoader = nullptr;
    return;
  }

  std::cout << "[MainWindow] Matrix loaded successfully" << std::endl;

  m_jogPanel = new JogPanelWindow(m_context, m_matrixLoader, this);
  std::cout << "[MainWindow] Jog panel created" << std::endl;

  m_jogKeyHandler = new JogKeyBindingHandler(m_jogPanel, this);
  m_jogKeyHandler->installOn(m_jogPanel);
  std::cout << "[MainWindow] Key handler installed" << std::endl;

  connect(m_jogPanel, &JogPanelWindow::visibilityChanged,
    m_jogKeyHandler, &JogKeyBindingHandler::setEnabled);

  std::cout << "[MainWindow] Jog panel setup complete!" << std::endl;

  if (m_context->logManager) {
    m_context->logManager->Info("Jog panel initialized successfully", "MainWindow");
  }
}

void MainWindow::setupKeyboardShortcuts()
{
  std::cout << "[MainWindow] Setting up keyboard shortcuts (F1-F8)..." << std::endl;

  // F1 - Run Program Page
  m_shortcutF1 = new QShortcut(QKeySequence(Qt::Key_F1), this);
  connect(m_shortcutF1, &QShortcut::activated, this, &MainWindow::onMainMenuProcess);
  std::cout << "[MainWindow]   F1 -> Run Program Page" << std::endl;

  // F2 - Recipe Page
  m_shortcutF2 = new QShortcut(QKeySequence(Qt::Key_F2), this);
  connect(m_shortcutF2, &QShortcut::activated, this, &MainWindow::onMainMenuRecipe);
  std::cout << "[MainWindow]   F2 -> Recipe Page" << std::endl;

  // F3 - Motion Page
  m_shortcutF3 = new QShortcut(QKeySequence(Qt::Key_F3), this);
  connect(m_shortcutF3, &QShortcut::activated, this, &MainWindow::onMainMenuMotion);
  std::cout << "[MainWindow]   F3 -> Motion Page" << std::endl;

  // F4 - Hardware Page
  m_shortcutF4 = new QShortcut(QKeySequence(Qt::Key_F4), this);
  connect(m_shortcutF4, &QShortcut::activated, this, &MainWindow::onMainMenuHardware);
  std::cout << "[MainWindow]   F4 -> Hardware Page" << std::endl;

  // F5 - Config Editor Page
  m_shortcutF5 = new QShortcut(QKeySequence(Qt::Key_F5), this);
  connect(m_shortcutF5, &QShortcut::activated, this, &MainWindow::onMainMenuEditConfig);
  std::cout << "[MainWindow]   F5 -> Config Editor Page" << std::endl;

  // F6 - Reserved (Future Update)
  m_shortcutF6 = new QShortcut(QKeySequence(Qt::Key_F6), this);
  connect(m_shortcutF6, &QShortcut::activated, this, &MainWindow::onMainMenuFutureUpdate);
  std::cout << "[MainWindow]   F6 -> Reserved (Future)" << std::endl;

  // F7 - Reserved (not connected yet)
  m_shortcutF7 = new QShortcut(QKeySequence(Qt::Key_F7), this);
  m_shortcutF7->setEnabled(false);  // Disabled until implemented
  std::cout << "[MainWindow]   F7 -> Reserved (Disabled)" << std::endl;

  // F8 - Reserved (not connected yet)
  m_shortcutF8 = new QShortcut(QKeySequence(Qt::Key_F8), this);
  m_shortcutF8->setEnabled(false);  // Disabled until implemented
  std::cout << "[MainWindow]   F8 -> Reserved (Disabled)" << std::endl;

  // Ctrl+J - Toggle Jog Panel
  m_shortcutCtrlJ = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_J), this);
  connect(m_shortcutCtrlJ, &QShortcut::activated, this, &MainWindow::onToggleJogPanel);
  std::cout << "[MainWindow]   Ctrl+J -> Toggle Jog Panel" << std::endl;

  // Ctrl+K - Toggle Aux Window
  m_shortcutCtrlK = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
  connect(m_shortcutCtrlK, &QShortcut::activated, this, &MainWindow::onToggleAuxWindow);
  std::cout << "[MainWindow]   Ctrl+K -> Toggle Aux Window" << std::endl;

  std::cout << "[MainWindow] Keyboard shortcuts setup complete!" << std::endl;

  if (m_context->logManager) {
    m_context->logManager->Info("Keyboard shortcuts (F1-F8, Ctrl+J, Ctrl+K) registered", "MainWindow");
  }
}

void MainWindow::updateProductInfo()
{
  if (!m_topBar || !m_context || !m_context->productPositionManager) {
    return;
  }

  if (m_context->productPositionManager->HasLoadedProduct()) {
    QString productName = QString::fromStdString(
      m_context->productPositionManager->GetCurrentProductName()
    );
    QString productPath = QString::fromStdString(
      m_context->productPositionManager->GetCurrentProductPath()
    );

    m_topBar->setProductInfo(productName, productPath);
  } else {
    m_topBar->setProductInfo("", "");
  }
}

void MainWindow::onMainMenuProcess()
{
  std::cout << "[MainWindow] Process clicked" << std::endl;
  m_context->logManager->Info("Navigating to Run Program page", "MainWindow");
  m_pageStack->setCurrentWidget(m_runProgramPage);  // ✅ CHANGED FROM WARNING
}

void MainWindow::onMainMenuRecipe()
{
  std::cout << "[MainWindow] Recipe clicked" << std::endl;
  m_context->logManager->Info("Navigating to Recipe page", "MainWindow");
  m_pageStack->setCurrentWidget(m_recipePage);  // ✅ CHANGED FROM WARNING
}

void MainWindow::onMainMenuMotion()
{
  std::cout << "[MainWindow] Motion clicked" << std::endl;
  m_context->logManager->Info("Navigating to Motion page", "MainWindow");
  m_pageStack->setCurrentWidget(m_motionPage);  // NEW - Navigate to motion page
}

void MainWindow::onMainMenuHardware()
{
  std::cout << "[MainWindow] Hardware clicked" << std::endl;
  m_context->logManager->Info("Navigating to Hardware & Instrument page", "MainWindow");
  m_pageStack->setCurrentWidget(m_hardwarePage);
}

void MainWindow::onMainMenuEditConfig()
{
  std::cout << "[MainWindow] Edit Config clicked" << std::endl;
  m_context->logManager->Info("Navigating to Config Editor page", "MainWindow");
  m_pageStack->setCurrentWidget(m_configEditorPage);
}

void MainWindow::onMainMenuFutureUpdate()
{
  std::cout << "[MainWindow] Future Update clicked (not implemented)" << std::endl;
  m_context->logManager->Info("Future update button clicked", "MainWindow");
}

void MainWindow::onBackToMainMenu()
{
  std::cout << "[MainWindow] Back to main menu" << std::endl;
  m_context->logManager->Debug("Returning to main menu", "MainWindow");

  // Clear previous page tracker when going back to main menu
  m_previousPage = nullptr;

  m_pageStack->setCurrentWidget(m_mainMenuPage);
}

void MainWindow::onOpenLogViewer()
{
  // Check if we're already on the log viewer page
  if (m_pageStack->currentWidget() == m_logViewerPage) {
    // Toggle back to previous page
    std::cout << "[MainWindow] Closing log viewer, returning to previous page" << std::endl;
    m_context->logManager->Debug("Closing log viewer", "MainWindow");

    if (m_previousPage) {
      m_pageStack->setCurrentWidget(m_previousPage);
    } else {
      // Fallback to main menu if no previous page tracked
      m_pageStack->setCurrentWidget(m_mainMenuPage);
    }
    m_previousPage = nullptr;
  } else {
    // Open log viewer and save current page
    std::cout << "[MainWindow] Opening log viewer" << std::endl;
    m_context->logManager->Info("Opening full log viewer", "MainWindow");

    m_previousPage = m_pageStack->currentWidget();
    m_logViewerPage->RefreshLogs();
    m_pageStack->setCurrentWidget(m_logViewerPage);
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
    std::cout << "[MainWindow] Jog panel hidden (Ctrl+J)" << std::endl;
  }
  else {
    m_jogPanel->showPanel();
    std::cout << "[MainWindow] Jog panel shown (Ctrl+J)" << std::endl;
  }
}

void MainWindow::onToggleAuxWindow()
{
  if (!m_context || !m_context->auxWindowManager) {
    std::cerr << "[MainWindow] Aux window manager not initialized!" << std::endl;
    return;
  }

  if (m_context->auxWindowManager->isVisible()) {
    m_context->auxWindowManager->hide();
    std::cout << "[MainWindow] Aux window hidden (Ctrl+K)" << std::endl;
    if (m_context->logManager) {
      m_context->logManager->Info("Aux window hidden", "MainWindow");
    }
  }
  else {
    m_context->auxWindowManager->show();
    std::cout << "[MainWindow] Aux window shown (Ctrl+K)" << std::endl;
    if (m_context->logManager) {
      m_context->logManager->Info("Aux window shown", "MainWindow");
    }
  }
}
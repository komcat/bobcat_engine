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
#include "AuxViewWindow.h"
#include "GdiWindow/GdiCameraWindowManager.h"  // GDI camera monitor
#include "QtCameraWindow/QtCameraWindow.h"     // Qt camera monitor
#include "SwirBeamWindow/SwirBeamWindowManager.h"  // SWIR beam processing window
#include "RecipePage.h"           // ✅ ADD THIS
#include "RunProgramPage.h"
#include "DarkTheme.h"            // ✅ ADD THIS - Centralized theme
#include "ThemeManager.h"         // Runtime theme switcher
#include "version.h"              // Version information
#include "VisionManagerPage.h"    // Vision Manager UI
#include "SettingsPage/ApplicationSettingsPage.h"  // Application Settings (referenced through container)
#include "SettingsPage/SettingsContainerPage.h"    // Settings container with sidebar
#include "TrainerContainerPage.h"                  // Trainer container
#include "DataBackupPage.h"
#include "ScriptRunnerPage.h"
#include "Common/ShutdownConfirmDialog.h"          // Shutdown confirmation
#include "safety/SafetyWatchManager.h"
#include "safety/SafetyAlertDialog.h"
#include "snapshot/StateSnapshotManager.h"
#include <QVBoxLayout>
#include <QApplication>
#include <QMessageBox>
#include <QSettings>
#include <QScreen>
#include <QInputDialog>
#include <iostream>

MainWindow::MainWindow(ApplicationContext* context, QWidget* parent)
  : QMainWindow(parent)
  , m_context(context)
  , m_pageStack(nullptr)
  , m_topBar(nullptr)
  , m_logStatusBar(nullptr)
  , m_previousPage(nullptr)
  // Page pointers are default-initialized to nullptr in MainWindow.h - they
  // are filled in lazily by ensurePageBuilt(), so every one of them must start
  // null (m_scriptRunnerPage previously had no initializer at all).
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
  restoreWindowSettings();  // Restore saved window position and size
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

  // Connect safety watch signals
  if (m_context->safetyWatchManager) {
    connect(m_context->safetyWatchManager, &SafetyWatchManager::safetyTriggered,
            this, [this](QString title, QString message, bool critical) {
      // Show safety alert dialog on UI thread
      QMetaObject::invokeMethod(this, [this, title, message, critical]() {
        // Append E-stop cascade summary if available
        QString dialogMessage = message;
        if (critical && m_context->safetyWatchManager) {
          std::string summary = m_context->safetyWatchManager->GetEStopSummary();
          if (!summary.empty()) {
            dialogMessage += "\n\n" + QString::fromStdString(summary);
          }
        }

        SafetyAlertDialog dialog(title, dialogMessage, critical, this);
        connect(&dialog, &SafetyAlertDialog::acknowledged, this, [this]() {
          if (m_context->safetyWatchManager) {
            m_context->safetyWatchManager->AcknowledgeSafetyFlag();
          }
        });
        dialog.exec();
      }, Qt::QueuedConnection);
    });

    // Connect TopBar safety indicator to SafetyWatchManager
    if (m_topBar) {
      m_topBar->connectSafetyManager(m_context->safetyWatchManager);
    }

    std::cout << "[MainWindow] Safety watch signals connected" << std::endl;
  }

  // Start TEC temperature polling in the top bar
  if (m_topBar) {
    m_topBar->startTECTemperaturePolling();
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

void MainWindow::closeEvent(QCloseEvent* event)
{
  // Show shutdown confirmation dialog — user must confirm save & machine state
  if (!ShutdownConfirmDialog::confirmShutdown(this)) {
    event->ignore();
    return;
  }

  std::cout << "[MainWindow] Close event triggered - shutting down application..." << std::endl;

  // Save window geometry before closing
  saveWindowSettings();

  // Allow auxiliary windows to close (they normally hide instead of closing)
  if (m_context && m_context->auxWindowManager) {
    auto* win = m_context->auxWindowManager->getWindow();
    if (win) win->allowClose();
  }

  // Accept the close event
  event->accept();

  // Signal the application to quit, which will trigger Application::shutdown()
  QApplication::quit();
}


void MainWindow::setupUI()
{
  // Set window title with version
  QString windowTitle = QString("Bobcat Engine v%1").arg(BobcatVersion::VERSION_STRING);
  setWindowTitle(windowTitle);

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
  connect(m_topBar, &TopBar::rtlmButtonClicked, this, &MainWindow::onToggleRtlmWindow);
  connect(m_topBar, &TopBar::rtlm2ButtonClicked, this, &MainWindow::onToggleRtlmWindow2);
  connect(m_topBar, &TopBar::swirWindowButtonClicked, this, &MainWindow::onToggleSwirWindow);
  connect(m_topBar, &TopBar::gdiButtonClicked, this, &MainWindow::onToggleGdiWindow);
  // Wire the scan-busy badge to the tracker so recipe/tab scans light it up.
  m_topBar->connectScanActivityTracker(m_context->scanActivityTracker);
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

// ---------------------------------------------------------------------------
// Page construction
//
// Only MainMenuPage is built here. Building all twelve pages up front cost
// ~18s of startup - the bulk of the app's boot time - for pages an operator
// may never open in a session. Everything else is built by ensurePageBuilt()
// on first navigation.
//
// MainMenuPage must stay eager for two reasons: it is the page shown on
// startup, and it owns the productChanged signal that the lazily-built pages
// connect to when they are created.
//
// Pages that need the current product read it in their own constructor, so a
// page built after a product change comes up with the right product; the
// connection made at build time covers every change after that.
// ---------------------------------------------------------------------------
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
  connect(m_mainMenuPage, &MainMenuPage::accuracyTrainerClicked,
    this, &MainWindow::onMainMenuAccuracyTrainer);
  connect(m_mainMenuPage, &MainMenuPage::dataBackupClicked,
    this, &MainWindow::onMainMenuDataBackup);
  connect(m_mainMenuPage, &MainMenuPage::luaScriptClicked,
    this, &MainWindow::onMainMenuScriptRunner);
  connect(m_mainMenuPage, &MainMenuPage::productChanged,
    this, &MainWindow::updateProductInfo);
  m_pageStack->addWidget(m_mainMenuPage);

  // ✅ Wire themeChanged for the always-present widgets. Lazily-built pages
  // that carry their own stylesheets (RunProgramPage, RecipePage) connect
  // themselves in ensurePageBuilt(); they pick up the active theme from the
  // global stylesheet when they are constructed.
  auto& tm = ThemeManager::instance();
  connect(&tm, &ThemeManager::themeChanged, m_topBar, &TopBar::applyTheme);
  // Also re-apply MainWindow's own stylesheet
  connect(&tm, &ThemeManager::themeChanged, this, [this]() {
    setStyleSheet(DarkTheme::getStyleSheet());
  });

  std::cout << "[MainWindow] Main menu ready (other pages built on first use)" << std::endl;
}

QWidget* MainWindow::ensurePageBuilt(PageId id)
{
  switch (id) {
  case PageId::MainMenu:
    // Always built by createPages().
    return m_mainMenuPage;

  case PageId::Hardware:
    if (!m_hardwarePage) {
      m_hardwarePage = new HardwareInstrumentPage(m_context, this);
      connect(m_hardwarePage, &HardwareInstrumentPage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);
      m_pageStack->addWidget(m_hardwarePage);
    }
    return m_hardwarePage;

  case PageId::ConfigEditor:
    // Note: nothing currently navigates here - F5 and the main-menu "Edit
    // Config" button both go to PageId::Settings. The page was built at boot
    // and never shown; leaving the case in place keeps it one nav call away
    // if it is wired up again.
    if (!m_configEditorPage) {
      m_configEditorPage = new ConfigEditorPage(m_context, this);
      connect(m_configEditorPage, &ConfigEditorPage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);
      m_pageStack->addWidget(m_configEditorPage);
    }
    return m_configEditorPage;

  case PageId::LogViewer:
    if (!m_logViewerPage) {
      m_logViewerPage = new LogViewerPage(m_context->logManager, this);
      connect(m_logViewerPage, &LogViewerPage::backRequested,
        this, &MainWindow::onBackToMainMenu);
      m_pageStack->addWidget(m_logViewerPage);
    }
    return m_logViewerPage;

  case PageId::Motion:
    if (!m_motionPage) {
      m_motionPage = new MotionPage(
        m_context->motionManager,
        m_context->productPositionManager,
        m_matrixLoader,
        m_context->dataStore,
        m_context->coordinateSystemManager,
        m_context->sequenceEngine,
        m_context->interlockManager,
        this
      );
      connect(m_motionPage, &MotionPage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);
      // Connect product change notifications from MainMenu to MotionPage
      connect(m_mainMenuPage, &MainMenuPage::productChanged,
        m_motionPage, &MotionPage::onProductChanged);
      m_pageStack->addWidget(m_motionPage);
    }
    return m_motionPage;

  case PageId::Recipe:
    if (!m_recipePage) {
      m_recipePage = new RecipePage(m_context, m_matrixLoader, this);
      connect(m_recipePage, &RecipePage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);
      // Connect product change notifications from MainMenu to RecipePage
      connect(m_mainMenuPage, &MainMenuPage::productChanged,
        m_recipePage, &RecipePage::onProductChanged);
      connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
        m_recipePage, &RecipePage::applyTheme);
      m_pageStack->addWidget(m_recipePage);
    }
    return m_recipePage;

  case PageId::RunProgram:
    if (!m_runProgramPage) {
      m_runProgramPage = new RunProgramPage(m_context, m_matrixLoader, this);
      connect(m_runProgramPage, &RunProgramPage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);
      // Connect product change notifications from MainMenu to RunProgramPage
      connect(m_mainMenuPage, &MainMenuPage::productChanged,
        m_runProgramPage, &RunProgramPage::onProductChanged);
      connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
        m_runProgramPage, &RunProgramPage::applyTheme);

      // Connect "Edit in Recipe Page" from RunProgramPage to RecipePage.
      // RecipePage is built inside the lambda, not here - opening the process
      // page should not drag the recipe editor in with it.
      connect(m_runProgramPage, &RunProgramPage::requestEditRecipe,
        this, [this](const QString& filePath) {
          auto* recipePage = static_cast<RecipePage*>(ensurePageBuilt(PageId::Recipe));
          if (!recipePage) return;
          recipePage->loadRecipeFromFile(filePath);
          m_pageStack->setCurrentWidget(recipePage);
        });

      m_pageStack->addWidget(m_runProgramPage);
    }
    return m_runProgramPage;

  case PageId::VisionManager:
    if (!m_visionManagerPage) {
      m_visionManagerPage = new VisionManagerPage(m_context, this);
      connect(m_visionManagerPage, &VisionManagerPage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);
      m_pageStack->addWidget(m_visionManagerPage);
    }
    return m_visionManagerPage;

  case PageId::Settings:
    if (!m_settingsContainerPage) {
      m_settingsContainerPage = new SettingsContainerPage(m_context, this);
      connect(m_settingsContainerPage, &SettingsContainerPage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);

      ApplicationSettingsPage* appSettings = m_settingsContainerPage->appSettingsPage();

      // Connect settings changes to GDI camera window managers
      if (m_context && m_context->gdiCameraWindowManager) {
        connect(appSettings, &ApplicationSettingsPage::settingsChanged,
          m_context->gdiCameraWindowManager, &GdiCameraWindowManager::applyApplicationSettings);
      }
      if (m_context && m_context->gdiCameraWindowManager2) {
        connect(appSettings, &ApplicationSettingsPage::settingsChanged,
          m_context->gdiCameraWindowManager2, &GdiCameraWindowManager::applyApplicationSettings);
      }

      // Theme live reload: when settings are saved, re-apply the active theme.
      // Only reachable once this page exists, since it is what emits the signal.
      connect(appSettings, &ApplicationSettingsPage::settingsChanged,
        this, [this]() {
          QSettings s("Fabrinet", "BobcatEngine");
          QString t = s.value("UI/Theme", "Default").toString();
          ThemeManager::instance().setTheme(
            t == "CC29" ? Theme::ThemeId::CC29 : Theme::ThemeId::Default);
        });

      m_pageStack->addWidget(m_settingsContainerPage);
    }
    return m_settingsContainerPage;

  case PageId::Trainer:
    if (!m_trainerContainerPage) {
      // Note: TrainerContainerPage is itself lazy internally - it builds each
      // of its seven trainer tabs on first selection.
      m_trainerContainerPage = new TrainerContainerPage(m_context, this);
      connect(m_trainerContainerPage, &TrainerContainerPage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);
      m_pageStack->addWidget(m_trainerContainerPage);
    }
    return m_trainerContainerPage;

  case PageId::DataBackup:
    if (!m_dataBackupPage) {
      m_dataBackupPage = new DataBackupPage(m_context, this);
      connect(m_dataBackupPage, &DataBackupPage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);
      connect(m_mainMenuPage, &MainMenuPage::productChanged,
        m_dataBackupPage, &DataBackupPage::onProductChanged);
      m_pageStack->addWidget(m_dataBackupPage);
    }
    return m_dataBackupPage;

  case PageId::ScriptRunner:
    if (!m_scriptRunnerPage) {
      m_scriptRunnerPage = new ScriptRunnerPage(m_context, this);
      connect(m_scriptRunnerPage, &ScriptRunnerPage::backToMainMenu,
        this, &MainWindow::onBackToMainMenu);
      connect(m_mainMenuPage, &MainMenuPage::productChanged,
        m_scriptRunnerPage, &ScriptRunnerPage::onProductChanged);
      // Track script-running state so we can block page navigation while a
      // script runs. Until this page exists no script can be running, so
      // m_scriptRunning correctly stays false.
      connect(m_scriptRunnerPage, &ScriptRunnerPage::scriptRunningChanged,
        this, [this](bool running) { m_scriptRunning = running; });
      m_pageStack->addWidget(m_scriptRunnerPage);
    }
    return m_scriptRunnerPage;
  }

  return nullptr;
}

void MainWindow::navigateTo(PageId id)
{
  if (QWidget* page = ensurePageBuilt(id)) {
    m_pageStack->setCurrentWidget(page);
  } else {
    std::cerr << "[MainWindow] Failed to build requested page!" << std::endl;
  }
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

  // F6 - Vision Manager Page
  m_shortcutF6 = new QShortcut(QKeySequence(Qt::Key_F6), this);
  connect(m_shortcutF6, &QShortcut::activated, this, &MainWindow::onMainMenuFutureUpdate);
  std::cout << "[MainWindow]   F6 -> Vision Manager Page" << std::endl;

  // F7 - Accuracy Trainer Page
  m_shortcutF7 = new QShortcut(QKeySequence(Qt::Key_F7), this);
  connect(m_shortcutF7, &QShortcut::activated, this, &MainWindow::onMainMenuAccuracyTrainer);
  std::cout << "[MainWindow]   F7 -> Accuracy Trainer Page" << std::endl;

  // F8 - Data Backup Page
  m_shortcutF8 = new QShortcut(QKeySequence(Qt::Key_F8), this);
  connect(m_shortcutF8, &QShortcut::activated, this, &MainWindow::onMainMenuDataBackup);
  std::cout << "[MainWindow]   F8 -> Data Backup Page" << std::endl;

  // F9 - Script Runner Page
  m_shortcutF9 = new QShortcut(QKeySequence(Qt::Key_F9), this);
  connect(m_shortcutF9, &QShortcut::activated, this, &MainWindow::onMainMenuScriptRunner);
  std::cout << "[MainWindow]   F9 -> Script Runner Page" << std::endl;

  // Ctrl+J - Toggle Jog Panel
  m_shortcutCtrlJ = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_J), this);
  connect(m_shortcutCtrlJ, &QShortcut::activated, this, &MainWindow::onToggleJogPanel);
  std::cout << "[MainWindow]   Ctrl+J -> Toggle Jog Panel" << std::endl;

  // Ctrl+K - Toggle Aux Window
  m_shortcutCtrlK = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
  connect(m_shortcutCtrlK, &QShortcut::activated, this, &MainWindow::onToggleAuxWindow);
  std::cout << "[MainWindow]   Ctrl+K -> Toggle Aux Window" << std::endl;

  // Ctrl+R - Toggle Realtime Camera Monitor (GDI)
  m_shortcutCtrlR = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this);
  connect(m_shortcutCtrlR, &QShortcut::activated, this, &MainWindow::onToggleRtlmWindow);
  std::cout << "[MainWindow]   Ctrl+R -> Toggle Realtime Camera Monitor" << std::endl;

  // Ctrl+T - Toggle Realtime Camera Monitor 2 (GDI)
  m_shortcutCtrlT = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this);
  connect(m_shortcutCtrlT, &QShortcut::activated, this, &MainWindow::onToggleRtlmWindow2);
  std::cout << "[MainWindow]   Ctrl+T -> Toggle Realtime Camera Monitor 2" << std::endl;

  // Ctrl+Shift+S - Capture labeled state snapshot
  m_shortcutCtrlShiftS = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this);
  connect(m_shortcutCtrlShiftS, &QShortcut::activated, this, &MainWindow::onCaptureSnapshot);
  std::cout << "[MainWindow]   Ctrl+Shift+S -> Capture state snapshot" << std::endl;

  std::cout << "[MainWindow] Keyboard shortcuts setup complete!" << std::endl;

  if (m_context->logManager) {
    m_context->logManager->Info("Keyboard shortcuts (F1-F9, Ctrl+J, Ctrl+K, Ctrl+R, Ctrl+T, Ctrl+Shift+S) registered", "MainWindow");
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

bool MainWindow::canNavigateAwayFromCurrentPage()
{
  // Block navigation away from ScriptRunnerPage while a script is running —
  // the Stop button lives there.
  if (m_scriptRunning && m_pageStack->currentWidget() == m_scriptRunnerPage) {
    QMessageBox::information(this, "Script Running",
      "A Lua script is currently running.\n"
      "Please stop the script (or wait for it to finish) before changing pages.");
    return false;
  }
  return true;
}

bool MainWindow::confirmLeaveRecipePage()
{
  // m_recipePage is null until the recipe editor has been opened at least
  // once. currentWidget() is never null once the stack has a page, so an
  // unbuilt recipe page can never compare equal here.
  if (m_pageStack->currentWidget() != m_recipePage) {
    return true;
  }
  return m_recipePage->canLeavePage();  // false if the user cancelled
}

void MainWindow::onMainMenuProcess()
{
  if (!canNavigateAwayFromCurrentPage()) return;
  if (!confirmLeaveRecipePage()) return;

  std::cout << "[MainWindow] Process clicked" << std::endl;
  m_context->logManager->Info("Navigating to Run Program page", "MainWindow");
  navigateTo(PageId::RunProgram);
}

void MainWindow::onMainMenuRecipe()
{
  if (!canNavigateAwayFromCurrentPage()) return;

  std::cout << "[MainWindow] Recipe clicked" << std::endl;
  m_context->logManager->Info("Navigating to Recipe page", "MainWindow");
  navigateTo(PageId::Recipe);
}

void MainWindow::onMainMenuMotion()
{
  if (!canNavigateAwayFromCurrentPage()) return;
  if (!confirmLeaveRecipePage()) return;

  std::cout << "[MainWindow] Motion clicked" << std::endl;
  m_context->logManager->Info("Navigating to Motion page", "MainWindow");
  navigateTo(PageId::Motion);
}

void MainWindow::onMainMenuHardware()
{
  if (!canNavigateAwayFromCurrentPage()) return;
  if (!confirmLeaveRecipePage()) return;

  std::cout << "[MainWindow] Hardware clicked" << std::endl;
  m_context->logManager->Info("Navigating to Hardware & Instrument page", "MainWindow");
  navigateTo(PageId::Hardware);
}

void MainWindow::onMainMenuEditConfig()
{
  if (!canNavigateAwayFromCurrentPage()) return;
  if (!confirmLeaveRecipePage()) return;

  std::cout << "[MainWindow] Application Settings clicked" << std::endl;
  m_context->logManager->Info("Navigating to Application Settings page", "MainWindow");
  navigateTo(PageId::Settings);
}

void MainWindow::onMainMenuFutureUpdate()
{
  if (!canNavigateAwayFromCurrentPage()) return;
  if (!confirmLeaveRecipePage()) return;

  std::cout << "[MainWindow] Vision Manager clicked" << std::endl;
  m_context->logManager->Info("Navigating to Vision Manager page", "MainWindow");
  navigateTo(PageId::VisionManager);
}

void MainWindow::onMainMenuAccuracyTrainer()
{
  if (!canNavigateAwayFromCurrentPage()) return;
  if (!confirmLeaveRecipePage()) return;

  std::cout << "[MainWindow] Accuracy Trainer clicked" << std::endl;
  m_context->logManager->Info("Navigating to Accuracy Trainer page", "MainWindow");
  navigateTo(PageId::Trainer);
}

void MainWindow::onMainMenuDataBackup()
{
  if (!canNavigateAwayFromCurrentPage()) return;
  if (!confirmLeaveRecipePage()) return;

  std::cout << "[MainWindow] Data Backup clicked" << std::endl;
  m_context->logManager->Info("Navigating to Data Backup page", "MainWindow");
  navigateTo(PageId::DataBackup);
}

void MainWindow::onMainMenuScriptRunner()
{
  // No canNavigateAwayFromCurrentPage() guard: F9 should always land on the
  // script runner (never navigates *away* from it).
  if (!confirmLeaveRecipePage()) return;

  std::cout << "[MainWindow] Script Runner clicked" << std::endl;
  m_context->logManager->Info("Navigating to Script Runner page", "MainWindow");
  navigateTo(PageId::ScriptRunner);
}

void MainWindow::onBackToMainMenu()
{
  if (!canNavigateAwayFromCurrentPage()) return;

  std::cout << "[MainWindow] Back to main menu" << std::endl;
  m_context->logManager->Debug("Returning to main menu", "MainWindow");

  // Clear previous page tracker when going back to main menu
  m_previousPage = nullptr;

  m_pageStack->setCurrentWidget(m_mainMenuPage);
}

void MainWindow::onOpenLogViewer()
{
  // Check if we're already on the log viewer page (null until first opened,
  // which never matches currentWidget())
  if (m_logViewerPage && m_pageStack->currentWidget() == m_logViewerPage) {
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
    // Check for unsaved changes if leaving RecipePage
    if (!confirmLeaveRecipePage()) return;

    // Open log viewer and save current page
    std::cout << "[MainWindow] Opening log viewer" << std::endl;
    m_context->logManager->Info("Opening full log viewer", "MainWindow");

    // Capture the outgoing page BEFORE building the log viewer, so the
    // "previous page" is where the user actually was.
    QWidget* outgoing = m_pageStack->currentWidget();

    auto* logViewer = static_cast<LogViewerPage*>(ensurePageBuilt(PageId::LogViewer));
    if (!logViewer) return;

    m_previousPage = outgoing;
    logViewer->RefreshLogs();
    m_pageStack->setCurrentWidget(logViewer);
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

void MainWindow::onToggleRtlmWindow()
{
  // Try Qt camera window first, fall back to GDI.
  // The Qt window is built lazily, so ask the context to build it rather than
  // testing qtCameraWindow for null - a null pointer here means "not built
  // yet", not "not available", and falling through to GDI would silently
  // toggle the wrong window.
  QtCameraWindow* qtWindow = m_context ? m_context->qtCameraWindow : nullptr;
  if (!qtWindow && m_context && m_context->ensureQtCameraWindow) {
    qtWindow = m_context->ensureQtCameraWindow();
  }

  if (qtWindow) {
    if (qtWindow->isVisible()) {
      qtWindow->hide();
      std::cout << "[MainWindow] Qt camera monitor hidden (Ctrl+R)" << std::endl;
    } else {
      qtWindow->show();
      std::cout << "[MainWindow] Qt camera monitor shown (Ctrl+R)" << std::endl;
    }
    return;
  }

  if (!m_context || !m_context->gdiCameraWindowManager) {
    std::cerr << "[MainWindow] Camera window not initialized!" << std::endl;
    return;
  }

  if (m_context->gdiCameraWindowManager->isVisible()) {
    m_context->gdiCameraWindowManager->hide();
    std::cout << "[MainWindow] GDI camera monitor hidden (Ctrl+R)" << std::endl;
  } else {
    m_context->gdiCameraWindowManager->show();
    std::cout << "[MainWindow] GDI camera monitor shown (Ctrl+R)" << std::endl;
  }
}

void MainWindow::onToggleRtlmWindow2()
{
  // Try Qt camera window 2 first, fall back to GDI (built lazily - see
  // onToggleRtlmWindow() for why the null check alone is not enough).
  QtCameraWindow* qtWindow2 = m_context ? m_context->qtCameraWindow2 : nullptr;
  if (!qtWindow2 && m_context && m_context->ensureQtCameraWindow2) {
    qtWindow2 = m_context->ensureQtCameraWindow2();
  }

  if (qtWindow2) {
    if (qtWindow2->isVisible()) {
      qtWindow2->hide();
      std::cout << "[MainWindow] Qt camera monitor 2 hidden (Ctrl+T)" << std::endl;
    } else {
      qtWindow2->show();
      std::cout << "[MainWindow] Qt camera monitor 2 shown (Ctrl+T)" << std::endl;
    }
    return;
  }

  if (!m_context || !m_context->gdiCameraWindowManager2) {
    std::cerr << "[MainWindow] Camera window 2 not initialized!" << std::endl;
    return;
  }

  if (m_context->gdiCameraWindowManager2->isVisible()) {
    m_context->gdiCameraWindowManager2->hide();
    std::cout << "[MainWindow] GDI camera monitor 2 hidden (Ctrl+T)" << std::endl;
  } else {
    m_context->gdiCameraWindowManager2->show();
    std::cout << "[MainWindow] GDI camera monitor 2 shown (Ctrl+T)" << std::endl;
  }
}

void MainWindow::onToggleSwirWindow()
{
  if (!m_context || !m_context->swirBeamWindowManager) {
    std::cerr << "[MainWindow] SWIR Beam window manager not initialized!" << std::endl;
    return;
  }

  if (m_context->swirBeamWindowManager->isVisible()) {
    m_context->swirBeamWindowManager->hide();
    std::cout << "[MainWindow] SWIR Beam window hidden" << std::endl;
    if (m_context->logManager) {
      m_context->logManager->Info("SWIR Beam window hidden", "MainWindow");
    }
  }
  else {
    m_context->swirBeamWindowManager->show();
    std::cout << "[MainWindow] SWIR Beam window shown" << std::endl;
    if (m_context->logManager) {
      m_context->logManager->Info("SWIR Beam window shown", "MainWindow");
    }
  }
}

void MainWindow::onToggleGdiWindow()
{
  // Force-show the native GDI camera monitor (#1). Distinct from
  // Rltm/Rltm2 which prefer the Qt camera window when present.
  if (!m_context || !m_context->gdiCameraWindowManager) {
    std::cerr << "[MainWindow] GDI camera window manager not initialized!" << std::endl;
    return;
  }

  if (m_context->gdiCameraWindowManager->isVisible()) {
    m_context->gdiCameraWindowManager->hide();
    std::cout << "[MainWindow] GDI camera monitor hidden" << std::endl;
  } else {
    m_context->gdiCameraWindowManager->show();
    std::cout << "[MainWindow] GDI camera monitor shown" << std::endl;
  }
}

void MainWindow::saveWindowSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  settings.setValue("MainWindow/geometry", saveGeometry());
  settings.setValue("MainWindow/windowState", saveState());
  std::cout << "[MainWindow] Window settings saved" << std::endl;
}

void MainWindow::restoreWindowSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  QByteArray geometry = settings.value("MainWindow/geometry").toByteArray();
  QByteArray state = settings.value("MainWindow/windowState").toByteArray();

  if (!geometry.isEmpty()) {
    restoreGeometry(geometry);
    std::cout << "[MainWindow] Window geometry restored" << std::endl;
  }

  if (!state.isEmpty()) {
    restoreState(state);
    std::cout << "[MainWindow] Window state restored" << std::endl;
  }
}

// ----------------------------------------------------------------------------
// Ctrl+Shift+S handler — pops a label prompt and writes a state snapshot via
// the StateSnapshotManager. Runs directly on the UI thread (no sequence,
// no script) so it's safe to invoke from any page at any time.
// ----------------------------------------------------------------------------
void MainWindow::onCaptureSnapshot()
{
  if (!m_context || !m_context->stateSnapshotManager) {
    QMessageBox::warning(this, "Snapshot",
      "StateSnapshotManager is not available.");
    return;
  }

  bool ok = false;
  QString label = QInputDialog::getText(this,
    "Capture State Snapshot",
    "Label (free text — used in filename):",
    QLineEdit::Normal,
    QString(),
    &ok);
  if (!ok) {
    return;  // user cancelled
  }

  SnapshotOptions opts;
  opts.label         = label.toStdString();
  opts.source        = "ui";
  opts.capture_image = true;

  auto result = m_context->stateSnapshotManager->Capture(opts);
  if (!result || !result->success) {
    QString err = result ? QString::fromStdString(result->error)
                         : QString("manager returned no result");
    QMessageBox::warning(this, "Snapshot failed", err);
    return;
  }

  QMessageBox::information(this, "Snapshot saved",
    QString("Saved to:\n%1\n\nCamera frames: %2")
      .arg(QString::fromStdString(result->snapshot_path))
      .arg(static_cast<int>(result->image_paths.size())));
}
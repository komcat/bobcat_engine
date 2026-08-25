// MainWindow.h
#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QShortcut>
#include <QCloseEvent>
#include <QSettings>

struct ApplicationContext;
class MainMenuPage;
class HardwareInstrumentPage;
class ConfigEditorPage;
class MotionPage;
class RecipePage;              // ✅ ADD THIS
class LogStatusBar;
class LogViewerPage;
class JogPanelWindow;
class JogKeyBindingHandler;
class TransformationMatrixLoader;
class RunProgramPage;
class TopBar;
class VisionManagerPage;       // Vision Manager UI
class ApplicationSettingsPage; // Application Settings editor
class SettingsContainerPage;   // Settings container with sidebar (App Settings + IO Watch)
class TrainerContainerPage;   // Trainer container with side panel
class DataBackupPage;
class ScriptRunnerPage;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(ApplicationContext* context, QWidget* parent = nullptr);
  ~MainWindow();

  /**
   * @brief Update the TopBar with current product information
   */
  void updateProductInfo();

protected:
  /**
   * @brief Handle window close event to properly shutdown the application
   */
  void closeEvent(QCloseEvent* event) override;

private slots:
  void onMainMenuProcess();
  void onMainMenuRecipe();
  void onMainMenuMotion();
  void onMainMenuHardware();
  void onMainMenuEditConfig();
  void onMainMenuFutureUpdate();
  void onMainMenuAccuracyTrainer();
  void onMainMenuDataBackup();
  void onMainMenuScriptRunner();
  void onBackToMainMenu();
  void onOpenLogViewer();
  void onToggleJogPanel();
  void onToggleAuxWindow();  // ✅ NEW: Toggle aux window visibility
  void onToggleRtlmWindow();  // Toggle realtime camera monitor (GDI)
  void onToggleRtlmWindow2(); // Toggle second realtime camera monitor (GDI)
  void onToggleSwirWindow();  // Toggle SWIR beam processing window
  void onToggleGdiWindow();   // Force-toggle native GDI camera monitor (skips Qt fallback)
  void onCaptureSnapshot();   // Ctrl+Shift+S — prompt for label, write snapshot

private:
  // Identifies a page in the stack. Every page except MainMenu is built on
  // first navigation - see ensurePageBuilt().
  enum class PageId {
    MainMenu,
    Hardware,
    ConfigEditor,
    LogViewer,
    Motion,
    Recipe,
    RunProgram,
    VisionManager,
    Settings,
    Trainer,
    DataBackup,
    ScriptRunner
  };

  void setupUI();
  void createPages();
  void setupLogging();
  void setupJogPanel();
  void setupKeyboardShortcuts();  // ✅ NEW: Setup F1-F8 shortcuts
  void saveWindowSettings();      // Save window geometry to settings
  void restoreWindowSettings();   // Restore window geometry from settings

  // Build `id` if it does not exist yet, wire its signals, add it to the page
  // stack, and return it. Idempotent. Returns nullptr only if construction
  // failed. This is the ONLY place a page is constructed - anything that needs
  // a page pointer must go through here rather than reading the member
  // directly, because a null member means "not built yet", not "unavailable".
  QWidget* ensurePageBuilt(PageId id);

  // ensurePageBuilt() + switch the stack to it.
  void navigateTo(PageId id);

  // Returns true if page navigation is currently allowed.
  // When a Lua script is running, the user must stay on ScriptRunnerPage.
  bool canNavigateAwayFromCurrentPage();

  // Returns true if it is safe to leave RecipePage (no unsaved changes, or the
  // user chose to discard them). Trivially true when RecipePage is not the
  // current page - including when it has never been built.
  bool confirmLeaveRecipePage();

  ApplicationContext* m_context;
  QStackedWidget* m_pageStack;
  TopBar* m_topBar;
  LogStatusBar* m_logStatusBar;
  QWidget* m_previousPage;  // Track page before opening log viewer

  // Page pointers. All null until ensurePageBuilt() creates them; only
  // m_mainMenuPage is built eagerly, by createPages().
  MainMenuPage* m_mainMenuPage = nullptr;
  HardwareInstrumentPage* m_hardwarePage = nullptr;
  ConfigEditorPage* m_configEditorPage = nullptr;
  LogViewerPage* m_logViewerPage = nullptr;
  MotionPage* m_motionPage = nullptr;
  RecipePage* m_recipePage = nullptr;
  RunProgramPage* m_runProgramPage = nullptr;
  VisionManagerPage* m_visionManagerPage = nullptr;  // Vision Manager page
  SettingsContainerPage* m_settingsContainerPage = nullptr;  // Settings container (App Settings + IO Watch)
  TrainerContainerPage* m_trainerContainerPage = nullptr;    // Trainer container page
  DataBackupPage* m_dataBackupPage = nullptr;
  ScriptRunnerPage* m_scriptRunnerPage = nullptr;

  JogPanelWindow* m_jogPanel;
  JogKeyBindingHandler* m_jogKeyHandler;
  TransformationMatrixLoader* m_matrixLoader;

  // ✅ NEW: Keyboard shortcuts for page navigation
  QShortcut* m_shortcutF1;  // Run Program Page
  QShortcut* m_shortcutF2;  // Recipe Page
  QShortcut* m_shortcutF3;  // Motion Page
  QShortcut* m_shortcutF4;  // Hardware Page
  QShortcut* m_shortcutF5;  // Config Editor Page
  QShortcut* m_shortcutF6;  // Reserved
  QShortcut* m_shortcutF7;  // Reserved
  QShortcut* m_shortcutF8;  // Reserved
  QShortcut* m_shortcutF9;  // Script Runner Page

  // ✅ NEW: Keyboard shortcuts for window toggles
  QShortcut* m_shortcutCtrlJ;  // Toggle Jog Panel
  QShortcut* m_shortcutCtrlK;  // Toggle Aux Window
  QShortcut* m_shortcutCtrlR;  // Toggle Realtime Camera Monitor
  QShortcut* m_shortcutCtrlT;  // Toggle Realtime Camera Monitor 2
  QShortcut* m_shortcutCtrlShiftS;  // Capture state snapshot

  bool m_scriptRunning = false;  // true while a Lua script is executing
};
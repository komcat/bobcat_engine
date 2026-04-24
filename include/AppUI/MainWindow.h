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

private:
  void setupUI();
  void createPages();
  void setupLogging();
  void setupJogPanel();
  void setupKeyboardShortcuts();  // ✅ NEW: Setup F1-F8 shortcuts
  void saveWindowSettings();      // Save window geometry to settings
  void restoreWindowSettings();   // Restore window geometry from settings

  // Returns true if page navigation is currently allowed.
  // When a Lua script is running, the user must stay on ScriptRunnerPage.
  bool canNavigateAwayFromCurrentPage();

  ApplicationContext* m_context;
  QStackedWidget* m_pageStack;
  TopBar* m_topBar;
  LogStatusBar* m_logStatusBar;
  LogViewerPage* m_logViewerPage;
  QWidget* m_previousPage;  // Track page before opening log viewer

  MainMenuPage* m_mainMenuPage;
  HardwareInstrumentPage* m_hardwarePage;
  ConfigEditorPage* m_configEditorPage;
  MotionPage* m_motionPage;
  RecipePage* m_recipePage;      // ✅ ADD THIS
  RunProgramPage* m_runProgramPage;
  VisionManagerPage* m_visionManagerPage;  // Vision Manager page
  SettingsContainerPage* m_settingsContainerPage;      // Settings container (holds App Settings + IO Watch)
  TrainerContainerPage* m_trainerContainerPage;  // Trainer container page
  DataBackupPage* m_dataBackupPage;
  ScriptRunnerPage* m_scriptRunnerPage;

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

  bool m_scriptRunning = false;  // true while a Lua script is executing
};
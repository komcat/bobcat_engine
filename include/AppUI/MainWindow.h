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
  void onBackToMainMenu();
  void onOpenLogViewer();
  void onToggleJogPanel();
  void onToggleAuxWindow();  // ✅ NEW: Toggle aux window visibility

private:
  void setupUI();
  void createPages();
  void setupLogging();
  void setupJogPanel();
  void setupKeyboardShortcuts();  // ✅ NEW: Setup F1-F8 shortcuts
  void saveWindowSettings();      // Save window geometry to settings
  void restoreWindowSettings();   // Restore window geometry from settings

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

  // ✅ NEW: Keyboard shortcuts for window toggles
  QShortcut* m_shortcutCtrlJ;  // Toggle Jog Panel
  QShortcut* m_shortcutCtrlK;  // Toggle Aux Window
};
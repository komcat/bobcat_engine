// MainWindow.h
#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>  // ADD THIS

struct ApplicationContext;
class MainMenuPage;
class HardwareInstrumentPage;
class ConfigEditorPage;       // NEW
class LogStatusBar;
class LogViewerPage;
class JogPanelWindow;  // ADD THIS
class JogKeyBindingHandler;  // ADD THIS
class TransformationMatrixLoader;  // ADD THIS

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(ApplicationContext* context, QWidget* parent = nullptr);
  ~MainWindow() = default;

private slots:
  void onMainMenuProcess();
  void onMainMenuRecipe();
  void onMainMenuMotion();
  void onMainMenuHardware();
  void onMainMenuEditConfig();      // NEW
  void onMainMenuFutureUpdate();    // NEW
  void onBackToMainMenu();
  void onOpenLogViewer();
  void onToggleJogPanel();  // ADD THIS


private:
  void setupUI();
  void createPages();
  void setupLogging();
  void setupJogPanel();  // ADD THIS

  ApplicationContext* m_context;
  QStackedWidget* m_pageStack;
  LogStatusBar* m_logStatusBar;
  LogViewerPage* m_logViewerPage;

  MainMenuPage* m_mainMenuPage;
  HardwareInstrumentPage* m_hardwarePage;

  ConfigEditorPage* m_configEditorPage;  // NEW

  // ADD THESE:
  QPushButton* m_jogButton;
  JogPanelWindow* m_jogPanel;
  JogKeyBindingHandler* m_jogKeyHandler;
  TransformationMatrixLoader* m_matrixLoader;
};
// MainWindow.h
#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>

struct ApplicationContext;
class MainMenuPage;
class HardwareInstrumentPage;
class ConfigEditorPage;
class MotionPage;              // NEW - Add this
class LogStatusBar;
class LogViewerPage;
class JogPanelWindow;
class JogKeyBindingHandler;
class TransformationMatrixLoader;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(ApplicationContext* context, QWidget* parent = nullptr);
  ~MainWindow();  // ✅ ADD THIS

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

private:
  void setupUI();
  void createPages();
  void setupLogging();
  void setupJogPanel();

  ApplicationContext* m_context;
  QStackedWidget* m_pageStack;
  LogStatusBar* m_logStatusBar;
  LogViewerPage* m_logViewerPage;

  MainMenuPage* m_mainMenuPage;
  HardwareInstrumentPage* m_hardwarePage;
  ConfigEditorPage* m_configEditorPage;
  MotionPage* m_motionPage;      // NEW - Add this

  QPushButton* m_jogButton;
  JogPanelWindow* m_jogPanel;
  JogKeyBindingHandler* m_jogKeyHandler;
  TransformationMatrixLoader* m_matrixLoader;
};
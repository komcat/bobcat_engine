// MainWindow.h
#pragma once

#include <QMainWindow>
#include <QStackedWidget>

struct ApplicationContext;
class MainMenuPage;
class HardwareInstrumentPage;
class ConfigEditorPage;       // NEW
class LogStatusBar;
class LogViewerPage;

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


private:
  void setupUI();
  void createPages();
  void setupLogging();

  ApplicationContext* m_context;
  QStackedWidget* m_pageStack;
  LogStatusBar* m_logStatusBar;
  LogViewerPage* m_logViewerPage;

  MainMenuPage* m_mainMenuPage;
  HardwareInstrumentPage* m_hardwarePage;

  ConfigEditorPage* m_configEditorPage;  // NEW
};
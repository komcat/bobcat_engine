// MainWindow.h
#pragma once

#include <QMainWindow>
#include <QStackedWidget>

struct ApplicationContext;
class MainMenuPage;
class HardwareInstrumentPage;
class LogStatusBar;      // ✅ NEW
class LogViewerPage;     // ✅ NEW

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
  void onBackToMainMenu();
  void onOpenLogViewer();  // ✅ NEW

private:
  void setupUI();
  void createPages();
  void setupLogging();     // ✅ NEW

  ApplicationContext* m_context;
  QStackedWidget* m_pageStack;
  LogStatusBar* m_logStatusBar;     // ✅ NEW
  LogViewerPage* m_logViewerPage;   // ✅ NEW

  MainMenuPage* m_mainMenuPage;
  HardwareInstrumentPage* m_hardwarePage;
};
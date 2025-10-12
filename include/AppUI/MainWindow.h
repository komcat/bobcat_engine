// MainWindow.h
#pragma once

#include <QMainWindow>
#include <QStackedWidget>

struct ApplicationContext;
class MainMenuPage;
class HardwareInstrumentPage;

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

private:
  void setupUI();
  void createPages();

  ApplicationContext* m_context;
  QStackedWidget* m_pageStack;

  MainMenuPage* m_mainMenuPage;
  HardwareInstrumentPage* m_hardwarePage;
};
#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <memory>

class MainMenuPage;
class HardwareInstrumentPage;
class PlaceholderPage;  // ← ADD THIS

/**
 * @brief Main window - holds everything
 */
class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow();

private slots:
  void showMainMenu();
  void showProcess();
  void showRecipe();
  void showMotion();
  void showHardwareInstrument();

private:
  void setupUI();
  void createPages();
  void connectSignals();

  // UI Components
  QStackedWidget* m_stackedWidget;

  // Pages
  MainMenuPage* m_mainMenuPage;
  HardwareInstrumentPage* m_hardwarePage;
  PlaceholderPage* m_processPage;     // ← ADD
  PlaceholderPage* m_recipePage;      // ← ADD
  PlaceholderPage* m_motionPage;      // ← ADD

  // Page indices
  enum PageIndex {
    PAGE_MAIN_MENU = 0,
    PAGE_PROCESS = 1,
    PAGE_RECIPE = 2,
    PAGE_MOTION = 3,
    PAGE_HARDWARE = 4
  };
};
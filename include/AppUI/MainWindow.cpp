// MainWindow.cpp
#include "MainWindow.h"
#include "MainMenuPage.h"
#include "HardwareInstrumentPage.h"
#include "PlaceholderPage.h"  // ← ADD THIS
#include <QVBoxLayout>
#include <QLabel>
#include <iostream>

MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent)
  , m_stackedWidget(nullptr)
  , m_mainMenuPage(nullptr)
  , m_hardwarePage(nullptr)
{
  setupUI();
  createPages();
  connectSignals();

  // Start at main menu
  showMainMenu();

  std::cout << "[MainWindow] Created" << std::endl;
}

MainWindow::~MainWindow()
{
  std::cout << "[MainWindow] Destroyed" << std::endl;
}

void MainWindow::setupUI()
{
  setWindowTitle("Bobcat Engine");
  resize(1200, 800);  // ← CHANGED from 1400x900
  setMinimumSize(800, 600);  // ← ADD THIS - allows resize down to 800x600

  // Create central widget
  QWidget* centralWidget = new QWidget(this);
  QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Create stacked widget for pages
  m_stackedWidget = new QStackedWidget();
  mainLayout->addWidget(m_stackedWidget);

  setCentralWidget(centralWidget);
}

void MainWindow::createPages()
{
  // Create main menu page
  m_mainMenuPage = new MainMenuPage();
  m_stackedWidget->insertWidget(PAGE_MAIN_MENU, m_mainMenuPage);

  // Create placeholder pages with back buttons
  m_processPage = new PlaceholderPage("Process");
  m_stackedWidget->insertWidget(PAGE_PROCESS, m_processPage);

  m_recipePage = new PlaceholderPage("Recipe");
  m_stackedWidget->insertWidget(PAGE_RECIPE, m_recipePage);

  m_motionPage = new PlaceholderPage("Motion");
  m_stackedWidget->insertWidget(PAGE_MOTION, m_motionPage);

  // Create hardware & instrument page
  m_hardwarePage = new HardwareInstrumentPage();
  m_stackedWidget->insertWidget(PAGE_HARDWARE, m_hardwarePage);

  std::cout << "[MainWindow] All pages created" << std::endl;
}

void MainWindow::connectSignals()
{
  // Connect main menu buttons to page navigation
  connect(m_mainMenuPage, &MainMenuPage::processClicked,
    this, &MainWindow::showProcess);
  connect(m_mainMenuPage, &MainMenuPage::recipeClicked,
    this, &MainWindow::showRecipe);
  connect(m_mainMenuPage, &MainMenuPage::motionClicked,
    this, &MainWindow::showMotion);
  connect(m_mainMenuPage, &MainMenuPage::hardwareClicked,
    this, &MainWindow::showHardwareInstrument);

  // Connect back buttons from all pages
  connect(m_processPage, &PlaceholderPage::backClicked,
    this, &MainWindow::showMainMenu);
  connect(m_recipePage, &PlaceholderPage::backClicked,
    this, &MainWindow::showMainMenu);
  connect(m_motionPage, &PlaceholderPage::backClicked,
    this, &MainWindow::showMainMenu);
  connect(m_hardwarePage, &HardwareInstrumentPage::backToMainMenu,
    this, &MainWindow::showMainMenu);
}


void MainWindow::showMainMenu()
{
  m_stackedWidget->setCurrentIndex(PAGE_MAIN_MENU);
  std::cout << "[MainWindow] Showing main menu" << std::endl;
}

void MainWindow::showProcess()
{
  m_stackedWidget->setCurrentIndex(PAGE_PROCESS);
  std::cout << "[MainWindow] Showing process page" << std::endl;
}

void MainWindow::showRecipe()
{
  m_stackedWidget->setCurrentIndex(PAGE_RECIPE);
  std::cout << "[MainWindow] Showing recipe page" << std::endl;
}

void MainWindow::showMotion()
{
  m_stackedWidget->setCurrentIndex(PAGE_MOTION);
  std::cout << "[MainWindow] Showing motion page" << std::endl;
}

void MainWindow::showHardwareInstrument()
{
  m_stackedWidget->setCurrentIndex(PAGE_HARDWARE);
  std::cout << "[MainWindow] Showing hardware & instrument page" << std::endl;
}
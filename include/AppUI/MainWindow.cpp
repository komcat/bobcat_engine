// MainWindow.cpp
#include "MainWindow.h"
#include "ApplicationContext.h"
#include "MainMenuPage.h"
#include "HardwareInstrumentPage.h"
#include <iostream>

MainWindow::MainWindow(ApplicationContext* context, QWidget* parent)
  : QMainWindow(parent)
  , m_context(context)
  , m_pageStack(nullptr)
  , m_mainMenuPage(nullptr)
  , m_hardwarePage(nullptr)
{
  setupUI();
  createPages();

  // Show main menu first
  m_pageStack->setCurrentWidget(m_mainMenuPage);
}

void MainWindow::setupUI()
{
  m_pageStack = new QStackedWidget(this);
  setCentralWidget(m_pageStack);

  resize(1024, 768);
}

void MainWindow::createPages()
{
  std::cout << "[MainWindow] Creating pages..." << std::endl;

  // Main menu page
  m_mainMenuPage = new MainMenuPage(this);
  connect(m_mainMenuPage, &MainMenuPage::processClicked,
    this, &MainWindow::onMainMenuProcess);
  connect(m_mainMenuPage, &MainMenuPage::recipeClicked,
    this, &MainWindow::onMainMenuRecipe);
  connect(m_mainMenuPage, &MainMenuPage::motionClicked,
    this, &MainWindow::onMainMenuMotion);
  connect(m_mainMenuPage, &MainMenuPage::hardwareClicked,
    this, &MainWindow::onMainMenuHardware);
  m_pageStack->addWidget(m_mainMenuPage);

  // Hardware & Instrument page (pass context)
  m_hardwarePage = new HardwareInstrumentPage(m_context, this);
  connect(m_hardwarePage, &HardwareInstrumentPage::backToMainMenu,
    this, &MainWindow::onBackToMainMenu);
  m_pageStack->addWidget(m_hardwarePage);

  std::cout << "[MainWindow] Pages created" << std::endl;
}

void MainWindow::onMainMenuProcess()
{
  std::cout << "[MainWindow] Process clicked (not implemented)" << std::endl;
}

void MainWindow::onMainMenuRecipe()
{
  std::cout << "[MainWindow] Recipe clicked (not implemented)" << std::endl;
}

void MainWindow::onMainMenuMotion()
{
  std::cout << "[MainWindow] Motion clicked (not implemented)" << std::endl;
}

void MainWindow::onMainMenuHardware()
{
  std::cout << "[MainWindow] Hardware clicked" << std::endl;
  m_pageStack->setCurrentWidget(m_hardwarePage);
}

void MainWindow::onBackToMainMenu()
{
  std::cout << "[MainWindow] Back to main menu" << std::endl;
  m_pageStack->setCurrentWidget(m_mainMenuPage);
}
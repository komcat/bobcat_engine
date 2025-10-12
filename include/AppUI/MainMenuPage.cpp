// MainMenuPage.cpp
#include "MainMenuPage.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>

MainMenuPage::MainMenuPage(QWidget* parent)
  : QWidget(parent)
{
  setupUI();
}

void MainMenuPage::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(50, 50, 50, 50);
  mainLayout->setSpacing(30);

  // Title
  QLabel* titleLabel = new QLabel("Bobcat Engine");
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 36px; "
    "  font-weight: bold; "
    "  color: #ffffff; "
    "  padding: 20px; "
    "}");
  mainLayout->addWidget(titleLabel);

  // Add stretch to center buttons vertically
  mainLayout->addStretch();

  // Button grid (2x2)
  QGridLayout* buttonGrid = new QGridLayout();
  buttonGrid->setSpacing(30);

  // Create buttons
  m_processButton = createMenuButton("Process");
  m_recipeButton = createMenuButton("Recipe");
  m_motionButton = createMenuButton("Motion");
  m_hardwareButton = createMenuButton("Hardware &\nInstrument");

  // Add to grid
  buttonGrid->addWidget(m_processButton, 0, 0);
  buttonGrid->addWidget(m_recipeButton, 0, 1);
  buttonGrid->addWidget(m_motionButton, 1, 0);
  buttonGrid->addWidget(m_hardwareButton, 1, 1);

  mainLayout->addLayout(buttonGrid);
  mainLayout->addStretch();

  // Connect signals
  connect(m_processButton, &QPushButton::clicked, this, &MainMenuPage::processClicked);
  connect(m_recipeButton, &QPushButton::clicked, this, &MainMenuPage::recipeClicked);
  connect(m_motionButton, &QPushButton::clicked, this, &MainMenuPage::motionClicked);
  connect(m_hardwareButton, &QPushButton::clicked, this, &MainMenuPage::hardwareClicked);

  // Set dark background
  setStyleSheet("QWidget { background-color: #2b2b2b; }");
}

QPushButton* MainMenuPage::createMenuButton(const QString& text, const QString& icon)
{
  QPushButton* button = new QPushButton(text);
  button->setMinimumSize(200, 150);  // ← CHANGED from 300x200
  button->setMaximumSize(600, 300);  // ← CHANGED from 400x250 (more flexible)
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);  // ← ADD THIS

  button->setStyleSheet(
    "QPushButton { "
    "  background-color: #3a3a3a; "
    "  color: white; "
    "  border: 2px solid #555555; "
    "  border-radius: 10px; "
    "  font-size: 24px; "
    "  font-weight: bold; "
    "  padding: 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4a; "
    "  border-color: #00aaff; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2a2a2a; "
    "}");

  return button;
}
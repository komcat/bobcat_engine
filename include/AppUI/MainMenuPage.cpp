// MainMenuPage.cpp
#include "MainMenuPage.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QIcon>

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

  // Create buttons WITH ICONS
  m_processButton = createMenuButton("Process", "assets/icons/process.png");
  m_recipeButton = createMenuButton("Recipe", "assets/icons/recipe.png");
  m_motionButton = createMenuButton("Motion", "assets/icons/motion.png");
  m_hardwareButton = createMenuButton("Hardware &\nInstrument", "assets/icons/hardware.png");

  // Add to grid
  buttonGrid->addWidget(m_processButton, 0, 0);
  buttonGrid->addWidget(m_recipeButton, 0, 1);
  buttonGrid->addWidget(m_motionButton, 1, 0);
  buttonGrid->addWidget(m_hardwareButton, 1, 1);

  mainLayout->addLayout(buttonGrid);
  mainLayout->addStretch();

  // Connect signals (QToolButton uses clicked() just like QPushButton)
  connect(m_processButton, &QToolButton::clicked, this, &MainMenuPage::processClicked);
  connect(m_recipeButton, &QToolButton::clicked, this, &MainMenuPage::recipeClicked);
  connect(m_motionButton, &QToolButton::clicked, this, &MainMenuPage::motionClicked);
  connect(m_hardwareButton, &QToolButton::clicked, this, &MainMenuPage::hardwareClicked);

  // Set dark background
  setStyleSheet("QWidget { background-color: #2b2b2b; }");
}

QToolButton* MainMenuPage::createMenuButton(const QString& text, const QString& iconPath)
{
  QToolButton* button = new QToolButton();
  button->setMinimumSize(200, 180);
  button->setMaximumSize(600, 300);
  button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // Load icon if path provided
  if (!iconPath.isEmpty()) {
    QIcon icon(iconPath);

    // Check if icon loaded successfully
    if (!icon.isNull()) {
      button->setIcon(icon);
      button->setIconSize(QSize(100, 100));  // Icon display size
    }
    else {
      // Icon failed to load - print warning
      qWarning() << "Failed to load icon:" << iconPath;
    }
  }

  // Set text
  button->setText(text);

  // ✅ THIS NOW WORKS with QToolButton!
  button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

  // Style for QToolButton
  button->setStyleSheet(
    "QToolButton { "
    "  background-color: #3a3a3a; "
    "  color: white; "
    "  border: 2px solid #555555; "
    "  border-radius: 10px; "
    "  font-size: 18px; "
    "  font-weight: bold; "
    "  padding: 15px; "
    "} "
    "QToolButton:hover { "
    "  background-color: #4a4a4a; "
    "  border-color: #00aaff; "
    "} "
    "QToolButton:pressed { "
    "  background-color: #2a2a2a; "
    "}");

  return button;
}
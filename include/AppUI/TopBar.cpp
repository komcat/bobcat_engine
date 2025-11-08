// TopBar.cpp
#include "TopBar.h"
#include "ApplicationContext.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFont>
#include <iostream>

TopBar::TopBar(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_productNameLabel(nullptr)
  , m_workingPathLabel(nullptr)
  , m_jogButton(nullptr)
  , m_auxWindowButton(nullptr)
{
  setupUI();
  std::cout << "[TopBar] Created" << std::endl;
}

TopBar::~TopBar()
{
  std::cout << "[TopBar] Destroyed" << std::endl;
}

void TopBar::setupUI()
{
  // Set fixed height (reduced from 100 to 50)
  setFixedHeight(50);

  // Dark background styling
  setStyleSheet(
    "TopBar { "
    "  background-color: #1a1a1a; "
    "  border-bottom: 2px solid #ff6600; "
    "}"
  );

  // Create horizontal layout
  QHBoxLayout* layout = new QHBoxLayout(this);
  layout->setContentsMargins(15, 5, 15, 5);
  layout->setSpacing(15);

  // Left side: Product info (vertical layout for name and path)
  QVBoxLayout* productInfoLayout = new QVBoxLayout();
  productInfoLayout->setSpacing(2);
  productInfoLayout->setContentsMargins(0, 0, 0, 0);

  // Product name label (top, bold, orange)
  m_productNameLabel = new QLabel("Product: None", this);
  QFont productFont;
  productFont.setPointSize(14);
  productFont.setBold(true);
  m_productNameLabel->setFont(productFont);
  m_productNameLabel->setStyleSheet(
    "QLabel { "
    "  color: #ff6600; "
    "  background: transparent; "
    "  border: none; "
    "}"
  );
  productInfoLayout->addWidget(m_productNameLabel);

  // Working path label (bottom, smaller, gray)
  m_workingPathLabel = new QLabel("", this);
  QFont pathFont;
  pathFont.setPointSize(9);
  m_workingPathLabel->setFont(pathFont);
  m_workingPathLabel->setStyleSheet(
    "QLabel { "
    "  color: #888888; "
    "  background: transparent; "
    "  border: none; "
    "}"
  );
  productInfoLayout->addWidget(m_workingPathLabel);

  layout->addLayout(productInfoLayout);

  // Spacer to push buttons to the right
  layout->addStretch();

  // Aux Window button (optional, can be enabled if needed)
  m_auxWindowButton = new QPushButton("AUX", this);
  m_auxWindowButton->setFixedSize(70, 35);
  m_auxWindowButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5dade2, stop:1 #3498db); "
    "  color: white; "
    "  border-radius: 5px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6dbdf2, stop:1 #5dade2); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2874a6; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 2px; "
    "  padding-left: 2px; "
    "}"
  );
  connect(m_auxWindowButton, &QPushButton::clicked, this, &TopBar::auxWindowButtonClicked);
  layout->addWidget(m_auxWindowButton);

  // JOG button (right side, orange)
  m_jogButton = new QPushButton("JOG", this);
  m_jogButton->setFixedSize(70, 35);
  m_jogButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ff9d2e, stop:1 #ff8c00); "
    "  color: white; "
    "  border-radius: 5px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffad3e, stop:1 #ff9d2e); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #e67e00; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 2px; "
    "  padding-left: 2px; "
    "}"
  );
  connect(m_jogButton, &QPushButton::clicked, this, &TopBar::jogButtonClicked);
  layout->addWidget(m_jogButton);

  setLayout(layout);
}

void TopBar::setProductInfo(const QString& productName, const QString& workingPath)
{
  if (productName.isEmpty()) {
    m_productNameLabel->setText("Product: None");
  } else {
    m_productNameLabel->setText("Product: " + productName);
  }

  if (workingPath.isEmpty()) {
    m_workingPathLabel->setText("");
  } else {
    m_workingPathLabel->setText("Path: " + workingPath);
  }

  std::cout << "[TopBar] Product info updated - Name: " << productName.toStdString()
            << ", Path: " << workingPath.toStdString() << std::endl;
}

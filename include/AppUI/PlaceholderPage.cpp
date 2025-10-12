// PlaceholderPage.cpp
#include "PlaceholderPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

PlaceholderPage::PlaceholderPage(const QString& title, QWidget* parent)
  : QWidget(parent)
  , m_title(title)
{
  setupUI();
}

void PlaceholderPage::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Top bar with back button
  QWidget* topBar = new QWidget();
  topBar->setStyleSheet("QWidget { background-color: #333333; }");
  topBar->setFixedHeight(60);

  QHBoxLayout* topBarLayout = new QHBoxLayout(topBar);
  topBarLayout->setContentsMargins(10, 10, 10, 10);

  // Back button
  m_backButton = new QPushButton("← Back to Main Menu");
  m_backButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #555555; "
    "  color: white; "
    "  border: none; "
    "  padding: 10px 20px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #666666; "
    "}");
  connect(m_backButton, &QPushButton::clicked, this, &PlaceholderPage::backClicked);
  topBarLayout->addWidget(m_backButton);
  topBarLayout->addStretch();

  mainLayout->addWidget(topBar);

  // Content area
  QWidget* contentArea = new QWidget();
  contentArea->setStyleSheet("QWidget { background-color: #2b2b2b; }");

  QVBoxLayout* contentLayout = new QVBoxLayout(contentArea);
  contentLayout->setAlignment(Qt::AlignCenter);

  // Title label
  m_titleLabel = new QLabel(m_title + "\n(Coming Soon)");
  m_titleLabel->setAlignment(Qt::AlignCenter);
  m_titleLabel->setStyleSheet(
    "QLabel { "
    "  font-size: 32px; "
    "  color: #888888; "
    "  padding: 50px; "
    "}");
  contentLayout->addWidget(m_titleLabel);

  mainLayout->addWidget(contentArea);
}
// UserPromptWidget.cpp
#include "UserPromptWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

UserPromptWidget::UserPromptWidget(QWidget* parent)
  : QWidget(parent)
{
  setupUI();
}

void UserPromptWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create centered container with max width
  QWidget* container = new QWidget();
  container->setMaximumWidth(600);
  container->setStyleSheet(
    "QWidget { "
    "  background-color: #1e1e1e; "  // ✅ Dark background
    "  border: 2px solid #0e639c; "  // ✅ Blue border for dark theme
    "  border-radius: 8px; "
    "}");

  QVBoxLayout* containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(30, 30, 30, 30);
  containerLayout->setSpacing(20);

  // Title
  m_titleLabel = new QLabel("Confirmation Required");
  m_titleLabel->setStyleSheet(
    "font-size: 20px; "
    "font-weight: bold; "
    "color: #0e639c; "  // ✅ Blue for dark theme
    "background: transparent; "
    "border: none;");
  m_titleLabel->setWordWrap(true);
  containerLayout->addWidget(m_titleLabel);

  // Message
  m_messageLabel = new QLabel("Please confirm to continue...");
  m_messageLabel->setStyleSheet(
    "font-size: 16px; "
    "background: transparent; "
    "border: none; "
    "padding: 10px 0px;");  // ✅ Color from global theme
  m_messageLabel->setWordWrap(true);
  m_messageLabel->setMinimumHeight(60);
  containerLayout->addWidget(m_messageLabel);

  containerLayout->addSpacing(10);

  // Button layout
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(15);

  // Yes button
  m_yesButton = new QPushButton("✓ Yes");
  m_yesButton->setObjectName("yesButton");  // ✅ Green button
  m_yesButton->setMinimumHeight(50);
  m_yesButton->setMinimumWidth(120);
  m_yesButton->setStyleSheet(
    "QPushButton#yesButton { "
    "  background-color: #27ae60; "
    "  font-size: 16px; "
    "  padding: 10px 20px; "
    "} "
    "QPushButton#yesButton:hover { "
    "  background-color: #2ecc71; "
    "}");

  // No button
  m_noButton = new QPushButton("✗ No");
  m_noButton->setObjectName("noButton");  // ✅ Red button
  m_noButton->setMinimumHeight(50);
  m_noButton->setMinimumWidth(120);
  m_noButton->setStyleSheet(
    "QPushButton#noButton { "
    "  background-color: #e74c3c; "
    "  font-size: 16px; "
    "  padding: 10px 20px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #da190b; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #c41c0c; "
    "}");

  buttonLayout->addStretch();
  buttonLayout->addWidget(m_yesButton);
  buttonLayout->addWidget(m_noButton);
  buttonLayout->addStretch();

  containerLayout->addLayout(buttonLayout);

  // Center the container in the main layout
  mainLayout->addStretch();
  QHBoxLayout* centerLayout = new QHBoxLayout();
  centerLayout->addStretch();
  centerLayout->addWidget(container);
  centerLayout->addStretch();
  mainLayout->addLayout(centerLayout);
  mainLayout->addStretch();

  // Connect signals
  connect(m_yesButton, &QPushButton::clicked, this, &UserPromptWidget::onYesClicked);
  connect(m_noButton, &QPushButton::clicked, this, &UserPromptWidget::onNoClicked);

  // Start hidden
  hide();
}

void UserPromptWidget::showPrompt(const QString& title, const QString& message)
{
  m_titleLabel->setText(title);
  m_messageLabel->setText(message);
  show();

  // Set focus to Yes button for keyboard navigation
  m_yesButton->setFocus();
}

void UserPromptWidget::hidePrompt()
{
  hide();
}

void UserPromptWidget::onYesClicked()
{
  emit responseReceived(true);
  hidePrompt();
}

void UserPromptWidget::onNoClicked()
{
  emit responseReceived(false);
  hidePrompt();
}
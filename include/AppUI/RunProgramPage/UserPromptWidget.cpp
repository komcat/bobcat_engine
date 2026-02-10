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

void UserPromptWidget::setAlignment(Alignment align)
{
  if (m_alignment == align) return;
  m_alignment = align;
  rebuildLayout();
}

void UserPromptWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create container with max width
  m_container = new QWidget();
  m_container->setMaximumWidth(600);
  m_container->setStyleSheet(
    "QWidget { "
    "  background-color: #1e1e1e; "
    "  border: 2px solid #0e639c; "
    "  border-radius: 8px; "
    "}");

  QVBoxLayout* containerLayout = new QVBoxLayout(m_container);
  containerLayout->setContentsMargins(30, 30, 30, 30);
  containerLayout->setSpacing(20);

  // Title
  m_titleLabel = new QLabel("Confirmation Required");
  m_titleLabel->setStyleSheet(
    "font-size: 20px; "
    "font-weight: bold; "
    "color: #0e639c; "
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
    "padding: 10px 0px;");
  m_messageLabel->setWordWrap(true);
  m_messageLabel->setMinimumHeight(60);
  containerLayout->addWidget(m_messageLabel);

  containerLayout->addSpacing(10);

  // Button layout
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(15);

  // Yes button
  m_yesButton = new QPushButton("\xe2\x9c\x93 Yes");
  m_yesButton->setObjectName("yesButton");
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
  m_noButton = new QPushButton("\xe2\x9c\x97 No");
  m_noButton->setObjectName("noButton");
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

  // Position the container according to alignment
  rebuildLayout();

  // Connect signals
  connect(m_yesButton, &QPushButton::clicked, this, &UserPromptWidget::onYesClicked);
  connect(m_noButton, &QPushButton::clicked, this, &UserPromptWidget::onNoClicked);

  // Start hidden
  hide();
}

void UserPromptWidget::rebuildLayout()
{
  QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
  if (!mainLayout || !m_container) return;

  // Remove all items from main layout (stretches + sub-layouts)
  while (mainLayout->count() > 0) {
    QLayoutItem* item = mainLayout->takeAt(0);
    if (item->layout()) {
      // Sub-layout: remove child items first (but don't delete m_container)
      while (item->layout()->count() > 0) {
        QLayoutItem* sub = item->layout()->takeAt(0);
        if (!sub->widget()) {
          delete sub;  // delete spacers only
        }
      }
      // item IS the layout (QLayout inherits QLayoutItem), single delete
      delete item;
    } else if (item->widget() == m_container) {
      // Don't delete the container widget, just the layout item wrapper
      delete item;
    } else {
      // Spacer items
      delete item;
    }
  }

  if (m_alignment == Alignment::TopLeft) {
    // Top-left: container at top-left, stretch fills remaining space
    QHBoxLayout* rowLayout = new QHBoxLayout();
    rowLayout->addWidget(m_container);
    rowLayout->addStretch();
    mainLayout->addLayout(rowLayout);
    mainLayout->addStretch();
    mainLayout->setContentsMargins(10, 10, 0, 0);
  } else {
    // Center (default): stretches on all sides
    mainLayout->addStretch();
    QHBoxLayout* centerLayout = new QHBoxLayout();
    centerLayout->addStretch();
    centerLayout->addWidget(m_container);
    centerLayout->addStretch();
    mainLayout->addLayout(centerLayout);
    mainLayout->addStretch();
    mainLayout->setContentsMargins(0, 0, 0, 0);
  }
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

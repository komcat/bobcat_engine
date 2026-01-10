// UserInputWidget.cpp
#include "UserInputWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QDateTime>

UserInputWidget::UserInputWidget(QWidget* parent)
  : QWidget(parent)
{
  setupUI();
}

void UserInputWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create centered container with max width
  QWidget* container = new QWidget();
  container->setMaximumWidth(600);
  container->setStyleSheet(
    "QWidget { "
    "  background-color: #1e1e1e; "  // Dark background
    "  border: 2px solid #0e639c; "  // Blue border for dark theme
    "  border-radius: 8px; "
    "}");

  QVBoxLayout* containerLayout = new QVBoxLayout(container);
  containerLayout->setContentsMargins(30, 30, 30, 30);
  containerLayout->setSpacing(20);

  // Title
  m_titleLabel = new QLabel("Input Required");
  m_titleLabel->setStyleSheet(
    "font-size: 20px; "
    "font-weight: bold; "
    "color: #0e639c; "  // Blue for dark theme
    "background: transparent; "
    "border: none;");
  m_titleLabel->setWordWrap(true);
  containerLayout->addWidget(m_titleLabel);

  // Prompt message
  m_promptLabel = new QLabel("Please enter a value...");
  m_promptLabel->setStyleSheet(
    "font-size: 16px; "
    "background: transparent; "
    "border: none; "
    "padding: 10px 0px;");
  m_promptLabel->setWordWrap(true);
  m_promptLabel->setMinimumHeight(40);
  containerLayout->addWidget(m_promptLabel);

  // Input field
  m_inputField = new QLineEdit();
  m_inputField->setStyleSheet(
    "QLineEdit { "
    "  background-color: #2d2d30; "
    "  border: 1px solid #3e3e42; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "  font-size: 16px; "
    "  color: #cccccc; "
    "} "
    "QLineEdit:focus { "
    "  border: 2px solid #0e639c; "
    "}");
  m_inputField->setMinimumHeight(40);
  containerLayout->addWidget(m_inputField);

  containerLayout->addSpacing(10);

  // Button layout
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(15);

  // OK button
  m_okButton = new QPushButton("✓ OK");
  m_okButton->setObjectName("okButton");
  m_okButton->setMinimumHeight(50);
  m_okButton->setMinimumWidth(120);
  m_okButton->setStyleSheet(
    "QPushButton#okButton { "
    "  background-color: #27ae60; "
    "  font-size: 16px; "
    "  padding: 10px 20px; "
    "} "
    "QPushButton#okButton:hover { "
    "  background-color: #2ecc71; "
    "}");

  // Cancel button
  m_cancelButton = new QPushButton("✗ Cancel");
  m_cancelButton->setObjectName("cancelButton");
  m_cancelButton->setMinimumHeight(50);
  m_cancelButton->setMinimumWidth(120);
  m_cancelButton->setStyleSheet(
    "QPushButton#cancelButton { "
    "  background-color: #e74c3c; "
    "  font-size: 16px; "
    "  padding: 10px 20px; "
    "} "
    "QPushButton#cancelButton:hover { "
    "  background-color: #da190b; "
    "} "
    "QPushButton#cancelButton:pressed { "
    "  background-color: #c41c0c; "
    "}");

  // Use Dummy button - generates TEST_timestamp serial
  m_useDummyButton = new QPushButton("Use Dummy");
  m_useDummyButton->setObjectName("useDummyButton");
  m_useDummyButton->setMinimumHeight(50);
  m_useDummyButton->setMinimumWidth(120);
  m_useDummyButton->setToolTip("Generate a test serial number: TEST_YYYYMMDD_HHMMSS");
  m_useDummyButton->setStyleSheet(
    "QPushButton#useDummyButton { "
    "  background-color: #f39c12; "
    "  color: #1e1e1e; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  padding: 10px 20px; "
    "} "
    "QPushButton#useDummyButton:hover { "
    "  background-color: #f5b041; "
    "} "
    "QPushButton#useDummyButton:pressed { "
    "  background-color: #d68910; "
    "}");

  buttonLayout->addStretch();
  buttonLayout->addWidget(m_okButton);
  buttonLayout->addWidget(m_useDummyButton);
  buttonLayout->addWidget(m_cancelButton);
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
  connect(m_okButton, &QPushButton::clicked, this, &UserInputWidget::onOkClicked);
  connect(m_cancelButton, &QPushButton::clicked, this, &UserInputWidget::onCancelClicked);
  connect(m_useDummyButton, &QPushButton::clicked, this, &UserInputWidget::onUseDummyClicked);

  // Connect Enter key to OK button
  connect(m_inputField, &QLineEdit::returnPressed, this, &UserInputWidget::onOkClicked);

  // Start hidden
  hide();
}

void UserInputWidget::showInput(const QString& title, const QString& prompt, const QString& defaultValue)
{
  m_titleLabel->setText(title);
  m_promptLabel->setText(prompt);
  m_inputField->setText(defaultValue);
  show();

  // Set focus to input field and select all text
  m_inputField->setFocus();
  m_inputField->selectAll();
}

void UserInputWidget::hideInput()
{
  hide();
}

void UserInputWidget::onOkClicked()
{
  emit responseReceived(true, m_inputField->text());
  hideInput();
}

void UserInputWidget::onCancelClicked()
{
  emit responseReceived(false, QString());
  hideInput();
}

void UserInputWidget::onUseDummyClicked()
{
  // Generate dummy serial number: TEST_YYYYMMDD_HHMMSS
  QString dummySerial = QString("TEST_%1")
    .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));

  emit responseReceived(true, dummySerial);
  hideInput();
}

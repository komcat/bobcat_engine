// SafetyAlertDialog.cpp
#include "SafetyAlertDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QApplication>
#include <QStyle>
#include <QScreen>

SafetyAlertDialog::SafetyAlertDialog(const QString& title,
                                      const QString& message,
                                      bool critical,
                                      QWidget* parent)
  : QDialog(parent)
  , m_critical(critical)
{
  setupUI(title, message);
  applyStyle();
}

void SafetyAlertDialog::setupUI(const QString& title, const QString& message)
{
  setWindowTitle(m_critical ? "CRITICAL SAFETY ALERT" : "Safety Warning");
  setModal(true);
  setMinimumWidth(500);
  setMinimumHeight(200);

  // Stay on top of all windows
  setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint | Qt::WindowTitleHint);

  // Main layout
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(20);
  mainLayout->setContentsMargins(30, 30, 30, 30);

  // Header with icon and title
  QHBoxLayout* headerLayout = new QHBoxLayout();

  // Icon
  m_iconLabel = new QLabel(this);
  QStyle::StandardPixmap iconType = m_critical ? QStyle::SP_MessageBoxCritical : QStyle::SP_MessageBoxWarning;
  QPixmap icon = QApplication::style()->standardIcon(iconType).pixmap(48, 48);
  m_iconLabel->setPixmap(icon);
  m_iconLabel->setAlignment(Qt::AlignTop);
  headerLayout->addWidget(m_iconLabel);

  headerLayout->addSpacing(15);

  // Title and message in vertical layout
  QVBoxLayout* textLayout = new QVBoxLayout();

  // Title label
  m_titleLabel = new QLabel(title, this);
  QFont titleFont = m_titleLabel->font();
  titleFont.setPointSize(14);
  titleFont.setBold(true);
  m_titleLabel->setFont(titleFont);
  m_titleLabel->setWordWrap(true);
  textLayout->addWidget(m_titleLabel);

  textLayout->addSpacing(10);

  // Message label
  m_messageLabel = new QLabel(message, this);
  QFont messageFont = m_messageLabel->font();
  messageFont.setPointSize(11);
  m_messageLabel->setFont(messageFont);
  m_messageLabel->setWordWrap(true);
  m_messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  textLayout->addWidget(m_messageLabel);

  headerLayout->addLayout(textLayout, 1);
  mainLayout->addLayout(headerLayout);

  mainLayout->addStretch();

  // Button layout
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  // Acknowledge button
  m_acknowledgeButton = new QPushButton("Acknowledge", this);
  m_acknowledgeButton->setMinimumSize(150, 45);
  m_acknowledgeButton->setDefault(true);
  m_acknowledgeButton->setFocus();

  connect(m_acknowledgeButton, &QPushButton::clicked, this, [this]() {
    emit acknowledged();
    accept();
  });

  buttonLayout->addWidget(m_acknowledgeButton);
  buttonLayout->addStretch();

  mainLayout->addLayout(buttonLayout);

  // Center on screen
  if (QScreen* screen = QApplication::primaryScreen()) {
    QRect screenGeometry = screen->availableGeometry();
    int x = (screenGeometry.width() - width()) / 2;
    int y = (screenGeometry.height() - height()) / 2;
    move(x, y);
  }
}

void SafetyAlertDialog::applyStyle()
{
  if (m_critical) {
    // Critical alert: Red theme
    setStyleSheet(
      "QDialog { "
      "  background-color: #2d1f1f; "
      "  border: 3px solid #e74c3c; "
      "}"
    );

    m_titleLabel->setStyleSheet(
      "QLabel { "
      "  color: #e74c3c; "
      "  background-color: transparent; "
      "}"
    );

    m_messageLabel->setStyleSheet(
      "QLabel { "
      "  color: #ffffff; "
      "  background-color: transparent; "
      "}"
    );

    m_acknowledgeButton->setStyleSheet(
      "QPushButton { "
      "  background-color: #e74c3c; "
      "  color: white; "
      "  border: none; "
      "  border-radius: 6px; "
      "  padding: 12px; "
      "  font-size: 12pt; "
      "  font-weight: bold; "
      "} "
      "QPushButton:hover { "
      "  background-color: #c0392b; "
      "} "
      "QPushButton:pressed { "
      "  background-color: #a93226; "
      "}"
    );
  } else {
    // Warning alert: Yellow/Orange theme
    setStyleSheet(
      "QDialog { "
      "  background-color: #2d2a1f; "
      "  border: 3px solid #f39c12; "
      "}"
    );

    m_titleLabel->setStyleSheet(
      "QLabel { "
      "  color: #f39c12; "
      "  background-color: transparent; "
      "}"
    );

    m_messageLabel->setStyleSheet(
      "QLabel { "
      "  color: #ffffff; "
      "  background-color: transparent; "
      "}"
    );

    m_acknowledgeButton->setStyleSheet(
      "QPushButton { "
      "  background-color: #f39c12; "
      "  color: white; "
      "  border: none; "
      "  border-radius: 6px; "
      "  padding: 12px; "
      "  font-size: 12pt; "
      "  font-weight: bold; "
      "} "
      "QPushButton:hover { "
      "  background-color: #e67e22; "
      "} "
      "QPushButton:pressed { "
      "  background-color: #d35400; "
      "}"
    );
  }
}

bool SafetyAlertDialog::ShowAlert(const QString& title,
                                   const QString& message,
                                   bool critical,
                                   QWidget* parent)
{
  SafetyAlertDialog dialog(title, message, critical, parent);
  return dialog.exec() == QDialog::Accepted;
}

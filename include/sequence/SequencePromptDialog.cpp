// SequencePromptDialog.cpp
#include "SequencePromptDialog.h"
#include <QFont>
#include <QApplication>
#include <QStyle>

SequencePromptDialog::SequencePromptDialog(const QString& title,
  const QString& message,
  bool blocking,
  QWidget* parent)
  : QDialog(parent)
{
  setWindowTitle(title);
  setModal(blocking);  // Only block if in blocking mode
  setMinimumWidth(450);
  setMinimumHeight(150);

  // Set window flags for non-modal dialogs
  if (!blocking) {
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);  // Auto-delete when closed
  }

  // Main layout
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(25);
  mainLayout->setContentsMargins(25, 25, 25, 25);

  // Message label with icon
  QHBoxLayout* messageLayout = new QHBoxLayout();

  // Info icon
  QLabel* iconLabel = new QLabel(this);
  QPixmap icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32);
  iconLabel->setPixmap(icon);
  iconLabel->setAlignment(Qt::AlignTop);
  messageLayout->addWidget(iconLabel);

  // Message text
  m_messageLabel = new QLabel(message, this);
  m_messageLabel->setWordWrap(true);
  QFont font = m_messageLabel->font();
  font.setPointSize(11);
  m_messageLabel->setFont(font);
  m_messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  messageLayout->addWidget(m_messageLabel, 1);

  mainLayout->addLayout(messageLayout);
  mainLayout->addStretch();

  // Button layout
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  // Cancel button
  m_cancelButton = new QPushButton("Cancel", this);
  m_cancelButton->setMinimumSize(100, 40);
  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #f0f0f0; "
    "  border: 1px solid #c0c0c0; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "  font-size: 11pt; "
    "} "
    "QPushButton:hover { "
    "  background-color: #e0e0e0; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #d0d0d0; "
    "}"
  );
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_cancelButton);

  buttonLayout->addSpacing(10);

  // Confirm button (default/highlighted)
  m_confirmButton = new QPushButton("Confirm", this);
  m_confirmButton->setMinimumSize(100, 40);
  m_confirmButton->setDefault(true);
  m_confirmButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #4CAF50; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 8px; "
    "  font-size: 11pt; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #45a049; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #3d8b40; "
    "}"
  );
  connect(m_confirmButton, &QPushButton::clicked, this, &QDialog::accept);
  buttonLayout->addWidget(m_confirmButton);

  mainLayout->addLayout(buttonLayout);

  // Focus on confirm button
  m_confirmButton->setFocus();
}

bool SequencePromptDialog::ShowPrompt(const QString& title,
  const QString& message,
  QWidget* parent)
{
  SequencePromptDialog dialog(title, message, true, parent);  // blocking = true
  return dialog.exec() == QDialog::Accepted;
}

void SequencePromptDialog::ShowNotification(const QString& title,
  const QString& message,
  QWidget* parent)
{
  // Create non-modal dialog that stays on top and auto-deletes
  SequencePromptDialog* dialog = new SequencePromptDialog(title, message, false, parent);
  dialog->show();  // Non-blocking show (not exec)
  // Dialog will auto-delete when closed due to WA_DeleteOnClose attribute
}
// ShutdownConfirmDialog.cpp
#include "ShutdownConfirmDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QApplication>
#include <QStyle>
#include <QScreen>

ShutdownConfirmDialog::ShutdownConfirmDialog(QWidget* parent)
  : QDialog(parent)
{
  setupUI();
  applyStyle();
}

void ShutdownConfirmDialog::setupUI()
{
  setWindowTitle("Confirm Close");
  setModal(true);
  setFixedWidth(480);
  setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);

  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(16);
  mainLayout->setContentsMargins(28, 24, 28, 24);

  // --- Header: icon + title ---
  auto* headerLayout = new QHBoxLayout();

  m_iconLabel = new QLabel(this);
  QPixmap icon = QApplication::style()
    ->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(48, 48);
  m_iconLabel->setPixmap(icon);
  m_iconLabel->setAlignment(Qt::AlignTop);
  headerLayout->addWidget(m_iconLabel);

  headerLayout->addSpacing(14);

  auto* textLayout = new QVBoxLayout();

  m_titleLabel = new QLabel("Close Application?", this);
  QFont titleFont = m_titleLabel->font();
  titleFont.setPointSize(14);
  titleFont.setBold(true);
  m_titleLabel->setFont(titleFont);
  m_titleLabel->setWordWrap(true);
  textLayout->addWidget(m_titleLabel);

  textLayout->addSpacing(6);

  m_messageLabel = new QLabel(
    "Please confirm you have completed the following\n"
    "before shutting down the application:", this);
  QFont msgFont = m_messageLabel->font();
  msgFont.setPointSize(10);
  m_messageLabel->setFont(msgFont);
  m_messageLabel->setWordWrap(true);
  textLayout->addWidget(m_messageLabel);

  headerLayout->addLayout(textLayout, 1);
  mainLayout->addLayout(headerLayout);

  // --- Checklist ---
  auto* checklistLayout = new QVBoxLayout();
  checklistLayout->setContentsMargins(20, 4, 0, 4);
  checklistLayout->setSpacing(10);

  m_saveProgramCheck = new QCheckBox("I have saved my program", this);
  QFont checkFont = m_saveProgramCheck->font();
  checkFont.setPointSize(10);
  m_saveProgramCheck->setFont(checkFont);
  checklistLayout->addWidget(m_saveProgramCheck);

  m_verifyMachineCheck = new QCheckBox(
    "I have verified the machine is in a safe state", this);
  m_verifyMachineCheck->setFont(checkFont);
  checklistLayout->addWidget(m_verifyMachineCheck);

  mainLayout->addLayout(checklistLayout);

  mainLayout->addSpacing(8);

  // --- Buttons ---
  auto* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel", this);
  m_cancelButton->setMinimumSize(120, 40);
  buttonLayout->addWidget(m_cancelButton);

  buttonLayout->addSpacing(10);

  m_closeButton = new QPushButton("Close Application", this);
  m_closeButton->setMinimumSize(160, 40);
  m_closeButton->setEnabled(false);
  buttonLayout->addWidget(m_closeButton);

  mainLayout->addLayout(buttonLayout);

  // --- Connections ---
  connect(m_saveProgramCheck, &QCheckBox::toggled,
          this, &ShutdownConfirmDialog::updateCloseButtonState);
  connect(m_verifyMachineCheck, &QCheckBox::toggled,
          this, &ShutdownConfirmDialog::updateCloseButtonState);

  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_closeButton,  &QPushButton::clicked, this, &QDialog::accept);

  // Center on parent (or screen)
  if (QWidget* p = parentWidget()) {
    QPoint center = p->geometry().center();
    move(center.x() - width() / 2, center.y() - height() / 2);
  } else if (QScreen* screen = QApplication::primaryScreen()) {
    QRect sg = screen->availableGeometry();
    move((sg.width() - width()) / 2, (sg.height() - height()) / 2);
  }
}

void ShutdownConfirmDialog::applyStyle()
{
  // Amber/orange caution theme matching existing safety-warning style
  setStyleSheet(
    "QDialog { "
    "  background-color: #2d2a1f; "
    "  border: 2px solid #f39c12; "
    "}"
  );

  m_titleLabel->setStyleSheet(
    "QLabel { color: #f39c12; background-color: transparent; }"
  );

  m_messageLabel->setStyleSheet(
    "QLabel { color: #cccccc; background-color: transparent; }"
  );

  const QString checkBoxStyle =
    "QCheckBox { color: #ffffff; background-color: transparent; spacing: 8px; }"
    "QCheckBox::indicator { width: 18px; height: 18px; }"
    "QCheckBox::indicator:unchecked { "
    "  border: 2px solid #888888; border-radius: 3px; background-color: #3c3c3c; }"
    "QCheckBox::indicator:checked { "
    "  border: 2px solid #27ae60; border-radius: 3px; background-color: #27ae60; }";

  m_saveProgramCheck->setStyleSheet(checkBoxStyle);
  m_verifyMachineCheck->setStyleSheet(checkBoxStyle);

  m_cancelButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3c3c3c; color: #cccccc; "
    "  border: 1px solid #555555; border-radius: 6px; "
    "  padding: 10px; font-size: 10pt; }"
    "QPushButton:hover { background-color: #4a4a4a; }"
    "QPushButton:pressed { background-color: #333333; }"
  );

  m_closeButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #e74c3c; color: white; "
    "  border: none; border-radius: 6px; "
    "  padding: 10px; font-size: 10pt; font-weight: bold; }"
    "QPushButton:hover { background-color: #c0392b; }"
    "QPushButton:pressed { background-color: #a93226; }"
    "QPushButton:disabled { background-color: #555555; color: #888888; }"
  );
}

void ShutdownConfirmDialog::updateCloseButtonState()
{
  m_closeButton->setEnabled(
    m_saveProgramCheck->isChecked() && m_verifyMachineCheck->isChecked());
}

bool ShutdownConfirmDialog::confirmShutdown(QWidget* parent)
{
  ShutdownConfirmDialog dialog(parent);
  return dialog.exec() == QDialog::Accepted;
}

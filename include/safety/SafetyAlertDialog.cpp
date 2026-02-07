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
  startAudioAlarm();
  startCountdown();
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
    stopAudioAlarm();
    if (m_countdownTimer) m_countdownTimer->stop();
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

void SafetyAlertDialog::startAudioAlarm()
{
  // Immediate beep on creation
  QApplication::beep();
  m_beepCount = 1;

  m_beepTimer = new QTimer(this);
  m_beepTimer->setInterval(BEEP_INTERVAL_MS);
  connect(m_beepTimer, &QTimer::timeout, this, [this]() {
    QApplication::beep();
    ++m_beepCount;
    // Warning: stop after max beeps; Critical: beep until acknowledged/timeout
    if (!m_critical && m_beepCount >= WARNING_BEEP_MAX) {
      m_beepTimer->stop();
    }
  });
  m_beepTimer->start();
}

void SafetyAlertDialog::stopAudioAlarm()
{
  if (m_beepTimer) {
    m_beepTimer->stop();
  }
}

void SafetyAlertDialog::startCountdown()
{
  m_remainingSeconds = m_critical ? CRITICAL_TIMEOUT_S : WARNING_TIMEOUT_S;
  m_acknowledgeButton->setText(QString("Acknowledge (%1s)").arg(m_remainingSeconds));

  m_countdownTimer = new QTimer(this);
  m_countdownTimer->setInterval(1000);
  connect(m_countdownTimer, &QTimer::timeout, this, &SafetyAlertDialog::onCountdownTick);
  m_countdownTimer->start();
}

void SafetyAlertDialog::onCountdownTick()
{
  --m_remainingSeconds;

  if (m_remainingSeconds <= 0) {
    m_countdownTimer->stop();
    stopAudioAlarm();
    emit acknowledged();
    accept();
    return;
  }

  m_acknowledgeButton->setText(QString("Acknowledge (%1s)").arg(m_remainingSeconds));
}

bool SafetyAlertDialog::ShowAlert(const QString& title,
                                   const QString& message,
                                   bool critical,
                                   QWidget* parent)
{
  SafetyAlertDialog dialog(title, message, critical, parent);
  return dialog.exec() == QDialog::Accepted;
}

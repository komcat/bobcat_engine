// ConnectionSplashScreen.cpp
#include "ConnectionSplashScreen.h"
#include <QApplication>
#include <QScreen>
#include <QTimer>

ConnectionSplashScreen::ConnectionSplashScreen(QWidget* parent)
  : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
  , m_totalComponents(5)  // TCP, Camera, Motion, I/O, TEC
  , m_connectedComponents(0)
{
  setupUI();

  // Center on screen
  QScreen* screen = QApplication::primaryScreen();
  QRect screenGeometry = screen->geometry();
  int x = (screenGeometry.width() - width()) / 2;
  int y = (screenGeometry.height() - height()) / 2;
  move(x, y);

  // Show the splash
  show();
  raise();
  activateWindow();
}

void ConnectionSplashScreen::setupUI()
{
  setFixedSize(450, 280);
  setWindowTitle("Connecting Hardware...");

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setSpacing(15);
  layout->setContentsMargins(30, 30, 30, 30);

  // Dark theme styling
  setStyleSheet(
    "QWidget { "
    "  background-color: #2d2d30; "
    "  border: 2px solid #007ACC; "
    "  border-radius: 8px; "
    "}"
  );

  // Title
  m_titleLabel = new QLabel("🔌 Connecting Hardware Components");
  m_titleLabel->setStyleSheet(
    "QLabel { "
    "  color: #ffffff; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  border: none; "
    "  padding: 5px; "
    "}"
  );
  m_titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_titleLabel);

  // Status label (current component)
  m_statusLabel = new QLabel("Initializing...");
  m_statusLabel->setStyleSheet(
    "QLabel { "
    "  color: #d4d4d4; "
    "  font-size: 13px; "
    "  border: none; "
    "  padding: 5px; "
    "}"
  );
  m_statusLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_statusLabel);

  // Progress bar
  m_progressBar = new QProgressBar();
  m_progressBar->setRange(0, 100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);
  m_progressBar->setStyleSheet(
    "QProgressBar { "
    "  border: 1px solid #555555; "
    "  border-radius: 5px; "
    "  background-color: #1e1e1e; "
    "  text-align: center; "
    "  color: #ffffff; "
    "  font-weight: bold; "
    "  height: 25px; "
    "} "
    "QProgressBar::chunk { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
    "    stop:0 #007ACC, stop:1 #00BFFF); "
    "  border-radius: 4px; "
    "}"
  );
  layout->addWidget(m_progressBar);

  // Component list label
  m_componentListLabel = new QLabel("");
  m_componentListLabel->setStyleSheet(
    "QLabel { "
    "  color: #d4d4d4; "
    "  font-size: 11px; "
    "  font-family: 'Courier New', monospace; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 5px; "
    "  background-color: #1e1e1e; "
    "  padding: 10px; "
    "}"
  );
  m_componentListLabel->setWordWrap(true);
  m_componentListLabel->setMinimumHeight(100);
  m_componentListLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  layout->addWidget(m_componentListLabel);

  layout->addStretch();
}

void ConnectionSplashScreen::updateStatus(QString component, QString status)
{
  // Update current status label
  m_statusLabel->setText(QString("Connecting %1...").arg(component));

  // Add to component list
  QString statusIcon;
  if (status == "success") {
    statusIcon = "✓";
    m_connectedComponents++;
  } else if (status == "failed") {
    statusIcon = "✗";
    m_connectedComponents++;
  } else {
    statusIcon = "⏳";
  }

  m_componentStatuses += QString("%1 %2\n").arg(statusIcon).arg(component);
  m_componentListLabel->setText(m_componentStatuses);

  // Update progress
  int progress = (m_connectedComponents * 100) / m_totalComponents;
  setProgress(progress);

  // Force repaint
  QApplication::processEvents();
}

void ConnectionSplashScreen::setProgress(int percent)
{
  m_progressBar->setValue(percent);
  QApplication::processEvents();
}

void ConnectionSplashScreen::onConnectionsComplete()
{
  m_statusLabel->setText("✅ All hardware connected!");
  m_statusLabel->setStyleSheet(
    "QLabel { "
    "  color: #4CAF50; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border: none; "
    "  padding: 5px; "
    "}"
  );

  setProgress(100);

  // Auto-close after 1.5 seconds
  QTimer::singleShot(1500, this, &ConnectionSplashScreen::close);
}

// ConnectionSplashScreen.cpp
#include "ConnectionSplashScreen.h"
#include <QApplication>
#include <QScreen>
#include <QTimer>

ConnectionSplashScreen::ConnectionSplashScreen(QWidget* parent)
  : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
  , m_currentPhase(Phase::Loading)
  , m_totalSteps(0)
  , m_completedSteps(0)
  , m_totalComponents(5)  // Legacy: TCP, Camera, Motion, I/O, TEC
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
  setFixedSize(450, 320);  // Slightly taller for more content
  setWindowTitle("Starting Bobcat Engine...");

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setSpacing(12);
  layout->setContentsMargins(30, 25, 30, 25);

  // Dark theme styling - default blue border
  setStyleSheet(
    "QWidget { "
    "  background-color: #2d2d30; "
    "  border: 2px solid #007ACC; "
    "  border-radius: 8px; "
    "}"
  );

  // Title - will be updated per phase
  m_titleLabel = new QLabel("Initializing...");
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

  // Status label (current step)
  m_statusLabel = new QLabel("Starting...");
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

  // Component/Step list label
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
  m_componentListLabel->setMinimumHeight(120);
  m_componentListLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  layout->addWidget(m_componentListLabel);

  layout->addStretch();
}

// ========== NEW FLEXIBLE PHASE API ==========

void ConnectionSplashScreen::startPhase(Phase phase, const QString& title, int totalSteps)
{
  m_currentPhase = phase;
  m_phaseTitle = title;
  m_totalSteps = totalSteps;
  m_completedSteps = 0;
  m_stepLog.clear();

  // Update UI for new phase
  updatePhaseUI();

  m_titleLabel->setText(QString("%1 %2").arg(getPhaseIcon(phase)).arg(title));
  m_statusLabel->setText("Starting...");
  m_progressBar->setValue(0);
  m_componentListLabel->setText("");

  QApplication::processEvents();
}

void ConnectionSplashScreen::startStep(const QString& stepName)
{
  m_statusLabel->setText(QString("Loading %1...").arg(stepName));

  // Don't add to log yet - wait for completeStep
  QApplication::processEvents();
}

void ConnectionSplashScreen::completeStep(const QString& stepName, bool success)
{
  m_completedSteps++;

  QString icon = success ? "✓" : "✗";
  m_stepLog += QString("%1 %2\n").arg(icon).arg(stepName);
  m_componentListLabel->setText(m_stepLog);

  // Update progress
  if (m_totalSteps > 0) {
    int progress = (m_completedSteps * 100) / m_totalSteps;
    m_progressBar->setValue(progress);
  }

  // Update status to show next step or completion
  if (m_completedSteps >= m_totalSteps) {
    m_statusLabel->setText("Phase complete");
  }

  QApplication::processEvents();
}

void ConnectionSplashScreen::onAllPhasesComplete()
{
  m_currentPhase = Phase::Complete;

  m_titleLabel->setText("✅ Bobcat Engine Ready");
  m_titleLabel->setStyleSheet(
    "QLabel { "
    "  color: #4CAF50; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  border: none; "
    "  padding: 5px; "
    "}"
  );

  m_statusLabel->setText("All systems initialized successfully!");
  m_statusLabel->setStyleSheet(
    "QLabel { "
    "  color: #4CAF50; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border: none; "
    "  padding: 5px; "
    "}"
  );

  m_progressBar->setValue(100);

  // Update border to green
  setStyleSheet(
    "QWidget { "
    "  background-color: #2d2d30; "
    "  border: 2px solid #4CAF50; "
    "  border-radius: 8px; "
    "}"
  );

  QApplication::processEvents();

  // Auto-close after 1.5 seconds
  QTimer::singleShot(1500, this, &ConnectionSplashScreen::close);
}

void ConnectionSplashScreen::updatePhaseUI()
{
  QString borderColor = getPhaseColor(m_currentPhase);
  setStyleSheet(
    QString("QWidget { "
    "  background-color: #2d2d30; "
    "  border: 2px solid %1; "
    "  border-radius: 8px; "
    "}").arg(borderColor)
  );
}

QString ConnectionSplashScreen::getPhaseIcon(Phase phase) const
{
  switch (phase) {
    case Phase::Loading:    return "⚙️";
    case Phase::UICreation: return "🖼️";
    case Phase::Connecting: return "🔌";
    case Phase::Complete:   return "✅";
    default:                return "📋";
  }
}

QString ConnectionSplashScreen::getPhaseColor(Phase phase) const
{
  switch (phase) {
    case Phase::Loading:    return "#FFA500";  // Orange
    case Phase::UICreation: return "#9B59B6";  // Purple
    case Phase::Connecting: return "#007ACC";  // Blue
    case Phase::Complete:   return "#4CAF50";  // Green
    default:                return "#007ACC";
  }
}

// ========== BACKWARD COMPATIBLE API ==========

void ConnectionSplashScreen::updateStatus(QString component, QString status)
{
  // Legacy API - map to new API
  if (status == "connecting") {
    startStep(component);
  } else if (status == "success") {
    completeStep(component, true);
  } else if (status == "failed") {
    completeStep(component, false);
  }

  // Also update legacy tracking
  if (status == "success" || status == "failed") {
    m_connectedComponents++;
  }
}

void ConnectionSplashScreen::setProgress(int percent)
{
  m_progressBar->setValue(percent);
  QApplication::processEvents();
}

void ConnectionSplashScreen::onConnectionsComplete()
{
  // Legacy API - just mark connection phase complete
  // If we're in the last phase, call onAllPhasesComplete
  onAllPhasesComplete();
}

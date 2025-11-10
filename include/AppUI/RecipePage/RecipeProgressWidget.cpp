// RecipeProgressWidget.cpp
#include "RecipeProgressWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>

RecipeProgressWidget::RecipeProgressWidget(QWidget* parent)
  : QWidget(parent)
  , m_totalSteps(0)
  , m_currentStep(0)
  , m_elapsedSeconds(0)
{
  m_elapsedTimer = new QTimer(this);
  connect(m_elapsedTimer, &QTimer::timeout, this, &RecipeProgressWidget::updateElapsedTime);

  setupUI();
}

RecipeProgressWidget::~RecipeProgressWidget()
{
  if (m_elapsedTimer) {
    m_elapsedTimer->stop();
    m_elapsedTimer->deleteLater();
  }
}

void RecipeProgressWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(15, 15, 15, 15);
  mainLayout->setSpacing(15);

  // TITLE
  QLabel* titleLabel = new QLabel("Recipe Execution");
  titleLabel->setStyleSheet(
    "font-weight: bold; "
    "color: #2d3e5f; "
    "padding: 5px;"
  );
  titleLabel->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(titleLabel);

  // Divider
  QFrame* line1 = new QFrame();
  line1->setFrameShape(QFrame::HLine);
  line1->setStyleSheet("color: #ccc;");
  mainLayout->addWidget(line1);

  // PROGRESS BAR
  m_progressBar = new QProgressBar();
  m_progressBar->setMinimum(0);
  m_progressBar->setMaximum(100);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(true);
  m_progressBar->setStyleSheet(
    "QProgressBar { "
    "  border: 2px solid #5a8fd6; "
    "  border-radius: 5px; "
    "  text-align: center; "
    "  font-weight: bold; "
    "  min-height: 30px; "
    "  background-color: white; "
    "} "
    "QProgressBar::chunk { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
    "    stop:0 #4CAF50, stop:1 #45a049); "
    "  border-radius: 3px; "
    "}"
  );
  mainLayout->addWidget(m_progressBar);

  // STEP INFO
  m_stepLabel = new QLabel("Step 0/0");
  m_stepLabel->setStyleSheet(
    "font-weight: bold; "
    "color: #2d3e5f; "
    "padding: 5px;"
  );
  m_stepLabel->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(m_stepLabel);

  m_stepNameLabel = new QLabel("Waiting to start...");
  m_stepNameLabel->setStyleSheet(
    "color: #555; "
    "padding: 8px; "
    "background-color: #f5f5f5; "
    "border: 1px solid #ddd; "
    "border-radius: 4px;"
  );
  m_stepNameLabel->setWordWrap(true);
  m_stepNameLabel->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(m_stepNameLabel);

  // ELAPSED TIME
  m_elapsedTimeLabel = new QLabel("Elapsed: 00:00:00");
  m_elapsedTimeLabel->setStyleSheet(
    "font-weight: bold; "
    "color: #ff8c00; "
    "padding: 8px; "
    "background-color: #fff9e6; "
    "border: 1px solid #ffcc80; "
    "border-radius: 4px;"
  );
  m_elapsedTimeLabel->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(m_elapsedTimeLabel);

  // STATUS TEXT
  m_statusLabel = new QLabel("Initializing...");
  m_statusLabel->setStyleSheet(
    "color: #666; "
    "padding: 8px; "
    "border: 1px solid #ddd; "
    "border-radius: 4px; "
    "background-color: white;"
  );
  m_statusLabel->setWordWrap(true);
  mainLayout->addWidget(m_statusLabel);

  // Divider
  QFrame* line2 = new QFrame();
  line2->setFrameShape(QFrame::HLine);
  line2->setStyleSheet("color: #ccc;");
  mainLayout->addWidget(line2);

  // STOP BUTTON
  m_stopButton = new QPushButton("⏹ Stop Execution");
  m_stopButton->setMinimumHeight(50);
  m_stopButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #f44336; "
    "  color: white; "
    "  border-radius: 5px; "
    "  font-weight: bold; "
    "  padding: 10px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #da190b; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #a91409; "
    "}"
  );
  connect(m_stopButton, &QPushButton::clicked, this, &RecipeProgressWidget::stopRequested);
  mainLayout->addWidget(m_stopButton);

  mainLayout->addStretch();
}

void RecipeProgressWidget::setTotalSteps(int total)
{
  m_totalSteps = total;
  m_currentStep = 0;
  m_progressBar->setValue(0);
  m_stepLabel->setText(QString("Step 0/%1").arg(total));
}

void RecipeProgressWidget::setCurrentStep(int current, const QString& stepName)
{
  m_currentStep = current;

  // Update progress bar
  int percentage = (m_totalSteps > 0) ? (current * 100 / m_totalSteps) : 0;
  m_progressBar->setValue(percentage);

  // Update step label
  m_stepLabel->setText(QString("Step %1/%2").arg(current).arg(m_totalSteps));

  // Update step name
  m_stepNameLabel->setText(stepName);

  // Update status
  m_statusLabel->setText(QString("Executing: %1").arg(stepName));
}

void RecipeProgressWidget::setStatusText(const QString& status)
{
  if (m_statusLabel) {
    m_statusLabel->setText(status);
  }
}

void RecipeProgressWidget::startTimer()
{
  m_startTime = QTime::currentTime();
  m_elapsedSeconds = 0;
  if (m_elapsedTimer) {
    m_elapsedTimer->start(1000);  // Update every second
  }
}

void RecipeProgressWidget::stopTimer()
{
  if (m_elapsedTimer && m_elapsedTimer->isActive()) {
    m_elapsedTimer->stop();
  }
}

void RecipeProgressWidget::resetTimer()
{
  m_elapsedSeconds = 0;
  if (m_elapsedTimeLabel) {
    m_elapsedTimeLabel->setText("Elapsed: 00:00:00");
  }
}

void RecipeProgressWidget::updateElapsedTime()
{
  m_elapsedSeconds++;
  if (m_elapsedTimeLabel) {
    m_elapsedTimeLabel->setText(QString("Elapsed: %1").arg(formatElapsedTime(m_elapsedSeconds)));
  }
}

QString RecipeProgressWidget::formatElapsedTime(int totalSeconds) const
{
  int hours = totalSeconds / 3600;
  int minutes = (totalSeconds % 3600) / 60;
  int seconds = totalSeconds % 60;

  return QString("%1:%2:%3")
    .arg(hours, 2, 10, QChar('0'))
    .arg(minutes, 2, 10, QChar('0'))
    .arg(seconds, 2, 10, QChar('0'));
}

// ✅ NEW: Show completion hint
void RecipeProgressWidget::showCompletionHint()
{
  // Disable stop button since execution is complete
  if (m_stopButton) {
    m_stopButton->setEnabled(false);
    m_stopButton->setText("Execution Complete");
    m_stopButton->setStyleSheet(
      "QPushButton { "
      "  background-color: #888; "
      "  color: white; "
      "  border-radius: 5px; "
      "  font-size: 14px; "
      "  font-weight: bold; "
      "  padding: 10px; "
      "}"
    );
  }
}
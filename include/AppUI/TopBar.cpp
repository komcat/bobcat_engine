// TopBar.cpp
#include "TopBar.h"
#include "ApplicationContext.h"
#include "safety/SafetyWatchManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <iostream>

// ============================================================================
// SafetyIndicatorWidget Implementation
// ============================================================================

SafetyIndicatorWidget::SafetyIndicatorWidget(QWidget* parent)
  : QWidget(parent)
  , m_state(State::Stopped)
  , m_animationTimer(new QTimer(this))
  , m_animationPhase(0)
  , m_blinkOn(true)
{
  setFixedSize(20, 20);
  setToolTip("Safety Watch: Stopped");

  connect(m_animationTimer, &QTimer::timeout, this, &SafetyIndicatorWidget::onAnimationTick);
}

SafetyIndicatorWidget::~SafetyIndicatorWidget()
{
  m_animationTimer->stop();
}

void SafetyIndicatorWidget::setState(State state)
{
  if (m_state == state) return;

  m_state = state;
  m_animationPhase = 0;
  m_blinkOn = true;

  // Configure animation timer based on state
  m_animationTimer->stop();

  switch (state) {
    case State::Stopped:
      setToolTip("Safety Watch: Stopped");
      break;

    case State::Running:
      setToolTip("Safety Watch: Running");
      m_animationTimer->start(50);  // 50ms for smooth pulsing
      break;

    case State::Alert:
      setToolTip("Safety Watch: ALERT TRIGGERED!");
      m_animationTimer->start(250);  // 250ms for blinking
      break;
  }

  update();
}

void SafetyIndicatorWidget::setToolTipText(const QString& text)
{
  setToolTip(text);
}

void SafetyIndicatorWidget::onAnimationTick()
{
  switch (m_state) {
    case State::Running:
      // Cycle phase 0-100 for pulsing effect
      m_animationPhase = (m_animationPhase + 3) % 100;
      break;

    case State::Alert:
      // Toggle blink state
      m_blinkOn = !m_blinkOn;
      break;

    default:
      break;
  }

  update();
}

void SafetyIndicatorWidget::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  QColor fillColor;
  QColor borderColor;

  switch (m_state) {
    case State::Stopped:
      fillColor = QColor(100, 100, 100);  // Gray
      borderColor = QColor(70, 70, 70);
      break;

    case State::Running: {
      // Pulsing green - vary brightness using sine wave
      double phase = m_animationPhase / 100.0 * 2.0 * M_PI;
      int brightness = static_cast<int>(180 + 75 * std::sin(phase));
      fillColor = QColor(0, brightness, 0);
      borderColor = QColor(0, 120, 0);
      break;
    }

    case State::Alert:
      if (m_blinkOn) {
        fillColor = QColor(255, 50, 50);  // Bright red
        borderColor = QColor(200, 0, 0);
      } else {
        fillColor = QColor(80, 20, 20);   // Dark red
        borderColor = QColor(60, 0, 0);
      }
      break;
  }

  // Draw circle with border
  QRectF rect(2, 2, width() - 4, height() - 4);

  // Draw border
  painter.setPen(QPen(borderColor, 2));
  painter.setBrush(fillColor);
  painter.drawEllipse(rect);

  // Add a subtle highlight for 3D effect
  if (m_state != State::Stopped || (m_state == State::Alert && m_blinkOn)) {
    QRadialGradient gradient(rect.center() - QPointF(2, 2), rect.width() / 2);
    gradient.setColorAt(0, QColor(255, 255, 255, 80));
    gradient.setColorAt(1, QColor(255, 255, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(rect);
  }
}

// ============================================================================
// TopBar Implementation
// ============================================================================

TopBar::TopBar(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_productNameLabel(nullptr)
  , m_workingPathLabel(nullptr)
  , m_jogButton(nullptr)
  , m_auxWindowButton(nullptr)
  , m_rtlmButton(nullptr)
  , m_safetyIndicator(nullptr)
{
  setupUI();
  std::cout << "[TopBar] Created" << std::endl;
}

TopBar::~TopBar()
{
  std::cout << "[TopBar] Destroyed" << std::endl;
}

void TopBar::setupUI()
{
  // Set fixed height (reduced from 100 to 50)
  setFixedHeight(50);

  // Dark background styling
  setStyleSheet(
    "TopBar { "
    "  background-color: #1a1a1a; "
    "  border-bottom: 2px solid #ff6600; "
    "}"
  );

  // Create horizontal layout
  QHBoxLayout* layout = new QHBoxLayout(this);
  layout->setContentsMargins(15, 5, 15, 5);
  layout->setSpacing(15);

  // Left side: Product info (vertical layout for name and path)
  QVBoxLayout* productInfoLayout = new QVBoxLayout();
  productInfoLayout->setSpacing(2);
  productInfoLayout->setContentsMargins(0, 0, 0, 0);

  // Product name label (top, bold, orange)
  m_productNameLabel = new QLabel("Product: None", this);
  QFont productFont;
  productFont.setPointSize(14);
  productFont.setBold(true);
  m_productNameLabel->setFont(productFont);
  m_productNameLabel->setStyleSheet(
    "QLabel { "
    "  color: #ff6600; "
    "  background: transparent; "
    "  border: none; "
    "}"
  );
  productInfoLayout->addWidget(m_productNameLabel);

  // Working path label (bottom, smaller, gray)
  m_workingPathLabel = new QLabel("", this);
  QFont pathFont;
  pathFont.setPointSize(9);
  m_workingPathLabel->setFont(pathFont);
  m_workingPathLabel->setStyleSheet(
    "QLabel { "
    "  color: #888888; "
    "  background: transparent; "
    "  border: none; "
    "}"
  );
  productInfoLayout->addWidget(m_workingPathLabel);

  layout->addLayout(productInfoLayout);

  // Spacer to push buttons to the right
  layout->addStretch();

  // Safety indicator (before the buttons)
  m_safetyIndicator = new SafetyIndicatorWidget(this);
  layout->addWidget(m_safetyIndicator);
  layout->addSpacing(10);

  // Aux Window button (optional, can be enabled if needed)
  m_auxWindowButton = new QPushButton("AUX", this);
  m_auxWindowButton->setFixedSize(70, 35);
  m_auxWindowButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5dade2, stop:1 #3498db); "
    "  color: white; "
    "  border-radius: 5px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #6dbdf2, stop:1 #5dade2); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #2874a6; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 2px; "
    "  padding-left: 2px; "
    "}"
  );
  connect(m_auxWindowButton, &QPushButton::clicked, this, &TopBar::auxWindowButtonClicked);
  layout->addWidget(m_auxWindowButton);

  // Rltm button (for GDI Camera Window - realtime view)
  m_rtlmButton = new QPushButton("Rltm", this);
  m_rtlmButton->setFixedSize(70, 35);
  m_rtlmButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #58d68d, stop:1 #27ae60); "
    "  color: white; "
    "  border-radius: 5px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #68e69d, stop:1 #58d68d); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #1e8449; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 2px; "
    "  padding-left: 2px; "
    "}"
  );
  m_rtlmButton->setToolTip("Toggle Realtime Camera Monitor");
  connect(m_rtlmButton, &QPushButton::clicked, this, &TopBar::rtlmButtonClicked);
  layout->addWidget(m_rtlmButton);

  // JOG button (right side, orange)
  m_jogButton = new QPushButton("JOG", this);
  m_jogButton->setFixedSize(70, 35);
  m_jogButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ff9d2e, stop:1 #ff8c00); "
    "  color: white; "
    "  border-radius: 5px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffad3e, stop:1 #ff9d2e); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #e67e00; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 2px; "
    "  padding-left: 2px; "
    "}"
  );
  connect(m_jogButton, &QPushButton::clicked, this, &TopBar::jogButtonClicked);
  layout->addWidget(m_jogButton);

  setLayout(layout);
}

void TopBar::setProductInfo(const QString& productName, const QString& workingPath)
{
  if (productName.isEmpty()) {
    m_productNameLabel->setText("Product: None");
  } else {
    m_productNameLabel->setText("Product: " + productName);
  }

  if (workingPath.isEmpty()) {
    m_workingPathLabel->setText("");
  } else {
    m_workingPathLabel->setText("Path: " + workingPath);
  }

  std::cout << "[TopBar] Product info updated - Name: " << productName.toStdString()
            << ", Path: " << workingPath.toStdString() << std::endl;
}

void TopBar::connectSafetyManager(SafetyWatchManager* manager)
{
  if (!manager || !m_safetyIndicator) {
    std::cout << "[TopBar] Cannot connect safety manager - null pointer" << std::endl;
    return;
  }

  // When watching starts, show running state
  connect(manager, &SafetyWatchManager::watchingStarted, this,
    [this]() {
      m_safetyIndicator->setState(SafetyIndicatorWidget::State::Running);
      m_safetyIndicator->setToolTipText("Safety Watch: Running");
    });

  // When watching stops, show stopped state
  connect(manager, &SafetyWatchManager::watchingStopped, this,
    [this]() {
      m_safetyIndicator->setState(SafetyIndicatorWidget::State::Stopped);
      m_safetyIndicator->setToolTipText("Safety Watch: Stopped");
    });

  // When safety is triggered, show alert state
  connect(manager, &SafetyWatchManager::safetyTriggered, this,
    [this](QString title, QString message, bool critical) {
      Q_UNUSED(message);
      Q_UNUSED(critical);
      m_safetyIndicator->setState(SafetyIndicatorWidget::State::Alert);
      m_safetyIndicator->setToolTipText("Safety Alert: " + title);
    });

  // When safety is cleared, return to running state (if still watching)
  connect(manager, &SafetyWatchManager::safetyCleared, this,
    [this, manager]() {
      if (manager->IsWatching()) {
        m_safetyIndicator->setState(SafetyIndicatorWidget::State::Running);
        m_safetyIndicator->setToolTipText("Safety Watch: Running");
      } else {
        m_safetyIndicator->setState(SafetyIndicatorWidget::State::Stopped);
        m_safetyIndicator->setToolTipText("Safety Watch: Stopped");
      }
    });

  // When an error occurs
  connect(manager, &SafetyWatchManager::watchError, this,
    [this](QString errorMessage) {
      m_safetyIndicator->setState(SafetyIndicatorWidget::State::Stopped);
      m_safetyIndicator->setToolTipText("Safety Watch Error: " + errorMessage);
    });

  // Set initial state based on current manager state
  if (manager->IsSafetyFlagRaised()) {
    m_safetyIndicator->setState(SafetyIndicatorWidget::State::Alert);
    m_safetyIndicator->setToolTipText("Safety Alert: " +
      QString::fromStdString(manager->GetActiveAlertTitle()));
  } else if (manager->IsWatching()) {
    m_safetyIndicator->setState(SafetyIndicatorWidget::State::Running);
    m_safetyIndicator->setToolTipText("Safety Watch: Running");
  } else {
    m_safetyIndicator->setState(SafetyIndicatorWidget::State::Stopped);
    m_safetyIndicator->setToolTipText("Safety Watch: Stopped");
  }

  std::cout << "[TopBar] Connected to SafetyWatchManager" << std::endl;
}

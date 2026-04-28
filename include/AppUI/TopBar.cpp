// TopBar.cpp
#include "TopBar.h"
#include "ApplicationContext.h"
#include "Theme.h"
#include "ThemeButtonStyles.h"
#include "safety/SafetyWatchManager.h"
#include "common/ScanActivityTracker.h"
#include "TECManager.h"
#include "TECTypes.h"
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
  , m_rtlm2Button(nullptr)
  , m_swirButton(nullptr)
  , m_safetyIndicator(nullptr)
  , m_scanBusyLabel(nullptr)
  , m_tecTemperatureLabel(nullptr)
  , m_tecPollTimer(nullptr)
{
  setupUI();
  std::cout << "[TopBar] Created" << std::endl;
}

TopBar::~TopBar()
{
  if (m_tecPollTimer) {
    m_tecPollTimer->stop();
  }
  std::cout << "[TopBar] Destroyed" << std::endl;
}

void TopBar::setupUI()
{
  // Two-row layout: product info on top, buttons on bottom aligned right
  setFixedHeight(95);

  // Dark background — uses the deep palette tone with an accent underline
  // so the TopBar visually separates from the page below.
  setStyleSheet(QString(
    "TopBar { "
    "  background-color: %1; "
    "  border-bottom: 2px solid %2; "
    "}"
  ).arg(QLatin1String(Theme::Color::bgDeep),
        QLatin1String(Theme::Color::accent)));

  // Root vertical layout holds two rows
  QVBoxLayout* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(15, 5, 15, 5);
  rootLayout->setSpacing(4);

  // ---------- Row 1: Product info (name + path) ----------
  QVBoxLayout* productInfoLayout = new QVBoxLayout();
  productInfoLayout->setSpacing(2);
  productInfoLayout->setContentsMargins(0, 0, 0, 0);

  // Product name label (top, bold, orange)
  m_productNameLabel = new QLabel("Product: None", this);
  QFont productFont;
  productFont.setPointSize(14);
  productFont.setBold(true);
  m_productNameLabel->setFont(productFont);
  m_productNameLabel->setStyleSheet(QString(
    "QLabel { "
    "  color: %1; "
    "  background: transparent; "
    "  border: none; "
    "}"
  ).arg(QLatin1String(Theme::Color::accent)));
  productInfoLayout->addWidget(m_productNameLabel);

  // Working path label (bottom, smaller, gray)
  m_workingPathLabel = new QLabel("", this);
  QFont pathFont;
  pathFont.setPointSize(9);
  m_workingPathLabel->setFont(pathFont);
  m_workingPathLabel->setStyleSheet(QString(
    "QLabel { "
    "  color: %1; "
    "  background: transparent; "
    "  border: none; "
    "}"
  ).arg(QLatin1String(Theme::Color::textSecondary)));
  productInfoLayout->addWidget(m_workingPathLabel);

  rootLayout->addLayout(productInfoLayout);

  // ---------- Row 2: Buttons (right-aligned) ----------
  QHBoxLayout* layout = new QHBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(15);

  // Spacer to push buttons to the right
  layout->addStretch();

  // Safety indicator (before the buttons)
  m_safetyIndicator = new SafetyIndicatorWidget(this);
  layout->addWidget(m_safetyIndicator);
  layout->addSpacing(10);

  // Scan-busy indicator: amber badge shown while any scan (tab-initiated
  // or recipe-initiated GridScan/XCAN) is active. Driven by ScanActivityTracker.
  m_scanBusyLabel = new QLabel(QString::fromUtf8("\xE2\x9A\xA1 SCANNING"), this);
  m_scanBusyLabel->setStyleSheet(QString(
    "QLabel { "
    "  background-color: %1; "
    "  color: %2; "
    "  font-weight: bold; "
    "  font-size: 11px; "
    "  padding: 4px 10px; "
    "  border-radius: 4px; "
    "}"
  ).arg(QLatin1String(Theme::Color::accent),
        QLatin1String(Theme::Color::bgDeep)));
  m_scanBusyLabel->setFixedHeight(24);
  m_scanBusyLabel->hide();  // idle by default
  layout->addWidget(m_scanBusyLabel);
  layout->addSpacing(10);

  // Aux Window button — primary blue family
  m_auxWindowButton = new QPushButton("AUX", this);
  m_auxWindowButton->setFixedSize(70, 35);
  m_auxWindowButton->setStyleSheet(Theme::Buttons::bezel(
    Theme::Color::primaryHover,    // light (top of gradient + hover fill)
    Theme::Color::primary,         // dark  (bottom of gradient)
    Theme::Color::primaryPressed   // pressed
  ));
  m_auxWindowButton->setEnabled(false);
  connect(m_auxWindowButton, &QPushButton::clicked, this, &TopBar::auxWindowButtonClicked);
  layout->addWidget(m_auxWindowButton);

  // SWIR button — danger red family (matches the original red/crimson)
  m_swirButton = new QPushButton("SWIR", this);
  m_swirButton->setFixedSize(70, 35);
  m_swirButton->setStyleSheet(Theme::Buttons::bezel(
    Theme::Color::dangerHover,   // light
    Theme::Color::danger,        // dark
    Theme::Palette::red700        // pressed (darkest red)
  ));
  m_swirButton->setToolTip("Toggle SWIR Beam Processing Window");
  connect(m_swirButton, &QPushButton::clicked, this, &TopBar::swirWindowButtonClicked);
  layout->addWidget(m_swirButton);

  // Rltm button — success green family
  m_rtlmButton = new QPushButton("Rltm", this);
  m_rtlmButton->setFixedSize(70, 35);
  m_rtlmButton->setStyleSheet(Theme::Buttons::bezel(
    Theme::Color::successHover,   // light
    Theme::Color::success,        // dark
    Theme::Palette::green700      // pressed (dark green)
  ));
  m_rtlmButton->setToolTip("Toggle Realtime Camera Monitor");
  connect(m_rtlmButton, &QPushButton::clicked, this, &TopBar::rtlmButtonClicked);
  layout->addWidget(m_rtlmButton);

  // Rltm2 button — success green family (matches Rltm)
  m_rtlm2Button = new QPushButton("Rltm2", this);
  m_rtlm2Button->setFixedSize(70, 35);
  m_rtlm2Button->setStyleSheet(Theme::Buttons::bezel(
    Theme::Color::successHover,
    Theme::Color::success,
    Theme::Palette::green700
  ));
  m_rtlm2Button->setToolTip("Toggle Realtime Camera Monitor 2");
  connect(m_rtlm2Button, &QPushButton::clicked, this, &TopBar::rtlm2ButtonClicked);
  layout->addWidget(m_rtlm2Button);

  // GDI button — accent teal family (force-shows the native Win32+GDI
  // camera monitor, distinct from the Qt-preferring Rltm/Rltm2 buttons).
  m_gdiButton = new QPushButton("GDI", this);
  m_gdiButton->setFixedSize(70, 35);
  m_gdiButton->setStyleSheet(Theme::Buttons::bezel(
    Theme::Palette::teal400,        // light
    Theme::Color::accentSoft,       // dark
    Theme::Palette::blue700         // pressed
  ));
  m_gdiButton->setToolTip("Toggle native GDI Camera Monitor");
  connect(m_gdiButton, &QPushButton::clicked, this, &TopBar::gdiButtonClicked);
  layout->addWidget(m_gdiButton);

  // TEC temperature indicator (next to JOG button)
  m_tecTemperatureLabel = new QLabel("TEC: --", this);
  m_tecTemperatureLabel->setStyleSheet(QString(
    "QLabel { "
    "  color: %1; "
    "  background: transparent; "
    "  border: none; "
    "  font-size: 13px; "
    "  font-weight: bold; "
    "}"
  ).arg(QLatin1String(Theme::Color::textSecondary)));
  m_tecTemperatureLabel->setToolTip("TEC Temperature");
  layout->addWidget(m_tecTemperatureLabel);

  // JOG button — warning orange family
  m_jogButton = new QPushButton("JOG", this);
  m_jogButton->setFixedSize(70, 35);
  m_jogButton->setStyleSheet(Theme::Buttons::bezel(
    Theme::Color::warningHover,   // light
    Theme::Color::warning,        // dark
    Theme::Palette::orange700     // pressed (darkest orange)
  ));
  connect(m_jogButton, &QPushButton::clicked, this, &TopBar::jogButtonClicked);
  layout->addWidget(m_jogButton);

  rootLayout->addLayout(layout);

  setLayout(rootLayout);
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

void TopBar::startTECTemperaturePolling()
{
  if (!m_context || !m_context->tecManager || !m_tecTemperatureLabel) {
    return;
  }

  if (m_tecPollTimer) {
    return;  // Already polling
  }

  m_tecPollTimer = new QTimer(this);
  connect(m_tecPollTimer, &QTimer::timeout, this, [this]() {
    if (!m_context || !m_context->tecManager) {
      return;
    }

    // Build the TEC label QSS once per branch (color is the only thing that
    // changes between disconnected/connected/error states).
    const QString labelQssTemplate = QStringLiteral(
      "QLabel { color: %1; background: transparent; border: none; "
      "font-size: 13px; font-weight: bold; }");

    // Find first connected TEC device and show cached temperature
    auto connectedDevices = m_context->tecManager->GetConnectedDeviceNames();
    if (connectedDevices.empty()) {
      m_tecTemperatureLabel->setText("TEC: --");
      m_tecTemperatureLabel->setStyleSheet(
        labelQssTemplate.arg(QLatin1String(Theme::Color::textDisabled)));
      return;
    }

    // Use cached device status (updated by TECParameterWorker polling)
    // to avoid concurrent VISA access from multiple threads
    auto status = m_context->tecManager->GetDeviceStatus(connectedDevices[0]);
    if (status.Connected && status.CurrentTemperature >= -50.0 && status.CurrentTemperature <= 100.0) {
      m_tecTemperatureLabel->setText(
        QString("TEC: %1 °C").arg(status.CurrentTemperature, 0, 'f', 2));
      m_tecTemperatureLabel->setStyleSheet(
        labelQssTemplate.arg(QLatin1String(Theme::Color::primaryHover)));
    } else if (status.Connected) {
      m_tecTemperatureLabel->setText("TEC: ERR");
      m_tecTemperatureLabel->setStyleSheet(
        labelQssTemplate.arg(QLatin1String(Theme::Color::danger)));
    } else {
      m_tecTemperatureLabel->setText("TEC: --");
      m_tecTemperatureLabel->setStyleSheet(
        labelQssTemplate.arg(QLatin1String(Theme::Color::textDisabled)));
    }
  });

  m_tecPollTimer->start(2000);  // Poll every 2 seconds
  std::cout << "[TopBar] TEC temperature polling started" << std::endl;
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

void TopBar::connectScanActivityTracker(ScanActivityTracker* tracker)
{
  if (!tracker || !m_scanBusyLabel) {
    std::cout << "[TopBar] Cannot connect scan activity tracker - null pointer" << std::endl;
    return;
  }

  // Tracker may emit from the SequenceEngine worker thread — marshal to UI
  // thread via QueuedConnection before touching the QLabel.
  connect(tracker, &ScanActivityTracker::activeChanged, this,
    [this](bool active) {
      if (m_scanBusyLabel) {
        m_scanBusyLabel->setVisible(active);
      }
    }, Qt::QueuedConnection);

  // Initialize visibility from current state (in case tracker already has
  // an active scan by the time we connect — defensive, shouldn't normally happen).
  m_scanBusyLabel->setVisible(tracker->IsActive());

  std::cout << "[TopBar] Connected to ScanActivityTracker" << std::endl;
}

void TopBar::applyTheme()
{
  // Re-apply the TopBar background + accent underline.
  setStyleSheet(QString(
    "TopBar { "
    "  background-color: %1; "
    "  border-bottom: 2px solid %2; "
    "}"
  ).arg(QLatin1String(Theme::Color::bgDeep),
        QLatin1String(Theme::Color::accent)));

  // Product name label — accent colour.
  if (m_productNameLabel) {
    m_productNameLabel->setStyleSheet(QString(
      "QLabel { "
      "  color: %1; "
      "  background: transparent; "
      "  border: none; "
      "}"
    ).arg(QLatin1String(Theme::Color::accent)));
  }

  // Working path label — secondary text colour.
  if (m_workingPathLabel) {
    m_workingPathLabel->setStyleSheet(QString(
      "QLabel { "
      "  color: %1; "
      "  background: transparent; "
      "  border: none; "
      "}"
    ).arg(QLatin1String(Theme::Color::textSecondary)));
  }

  // Scan-busy badge — accent background, bgDeep text.
  if (m_scanBusyLabel) {
    m_scanBusyLabel->setStyleSheet(QString(
      "QLabel { "
      "  background-color: %1; "
      "  color: %2; "
      "  font-weight: bold; "
      "  font-size: 11px; "
      "  padding: 4px 10px; "
      "  border-radius: 4px; "
      "}"
    ).arg(QLatin1String(Theme::Color::accent),
          QLatin1String(Theme::Color::bgDeep)));
  }

  // AUX button — primary blue family (bezelPrimary).
  if (m_auxWindowButton) {
    m_auxWindowButton->setStyleSheet(Theme::Buttons::bezel(
      Theme::Color::primaryHover,
      Theme::Color::primary,
      Theme::Color::primaryPressed
    ));
  }

  // SWIR button — danger red family.
  if (m_swirButton) {
    m_swirButton->setStyleSheet(Theme::Buttons::bezel(
      Theme::Color::dangerHover,
      Theme::Color::danger,
      Theme::Palette::red700
    ));
  }

  // Rltm button — success green family.
  if (m_rtlmButton) {
    m_rtlmButton->setStyleSheet(Theme::Buttons::bezel(
      Theme::Color::successHover,
      Theme::Color::success,
      Theme::Palette::green700
    ));
  }

  // Rltm2 button — success green family (matches Rltm).
  if (m_rtlm2Button) {
    m_rtlm2Button->setStyleSheet(Theme::Buttons::bezel(
      Theme::Color::successHover,
      Theme::Color::success,
      Theme::Palette::green700
    ));
  }

  // GDI button — accent teal family.
  if (m_gdiButton) {
    m_gdiButton->setStyleSheet(Theme::Buttons::bezel(
      Theme::Palette::teal400,
      Theme::Color::accentSoft,
      Theme::Palette::blue700
    ));
  }

  // TEC temperature label — initial state uses textSecondary.
  // (The poll timer will override this on the next tick once a device is found;
  // resetting it here ensures consistency immediately after a theme change.)
  if (m_tecTemperatureLabel) {
    m_tecTemperatureLabel->setStyleSheet(QString(
      "QLabel { "
      "  color: %1; "
      "  background: transparent; "
      "  border: none; "
      "  font-size: 13px; "
      "  font-weight: bold; "
      "}"
    ).arg(QLatin1String(Theme::Color::textSecondary)));
  }

  // JOG button — warning orange family.
  if (m_jogButton) {
    m_jogButton->setStyleSheet(Theme::Buttons::bezel(
      Theme::Color::warningHover,
      Theme::Color::warning,
      Theme::Palette::orange700
    ));
  }
}

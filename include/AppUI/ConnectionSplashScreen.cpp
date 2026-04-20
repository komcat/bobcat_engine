// ConnectionSplashScreen.cpp
#include "ConnectionSplashScreen.h"
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

ConnectionSplashScreen::ConnectionSplashScreen(QWidget* parent)
  : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
  , m_currentPhase(Phase::Loading)
  , m_totalSteps(0)
  , m_completedSteps(0)
  , m_totalComponents(5)  // Legacy: TCP, Camera, Motion, I/O, TEC
  , m_connectedComponents(0)
  , m_swirlTimer(nullptr)
  , m_phaseChangeElapsedMs(0)
{
  // Swirl phase is derived from wall-clock elapsed time so paints triggered
  // by QApplication::processEvents() (during blocking startup work) still
  // advance the animation instead of freezing.
  m_swirlElapsed.start();

  setSwirlPalette(Phase::Loading);
  // Sync prev = target so the initial palette doesn't fade in from black.
  m_prevSwirlA = m_targetSwirlA;
  m_prevSwirlB = m_targetSwirlB;
  m_prevSwirlC = m_targetSwirlC;
  m_prevBorderColor = m_targetBorderColor;

  setupUI();

  // Timer nudges a repaint at 20 FPS when the event loop is idle. When the
  // loop is blocked the timer can't fire, but processEvents() calls in
  // startStep/completeStep will repaint at step boundaries.
  m_swirlTimer = new QTimer(this);
  connect(m_swirlTimer, &QTimer::timeout, this, &ConnectionSplashScreen::onSwirlTick);
  m_swirlTimer->start(50);

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

  // Root background is custom-painted (see paintEvent) — no root stylesheet.

  // Title - will be updated per phase
  m_titleLabel = new QLabel("Initializing...");
  m_titleLabel->setStyleSheet(
    "QLabel { "
    "  color: #ffffff; "
    "  font-size: 16px; "
    "  font-weight: bold; "
    "  border: none; "
    "  padding: 5px; "
    "  background: transparent; "
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
    "  background: transparent; "
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
    "  background: transparent; "
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
    "  background: transparent; "
    "}"
  );

  m_progressBar->setValue(100);

  setSwirlPalette(Phase::Complete);
  update();

  QApplication::processEvents();

  // Auto-close after 1.5 seconds
  QTimer::singleShot(1500, this, &ConnectionSplashScreen::close);
}

void ConnectionSplashScreen::updatePhaseUI()
{
  setSwirlPalette(m_currentPhase);
  update();
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

// ========== GAS SWIRL ANIMATED BACKGROUND ==========

void ConnectionSplashScreen::onSwirlTick()
{
  update();
}

void ConnectionSplashScreen::setSwirlPalette(Phase phase)
{
  // Snapshot current target as prev so paint can crossfade from where we are.
  m_prevSwirlA = m_targetSwirlA;
  m_prevSwirlB = m_targetSwirlB;
  m_prevSwirlC = m_targetSwirlC;
  m_prevBorderColor = m_targetBorderColor;
  m_phaseChangeElapsedMs = m_swirlElapsed.isValid() ? m_swirlElapsed.elapsed() : 0;

  switch (phase) {
    case Phase::Loading:
      m_targetSwirlA = QColor(255, 165, 0);    // amber
      m_targetSwirlB = QColor(255, 90, 30);    // warm orange-red
      m_targetSwirlC = QColor(180, 70, 200);   // magenta accent
      break;
    case Phase::UICreation:
      m_targetSwirlA = QColor(155, 89, 182);   // purple
      m_targetSwirlB = QColor(90, 60, 200);    // deep violet
      m_targetSwirlC = QColor(60, 180, 220);   // cyan accent
      break;
    case Phase::Connecting:
      m_targetSwirlA = QColor(0, 122, 204);    // blue
      m_targetSwirlB = QColor(0, 191, 255);    // cyan
      m_targetSwirlC = QColor(120, 80, 220);   // violet accent
      break;
    case Phase::Complete:
      m_targetSwirlA = QColor(76, 175, 80);    // green
      m_targetSwirlB = QColor(30, 200, 160);   // teal
      m_targetSwirlC = QColor(160, 220, 80);   // lime accent
      break;
    default:
      m_targetSwirlA = QColor(0, 122, 204);
      m_targetSwirlB = QColor(0, 191, 255);
      m_targetSwirlC = QColor(120, 80, 220);
      break;
  }
  m_targetBorderColor = QColor(getPhaseColor(phase));
}

void ConnectionSplashScreen::paintEvent(QPaintEvent* /*event*/)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  const QRectF bounds(0.5, 0.5, width() - 1.0, height() - 1.0);
  constexpr qreal kRadius = 8.0;

  // Rounded-corner clip
  QPainterPath clipPath;
  clipPath.addRoundedRect(bounds, kRadius, kRadius);
  painter.setClipPath(clipPath);

  // Dark base fill
  painter.fillRect(bounds, QColor(26, 26, 31));

  // Wall-clock-derived phase so paints forced by processEvents() during blocking
  // startup work still advance the animation. Scale 0.02 => 20 units/sec
  // (matches the original per-tick rate of +1 per 50 ms).
  const qint64 elapsedMs = m_swirlElapsed.elapsed();
  const qreal phase = static_cast<qreal>(elapsedMs) * 0.02;
  const qreal w = bounds.width();
  const qreal h = bounds.height();
  const qreal ax = w * 0.28;
  const qreal ay = h * 0.32;
  const qreal blobRadius = std::min(w, h) * 0.60;

  // Palette crossfade: interpolate prev -> target over kPaletteFadeMs so phase
  // transitions (Loading -> Connecting -> Complete) glide instead of snapping.
  constexpr qreal kPaletteFadeMs = 600.0;
  const qreal t = std::clamp(
    static_cast<qreal>(elapsedMs - m_phaseChangeElapsedMs) / kPaletteFadeMs,
    0.0, 1.0);
  auto lerpColor = [t](const QColor& a, const QColor& b) -> QColor {
    const int r  = static_cast<int>(a.red()   + (b.red()   - a.red())   * t);
    const int g  = static_cast<int>(a.green() + (b.green() - a.green()) * t);
    const int bl = static_cast<int>(a.blue()  + (b.blue()  - a.blue())  * t);
    return QColor(r, g, bl);
  };
  const QColor colorA      = lerpColor(m_prevSwirlA, m_targetSwirlA);
  const QColor colorB      = lerpColor(m_prevSwirlB, m_targetSwirlB);
  const QColor colorC      = lerpColor(m_prevSwirlC, m_targetSwirlC);
  const QColor borderColor = lerpColor(m_prevBorderColor, m_targetBorderColor);

  // Three drifting radial-gradient "blobs" composited additively for a gas/nebula look.
  // Lissajous coefficients are intentionally mutually irrational-ish so the pattern
  // does not visibly repeat during a startup session.
  struct Blob {
    const QColor* color;
    qreal kx;
    qreal ky;
    qreal phi;
  };
  const Blob blobs[3] = {
    { &colorA, 0.013, 0.017, 0.0      },
    { &colorB, 0.009, 0.023, 2.09440  },  // +120 deg
    { &colorC, 0.021, 0.011, 4.18879  },  // +240 deg
  };

  painter.setCompositionMode(QPainter::CompositionMode_Screen);
  painter.setPen(Qt::NoPen);
  for (const Blob& b : blobs) {
    const qreal cx = bounds.left() + w * 0.5 + ax * std::sin(phase * b.kx + b.phi);
    const qreal cy = bounds.top()  + h * 0.5 + ay * std::cos(phase * b.ky + b.phi);

    QRadialGradient grad(QPointF(cx, cy), blobRadius);
    QColor inner = *b.color;
    inner.setAlpha(170);
    QColor outer = *b.color;
    outer.setAlpha(0);
    grad.setColorAt(0.0, inner);
    grad.setColorAt(1.0, outer);
    painter.setBrush(grad);
    painter.drawRect(bounds);
  }

  // Phase-tinted border stroke on top of the swirl.
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(borderColor, 2.0));
  painter.drawRoundedRect(bounds, kRadius, kRadius);
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

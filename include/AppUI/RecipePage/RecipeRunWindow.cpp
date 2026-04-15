// RecipeRunWindow.cpp
#include "RecipeRunWindow.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
constexpr int kDefaultWidth  = 700;
constexpr int kDefaultHeight = 550;
const QString kSettingsGroup    = QStringLiteral("RecipeRunWindow");
const QString kSettingsGeometry = QStringLiteral("geometry");
}  // namespace

RecipeRunWindow::RecipeRunWindow(QWidget* parent)
  : QWidget(parent, Qt::Window)
{
  setWindowTitle(tr("Recipe Execution"));
  resize(kDefaultWidth, kDefaultHeight);

  // Single child stack widget fills the window.
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(0);

  m_stack = new QStackedWidget(this);
  layout->addWidget(m_stack, 1);

  // Match the dark theme used by RecipePage.
  setStyleSheet(
    "QWidget { background-color: #2d2d30; color: #d4d4d4; }"
  );
}

RecipeRunWindow::~RecipeRunWindow() = default;

void RecipeRunWindow::setActiveWidget(QWidget* w)
{
  if (!m_stack) {
    return;
  }
  if (w == nullptr) {
    // Nothing to do — leave the stack as-is, just stop showing it if needed.
    return;
  }
  // Add the widget to the stack on first encounter.  addWidget() reparents
  // and indexOf() returns -1 if the widget is not present.
  if (m_stack->indexOf(w) < 0) {
    m_stack->addWidget(w);
  }
  m_stack->setCurrentWidget(w);
}

void RecipeRunWindow::openForRun()
{
  m_running = true;
  if (!m_geometryRestored) {
    restorePersistedGeometry();
    m_geometryRestored = true;
  }
  show();
  raise();
  activateWindow();
}

void RecipeRunWindow::closeAfterRun()
{
  persistGeometry();
  m_running = false;
  hide();
}

void RecipeRunWindow::closeEvent(QCloseEvent* event)
{
  if (m_running) {
    // Block close while a recipe is executing.  Operator must press STOP.
    event->ignore();
    emit closeAttemptedWhileRunning();
    return;
  }
  persistGeometry();
  QWidget::closeEvent(event);
}

void RecipeRunWindow::showEvent(QShowEvent* event)
{
  if (!m_geometryRestored) {
    restorePersistedGeometry();
    m_geometryRestored = true;
  }
  QWidget::showEvent(event);
}

void RecipeRunWindow::persistGeometry()
{
  QSettings settings;
  settings.beginGroup(kSettingsGroup);
  settings.setValue(kSettingsGeometry, saveGeometry());
  settings.endGroup();
}

void RecipeRunWindow::restorePersistedGeometry()
{
  QSettings settings;
  settings.beginGroup(kSettingsGroup);
  const QByteArray geom = settings.value(kSettingsGeometry).toByteArray();
  settings.endGroup();

  bool restored = false;
  if (!geom.isEmpty()) {
    restored = restoreGeometry(geom);
  }

  // Sanity check: make sure the restored geometry intersects an available
  // screen.  If not (e.g., a monitor was unplugged), fall back to centered.
  if (restored) {
    bool onScreen = false;
    const QRect frame = frameGeometry();
    for (const QScreen* screen : QGuiApplication::screens()) {
      if (screen && screen->availableGeometry().intersects(frame)) {
        onScreen = true;
        break;
      }
    }
    if (!onScreen) {
      restored = false;
    }
  }

  if (!restored) {
    centerOnParent();
  }
}

void RecipeRunWindow::centerOnParent()
{
  QRect target;
  if (parentWidget()) {
    target = parentWidget()->frameGeometry();
  } else if (QScreen* screen = QGuiApplication::primaryScreen()) {
    target = screen->availableGeometry();
  }
  if (!target.isValid()) {
    return;
  }
  const int x = target.center().x() - width()  / 2;
  const int y = target.center().y() - height() / 2;
  move(x, y);
}

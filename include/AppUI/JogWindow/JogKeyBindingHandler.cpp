// JogKeyBindingHandler.cpp
#include "JogKeyBindingHandler.h"
#include "JogPanelWindow.h"
#include <QWidget>
#include <iostream>

JogKeyBindingHandler::JogKeyBindingHandler(JogPanelWindow* jogPanel, QObject* parent)
  : QObject(parent)
  , m_jogPanel(jogPanel)
  , m_enabled(false)
{
  initializeKeyMap();
}

JogKeyBindingHandler::~JogKeyBindingHandler()
{
}

void JogKeyBindingHandler::initializeKeyMap()
{
  // Step size control
  m_keyMap[Qt::Key_Q] = JogAction::StepDecrease;
  m_keyMap[Qt::Key_E] = JogAction::StepIncrease;

  // Y axis (W/S)
  m_keyMap[Qt::Key_W] = JogAction::YMinus;
  m_keyMap[Qt::Key_S] = JogAction::YPlus;

  // X axis (A/D)
  m_keyMap[Qt::Key_A] = JogAction::XMinus;
  m_keyMap[Qt::Key_D] = JogAction::XPlus;

  // Z axis (R/F)
  m_keyMap[Qt::Key_R] = JogAction::ZPlus;
  m_keyMap[Qt::Key_F] = JogAction::ZMinus;
}

void JogKeyBindingHandler::setEnabled(bool enabled)
{
  m_enabled = enabled;

  if (enabled) {
    std::cout << "[JogKeyBindingHandler] Key bindings ENABLED" << std::endl;
  }
  else {
    std::cout << "[JogKeyBindingHandler] Key bindings DISABLED" << std::endl;
  }
}

void JogKeyBindingHandler::installOn(QWidget* widget)
{
  if (widget) {
    widget->installEventFilter(this);
    std::cout << "[JogKeyBindingHandler] Installed on widget" << std::endl;
  }
}

void JogKeyBindingHandler::removeFrom(QWidget* widget)
{
  if (widget) {
    widget->removeEventFilter(this);
    std::cout << "[JogKeyBindingHandler] Removed from widget" << std::endl;
  }
}

bool JogKeyBindingHandler::eventFilter(QObject* obj, QEvent* event)
{
  // Only process if enabled
  if (!m_enabled) {
    return QObject::eventFilter(obj, event);
  }

  // Only process key press events
  if (event->type() == QEvent::KeyPress) {
    QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

    // Ignore auto-repeat
    if (keyEvent->isAutoRepeat()) {
      return QObject::eventFilter(obj, event);
    }

    handleKeyPress(keyEvent);

    // Check if this key is in our map
    Qt::Key key = static_cast<Qt::Key>(keyEvent->key());
    if (m_keyMap.find(key) != m_keyMap.end()) {
      // Consume the event (don't pass it further)
      return true;
    }
  }

  // Pass event to parent class
  return QObject::eventFilter(obj, event);
}

void JogKeyBindingHandler::handleKeyPress(QKeyEvent* keyEvent)
{
  if (!m_jogPanel) return;

  Qt::Key key = static_cast<Qt::Key>(keyEvent->key());

  auto it = m_keyMap.find(key);
  if (it != m_keyMap.end()) {
    executeAction(it->second);
  }
}

void JogKeyBindingHandler::executeAction(JogAction action)
{
  if (!m_jogPanel) return;

  switch (action) {
  case JogAction::StepDecrease:
    m_jogPanel->cycleStepSizeDown();
    break;

  case JogAction::StepIncrease:
    m_jogPanel->cycleStepSizeUp();
    break;

  case JogAction::YMinus:
    m_jogPanel->jogY(-1.0);
    break;

  case JogAction::YPlus:
    m_jogPanel->jogY(1.0);
    break;

  case JogAction::XMinus:
    m_jogPanel->jogX(-1.0);
    break;

  case JogAction::XPlus:
    m_jogPanel->jogX(1.0);
    break;

  case JogAction::ZPlus:
    m_jogPanel->jogZ(1.0);
    break;

  case JogAction::ZMinus:
    m_jogPanel->jogZ(-1.0);
    break;
  }
}
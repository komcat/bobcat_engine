// JogKeyBindingHandler.h
#pragma once

#include <QObject>
#include <QKeyEvent>
#include <map>

class JogPanelWindow;

class JogKeyBindingHandler : public QObject
{
  Q_OBJECT

public:
  explicit JogKeyBindingHandler(JogPanelWindow* jogPanel, QObject* parent = nullptr);
  ~JogKeyBindingHandler();

  // Enable/disable key bindings
  void setEnabled(bool enabled);
  bool isEnabled() const { return m_enabled; }

  // Install event filter on target widget
  void installOn(QWidget* widget);
  void removeFrom(QWidget* widget);

protected:
  // Event filter to catch keyboard events
  bool eventFilter(QObject* obj, QEvent* event) override;

private:
  void handleKeyPress(QKeyEvent* keyEvent);

  JogPanelWindow* m_jogPanel;
  bool m_enabled;

  // Key mappings
  enum class JogAction {
    StepDecrease,  // Q
    StepIncrease,  // E
    YMinus,        // W
    YPlus,         // S
    XMinus,        // A
    XPlus,         // D
    ZPlus,         // R
    ZMinus         // F
  };

  std::map<Qt::Key, JogAction> m_keyMap;

  void initializeKeyMap();
  void executeAction(JogAction action);
};
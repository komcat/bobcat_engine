# Keyboard Shortcuts Guide

**Date:** October 31, 2025
**Component:** MainWindow
**Feature:** Global keyboard shortcuts for navigation and window control

---

## Overview

The Bobcat Engine application provides global keyboard shortcuts for quick navigation between pages and toggling windows. All shortcuts are active from anywhere in the application when the main window has focus.

---

## Available Shortcuts

### Page Navigation (F1-F8)

| Shortcut | Page | Description | Status |
|----------|------|-------------|--------|
| **F1** | Run Program | Execute programs and view execution status | ✅ Active |
| **F2** | Recipe | Create and edit process recipes | ✅ Active |
| **F3** | Motion | Control motion devices and positions | ✅ Active |
| **F4** | Hardware | Manage hardware instruments and devices | ✅ Active |
| **F5** | Config Editor | Edit JSON configuration files | ✅ Active |
| **F6** | Future Update | Reserved for future functionality | ✅ Active (placeholder) |
| **F7** | Reserved | Not yet assigned | 🔒 Disabled |
| **F8** | Reserved | Not yet assigned | 🔒 Disabled |

### Window Toggles (Ctrl+Key)

| Shortcut | Window | Description | Status |
|----------|--------|-------------|--------|
| **Ctrl+J** | Jog Panel | Toggle motion control jog panel | ✅ Active |
| **Ctrl+K** | Aux Window | Toggle auxiliary display (camera + charts) | ✅ Active |

---

## Usage Examples

### Quick Page Navigation
```
User at Main Menu → Press F2 → Instantly switch to Recipe page
User at Recipe page → Press F3 → Instantly switch to Motion page
User at Motion page → Press F1 → Instantly switch to Run Program page
```

### Window Management
```
Jog panel visible → Press Ctrl+J → Jog panel hidden
Jog panel hidden → Press Ctrl+J → Jog panel shown

Aux window visible → Press Ctrl+K → Aux window hidden
Aux window hidden → Press Ctrl+K → Aux window shown
```

---

## Implementation Details

### Architecture

**Location:** `include/AppUI/MainWindow.h` and `MainWindow.cpp`

The keyboard shortcuts are implemented using Qt's `QShortcut` class:

```cpp
// Header file
class MainWindow : public QMainWindow
{
  Q_OBJECT

private slots:
  void onMainMenuProcess();    // F1
  void onMainMenuRecipe();     // F2
  void onMainMenuMotion();     // F3
  void onMainMenuHardware();   // F4
  void onMainMenuEditConfig(); // F5
  void onToggleJogPanel();     // Ctrl+J
  void onToggleAuxWindow();    // Ctrl+K

private:
  void setupKeyboardShortcuts();

  QShortcut* m_shortcutF1;
  QShortcut* m_shortcutF2;
  // ... etc
  QShortcut* m_shortcutCtrlJ;
  QShortcut* m_shortcutCtrlK;
};
```

### Initialization Flow

1. **Constructor** (`MainWindow::MainWindow`):
   ```cpp
   setupUI();
   setupJogPanel();
   createPages();
   setupLogging();
   setupKeyboardShortcuts();  // ✅ Called last
   ```

2. **Setup Method** (`MainWindow::setupKeyboardShortcuts`):
   ```cpp
   // Create shortcut
   m_shortcutF1 = new QShortcut(QKeySequence(Qt::Key_F1), this);

   // Connect to slot
   connect(m_shortcutF1, &QShortcut::activated,
           this, &MainWindow::onMainMenuProcess);
   ```

3. **Slot Execution**:
   ```cpp
   void MainWindow::onMainMenuProcess()
   {
     m_pageStack->setCurrentWidget(m_runProgramPage);
   }
   ```

### Key Implementation Files

**MainWindow.h** (lines 7, 40, 46, 66-78):
```cpp
#include <QShortcut>  // Required include

private slots:
  void onToggleAuxWindow();  // Ctrl+K handler

private:
  void setupKeyboardShortcuts();  // Setup method

  // Shortcut pointers
  QShortcut* m_shortcutF1;
  QShortcut* m_shortcutCtrlJ;
  QShortcut* m_shortcutCtrlK;
```

**MainWindow.cpp** (lines 36-45, 51, 280-341, 400-438):
```cpp
// Constructor initialization
, m_shortcutF1(nullptr)
, m_shortcutCtrlJ(nullptr)
, m_shortcutCtrlK(nullptr)

// Setup call
setupKeyboardShortcuts();

// Implementation
void MainWindow::setupKeyboardShortcuts()
{
  // F1-F8 page navigation shortcuts
  m_shortcutF1 = new QShortcut(QKeySequence(Qt::Key_F1), this);
  connect(m_shortcutF1, &QShortcut::activated,
          this, &MainWindow::onMainMenuProcess);

  // Ctrl+J/K window toggle shortcuts
  m_shortcutCtrlJ = new QShortcut(
    QKeySequence(Qt::CTRL | Qt::Key_J), this);
  connect(m_shortcutCtrlJ, &QShortcut::activated,
          this, &MainWindow::onToggleJogPanel);
}
```

---

## Adding New Shortcuts

### Step-by-Step Guide

#### 1. Add Shortcut Member Variable (MainWindow.h)

```cpp
private:
  QShortcut* m_shortcutNewKey;  // Your new shortcut
```

#### 2. Initialize in Constructor (MainWindow.cpp)

```cpp
MainWindow::MainWindow(...)
  : ...
  , m_shortcutNewKey(nullptr)  // Initialize to nullptr
{
  ...
}
```

#### 3. Create Shortcut in setupKeyboardShortcuts() (MainWindow.cpp)

```cpp
void MainWindow::setupKeyboardShortcuts()
{
  ...

  // Your new shortcut
  m_shortcutNewKey = new QShortcut(QKeySequence(Qt::Key_F9), this);
  connect(m_shortcutNewKey, &QShortcut::activated,
          this, &MainWindow::onNewFunction);
  std::cout << "[MainWindow]   F9 -> New Function" << std::endl;
}
```

#### 4. Create Handler Slot (MainWindow.h + .cpp)

**Header:**
```cpp
private slots:
  void onNewFunction();
```

**Implementation:**
```cpp
void MainWindow::onNewFunction()
{
  std::cout << "[MainWindow] New function triggered" << std::endl;
  // Your logic here
}
```

### Common Key Combinations

```cpp
// Single key
QKeySequence(Qt::Key_F1)

// Ctrl + key
QKeySequence(Qt::CTRL | Qt::Key_J)

// Shift + key
QKeySequence(Qt::SHIFT | Qt::Key_F1)

// Ctrl + Shift + key
QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A)

// Alt + key
QKeySequence(Qt::ALT | Qt::Key_M)
```

---

## Best Practices

### 1. Avoid Conflicts

**Check existing shortcuts:**
- System shortcuts (Ctrl+C, Ctrl+V, etc.)
- Qt default shortcuts (F1 for help in some widgets)
- Jog panel shortcuts (when active)

### 2. Use Descriptive Names

```cpp
// ✅ GOOD
m_shortcutCtrlJ    // Clear what key it is
onToggleJogPanel() // Clear what it does

// ❌ BAD
m_shortcut1        // Unclear
onSlot2()          // Unclear
```

### 3. Log Actions

```cpp
void MainWindow::onMyAction()
{
  std::cout << "[MainWindow] Action triggered (F9)" << std::endl;

  if (m_context->logManager) {
    m_context->logManager->Info("Action executed", "MainWindow");
  }
}
```

### 4. Check Initialization

```cpp
void MainWindow::onToggleSomething()
{
  if (!m_something) {
    std::cerr << "[MainWindow] Something not initialized!" << std::endl;
    return;
  }

  // Proceed with action
}
```

### 5. Provide Feedback

- Console output for debugging
- Log messages for user tracking
- Visual feedback (status bar, window changes)

---

## Debugging Shortcuts

### Enable Debug Logging

On application startup, you'll see:
```
[MainWindow] Setting up keyboard shortcuts (F1-F8)...
[MainWindow]   F1 -> Run Program Page
[MainWindow]   F2 -> Recipe Page
[MainWindow]   F3 -> Motion Page
[MainWindow]   F4 -> Hardware Page
[MainWindow]   F5 -> Config Editor Page
[MainWindow]   F6 -> Reserved (Future)
[MainWindow]   F7 -> Reserved (Disabled)
[MainWindow]   F8 -> Reserved (Disabled)
[MainWindow]   Ctrl+J -> Toggle Jog Panel
[MainWindow]   Ctrl+K -> Toggle Aux Window
[MainWindow] Keyboard shortcuts setup complete!
```

### Check Shortcut Activation

When you press a shortcut, you'll see:
```
[MainWindow] Process clicked
[MainWindow] Jog panel shown (Ctrl+J)
[MainWindow] Aux window hidden (Ctrl+K)
```

### Common Issues

**Shortcut not working:**
1. Check if widget has focus
2. Verify shortcut wasn't disabled: `m_shortcut->setEnabled(true)`
3. Check for conflicting shortcuts in child widgets
4. Ensure shortcut parent is `this` (main window)

**Wrong action triggered:**
1. Verify `connect()` statement targets correct slot
2. Check if multiple shortcuts use same key sequence
3. Review log output to see which handler is called

---

## Future Enhancements

### Planned Shortcuts

- **F7**: Log viewer page (quick access to full logs)
- **F8**: Settings/preferences page
- **Ctrl+L**: Toggle live data overlay
- **Ctrl+M**: Toggle main menu visibility

### Customizable Shortcuts

Future enhancement could allow users to customize shortcuts via settings:

```json
{
  "keyboard_shortcuts": {
    "run_program": "F1",
    "recipe": "F2",
    "motion": "F3",
    "toggle_jog": "Ctrl+J",
    "toggle_aux": "Ctrl+K"
  }
}
```

---

## Related Files

- **MainWindow.h** - Class declaration, shortcut members
- **MainWindow.cpp** - Implementation, setup, handlers
- **ApplicationContext.h** - Access to auxWindowManager
- **JogPanelWindow.h** - Jog panel show/hide methods
- **AuxWindowManager.h** - Aux window show/hide/isVisible methods

---

## References

- **Qt QShortcut Documentation**: https://doc.qt.io/qt-6/qshortcut.html
- **Qt QKeySequence**: https://doc.qt.io/qt-6/qkeysequence.html
- **Related Issue**: Keyboard shortcuts for quick navigation
- **Implemented**: October 31, 2025

---

## Troubleshooting

### Shortcut Not Responding

**Symptom:** Pressing F1-F8 or Ctrl+J/K does nothing

**Possible Causes:**
1. Another widget has keyboard focus and is consuming the event
2. Shortcut was disabled with `setEnabled(false)`
3. Modal dialog is blocking main window shortcuts
4. Operating system is intercepting the key combination

**Solutions:**
- Check which widget has focus: `QApplication::focusWidget()`
- Verify shortcut enabled: `m_shortcutF1->isEnabled()`
- Close modal dialogs before testing
- Try different key combinations if OS intercepts

### Conflicting Shortcuts

**Symptom:** Shortcut triggers wrong action or multiple actions

**Possible Causes:**
1. Multiple shortcuts registered with same key sequence
2. Child widget has same shortcut registered
3. Parent/child widget hierarchy conflict

**Solutions:**
- Search codebase for duplicate `QKeySequence` definitions
- Use unique key combinations for each action
- Set shortcut context: `m_shortcut->setContext(Qt::ApplicationShortcut)`

### Jog Panel Shortcuts Override

**Symptom:** F1-F8 don't work when jog panel is visible

**Expected Behavior:** The jog panel has its own keyboard handler (`JogKeyBindingHandler`) that takes priority when the panel is visible. This is intentional to allow axis jogging with keyboard.

**Solution:** This is correct behavior. Use Ctrl+J to hide jog panel if you need F1-F8 page navigation.

---

**Status:** ✅ **IMPLEMENTED**
**Last Updated:** October 31, 2025

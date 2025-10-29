# Global Dark Theme System - Developer Guide

## Overview

Instead of setting styles individually for every widget, the application now uses a **centralized dark theme** applied globally at the MainWindow level. This approach:

- ✅ Reduces code duplication
- ✅ Ensures consistent styling across the entire application
- ✅ Makes maintenance and updates easier
- ✅ Improves performance (styles cascade instead of being recomputed)

## Architecture

```
Application
    └── MainWindow::setupUI()
            └── setStyleSheet(DarkTheme::getStyleSheet())
                    └── All child widgets inherit the dark theme automatically
```

## Files

### 1. `DarkTheme.h` / `DarkTheme.cpp`
Location: `include/AppUI/`

Central theme manager that provides the complete stylesheet for the application.

### 2. `MainWindow.cpp`
The global theme is applied once in `MainWindow::setupUI()`:

```cpp
#include "DarkTheme.h"

void MainWindow::setupUI()
{
    // Apply centralized dark theme globally to all widgets
    setStyleSheet(DarkTheme::getStyleSheet());

    // ... rest of setup
}
```

## Color Palette

The dark theme uses a consistent color palette:

| Purpose | Color | Hex Code | Usage |
|---------|-------|----------|-------|
| **Backgrounds** |
| Main background | Dark gray | `#2d2d30` | Windows, panels |
| Darker background | Very dark gray | `#1e1e1e` | Dropdowns, insets |
| Surface | Medium gray | `#3c3c3c` | Input fields, panels |
| **Borders** |
| Normal border | Gray | `#555555` | Default borders |
| Subtle border | Dark border | `#3f3f46` | Subtle separators |
| **Text** |
| Primary text | White | `#ffffff` | Main text, labels |
| Secondary text | Light gray | `#d4d4d4` | Secondary information |
| Tertiary text | Medium gray | `#a0a0a0` | Hints, placeholders |
| Disabled text | Dark gray | `#888888` | Disabled controls |
| **Action Colors** |
| Primary (Blue) | Blue | `#0e639c` | Default buttons, accents |
| Primary hover | Light blue | `#1177bb` | Button hover states |
| Success (Green) | Green | `#27ae60` | Create, Add actions |
| Success hover | Light green | `#2ecc71` | Success hover |
| Warning (Orange) | Orange | `#f39c12` | Warnings, special actions |
| Warning hover | Dark orange | `#e67e22` | Warning hover |
| Danger (Red) | Red | `#e74c3c` | Delete, Remove actions |
| Danger hover | Dark red | `#c0392b` | Danger hover |
| **Disabled** |
| Disabled background | Gray | `#555555` | Disabled controls |

## Best Practices

### ✅ DO

1. **Let the global theme handle standard widgets**
   ```cpp
   // Good - no stylesheet needed
   QLineEdit* input = new QLineEdit();
   QComboBox* combo = new QComboBox();
   QPushButton* button = new QPushButton("OK");
   ```

2. **Only override for custom styling needs**
   ```cpp
   // Good - only override specific properties
   QLabel* title = new QLabel("Title");
   title->setStyleSheet("font-size: 18px; font-weight: bold;");
   ```

3. **Use object names for button color differentiation**
   ```cpp
   // Good - use object name for specific styling
   QPushButton* deleteBtn = new QPushButton("Delete");
   deleteBtn->setObjectName("deleteButton");
   deleteBtn->setStyleSheet(
       "QPushButton#deleteButton { background-color: #e74c3c; }"
   );
   ```

4. **Keep custom stylesheets minimal**
   ```cpp
   // Good - only override what you need
   widget->setStyleSheet("font-size: 14px;");
   ```

### ❌ DON'T

1. **Don't set dialog background colors**
   ```cpp
   // Bad - unnecessary, inherited from MainWindow
   setStyleSheet("QDialog { background-color: #2d2d30; }");
   ```

2. **Don't repeat the global theme**
   ```cpp
   // Bad - already handled by global theme
   spinBox->setStyleSheet(
       "QDoubleSpinBox {"
       "  background-color: #3c3c3c;"
       "  color: #ffffff;"
       "  border: 1px solid #555555;"
       "  ..."
       "}"
   );
   ```

3. **Don't set basic widget colors**
   ```cpp
   // Bad - global theme already handles this
   label->setStyleSheet("color: #ffffff;");
   button->setStyleSheet("background-color: #0e639c; color: white;");
   ```

## Button Color Coding

For semantic button colors, use specific object names and minimal overrides:

```cpp
// Cancel/Close buttons - Gray
QPushButton* cancelBtn = new QPushButton("Cancel");
cancelBtn->setObjectName("cancelButton");
cancelBtn->setStyleSheet("QPushButton#cancelButton { background-color: #555555; }");

// Create/Add buttons - Green
QPushButton* createBtn = new QPushButton("Create");
createBtn->setObjectName("createButton");
createBtn->setStyleSheet("QPushButton#createButton { background-color: #27ae60; }");

// Delete/Remove buttons - Red
QPushButton* deleteBtn = new QPushButton("Delete");
deleteBtn->setObjectName("deleteButton");
deleteBtn->setStyleSheet("QPushButton#deleteButton { background-color: #e74c3c; }");

// Special/Offset buttons - Orange
QPushButton* specialBtn = new QPushButton("Move Offset");
specialBtn->setObjectName("specialButton");
specialBtn->setStyleSheet("QPushButton#specialButton { background-color: #f39c12; }");

// Default buttons - Blue (no override needed, uses global theme)
QPushButton* okBtn = new QPushButton("OK");
```

## Updating the Global Theme

To modify the application's appearance globally:

1. Edit `include/AppUI/DarkTheme.cpp`
2. Modify the stylesheet in `DarkTheme::getStyleSheet()`
3. Rebuild the application
4. All widgets automatically reflect the changes

### Example: Changing Primary Button Color

```cpp
// In DarkTheme.cpp
QString DarkTheme::getStyleSheet()
{
    return R"(
        /* Change all primary buttons from blue to purple */
        QPushButton {
            background-color: #9b59b6;  /* Changed from #0e639c */
            border: 1px solid #8e44ad;
        }

        QPushButton:hover {
            background-color: #af7ac5;  /* Changed from #1177bb */
        }
    )";
}
```

## Migration Guide

### Converting Old Widget Code to New System

**Before (Old Way):**
```cpp
QLineEdit* input = new QLineEdit();
input->setStyleSheet(
    "QLineEdit {"
    "  background-color: #3c3c3c;"
    "  color: #ffffff;"
    "  border: 1px solid #555555;"
    "  border-radius: 3px;"
    "  padding: 4px 8px;"
    "}"
);
```

**After (New Way):**
```cpp
QLineEdit* input = new QLineEdit();
// That's it! Global theme handles everything
```

---

**Before (Dialog with full theme):**
```cpp
void MyDialog::setupUI()
{
    setStyleSheet("QDialog { background-color: #2d2d30; }");

    QLabel* label = new QLabel("Text");
    label->setStyleSheet("color: #ffffff;");

    QLineEdit* input = new QLineEdit();
    input->setStyleSheet(
        "QLineEdit {"
        "  background-color: #3c3c3c;"
        "  color: #ffffff;"
        "  ..."
        "}"
    );
}
```

**After (Dialog with global theme):**
```cpp
void MyDialog::setupUI()
{
    // No dialog stylesheet needed

    QLabel* label = new QLabel("Text");
    // No stylesheet needed

    QLineEdit* input = new QLineEdit();
    // No stylesheet needed
}
```

## Common Scenarios

### Scenario 1: Creating a New Dialog

```cpp
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

void MyNewDialog::setupUI()
{
    // ✅ No theme setup needed - inherits from MainWindow

    QVBoxLayout* layout = new QVBoxLayout(this);

    // ✅ Widgets automatically styled
    QLabel* label = new QLabel("Enter name:");
    QLineEdit* input = new QLineEdit();

    // ✅ Only override for semantic colors
    QPushButton* okBtn = new QPushButton("OK");  // Blue by default

    QPushButton* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setObjectName("cancelButton");
    cancelBtn->setStyleSheet("QPushButton#cancelButton { background-color: #555555; }");

    layout->addWidget(label);
    layout->addWidget(input);
    layout->addWidget(okBtn);
    layout->addWidget(cancelBtn);
}
```

### Scenario 2: Custom Widget with Unique Styling

```cpp
void CustomPanel::setupUI()
{
    // Most widgets use global theme
    QLabel* title = new QLabel("Custom Panel");
    QListWidget* list = new QListWidget();

    // Only override unique styling needs
    title->setStyleSheet(
        "font-size: 16px;"
        "font-weight: bold;"
        "color: #f39c12;"  // Special accent color
    );

    // List inherits dark theme automatically
}
```

### Scenario 3: Temporarily Testing a Different Color

```cpp
void TestWidget::setupUI()
{
    QPushButton* testBtn = new QPushButton("Test");

    // ✅ Override specific property for testing
    testBtn->setStyleSheet("background-color: #e74c3c;");

    // Later, if you want this everywhere, add to DarkTheme.cpp
}
```

## Troubleshooting

### Issue: Widget doesn't inherit theme

**Problem:** A widget appears unstyled or uses default Qt styling.

**Solution:** Check that the widget is a child of MainWindow or a widget parented to MainWindow.

```cpp
// Bad - widget has no parent
QDialog* dialog = new QDialog();

// Good - widget inherits from MainWindow
QDialog* dialog = new QDialog(this);  // 'this' is MainWindow
```

### Issue: Custom stylesheet completely overrides theme

**Problem:** Setting any stylesheet on a widget removes all inherited styles.

**Solution:** Be specific with selectors and only override needed properties.

```cpp
// Bad - removes all inherited styles
widget->setStyleSheet("font-size: 14px;");

// Good - only changes font size, keeps other inherited styles
widget->setStyleSheet("QLabel { font-size: 14px; }");
```

### Issue: Theme changes not appearing

**Problem:** After editing DarkTheme.cpp, changes don't show.

**Solution:**
1. Rebuild the application (`cmake --build out/build/x64-debug`)
2. Ensure you're running the newly built executable
3. Check that MainWindow is calling `DarkTheme::getStyleSheet()`

## Performance Benefits

Using a global stylesheet instead of individual widget stylesheets:

- **Faster rendering**: Styles cascade instead of being recomputed per widget
- **Lower memory**: One stylesheet vs. thousands of duplicate strings
- **Better caching**: Qt caches the global stylesheet
- **Reduced startup time**: Less parsing and compilation of stylesheets

## Future Enhancements

### Theme Switching (if needed in the future)

```cpp
// DarkTheme.h
class DarkTheme
{
public:
    static QString getStyleSheet();
    static QString getLightTheme();  // Future: light theme variant
};

// Usage
mainWindow->setStyleSheet(DarkTheme::getStyleSheet());  // Dark theme
// or
mainWindow->setStyleSheet(DarkTheme::getLightTheme());  // Light theme
```

### Theme from File (optional)

```cpp
// Load theme from external .qss file
QFile styleFile(":/themes/dark.qss");
styleFile.open(QFile::ReadOnly);
QString styleSheet = QLatin1String(styleFile.readAll());
mainWindow->setStyleSheet(styleSheet);
```

## Summary

**Key Principle:** Set styles globally once, override locally only when necessary.

- ✅ Global theme handles 95% of styling
- ✅ Only override for custom colors or sizes
- ✅ Use object names for semantic button colors
- ✅ Keep overrides minimal and specific

This approach keeps your codebase clean, maintainable, and performant!

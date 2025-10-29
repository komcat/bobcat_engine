// DarkTheme.h
#pragma once

#include <QString>

/**
 * @brief Centralized dark theme stylesheet for the entire application
 *
 * This class provides a single source of truth for the application's dark theme.
 * Apply this stylesheet to QApplication or MainWindow to theme all widgets globally.
 *
 * Usage:
 *   mainWindow->setStyleSheet(DarkTheme::getStyleSheet());
 *
 * Color Palette:
 *   - Background: #2d2d30 (main), #1e1e1e (darker)
 *   - Surface: #3c3c3c (input fields, panels)
 *   - Border: #555555 (normal), #3f3f46 (subtle)
 *   - Text: #ffffff (primary), #d4d4d4 (secondary), #a0a0a0 (tertiary)
 *   - Primary (Blue): #0e639c (buttons), #1177bb (hover)
 *   - Success (Green): #27ae60 (create/add), #2ecc71 (hover)
 *   - Warning (Orange): #f39c12 (offset/special), #e67e22 (hover)
 *   - Danger (Red): #e74c3c (delete/remove), #c0392b (hover)
 *   - Disabled: #555555 (background), #888888 (text)
 */
class DarkTheme
{
public:
    /**
     * @brief Get the complete dark theme stylesheet
     * @return QString containing all CSS rules for the dark theme
     */
    static QString getStyleSheet();

private:
    DarkTheme() = delete;  // Static class, no instantiation
};

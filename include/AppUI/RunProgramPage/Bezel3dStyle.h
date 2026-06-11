// Bezel3dStyle.h
// Shared 3D bevelled-button QSS used by the Run Program tab widgets
// (M4 Angle, Poynt Optimize). Same raised gradient + light-top/left,
// dark-bottom/right bevel borders as the TopBar status badges
// (Theme::Buttons::bezel), but with a caller-chosen text colour: several presets
// here (yellow, cyan, grey) need dark text to stay readable, whereas the shared
// Theme helper hard-codes white. The pressed state inverts the bevel and nudges
// the label down-right so the button visibly sinks.
#pragma once

#include <QLatin1String>
#include <QString>

inline QString bezel3d(const char* top, const char* bottom, const char* hover,
                       const char* pressed, const char* text,
                       const char* disabledBg = "#5a5a5a",
                       const char* disabledText = "#9a9a9a") {
  return QStringLiteral(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2); "
    "  color: %5; font-weight: bold; padding: 6px 12px; border-radius: 5px; "
    "  border-top: 2px solid rgba(255,255,255,0.5); "
    "  border-left: 2px solid rgba(255,255,255,0.5); "
    "  border-bottom: 2px solid rgba(0,0,0,0.45); "
    "  border-right: 2px solid rgba(0,0,0,0.45); } "
    "QPushButton:hover { background-color: %3; "
    "  border-top: 2px solid rgba(255,255,255,0.6); "
    "  border-left: 2px solid rgba(255,255,255,0.6); } "
    "QPushButton:pressed { background-color: %4; padding-top: 8px; padding-left: 14px; "
    "  border-top: 2px solid rgba(0,0,0,0.45); "
    "  border-left: 2px solid rgba(0,0,0,0.45); "
    "  border-bottom: 2px solid rgba(255,255,255,0.3); "
    "  border-right: 2px solid rgba(255,255,255,0.3); } "
    "QPushButton:disabled { background-color: %6; color: %7; "
    "  border-top: 1px solid rgba(255,255,255,0.2); "
    "  border-left: 1px solid rgba(255,255,255,0.2); "
    "  border-bottom: 1px solid rgba(0,0,0,0.3); "
    "  border-right: 1px solid rgba(0,0,0,0.3); }")
    .arg(QLatin1String(top), QLatin1String(bottom), QLatin1String(hover),
         QLatin1String(pressed), QLatin1String(text),
         QLatin1String(disabledBg), QLatin1String(disabledText));
}

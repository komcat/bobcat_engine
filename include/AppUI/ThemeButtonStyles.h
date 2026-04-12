// ThemeButtonStyles.h
#pragma once

#include "Theme.h"
#include <QString>
#include <QLatin1String>

/**
 * @brief Reusable QSS builders for the gradient-bezel button style.
 *
 * Several pages (TopBar, RunProgramPage, RollCorrectionWidget, ...) use the
 * same bevelled-border + vertical-gradient button style with a hover lift and
 * a pressed sink. Rather than duplicating ~30 lines of QSS at every call site,
 * the call sites read from this header and only specify which palette family
 * the button belongs to.
 *
 * Usage:
 *   button->setStyleSheet(Theme::Buttons::bezel(
 *     Theme::Color::primaryHover,    // light: top of gradient + solid hover fill
 *     Theme::Color::primary,         // dark:  bottom of gradient
 *     Theme::Color::primaryPressed   // pressed: solid fill while pressed
 *   ));
 *
 * For the common semantic variants there are convenience builders:
 *   Theme::Buttons::bezelPrimary()  // blue
 *   Theme::Buttons::bezelSuccess()  // green
 *   Theme::Buttons::bezelWarning()  // orange
 *   Theme::Buttons::bezelDanger()   // red
 */
namespace Theme {
namespace Buttons {

inline QString bezel(const char* light, const char* dark, const char* pressed)
{
    return QStringLiteral(
        "QPushButton { "
        "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2); "
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
        "  background-color: %1; "
        "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
        "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
        "} "
        "QPushButton:pressed { "
        "  background-color: %3; "
        "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
        "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
        "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
        "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
        "  padding-top: 2px; "
        "  padding-left: 2px; "
        "} "
        "QPushButton:disabled { "
        "  background-color: %4; "
        "  color: %5; "
        "  border-top: 1px solid rgba(255, 255, 255, 0.2); "
        "  border-left: 1px solid rgba(255, 255, 255, 0.2); "
        "  border-bottom: 1px solid rgba(0, 0, 0, 0.3); "
        "  border-right: 1px solid rgba(0, 0, 0, 0.3); "
        "}"
    ).arg(QLatin1String(light),
          QLatin1String(dark),
          QLatin1String(pressed),
          QLatin1String(Theme::Color::disabledBg),
          QLatin1String(Theme::Color::textDisabled));
}

inline QString bezelPrimary()
{
    return bezel(Theme::Color::primaryHover,
                 Theme::Color::primary,
                 Theme::Color::primaryPressed);
}

inline QString bezelSuccess()
{
    return bezel(Theme::Color::successHover,
                 Theme::Color::success,
                 Theme::Palette::green700);
}

inline QString bezelWarning()
{
    return bezel(Theme::Color::warningHover,
                 Theme::Color::warning,
                 Theme::Palette::orange700);  // pressed = darkest orange
}

inline QString bezelDanger()
{
    return bezel(Theme::Color::dangerHover,
                 Theme::Color::danger,
                 Theme::Palette::red700);     // pressed = darkest red
}

// Low-emphasis neutral button — for Close, Move Up/Down, Rename, etc.
// Uses derived gray shades so it sits visibly above a dark window background
// without competing with the colored action buttons.
inline QString bezelNeutral()
{
    return bezel(Theme::Palette::gray400,
                 Theme::Palette::gray500,
                 Theme::Palette::gray700);
}

}  // namespace Buttons

// ============================================================================
// Box / panel QSS builders
// ============================================================================
namespace Boxes {

// Standard dark group box used by Edit Program / Recipe Slot / etc. Centers
// the title at the top-left with a subtle border and the deep panel fill.
inline QString groupBoxDark()
{
    return QString(
        "QGroupBox { "
        "  background-color: %1; "
        "  border: 1px solid %2; "
        "  border-radius: 4px; "
        "  margin-top: 10px; "
        "  padding-top: 15px; "
        "  color: %3; "
        "  font-weight: bold; "
        "} "
        "QGroupBox::title { "
        "  subcontrol-origin: margin; "
        "  subcontrol-position: top left; "
        "  padding: 2px 5px; "
        "  color: %4; "
        "}"
    ).arg(QLatin1String(Theme::Color::bgDeep),
          QLatin1String(Theme::Color::borderSubtle),
          QLatin1String(Theme::Color::textSecondary),
          QLatin1String(Theme::Color::textPrimary));
}

}  // namespace Boxes
}  // namespace Theme

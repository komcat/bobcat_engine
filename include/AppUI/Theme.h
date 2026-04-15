// Theme.h
#pragma once

/**
 * @brief Centralized color palette and semantic role constants.
 *
 * Single source of truth for the application's color scheme. All UI code
 * (DarkTheme global QSS, local widget stylesheets, painted graphics) should
 * read from `Theme::Color::*` rather than hardcoding hex literals — that way
 * a palette swap touches one file.
 *
 * Header-only by design: the raw palette constants are `inline constexpr const char*`
 * and the semantic Color role pointers are `inline const char*` (non-constexpr so
 * they can be re-pointed at runtime via `Theme::applyPalette()`). The header can be
 * included from any of the executables that compile DarkTheme.cpp (bobcat_engine,
 * config_editor, data_explorer, vision_profile_tester) with no link-order or extra
 * .cpp pairing required.
 *
 * To swap palettes at runtime:
 *   1. Call `Theme::applyPalette(Theme::ThemeId::CC29)` early in main() / on user action.
 *   2. Re-apply the global QSS (DarkTheme::apply()) so Qt picks up the new values.
 *   3. All `Theme::Color::*` reads throughout the codebase will immediately see the
 *      new values — no further code changes required.
 */
namespace Theme {

// ============================================================================
// Theme identifier
// ============================================================================
enum class ThemeId {
    Default,  // VS Code Dark (original bobcat_engine palette)
    CC29      // CC29 purple/navy dark theme
};

// ============================================================================
// Raw palette — "VS Code Dark" (the original bobcat_engine palette)
// ============================================================================
// Restored after a brief experiment with the "Midnight Cobalt" palette. The
// VS Code-style neutrals are easier on the eyes for an industrial control UI
// and match what operators are used to.
namespace Palette {

// Neutrals (darkest → lightest)
inline constexpr const char* gray950 = "#1e1e1e";  // very dark — dropdowns, headers
inline constexpr const char* gray900 = "#2d2d30";  // window background
inline constexpr const char* gray850 = "#3c3c3c";  // surface (inputs, panels)
inline constexpr const char* gray800 = "#3f3f46";  // subtle border
inline constexpr const char* gray700 = "#454545";  // table alt rows
inline constexpr const char* gray600 = "#4a4a4a";  // hover bg
inline constexpr const char* gray500 = "#555555";  // normal border, disabled bg
inline constexpr const char* gray400 = "#666666";  // scrollbar hover
inline constexpr const char* gray300 = "#888888";  // disabled text
inline constexpr const char* gray200 = "#a0a0a0";  // tertiary text
inline constexpr const char* gray100 = "#d4d4d4";  // secondary text
inline constexpr const char* white   = "#ffffff";  // primary text

// Primary blue
inline constexpr const char* blue700 = "#0c5280";  // pressed
inline constexpr const char* blue500 = "#0e639c";  // primary
inline constexpr const char* blue400 = "#1177bb";  // hover

// Success green
inline constexpr const char* green700 = "#1e7e34";  // pressed
inline constexpr const char* green500 = "#27ae60";  // success
inline constexpr const char* green400 = "#2ecc71";  // hover

// Warning orange
inline constexpr const char* orange700 = "#e67e22"; // hover (was darker side)
inline constexpr const char* orange500 = "#f39c12"; // warning
inline constexpr const char* orange400 = "#f1c40f"; // brighter warning

// Danger red
inline constexpr const char* red700 = "#c0392b";   // pressed/hover
inline constexpr const char* red500 = "#e74c3c";   // danger
inline constexpr const char* red400 = "#ec7063";   // lighter danger

// Accent / status
inline constexpr const char* teal400 = "#4ec9b0";  // teal accent (loop status, etc.)
inline constexpr const char* amber500 = "#ca8a04"; // amber (scan-busy badge)

// ============================================================================
// CC29 sub-palette — purple/navy dark theme
// ============================================================================
namespace CC29 {

// Neutrals
inline constexpr const char* gray950 = "#212123";  // bg (user-specified)
inline constexpr const char* gray900 = "#212123";  // same as bg
inline constexpr const char* gray850 = "#352b42";  // surface (purple)
inline constexpr const char* gray800 = "#45444f";  // subtle border
inline constexpr const char* gray700 = "#3a3858";  // alt rows (navy)
inline constexpr const char* gray600 = "#43436a";  // hover (blue)
inline constexpr const char* gray500 = "#646365";  // normal border
inline constexpr const char* gray400 = "#868188";  // stronger border
inline constexpr const char* gray300 = "#646365";  // disabled text
inline constexpr const char* gray200 = "#b8b5b9";  // tertiary text
inline constexpr const char* gray100 = "#b8b5b9";  // secondary text
inline constexpr const char* white   = "#f2f0e5";  // primary text (cream)

// Primary blue
inline constexpr const char* blue700 = "#3a3858";  // pressed
inline constexpr const char* blue500 = "#4b80ca";  // primary
inline constexpr const char* blue400 = "#68c2d3";  // hover (cyan)

// Success green
inline constexpr const char* green700 = "#4e584a";  // pressed
inline constexpr const char* green500 = "#8ab060";  // success (sage)
inline constexpr const char* green400 = "#c2d368";  // hover (lime)

// Warning
inline constexpr const char* orange700 = "#80493a"; // hover-darker
inline constexpr const char* orange500 = "#d3a068"; // warning (tan)
inline constexpr const char* orange400 = "#edc8c4"; // lighter

// Danger
inline constexpr const char* red700 = "#6a536e";   // pressed
inline constexpr const char* red500 = "#b45252";   // danger (coral)
inline constexpr const char* red400 = "#e5ceb4";   // lighter

// Accent
inline constexpr const char* teal400 = "#a2dcc7";  // mint accent
inline constexpr const char* amber500 = "#cf8acb"; // pink accent (scan badge)

}  // namespace CC29

}  // namespace Palette

// ============================================================================
// Semantic roles — what to use everywhere outside this file
// ============================================================================
// These are aliases for Palette::* values. UI code should reference these
// (e.g. Theme::Color::primary) instead of raw palette names so the role can
// be re-bound to a different palette slot without touching every consumer.
//
// Non-constexpr (plain `inline const char*`) so that applyPalette() can
// re-point them at runtime to a different palette's constants.
namespace Color {

// Backgrounds
inline const char* bgDeep       = Palette::gray950;  // toolbars, menus, headers, dropdowns
inline const char* bgPrimary    = Palette::gray900;  // window background
inline const char* bgSurface    = Palette::gray850;  // panels, input fields
inline const char* bgSurfaceAlt = Palette::gray700;  // alternating-row tables
inline const char* bgHover      = Palette::gray600;  // hover state on inert surfaces

// Borders
inline const char* borderSubtle = Palette::gray800;  // self-blending separators
inline const char* borderNormal = Palette::gray500;  // default widget borders
inline const char* borderStrong = Palette::gray400;  // emphasized borders

// Text
inline const char* textPrimary   = Palette::white;     // body text
inline const char* textSecondary = Palette::gray100;   // softer / supporting text
inline const char* textTertiary  = Palette::gray200;   // hints, placeholders
inline const char* textDisabled  = Palette::gray300;   // disabled controls
inline const char* textOnPrimary = Palette::white;     // text drawn on a primary-fill button

// Disabled background
inline const char* disabledBg = Palette::gray500;

// Primary action (default buttons, focus rings, selection highlights)
inline const char* primary        = Palette::blue500;
inline const char* primaryHover   = Palette::blue400;
inline const char* primaryPressed = Palette::blue700;

// Success (Add, Create, Run, Confirm)
inline const char* success      = Palette::green500;
inline const char* successHover = Palette::green400;

// Warning (Caution, Special-mode)
inline const char* warning      = Palette::orange500;
inline const char* warningHover = Palette::orange700;

// Danger (Delete, Stop, Abort)
inline const char* danger      = Palette::red500;
inline const char* dangerHover = Palette::red700;

// Accent (secondary highlights, badges, scan-busy indicator)
inline const char* accent     = Palette::amber500;
inline const char* accentSoft = Palette::teal400;

}  // namespace Color

// ============================================================================
// Runtime palette switcher
// ============================================================================
// Re-points every Color::* alias to the chosen palette's constants.
// Call this before (re-)applying the global QSS so the stylesheet generator
// picks up the new values.
inline void applyPalette(ThemeId id) {
    if (id == ThemeId::CC29) {
        // Backgrounds
        Color::bgDeep       = Palette::CC29::gray950;
        Color::bgPrimary    = Palette::CC29::gray900;
        Color::bgSurface    = Palette::CC29::gray850;
        Color::bgSurfaceAlt = Palette::CC29::gray700;
        Color::bgHover      = Palette::CC29::gray600;
        // Borders
        Color::borderSubtle = Palette::CC29::gray800;
        Color::borderNormal = Palette::CC29::gray500;
        Color::borderStrong = Palette::CC29::gray400;
        // Text
        Color::textPrimary   = Palette::CC29::white;
        Color::textSecondary = Palette::CC29::gray100;
        Color::textTertiary  = Palette::CC29::gray200;
        Color::textDisabled  = Palette::CC29::gray300;
        Color::textOnPrimary = Palette::CC29::white;
        // Disabled background
        Color::disabledBg = Palette::CC29::gray500;
        // Primary action
        Color::primary        = Palette::CC29::blue500;
        Color::primaryHover   = Palette::CC29::blue400;
        Color::primaryPressed = Palette::CC29::blue700;
        // Success
        Color::success      = Palette::CC29::green500;
        Color::successHover = Palette::CC29::green400;
        // Warning
        Color::warning      = Palette::CC29::orange500;
        Color::warningHover = Palette::CC29::orange700;
        // Danger
        Color::danger      = Palette::CC29::red500;
        Color::dangerHover = Palette::CC29::red700;
        // Accent
        Color::accent     = Palette::CC29::amber500;
        Color::accentSoft = Palette::CC29::teal400;
    } else {
        // Default — VS Code Dark, point back to original Palette::*
        // Backgrounds
        Color::bgDeep       = Palette::gray950;
        Color::bgPrimary    = Palette::gray900;
        Color::bgSurface    = Palette::gray850;
        Color::bgSurfaceAlt = Palette::gray700;
        Color::bgHover      = Palette::gray600;
        // Borders
        Color::borderSubtle = Palette::gray800;
        Color::borderNormal = Palette::gray500;
        Color::borderStrong = Palette::gray400;
        // Text
        Color::textPrimary   = Palette::white;
        Color::textSecondary = Palette::gray100;
        Color::textTertiary  = Palette::gray200;
        Color::textDisabled  = Palette::gray300;
        Color::textOnPrimary = Palette::white;
        // Disabled background
        Color::disabledBg = Palette::gray500;
        // Primary action
        Color::primary        = Palette::blue500;
        Color::primaryHover   = Palette::blue400;
        Color::primaryPressed = Palette::blue700;
        // Success
        Color::success      = Palette::green500;
        Color::successHover = Palette::green400;
        // Warning
        Color::warning      = Palette::orange500;
        Color::warningHover = Palette::orange700;
        // Danger
        Color::danger      = Palette::red500;
        Color::dangerHover = Palette::red700;
        // Accent
        Color::accent     = Palette::amber500;
        Color::accentSoft = Palette::teal400;
    }
}

}  // namespace Theme

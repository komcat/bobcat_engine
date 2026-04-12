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
 * Header-only by design: every constant is `inline constexpr const char*` so
 * the header can be included from any of the executables that compile
 * DarkTheme.cpp (bobcat_engine, config_editor, data_explorer,
 * vision_profile_tester) with no link-order or extra .cpp pairing required.
 *
 * To swap palettes:
 *   1. Update the raw values in `Theme::Palette::*`
 *   2. Re-bind semantic roles in `Theme::Color::*` if needed
 *   3. (No code changes needed in DarkTheme.cpp or any consumer.)
 */
namespace Theme {

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

}  // namespace Palette

// ============================================================================
// Semantic roles — what to use everywhere outside this file
// ============================================================================
// These are aliases for Palette::* values. UI code should reference these
// (e.g. Theme::Color::primary) instead of raw palette names so the role can
// be re-bound to a different palette slot without touching every consumer.
namespace Color {

// Backgrounds
inline constexpr const char* bgDeep       = Palette::gray950;  // toolbars, menus, headers, dropdowns
inline constexpr const char* bgPrimary    = Palette::gray900;  // window background
inline constexpr const char* bgSurface    = Palette::gray850;  // panels, input fields
inline constexpr const char* bgSurfaceAlt = Palette::gray700;  // alternating-row tables
inline constexpr const char* bgHover      = Palette::gray600;  // hover state on inert surfaces

// Borders
inline constexpr const char* borderSubtle = Palette::gray800;  // self-blending separators
inline constexpr const char* borderNormal = Palette::gray500;  // default widget borders
inline constexpr const char* borderStrong = Palette::gray400;  // emphasized borders

// Text
inline constexpr const char* textPrimary   = Palette::white;     // body text
inline constexpr const char* textSecondary = Palette::gray100;   // softer / supporting text
inline constexpr const char* textTertiary  = Palette::gray200;   // hints, placeholders
inline constexpr const char* textDisabled  = Palette::gray300;   // disabled controls
inline constexpr const char* textOnPrimary = Palette::white;     // text drawn on a primary-fill button

// Disabled background
inline constexpr const char* disabledBg = Palette::gray500;

// Primary action (default buttons, focus rings, selection highlights)
inline constexpr const char* primary        = Palette::blue500;
inline constexpr const char* primaryHover   = Palette::blue400;
inline constexpr const char* primaryPressed = Palette::blue700;

// Success (Add, Create, Run, Confirm)
inline constexpr const char* success      = Palette::green500;
inline constexpr const char* successHover = Palette::green400;

// Warning (Caution, Special-mode)
inline constexpr const char* warning      = Palette::orange500;
inline constexpr const char* warningHover = Palette::orange700;

// Danger (Delete, Stop, Abort)
inline constexpr const char* danger      = Palette::red500;
inline constexpr const char* dangerHover = Palette::red700;

// Accent (secondary highlights, badges, scan-busy indicator)
inline constexpr const char* accent     = Palette::amber500;
inline constexpr const char* accentSoft = Palette::teal400;

}  // namespace Color
}  // namespace Theme

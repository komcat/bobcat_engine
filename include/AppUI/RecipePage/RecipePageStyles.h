// RecipePageStyles.h
// Centralized stylesheet constants for RecipePage components
#pragma once

#include <QString>

namespace RecipeStyles {

//=============================================================================
// COLORS
//=============================================================================
namespace Colors {
  const QString BG_DARK = "#1e1e1e";
  const QString BG_MEDIUM = "#2d2d30";
  const QString BG_LIGHT = "#3c3c3c";
  const QString BORDER = "#3f3f46";
  const QString BORDER_LIGHT = "#555555";
  const QString TEXT_PRIMARY = "#d4d4d4";
  const QString TEXT_SECONDARY = "#999999";
  const QString TEXT_CATEGORY = "#dcdcaa";  // Yellow/gold for categories
  const QString TEXT_COMMENT = "#6a9955";   // Green for comments
  const QString ACCENT_BLUE = "#007acc";
  const QString ACCENT_RED = "#ff3333";
  const QString HOVER = "#2a2d2e";
  const QString SELECTED = "#0e639c";
  const QString HIGHLIGHT_EXECUTING = "#264f78";
}

//=============================================================================
// FONTS
//=============================================================================
namespace Fonts {
  const QString MONOSPACE = "'RobotoMono-Regular', 'Roboto Mono', 'Consolas', monospace";
  const int SIZE_SMALL = 12;
  const int SIZE_NORMAL = 13;
  const int SIZE_MEDIUM = 14;
  const int SIZE_LARGE = 16;
  const int SIZE_TITLE = 18;
}

//=============================================================================
// TREE WIDGET STYLES
//=============================================================================

// Process template tree (left panel - available processes)
inline QString processTemplateTree() {
  return QString(
    "QTreeWidget { "
    "  background-color: %1; "
    "  border: 1px solid %2; "
    "  border-radius: 4px; "
    "  color: %3; "
    "  gridline-color: %2; "
    "  font-family: %4; "
    "  font-size: %5px; "
    "} "
    "QTreeWidget::item { "
    "  padding: 5px; "
    "  border-bottom: 1px solid %2; "
    "} "
    "QTreeWidget::item:hover { "
    "  background-color: %6; "
    "} "
    "QTreeWidget::item:selected { "
    "  background-color: %7; "
    "  color: white; "
    "} "
    "QTreeWidget::branch:has-children:closed { "
    "  border-image: none; "
    "} "
    "QTreeWidget::branch:has-children:open { "
    "  border-image: none; "
    "}"
  ).arg(Colors::BG_DARK, Colors::BORDER, Colors::TEXT_PRIMARY,
        Fonts::MONOSPACE, QString::number(Fonts::SIZE_NORMAL),
        Colors::HOVER, Colors::SELECTED);
}

// Recipe steps tree (main recipe list)
inline QString recipeStepsTree() {
  return QString(
    "QTreeWidget { "
    "  background-color: %1; "
    "  border: 1px solid %2; "
    "  border-radius: 4px; "
    "  alternate-background-color: #252526; "
    "  font-family: %3; "
    "  font-size: %4px; "
    "} "
    "QTreeWidget::item { "
    "  padding: 2px; "
    "  margin: 0px; "
    "} "
    "QTreeWidget::item:selected { "
    "  background-color: %5; "
    "} "
    "QTreeWidget::item:hover:!selected { "
    "  background-color: %6; "
    "} "
    "QHeaderView::section { "
    "  background-color: %7; "
    "  color: %8; "
    "  padding: 8px 4px; "
    "  border: none; "
    "  border-bottom: 2px solid %2; "
    "  font-weight: bold; "
    "  font-size: %4px; "
    "}"
  ).arg(Colors::BG_DARK, Colors::BORDER, Fonts::MONOSPACE,
        QString::number(Fonts::SIZE_MEDIUM), Colors::SELECTED,
        Colors::HOVER, Colors::BG_MEDIUM, Colors::TEXT_PRIMARY);
}

//=============================================================================
// BREAKPOINT CHECKBOX
//=============================================================================
inline QString breakpointCheckbox() {
  return QString(
    "QCheckBox { spacing: 0px; }"
    "QCheckBox::indicator { width: 16px; height: 16px; }"
    "QCheckBox::indicator:unchecked { "
    "  background-color: #444444; "
    "  border: 1px solid #666666; "
    "  border-radius: 3px; "
    "}"
    "QCheckBox::indicator:checked { "
    "  background-color: %1; "
    "  border: 1px solid %1; "
    "  border-radius: 3px; "
    "}"
  ).arg(Colors::ACCENT_RED);
}

//=============================================================================
// RUN CHECKBOX (green when enabled)
//=============================================================================
inline QString runCheckbox() {
  return QString(
    "QCheckBox { spacing: 0px; }"
    "QCheckBox::indicator { "
    "  width: 20px; "
    "  height: 20px; "
    "}"
    "QCheckBox::indicator:unchecked { "
    "  background-color: #555555; "
    "  border: 1px solid #666666; "
    "  border-radius: 3px; "
    "}"
    "QCheckBox::indicator:checked { "
    "  background-color: #4caf50; "
    "  border: 1px solid #4caf50; "
    "  border-radius: 3px; "
    "}"
  );
}

//=============================================================================
// STEP LABELS
//=============================================================================
inline QString stepNumberLabel(const QString& bgColor, bool enabled) {
  return QString(
    "QLabel { "
    "  background-color: %1; "
    "  color: %2; "
    "  padding: 4px; "
    "  font-family: %3; "
    "  font-weight: bold; "
    "  font-size: %4px; "
    "}"
  ).arg(bgColor, enabled ? Colors::TEXT_PRIMARY : Colors::TEXT_SECONDARY,
        Fonts::MONOSPACE, QString::number(Fonts::SIZE_MEDIUM));
}

inline QString symbolLabel(const QString& bgColor, bool enabled) {
  return QString(
    "QLabel { "
    "  background-color: %1; "
    "  color: %2; "
    "  padding: 4px; "
    "  font-size: %3px; "
    "}"
  ).arg(bgColor, enabled ? Colors::TEXT_PRIMARY : Colors::TEXT_SECONDARY,
        QString::number(Fonts::SIZE_MEDIUM));
}

inline QString processNameLabel(const QString& bgColor, bool enabled, bool isComment) {
  QString textDecoration = enabled ? "" : "text-decoration: line-through;";
  QString fontStyle = (enabled && isComment) ? "font-style: italic;" : "";
  QString color;

  if (!enabled) {
    color = Colors::TEXT_SECONDARY;
  } else if (isComment) {
    color = Colors::TEXT_COMMENT;
  } else {
    color = Colors::TEXT_PRIMARY;
  }

  return QString(
    "QLabel { "
    "  background-color: %1; "
    "  color: %2; "
    "  padding: 4px 8px; "
    "  font-family: %3; "
    "  font-size: %4px; "
    "  %5"
    "  %6"
    "}"
  ).arg(bgColor, color, Fonts::MONOSPACE,
        QString::number(Fonts::SIZE_NORMAL), textDecoration, fontStyle);
}

// Inline parameter summary label shown in the new "Details" column.
// Italic, dim, monospace, single-line.
inline QString detailsLabel(const QString& bgColor, bool enabled) {
  const QString color = enabled ? Colors::TEXT_SECONDARY : QString("#666666");
  return QString(
    "QLabel { "
    "  background-color: %1; "
    "  color: %2; "
    "  padding: 4px 6px; "
    "  font-family: %3; "
    "  font-size: %4px; "
    "  font-style: italic; "
    "}"
  ).arg(bgColor, color, Fonts::MONOSPACE,
        QString::number(Fonts::SIZE_SMALL));
}

// 3px-wide colored stripe placed flush-left of each step row.
// stripeColorHex is "#RRGGBB" or "transparent".
inline QString categoryStripe(const QString& stripeColorHex) {
  return QString(
    "QWidget { "
    "  background-color: %1; "
    "  min-width: 3px; "
    "  max-width: 3px; "
    "  border: none; "
    "}"
  ).arg(stripeColorHex);
}

//=============================================================================
// BUTTONS
//=============================================================================
inline QString compactButton(const QString& bgColor) {
  return QString(
    "QPushButton { "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 6px 10px; "
    "  min-height: 28px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-radius: 4px; "
    "  background-color: %1; "
    "}"
    "QPushButton:hover { "
    "  background-color: %1; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "}"
    "QPushButton:pressed { "
    "  border-top: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.4); "
    "}"
    "QPushButton:disabled { "
    "  background-color: #555555; "
    "  color: #888888; "
    "}"
  ).arg(bgColor);
}

inline QString runButton() {
  return compactButton("#e07020");  // Orange
}

inline QString runSelectedButton() {
  return compactButton("#2196F3");  // Blue
}

inline QString stepButton() {
  return compactButton("#17a2b8");  // Teal
}

inline QString continueButton() {
  return compactButton("#6c757d");  // Gray
}

inline QString stopButton() {
  return QString(
    "QPushButton { "
    "  background-color: #c62828; "
    "  color: white; "
    "  font-weight: bold; "
    "  padding: 8px 20px; "
    "  border: none; "
    "  border-radius: 4px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #e53935; "
    "} "
    "QPushButton:pressed { "
    "  background-color: #b71c1c; "
    "}"
  );
}

inline QString addProcessButton() {
  return QString(
    "QPushButton { "
    "  background-color: %1; "
    "  color: white; "
    "  font-weight: bold; "
    "  font-size: 18px; "
    "  border-radius: 4px; "
    "  border: 1px solid %2; "
    "}"
    "QPushButton:hover { "
    "  background-color: #3a8a3a; "
    "}"
  ).arg("#2e7d32", Colors::BORDER);
}

//=============================================================================
// INPUT FIELDS
//=============================================================================
inline QString recipeNameEdit() {
  return QString(
    "QLineEdit { "
    "  font-size: %1px; "
    "  font-weight: bold; "
    "  color: %2; "
    "  background-color: %3; "
    "  border: 1px solid %4; "
    "  border-radius: 4px; "
    "  padding: 6px 8px; "
    "}"
    "QLineEdit:focus { "
    "  border: 1px solid %5; "
    "}"
  ).arg(QString::number(Fonts::SIZE_MEDIUM), Colors::TEXT_PRIMARY,
        Colors::BG_MEDIUM, Colors::BORDER, Colors::ACCENT_BLUE);
}

//=============================================================================
// LABELS
//=============================================================================
inline QString titleLabel() {
  return QString(
    "font-weight: bold; "
    "color: %1; "
    "font-size: %2px; "
    "padding: 4px 0px;"
  ).arg(Colors::TEXT_PRIMARY, QString::number(Fonts::SIZE_TITLE));
}

inline QString sectionLabel() {
  return QString(
    "font-weight: bold; "
    "color: %1; "
    "font-size: %2px;"
  ).arg(Colors::TEXT_PRIMARY, QString::number(Fonts::SIZE_MEDIUM));
}

//=============================================================================
// CONTAINER WIDGET (for row backgrounds)
//=============================================================================
inline QString rowContainer(const QString& bgColor) {
  return QString("background-color: %1;").arg(bgColor);
}

//=============================================================================
// EXECUTING STEP HIGHLIGHT
//=============================================================================
inline QString executingStepHighlight() {
  return QString("background-color: %1;").arg(Colors::HIGHLIGHT_EXECUTING);
}

//=============================================================================
// PAGE-LEVEL STYLES
//=============================================================================
inline QString pageBackground() {
  return QString(
    "RecipePage { "
    "  background-color: %1; "
    "  color: %2; "
    "} "
    "QLabel { "
    "  color: %2; "
    "} "
    "QLineEdit { "
    "  background-color: %3; "
    "  color: %2; "
    "  border: 1px solid %4; "
    "  border-radius: 3px; "
    "  padding: 5px; "
    "} "
    "QLineEdit:focus { "
    "  border: 1px solid %5; "
    "} "
    "QScrollArea { "
    "  background-color: %1; "
    "  border: none; "
    "}"
  ).arg(Colors::BG_MEDIUM, Colors::TEXT_PRIMARY, Colors::BG_LIGHT,
        Colors::BORDER_LIGHT, Colors::SELECTED);
}

inline QString backButton() {
  return QString(
    "QPushButton { "
    "  background-color: #3d5a8c; "
    "  color: white; "
    "  border-radius: 3px; "
    "  font-size: 11px; "
    "  font-weight: bold; "
    "  padding: 4px 12px; "
    "} "
    "QPushButton:hover { background-color: #5a8fd6; }"
  );
}

inline QString pageTitleLabel() {
  return QString(
    "font-size: 15px; "
    "font-weight: bold; "
    "color: #ffffff; "
    "padding: 0px; "
    "margin: 0px;"
  );
}

inline QString recipeFileLabel() {
  return QString(
    "font-size: 11px; "
    "color: %1; "
    "padding: 4px; "
    "background-color: %2; "
    "border: 1px solid %3; "
    "border-radius: 3px;"
  ).arg(Colors::TEXT_PRIMARY, Colors::BG_DARK, Colors::BORDER);
}

inline QString productStatusLabel() {
  return QString(
    "font-size: 11px; "
    "color: %1; "
    "padding: 4px;"
  ).arg(Colors::TEXT_PRIMARY);
}

inline QString productStatusLabelLoaded() {
  return QString(
    "font-size: 11px; "
    "color: #4ec9b0; "
    "font-weight: bold;"
  );
}

//=============================================================================
// MENU BAR STYLES
//=============================================================================
inline QString menuBar() {
  return QString(
    "QMenuBar { "
    "  background-color: %1; "
    "  color: %2; "
    "  border: 1px solid %3; "
    "  border-radius: 3px; "
    "  padding: 2px; "
    "  font-size: 12px; "
    "} "
    "QMenuBar::item { "
    "  padding: 4px 8px; "
    "  border-radius: 3px; "
    "} "
    "QMenuBar::item:selected { "
    "  background-color: %4; "
    "} "
    "QMenu { "
    "  background-color: %1; "
    "  color: %2; "
    "  border: 1px solid %3; "
    "} "
    "QMenu::item { "
    "  padding: 5px 20px; "
    "} "
    "QMenu::item:selected { "
    "  background-color: %4; "
    "}"
  ).arg(Colors::BG_LIGHT, Colors::TEXT_PRIMARY, Colors::BORDER, Colors::SELECTED);
}

//=============================================================================
// SMALL TOOL BUTTONS
//=============================================================================
inline QString smallToolButton() {
  return QString(
    "QPushButton { "
    "  background-color: %1; "
    "  color: %2; "
    "  border: 1px solid %3; "
    "  border-radius: 3px; "
    "  padding: 4px 8px; "
    "  font-size: 11px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #4a4a4d; "
    "} "
    "QPushButton:pressed { "
    "  background-color: %4; "
    "}"
  ).arg(Colors::BG_LIGHT, Colors::TEXT_PRIMARY, Colors::BORDER, Colors::BG_DARK);
}

inline QString contextViewerButton() {
  return QString(
    "QPushButton { "
    "  background-color: #4a90a4; "
    "  color: white; "
    "  font-weight: bold; "
    "  font-size: 11px; "
    "  border-radius: 3px; "
    "} "
    "QPushButton:hover { "
    "  background-color: #5aa0b4; "
    "}"
  );
}

inline QString emergencyStopButton() {
  return QString(
    "QPushButton { "
    "  background-color: #c0392b; "
    "  color: white; "
    "  border-radius: 3px; "
    "  font-size: 11px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { background-color: #e74c3c; }"
  );
}

inline QString searchBox() {
  return QString(
    "QLineEdit { "
    "  padding: 5px; "
    "  border: 1px solid %1; "
    "  border-radius: 3px; "
    "  background-color: %2; "
    "  color: %3; "
    "}"
  ).arg(Colors::BORDER_LIGHT, Colors::BG_LIGHT, Colors::TEXT_PRIMARY);
}

inline QString selectedProcessLabel() {
  return QString(
    "QLabel { "
    "  padding: 5px; "
    "  border: 1px solid %1; "
    "  border-radius: 3px; "
    "  background-color: %2; "
    "  color: #4ec9b0; "
    "  font-style: italic; "
    "  font-size: 13px; "
    "}"
  ).arg(Colors::BORDER_LIGHT, Colors::BG_MEDIUM);
}

//=============================================================================
// SCROLL AREA
//=============================================================================
inline QString configScrollArea() {
  return QString(
    "QScrollArea { "
    "  background-color: %1; "
    "  border: 1px solid %2; "
    "} "
    "QScrollArea > QWidget > QWidget { "
    "  background-color: %1; "
    "} "
    "QScrollBar:vertical { "
    "  background: %3; "
    "  width: 12px; "
    "  border-radius: 6px; "
    "} "
    "QScrollBar::handle:vertical { "
    "  background: %4; "
    "  border-radius: 5px; "
    "  min-height: 30px; "
    "} "
    "QScrollBar::handle:vertical:hover { "
    "  background: #666666; "
    "} "
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
    "  height: 0px; "
    "}"
  ).arg(Colors::BG_DARK, Colors::BORDER, Colors::BG_MEDIUM, Colors::BORDER_LIGHT);
}

} // namespace RecipeStyles

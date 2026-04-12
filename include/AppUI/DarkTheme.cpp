// DarkTheme.cpp
#include "DarkTheme.h"
#include "Theme.h"

#include <QString>

// The QSS template uses double-underscore placeholder tokens (e.g. __PRIMARY__)
// that are substituted with semantic colors from `Theme::Color::*` at runtime.
// To swap palettes, edit Theme.h — no changes needed here.
QString DarkTheme::getStyleSheet()
{
    QString css = QStringLiteral(R"(
/* ============================================
   GLOBAL STYLES
   ============================================ */
QWidget {
    background-color: __BG_PRIMARY__;
    color: __TEXT_PRIMARY__;
    font-size: 11px;
}

QMainWindow {
    background-color: __BG_PRIMARY__;
}

QStackedWidget {
    background-color: __BG_PRIMARY__;
}

/* ============================================
   DIALOGS
   ============================================ */
QDialog {
    background-color: __BG_PRIMARY__;
    color: __TEXT_PRIMARY__;
}

QMessageBox {
    background-color: __BG_PRIMARY__;
    color: __TEXT_PRIMARY__;
}

QMessageBox QLabel {
    color: __TEXT_PRIMARY__;
    background-color: transparent;
    min-width: 300px;
}

QMessageBox QPushButton {
    min-width: 80px;
    min-height: 26px;
}

QInputDialog {
    background-color: __BG_PRIMARY__;
    color: __TEXT_PRIMARY__;
}

QInputDialog QLabel {
    color: __TEXT_PRIMARY__;
    background-color: transparent;
}

QInputDialog QLineEdit {
    background-color: __BG_SURFACE__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    padding: 5px;
}

QFileDialog {
    background-color: __BG_PRIMARY__;
    color: __TEXT_PRIMARY__;
}

QFileDialog QLabel {
    background-color: transparent;
    color: __TEXT_PRIMARY__;
}

/* ============================================
   LABELS
   ============================================ */
QLabel {
    background-color: transparent;
    color: __TEXT_PRIMARY__;
}

/* ============================================
   BUTTONS (Default)
   ============================================ */
QPushButton {
    background-color: __PRIMARY__;
    color: __TEXT_ON_PRIMARY__;
    border: 1px solid __PRIMARY_PRESSED__;
    border-radius: 3px;
    padding: 6px 12px;
    font-size: 11px;
    font-weight: 600;
    min-height: 24px;
}

QPushButton:hover {
    background-color: __PRIMARY_HOVER__;
}

QPushButton:pressed {
    background-color: __PRIMARY_PRESSED__;
}

QPushButton:disabled {
    background-color: __DISABLED_BG__;
    color: __TEXT_DISABLED__;
    border-color: __BORDER_SUBTLE__;
}

/* ============================================
   LINE EDITS / TEXT INPUTS
   ============================================ */
QLineEdit {
    background-color: __BG_SURFACE__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    padding: 4px 8px;
    selection-background-color: __PRIMARY__;
}

QLineEdit:focus {
    border: 1px solid __PRIMARY__;
}

QLineEdit:disabled {
    background-color: __BG_PRIMARY__;
    color: __TEXT_DISABLED__;
}

/* ============================================
   TEXT EDITS / MULTI-LINE INPUTS
   ============================================ */
QTextEdit, QPlainTextEdit {
    background-color: __BG_SURFACE__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    selection-background-color: __PRIMARY__;
}

QTextEdit:focus, QPlainTextEdit:focus {
    border: 1px solid __PRIMARY__;
}

/* ============================================
   SPIN BOXES (Number Inputs)
   ============================================ */
QSpinBox, QDoubleSpinBox {
    background-color: __BG_SURFACE__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    padding: 4px;
    min-height: 24px;
}

QSpinBox:focus, QDoubleSpinBox:focus {
    border: 1px solid __PRIMARY__;
}

QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    background-color: __BORDER_NORMAL__;
    border: none;
    width: 16px;
}

QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background-color: __PRIMARY__;
}

QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 5px solid __TEXT_PRIMARY__;
    width: 0px;
    height: 0px;
}

QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid __TEXT_PRIMARY__;
    width: 0px;
    height: 0px;
}

/* ============================================
   COMBO BOXES (Dropdowns)
   ============================================ */
QComboBox {
    background-color: __BG_SURFACE__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    padding: 4px 8px;
    min-height: 24px;
}

QComboBox:hover {
    border-color: __PRIMARY__;
}

QComboBox:focus {
    border: 1px solid __PRIMARY__;
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}

QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid __TEXT_PRIMARY__;
    width: 0px;
    height: 0px;
    margin-right: 5px;
}

QComboBox QAbstractItemView {
    background-color: __BG_DEEP__;
    color: __TEXT_PRIMARY__;
    selection-background-color: __PRIMARY__;
    selection-color: __TEXT_ON_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    outline: 0;
}

/* ============================================
   LIST WIDGETS
   ============================================ */
QListWidget {
    background-color: __BG_SURFACE__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    outline: 0;
}

QListWidget::item {
    padding: 6px;
    border-bottom: 1px solid __BORDER_NORMAL__;
}

QListWidget::item:selected {
    background-color: __PRIMARY__;
    color: __TEXT_ON_PRIMARY__;
}

QListWidget::item:hover {
    background-color: __BG_HOVER__;
}

/* ============================================
   TREE WIDGETS
   ============================================ */
QTreeWidget, QTreeView {
    background-color: __BG_SURFACE__;
    alternate-background-color: __BG_SURFACE__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    outline: 0;
}

QTreeWidget::item {
    padding: 4px;
}

QTreeWidget::item:selected {
    background-color: __PRIMARY__;
    color: __TEXT_ON_PRIMARY__;
}

QTreeWidget::item:hover {
    background-color: __BG_HOVER__;
}

/* ============================================
   TABLE WIDGETS
   ============================================ */
QTableWidget, QTableView {
    background-color: __BG_SURFACE__;
    alternate-background-color: __BG_SURFACE_ALT__;
    color: __TEXT_PRIMARY__;
    gridline-color: __BORDER_NORMAL__;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    selection-background-color: __PRIMARY__;
}

QTableWidget::item, QTableView::item {
    padding: 4px;
}

QTableWidget::item:selected, QTableView::item:selected {
    background-color: __PRIMARY__;
    color: __TEXT_ON_PRIMARY__;
}

QHeaderView::section {
    background-color: __BG_DEEP__;
    color: __TEXT_PRIMARY__;
    padding: 6px;
    border: none;
    border-right: 1px solid __BORDER_NORMAL__;
    border-bottom: 1px solid __BORDER_NORMAL__;
    font-weight: bold;
}

/* ============================================
   GROUP BOXES
   ============================================ */
QGroupBox {
    background-color: __BG_PRIMARY__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 4px;
    margin-top: 12px;
    padding-top: 12px;
    font-weight: bold;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    padding: 0 5px;
    color: __TEXT_PRIMARY__;
}

/* ============================================
   TABS
   ============================================ */
QTabWidget::pane {
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    background-color: __BG_PRIMARY__;
    top: -1px;
}

QTabBar::tab {
    background-color: __BG_SURFACE__;
    color: __TEXT_SECONDARY__;
    padding: 8px 16px;
    margin-right: 2px;
    border-top-left-radius: 3px;
    border-top-right-radius: 3px;
}

QTabBar::tab:selected {
    background-color: __PRIMARY__;
    color: __TEXT_ON_PRIMARY__;
    font-weight: bold;
}

QTabBar::tab:hover:!selected {
    background-color: __BG_HOVER__;
}

/* ============================================
   SCROLL BARS
   ============================================ */
QScrollBar:vertical {
    background-color: __BG_PRIMARY__;
    width: 12px;
    border: none;
}

QScrollBar::handle:vertical {
    background-color: __BORDER_NORMAL__;
    min-height: 20px;
    border-radius: 6px;
    margin: 2px;
}

QScrollBar::handle:vertical:hover {
    background-color: __BORDER_STRONG__;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

QScrollBar:horizontal {
    background-color: __BG_PRIMARY__;
    height: 12px;
    border: none;
}

QScrollBar::handle:horizontal {
    background-color: __BORDER_NORMAL__;
    min-width: 20px;
    border-radius: 6px;
    margin: 2px;
}

QScrollBar::handle:horizontal:hover {
    background-color: __BORDER_STRONG__;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
}

/* ============================================
   CHECKBOXES
   ============================================ */
QCheckBox {
    spacing: 8px;
    color: __TEXT_PRIMARY__;
}

QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    background-color: __BG_SURFACE__;
}

QCheckBox::indicator:checked {
    background-color: __PRIMARY__;
    border-color: __PRIMARY__;
}

QCheckBox::indicator:hover {
    border-color: __PRIMARY__;
}

/* ============================================
   RADIO BUTTONS
   ============================================ */
QRadioButton {
    spacing: 8px;
    color: __TEXT_PRIMARY__;
}

QRadioButton::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 8px;
    background-color: __BG_SURFACE__;
}

QRadioButton::indicator:checked {
    background-color: __PRIMARY__;
    border-color: __PRIMARY__;
}

QRadioButton::indicator:hover {
    border-color: __PRIMARY__;
}

/* ============================================
   SLIDERS
   ============================================ */
QSlider::groove:horizontal {
    background-color: __BG_SURFACE__;
    height: 6px;
    border-radius: 3px;
}

QSlider::handle:horizontal {
    background-color: __PRIMARY__;
    width: 16px;
    margin: -5px 0;
    border-radius: 8px;
}

QSlider::handle:horizontal:hover {
    background-color: __PRIMARY_HOVER__;
}

/* ============================================
   PROGRESS BARS
   ============================================ */
QProgressBar {
    background-color: __BG_SURFACE__;
    border: 1px solid __BORDER_NORMAL__;
    border-radius: 3px;
    text-align: center;
    color: __TEXT_PRIMARY__;
}

QProgressBar::chunk {
    background-color: __SUCCESS_HOVER__;
    border-radius: 2px;
}

/* ============================================
   MENU BAR (if used)
   ============================================ */
QMenuBar {
    background-color: __BG_PRIMARY__;
    color: __TEXT_PRIMARY__;
}

QMenuBar::item:selected {
    background-color: __PRIMARY__;
}

QMenu {
    background-color: __BG_DEEP__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
}

QMenu::item:selected {
    background-color: __PRIMARY__;
}

/* ============================================
   TOOL TIP
   ============================================ */
QToolTip {
    background-color: __BG_DEEP__;
    color: __TEXT_PRIMARY__;
    border: 1px solid __BORDER_NORMAL__;
    padding: 4px;
}

/* ============================================
   FRAMES / SEPARATORS
   ============================================ */
QFrame[frameShape="4"], /* HLine */
QFrame[frameShape="5"]  /* VLine */
{
    background-color: __BORDER_SUBTLE__;
    border: none;
}
)");

    // Substitute semantic placeholders. Replacement order matters: longer
    // tokens must come before shorter prefixes that they contain. The two
    // pairs that need this care are:
    //   __BG_SURFACE_ALT__   before  __BG_SURFACE__
    //   __PRIMARY_PRESSED__  and  __PRIMARY_HOVER__   before  __PRIMARY__
    // Hex expansions (e.g. "#4c2242") never contain "__" so no recursive
    // substitution is possible.
    css.replace(QLatin1String("__BG_DEEP__"),         QLatin1String(Theme::Color::bgDeep));
    css.replace(QLatin1String("__BG_PRIMARY__"),      QLatin1String(Theme::Color::bgPrimary));
    css.replace(QLatin1String("__BG_SURFACE_ALT__"),  QLatin1String(Theme::Color::bgSurfaceAlt));
    css.replace(QLatin1String("__BG_SURFACE__"),      QLatin1String(Theme::Color::bgSurface));
    css.replace(QLatin1String("__BG_HOVER__"),        QLatin1String(Theme::Color::bgHover));

    css.replace(QLatin1String("__BORDER_SUBTLE__"),   QLatin1String(Theme::Color::borderSubtle));
    css.replace(QLatin1String("__BORDER_NORMAL__"),   QLatin1String(Theme::Color::borderNormal));
    css.replace(QLatin1String("__BORDER_STRONG__"),   QLatin1String(Theme::Color::borderStrong));

    css.replace(QLatin1String("__TEXT_PRIMARY__"),    QLatin1String(Theme::Color::textPrimary));
    css.replace(QLatin1String("__TEXT_SECONDARY__"),  QLatin1String(Theme::Color::textSecondary));
    css.replace(QLatin1String("__TEXT_DISABLED__"),   QLatin1String(Theme::Color::textDisabled));
    css.replace(QLatin1String("__TEXT_ON_PRIMARY__"), QLatin1String(Theme::Color::textOnPrimary));

    css.replace(QLatin1String("__DISABLED_BG__"),     QLatin1String(Theme::Color::disabledBg));

    css.replace(QLatin1String("__PRIMARY_PRESSED__"), QLatin1String(Theme::Color::primaryPressed));
    css.replace(QLatin1String("__PRIMARY_HOVER__"),   QLatin1String(Theme::Color::primaryHover));
    css.replace(QLatin1String("__PRIMARY__"),         QLatin1String(Theme::Color::primary));

    css.replace(QLatin1String("__SUCCESS_HOVER__"),   QLatin1String(Theme::Color::successHover));
    css.replace(QLatin1String("__SUCCESS__"),         QLatin1String(Theme::Color::success));
    css.replace(QLatin1String("__WARNING_HOVER__"),   QLatin1String(Theme::Color::warningHover));
    css.replace(QLatin1String("__WARNING__"),         QLatin1String(Theme::Color::warning));
    css.replace(QLatin1String("__DANGER_HOVER__"),    QLatin1String(Theme::Color::dangerHover));
    css.replace(QLatin1String("__DANGER__"),          QLatin1String(Theme::Color::danger));

    return css;
}

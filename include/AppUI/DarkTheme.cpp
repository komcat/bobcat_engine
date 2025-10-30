// DarkTheme.cpp
#include "DarkTheme.h"

QString DarkTheme::getStyleSheet()
{
    return R"(
/* ============================================
   GLOBAL STYLES
   ============================================ */
QWidget {
    background-color: #2d2d30;
    color: #ffffff;
    font-size: 11px;
}

QMainWindow {
    background-color: #2d2d30;
}

QStackedWidget {
    background-color: #2d2d30;
}

/* ============================================
   DIALOGS
   ============================================ */
QDialog {
    background-color: #2d2d30;
    color: #ffffff;
}

QMessageBox {
    background-color: #2d2d30;
    color: #ffffff;
}

QMessageBox QLabel {
    color: #ffffff;
    background-color: transparent;
    min-width: 300px;
}

QMessageBox QPushButton {
    min-width: 80px;
    min-height: 26px;
}

QInputDialog {
    background-color: #2d2d30;
    color: #ffffff;
}

QInputDialog QLabel {
    color: #ffffff;
    background-color: transparent;
}

QInputDialog QLineEdit {
    background-color: #3c3c3c;
    color: #ffffff;
    border: 1px solid #555555;
    padding: 5px;
}

QFileDialog {
    background-color: #2d2d30;
    color: #ffffff;
}

QFileDialog QLabel {
    background-color: transparent;
    color: #ffffff;
}

/* ============================================
   LABELS
   ============================================ */
QLabel {
    background-color: transparent;
    color: #ffffff;
}

/* ============================================
   BUTTONS (Default)
   ============================================ */
QPushButton {
    background-color: #0e639c;
    color: white;
    border: 1px solid #0c5280;
    border-radius: 3px;
    padding: 6px 12px;
    font-size: 11px;
    font-weight: 600;
    min-height: 24px;
}

QPushButton:hover {
    background-color: #1177bb;
}

QPushButton:pressed {
    background-color: #0c5280;
}

QPushButton:disabled {
    background-color: #555555;
    color: #888888;
    border-color: #3f3f46;
}

/* ============================================
   LINE EDITS / TEXT INPUTS
   ============================================ */
QLineEdit {
    background-color: #3c3c3c;
    color: #ffffff;
    border: 1px solid #555555;
    border-radius: 3px;
    padding: 4px 8px;
    selection-background-color: #0e639c;
}

QLineEdit:focus {
    border: 1px solid #0e639c;
}

QLineEdit:disabled {
    background-color: #2d2d30;
    color: #666666;
}

/* ============================================
   TEXT EDITS / MULTI-LINE INPUTS
   ============================================ */
QTextEdit, QPlainTextEdit {
    background-color: #3c3c3c;
    color: #ffffff;
    border: 1px solid #555555;
    border-radius: 3px;
    selection-background-color: #0e639c;
}

QTextEdit:focus, QPlainTextEdit:focus {
    border: 1px solid #0e639c;
}

/* ============================================
   SPIN BOXES (Number Inputs)
   ============================================ */
QSpinBox, QDoubleSpinBox {
    background-color: #3c3c3c;
    color: #ffffff;
    border: 1px solid #555555;
    border-radius: 3px;
    padding: 4px;
    min-height: 24px;
}

QSpinBox:focus, QDoubleSpinBox:focus {
    border: 1px solid #0e639c;
}

QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    background-color: #555555;
    border: none;
    width: 16px;
}

QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background-color: #0e639c;
}

QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 5px solid #ffffff;
    width: 0px;
    height: 0px;
}

QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #ffffff;
    width: 0px;
    height: 0px;
}

/* ============================================
   COMBO BOXES (Dropdowns)
   ============================================ */
QComboBox {
    background-color: #3c3c3c;
    color: #ffffff;
    border: 1px solid #555555;
    border-radius: 3px;
    padding: 4px 8px;
    min-height: 24px;
}

QComboBox:hover {
    border-color: #0e639c;
}

QComboBox:focus {
    border: 1px solid #0e639c;
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}

QComboBox::down-arrow {
    image: none;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 5px solid #ffffff;
    width: 0px;
    height: 0px;
    margin-right: 5px;
}

QComboBox QAbstractItemView {
    background-color: #1e1e1e;
    color: #ffffff;
    selection-background-color: #0e639c;
    selection-color: #ffffff;
    border: 1px solid #555555;
    outline: 0;
}

/* ============================================
   LIST WIDGETS
   ============================================ */
QListWidget {
    background-color: #3c3c3c;
    color: #ffffff;
    border: 1px solid #555555;
    border-radius: 3px;
    outline: 0;
}

QListWidget::item {
    padding: 6px;
    border-bottom: 1px solid #555555;
}

QListWidget::item:selected {
    background-color: #0e639c;
    color: white;
}

QListWidget::item:hover {
    background-color: #4a4a4a;
}

/* ============================================
   TREE WIDGETS
   ============================================ */
QTreeWidget {
    background-color: #3c3c3c;
    color: #ffffff;
    border: 1px solid #555555;
    border-radius: 3px;
    outline: 0;
}

QTreeWidget::item {
    padding: 4px;
}

QTreeWidget::item:selected {
    background-color: #0e639c;
    color: white;
}

QTreeWidget::item:hover {
    background-color: #4a4a4a;
}

/* ============================================
   TABLE WIDGETS
   ============================================ */
QTableWidget, QTableView {
    background-color: #3c3c3c;
    alternate-background-color: #454545;
    color: #ffffff;
    gridline-color: #555555;
    border: 1px solid #555555;
    border-radius: 3px;
    selection-background-color: #0e639c;
}

QTableWidget::item, QTableView::item {
    padding: 4px;
}

QTableWidget::item:selected, QTableView::item:selected {
    background-color: #0e639c;
    color: white;
}

QHeaderView::section {
    background-color: #1e1e1e;
    color: #ffffff;
    padding: 6px;
    border: none;
    border-right: 1px solid #555555;
    border-bottom: 1px solid #555555;
    font-weight: bold;
}

/* ============================================
   GROUP BOXES
   ============================================ */
QGroupBox {
    background-color: #2d2d30;
    color: #ffffff;
    border: 1px solid #555555;
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
    color: #ffffff;
}

/* ============================================
   TABS
   ============================================ */
QTabWidget::pane {
    border: 1px solid #555555;
    border-radius: 3px;
    background-color: #2d2d30;
    top: -1px;
}

QTabBar::tab {
    background-color: #3c3c3c;
    color: #d4d4d4;
    padding: 8px 16px;
    margin-right: 2px;
    border-top-left-radius: 3px;
    border-top-right-radius: 3px;
}

QTabBar::tab:selected {
    background-color: #0e639c;
    color: white;
    font-weight: bold;
}

QTabBar::tab:hover:!selected {
    background-color: #4a4a4a;
}

/* ============================================
   SCROLL BARS
   ============================================ */
QScrollBar:vertical {
    background-color: #2d2d30;
    width: 12px;
    border: none;
}

QScrollBar::handle:vertical {
    background-color: #555555;
    min-height: 20px;
    border-radius: 6px;
    margin: 2px;
}

QScrollBar::handle:vertical:hover {
    background-color: #666666;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

QScrollBar:horizontal {
    background-color: #2d2d30;
    height: 12px;
    border: none;
}

QScrollBar::handle:horizontal {
    background-color: #555555;
    min-width: 20px;
    border-radius: 6px;
    margin: 2px;
}

QScrollBar::handle:horizontal:hover {
    background-color: #666666;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
}

/* ============================================
   CHECKBOXES
   ============================================ */
QCheckBox {
    spacing: 8px;
    color: #ffffff;
}

QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #555555;
    border-radius: 3px;
    background-color: #3c3c3c;
}

QCheckBox::indicator:checked {
    background-color: #0e639c;
    border-color: #0e639c;
}

QCheckBox::indicator:hover {
    border-color: #0e639c;
}

/* ============================================
   RADIO BUTTONS
   ============================================ */
QRadioButton {
    spacing: 8px;
    color: #ffffff;
}

QRadioButton::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #555555;
    border-radius: 8px;
    background-color: #3c3c3c;
}

QRadioButton::indicator:checked {
    background-color: #0e639c;
    border-color: #0e639c;
}

QRadioButton::indicator:hover {
    border-color: #0e639c;
}

/* ============================================
   SLIDERS
   ============================================ */
QSlider::groove:horizontal {
    background-color: #3c3c3c;
    height: 6px;
    border-radius: 3px;
}

QSlider::handle:horizontal {
    background-color: #0e639c;
    width: 16px;
    margin: -5px 0;
    border-radius: 8px;
}

QSlider::handle:horizontal:hover {
    background-color: #1177bb;
}

/* ============================================
   PROGRESS BARS
   ============================================ */
QProgressBar {
    background-color: #3c3c3c;
    border: 1px solid #555555;
    border-radius: 3px;
    text-align: center;
    color: #ffffff;
}

QProgressBar::chunk {
    background-color: #0e639c;
    border-radius: 2px;
}

/* ============================================
   MENU BAR (if used)
   ============================================ */
QMenuBar {
    background-color: #2d2d30;
    color: #ffffff;
}

QMenuBar::item:selected {
    background-color: #0e639c;
}

QMenu {
    background-color: #1e1e1e;
    color: #ffffff;
    border: 1px solid #555555;
}

QMenu::item:selected {
    background-color: #0e639c;
}

/* ============================================
   TOOL TIP
   ============================================ */
QToolTip {
    background-color: #1e1e1e;
    color: #ffffff;
    border: 1px solid #555555;
    padding: 4px;
}

/* ============================================
   FRAMES / SEPARATORS
   ============================================ */
QFrame[frameShape="4"], /* HLine */
QFrame[frameShape="5"]  /* VLine */
{
    background-color: #3f3f46;
    border: none;
}
)";
}

"""Dark, console-style theme for PyQt6 applications.

Reusable across internal Python/Qt tools. Drop-in:

    from PyQt6 import QtWidgets
    import theme
    app = QtWidgets.QApplication(sys.argv)
    theme.apply(app)

    # Color-coded log lines:
    edit.appendHtml(theme.log_html("ERROR", "Connection refused"))

    # Mono font for logs/tables:
    edit.setFont(theme.mono_font(10))

    # Raw palette colors:
    item.setForeground(QtGui.QColor(theme.SUCCESS))
"""

from __future__ import annotations

from datetime import datetime
from html import escape

from PyQt6 import QtGui, QtWidgets
from PyQt6.QtCore import Qt


BG          = "#1e1e1e"
BG_ELEVATED = "#252526"
BG_PANEL    = "#2d2d30"
BG_INPUT    = "#1c1c1c"
BG_HOVER    = "#323234"
BORDER      = "#3c3c3c"
BORDER_HI   = "#505053"

FG          = "#d4d4d4"
FG_MUTED    = "#9e9e9e"
FG_DIM      = "#6a6a6a"

ACCENT      = "#4fc3f7"
SUCCESS     = "#81c784"
WARN        = "#ffb74d"
ERROR       = "#ef5350"
INFO        = "#64b5f6"
MAGENTA     = "#ba68c8"

MONO_FAMILIES = ["Cascadia Code", "JetBrains Mono", "Consolas",
                 "Courier New", "monospace"]
UI_FAMILIES = ["Segoe UI", "Arial", "sans-serif"]

_LEVEL_COLORS = {
    "INFO":  INFO,
    "OK":    SUCCESS,
    "WARN":  WARN,
    "ERROR": ERROR,
    "CMD":   ACCENT,
    "DEBUG": FG_DIM,
}


def level_color(level: str) -> str:
    return _LEVEL_COLORS.get(level.upper(), INFO)


def mono_font(pt: int = 10) -> QtGui.QFont:
    f = QtGui.QFont()
    f.setFamilies(MONO_FAMILIES)
    f.setStyleHint(QtGui.QFont.StyleHint.Monospace)
    f.setFixedPitch(True)
    f.setPointSize(pt)
    return f


def ui_font(pt: int = 10) -> QtGui.QFont:
    f = QtGui.QFont()
    f.setFamilies(UI_FAMILIES)
    f.setStyleHint(QtGui.QFont.StyleHint.SansSerif)
    f.setPointSize(pt)
    return f


def log_html(level: str, msg: str, ts: datetime | None = None) -> str:
    """Render one log line as HTML for appendHtml()."""
    if ts is None:
        ts = datetime.now()
    lvl = level.upper()
    color = level_color(lvl)
    return (
        f'<span style="color:{FG_DIM}">[{ts.strftime("%H:%M:%S")}]</span> '
        f'<span style="color:{color};font-weight:600">{escape(lvl):<5}</span> '
        f'<span style="color:{FG}">{escape(msg)}</span>'
    )


def apply(app: QtWidgets.QApplication, base_pt: int = 10,
          monospace_ui: bool = False) -> None:
    """Apply dark palette + default font + QSS to the application."""
    pal = QtGui.QPalette()
    pal.setColor(QtGui.QPalette.ColorRole.Window,         QtGui.QColor(BG))
    pal.setColor(QtGui.QPalette.ColorRole.WindowText,     QtGui.QColor(FG))
    pal.setColor(QtGui.QPalette.ColorRole.Base,           QtGui.QColor(BG_INPUT))
    pal.setColor(QtGui.QPalette.ColorRole.AlternateBase,  QtGui.QColor(BG_ELEVATED))
    pal.setColor(QtGui.QPalette.ColorRole.Text,           QtGui.QColor(FG))
    pal.setColor(QtGui.QPalette.ColorRole.Button,         QtGui.QColor(BG_PANEL))
    pal.setColor(QtGui.QPalette.ColorRole.ButtonText,     QtGui.QColor(FG))
    pal.setColor(QtGui.QPalette.ColorRole.ToolTipBase,    QtGui.QColor(BG_ELEVATED))
    pal.setColor(QtGui.QPalette.ColorRole.ToolTipText,    QtGui.QColor(FG))
    pal.setColor(QtGui.QPalette.ColorRole.Highlight,      QtGui.QColor(ACCENT))
    pal.setColor(QtGui.QPalette.ColorRole.HighlightedText, QtGui.QColor(BG))
    pal.setColor(QtGui.QPalette.ColorRole.PlaceholderText, QtGui.QColor(FG_DIM))
    pal.setColor(QtGui.QPalette.ColorRole.Link,           QtGui.QColor(ACCENT))
    app.setPalette(pal)

    app.setFont(mono_font(base_pt) if monospace_ui else ui_font(base_pt))

    app.setStyleSheet(_QSS.format(
        BG=BG, BG_ELEVATED=BG_ELEVATED, BG_PANEL=BG_PANEL, BG_INPUT=BG_INPUT,
        BG_HOVER=BG_HOVER, BORDER=BORDER, BORDER_HI=BORDER_HI,
        FG=FG, FG_MUTED=FG_MUTED, FG_DIM=FG_DIM,
        ACCENT=ACCENT, SUCCESS=SUCCESS, WARN=WARN, ERROR=ERROR, INFO=INFO,
    ))


_QSS = """
QWidget {{
    background-color: {BG};
    color: {FG};
}}
QMainWindow, QDialog {{
    background-color: {BG};
}}
QMenuBar {{
    background-color: {BG_ELEVATED};
    color: {FG};
    border-bottom: 1px solid {BORDER};
}}
QMenuBar::item {{
    background: transparent;
    padding: 4px 10px;
}}
QMenuBar::item:selected, QMenuBar::item:pressed {{
    background: {BG_HOVER};
    color: {ACCENT};
}}
QMenu {{
    background-color: {BG_ELEVATED};
    color: {FG};
    border: 1px solid {BORDER};
    padding: 4px;
}}
QMenu::item {{
    padding: 5px 20px;
    border-radius: 3px;
}}
QMenu::item:selected {{
    background: {BG_HOVER};
    color: {ACCENT};
}}
QMenu::separator {{
    height: 1px;
    background: {BORDER};
    margin: 4px 6px;
}}
QLabel {{
    background: transparent;
    color: {FG};
}}
QPushButton {{
    background-color: {BG_PANEL};
    color: {FG};
    border: 1px solid {BORDER};
    border-radius: 4px;
    padding: 5px 14px;
    min-height: 20px;
}}
QPushButton:hover {{
    background-color: {BG_HOVER};
    border-color: {BORDER_HI};
}}
QPushButton:pressed {{
    background-color: {BG_INPUT};
    border-color: {ACCENT};
}}
QPushButton:disabled {{
    color: {FG_DIM};
    background-color: {BG_ELEVATED};
    border-color: {BORDER};
}}
QComboBox {{
    background-color: {BG_PANEL};
    color: {FG};
    border: 1px solid {BORDER};
    border-radius: 4px;
    padding: 4px 10px;
    min-height: 20px;
}}
QComboBox:hover {{ border-color: {BORDER_HI}; }}
QComboBox::drop-down {{ border: none; width: 18px; }}
QComboBox QAbstractItemView {{
    background-color: {BG_ELEVATED};
    color: {FG};
    selection-background-color: {BG_HOVER};
    selection-color: {ACCENT};
    border: 1px solid {BORDER};
}}
QLineEdit, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit {{
    background-color: {BG_INPUT};
    color: {FG};
    border: 1px solid {BORDER};
    border-radius: 3px;
    padding: 3px 6px;
    selection-background-color: {ACCENT};
    selection-color: {BG};
}}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QPlainTextEdit:focus, QTextEdit:focus {{
    border-color: {ACCENT};
}}
QCheckBox {{
    color: {FG};
    spacing: 6px;
}}
QCheckBox::indicator {{
    width: 14px;
    height: 14px;
    border: 1px solid {BORDER_HI};
    border-radius: 3px;
    background: {BG_INPUT};
}}
QCheckBox::indicator:checked {{
    background: {ACCENT};
    border-color: {ACCENT};
}}
QHeaderView::section {{
    background-color: {BG_ELEVATED};
    color: {FG_MUTED};
    border: none;
    border-right: 1px solid {BORDER};
    border-bottom: 1px solid {BORDER};
    padding: 4px 8px;
    font-weight: 600;
}}
QTableWidget, QTableView {{
    background-color: {BG_INPUT};
    alternate-background-color: {BG_ELEVATED};
    color: {FG};
    gridline-color: {BORDER};
    border: 1px solid {BORDER};
}}
QTableWidget::item, QTableView::item {{
    padding: 3px;
}}
QTableWidget::item:selected, QTableView::item:selected {{
    background: {BG_HOVER};
    color: {FG};
}}
QScrollBar:vertical {{
    background: {BG};
    width: 12px;
    margin: 0;
}}
QScrollBar::handle:vertical {{
    background: {BG_PANEL};
    border-radius: 4px;
    min-height: 30px;
}}
QScrollBar::handle:vertical:hover {{ background: {BG_HOVER}; }}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}
QScrollBar:horizontal {{
    background: {BG};
    height: 12px;
    margin: 0;
}}
QScrollBar::handle:horizontal {{
    background: {BG_PANEL};
    border-radius: 4px;
    min-width: 30px;
}}
QScrollBar::handle:horizontal:hover {{ background: {BG_HOVER}; }}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {{ width: 0; }}
QToolTip {{
    background-color: {BG_ELEVATED};
    color: {FG};
    border: 1px solid {BORDER_HI};
    padding: 4px;
}}
QGroupBox {{
    border: 1px solid {BORDER};
    border-radius: 4px;
    margin-top: 12px;
    padding-top: 6px;
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: {FG_MUTED};
}}
"""

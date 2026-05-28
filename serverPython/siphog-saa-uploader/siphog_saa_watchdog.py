"""PyQt6 system-tray watchdog / GUI for upload_saa_to_sheet.

Install:
    pip install PyQt6

Run:
    python siphog_saa_watchdog.py
"""

from __future__ import annotations

import json
import sys
import traceback
from datetime import datetime
from pathlib import Path

from PyQt6 import QtCore, QtGui, QtWidgets

import upload_saa_to_sheet as core
import theme


APP_NAME = "SIPHOG SAA Watchdog"
CONFIG_FILE = Path(__file__).parent / "watchdog_config.json"
DEFAULT_CONFIG = {
    "watchdog_enabled": False,
    "interval_minutes": 15,
    "autostart": False,
    "tab_name": "test",
    "data_folder": str(core.DATA_DIR),
    "scope": {
        "date_preset": "All time",   # one of DATE_PRESETS
        "lot_enabled": False,
        "lot_id": "",
    },
}
SAFE_TAB = "test"
REGISTRY_RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
REGISTRY_VALUE_NAME = "SIPHOGSAAWatchdog"

SAA_FIELDS = [
    "saa_date", "saa_Sled_current", "saa_PD_uA", "saa_Target_V",
    "saa_Actual_V", "saa_Wet_V", "saa_UV_V", "saa_completed",
]

PRESET_ALL = "All time"
DATE_PRESETS = [PRESET_ALL, "Today", "This week", "Last 2 weeks", "This month"]


def load_config() -> dict:
    if CONFIG_FILE.exists():
        try:
            data = json.loads(CONFIG_FILE.read_text())
            return {**DEFAULT_CONFIG, **data}
        except (OSError, json.JSONDecodeError):
            pass
    return dict(DEFAULT_CONFIG)


def save_config(cfg: dict) -> None:
    CONFIG_FILE.write_text(json.dumps(cfg, indent=2))


def resolve_data_folder(cfg: dict) -> Path:
    df = (cfg.get("data_folder") or "").strip()
    return Path(df) if df else core.DATA_DIR


def set_autostart(enabled: bool) -> None:
    import winreg
    key = winreg.OpenKey(
        winreg.HKEY_CURRENT_USER, REGISTRY_RUN_KEY, 0,
        winreg.KEY_SET_VALUE | winreg.KEY_QUERY_VALUE)
    try:
        if enabled:
            exe = f'"{sys.executable}" "{Path(__file__).resolve()}"'
            winreg.SetValueEx(key, REGISTRY_VALUE_NAME, 0, winreg.REG_SZ, exe)
        else:
            try:
                winreg.DeleteValue(key, REGISTRY_VALUE_NAME)
            except FileNotFoundError:
                pass
    finally:
        winreg.CloseKey(key)


def make_tray_icon(color_hex: str) -> QtGui.QIcon:
    pm = QtGui.QPixmap(64, 64)
    pm.fill(QtCore.Qt.GlobalColor.transparent)
    painter = QtGui.QPainter(pm)
    painter.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing)
    painter.setBrush(QtGui.QColor(color_hex))
    painter.setPen(QtGui.QPen(QtGui.QColor("#111"), 4))
    painter.drawEllipse(4, 4, 56, 56)
    painter.end()
    return QtGui.QIcon(pm)


class TabListWorker(QtCore.QThread):
    ok = QtCore.pyqtSignal(list)
    failed = QtCore.pyqtSignal(str)

    def run(self):
        try:
            gc = core.authorize()
            ss = gc.open_by_key(core.SHEET_ID)
            names = [ws.title for ws in ss.worksheets()]
            self.ok.emit(names)
        except Exception as e:
            self.failed.emit(f"{e}\n\n{traceback.format_exc()}")


class LotIDWorker(QtCore.QThread):
    ok = QtCore.pyqtSignal(list, bool)  # lot_ids, lot_column_present
    failed = QtCore.pyqtSignal(str)

    def __init__(self, tab_name: str, parent=None):
        super().__init__(parent)
        self.tab_name = tab_name

    def run(self):
        try:
            gc = core.authorize()
            ws = gc.open_by_key(core.SHEET_ID).worksheet(self.tab_name)
            all_values, col_idx = core.fetch_sheet(ws)
            lots = core.collect_lot_ids(all_values, col_idx)
            self.ok.emit(lots, "Lot ID" in col_idx)
        except Exception as e:
            self.failed.emit(f"{e}\n\n{traceback.format_exc()}")


class UploadWorker(QtCore.QThread):
    finished_ok = QtCore.pyqtSignal(dict, list, list, list)
    failed = QtCore.pyqtSignal(str)

    def __init__(self, apply_mode: bool, tab_name: str, data_folder: Path,
                 scope: dict | None = None, parent=None):
        super().__init__(parent)
        self.apply_mode = apply_mode
        self.tab_name = tab_name
        self.data_folder = data_folder
        self.scope = scope

    def run(self):
        try:
            if not self.data_folder.is_dir():
                raise RuntimeError(
                    f"Data folder does not exist: {self.data_folder}")
            gc = core.authorize()
            ws = gc.open_by_key(core.SHEET_ID).worksheet(self.tab_name)
            all_values, col_idx = core.fetch_sheet(ws)
            counts, rows, cells, warnings = core.compute_plan(
                all_values, col_idx, self.data_folder, scope=self.scope)
            if self.apply_mode and cells:
                core.apply_updates(ws, cells)
            self.finished_ok.emit(counts, rows, cells, warnings)
        except Exception as e:
            self.failed.emit(f"{e}\n\n{traceback.format_exc()}")


class ScopePanel(QtWidgets.QFrame):
    """Filter scope controls: date range, lot ID."""
    changed = QtCore.pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFrameShape(QtWidgets.QFrame.Shape.StyledPanel)
        self.setStyleSheet(
            f"ScopePanel {{ background: {theme.BG_ELEVATED}; "
            f"border: 1px solid {theme.BORDER}; border-radius: 4px; }}")

        row = QtWidgets.QHBoxLayout(self)
        row.setContentsMargins(8, 6, 8, 6)
        row.setSpacing(12)

        title = QtWidgets.QLabel("Scope:")
        title.setStyleSheet(f"color:{theme.FG_MUTED}; font-weight:600;")
        row.addWidget(title)

        date_lbl = QtWidgets.QLabel("Date:")
        date_lbl.setStyleSheet(f"color:{theme.FG_MUTED};")
        self.date_preset = QtWidgets.QComboBox()
        self.date_preset.addItems(DATE_PRESETS)
        self.date_preset.setMinimumWidth(120)
        row.addWidget(date_lbl)
        row.addWidget(self.date_preset)

        row.addSpacing(8)
        self.lot_cb = QtWidgets.QCheckBox("Lot ID")
        self.lot_combo = QtWidgets.QComboBox()
        self.lot_combo.setMinimumWidth(140)
        self.lot_combo.setEditable(True)
        row.addWidget(self.lot_cb)
        row.addWidget(self.lot_combo)

        row.addStretch()

        self.reset_btn = QtWidgets.QPushButton("Reset")
        self.reset_btn.setToolTip("Disable all scope filters")
        self.reset_btn.clicked.connect(self.reset)
        row.addWidget(self.reset_btn)

        self.lot_cb.toggled.connect(self._sync_enabled)
        self.lot_cb.toggled.connect(self.changed)
        self.date_preset.currentTextChanged.connect(self.changed)
        self.lot_combo.currentTextChanged.connect(self.changed)

        self._sync_enabled()

    def _sync_enabled(self):
        self.lot_combo.setEnabled(self.lot_cb.isChecked())

    def set_from_config(self, s: dict):
        b = self.blockSignals(True)
        try:
            preset = s.get("date_preset") or PRESET_ALL
            if preset not in DATE_PRESETS:
                preset = PRESET_ALL
            self.date_preset.setCurrentText(preset)
            self.lot_cb.setChecked(bool(s.get("lot_enabled")))
            self.lot_combo.setCurrentText(s.get("lot_id") or "")
        finally:
            self.blockSignals(b)
        self._sync_enabled()

    def to_config(self) -> dict:
        return {
            "date_preset": self.date_preset.currentText(),
            "lot_enabled": self.lot_cb.isChecked(),
            "lot_id": self.lot_combo.currentText().strip(),
        }

    def to_scope(self) -> dict:
        from datetime import date as _date, timedelta as _td
        s = {}
        preset = self.date_preset.currentText()
        today = _date.today()
        date_range = None
        if preset == "Today":
            date_range = (today, today)
        elif preset == "This week":
            date_range = (today - _td(days=today.weekday()), today)
        elif preset == "Last 2 weeks":
            date_range = (today - _td(days=13), today)
        elif preset == "This month":
            date_range = (today.replace(day=1), today)
        if date_range:
            s["date_from"], s["date_to"] = date_range
        if self.lot_cb.isChecked() and self.lot_combo.currentText().strip():
            s["lot_id"] = self.lot_combo.currentText().strip()
        return s

    def is_any_active(self) -> bool:
        return (self.date_preset.currentText() != PRESET_ALL
                or self.lot_cb.isChecked())

    def set_lot_ids(self, lots: list[str], available: bool):
        current = self.lot_combo.currentText()
        b = self.lot_combo.blockSignals(True)
        try:
            self.lot_combo.clear()
            self.lot_combo.addItems(lots)
            if current:
                self.lot_combo.setCurrentText(current)
        finally:
            self.lot_combo.blockSignals(b)
        self.lot_cb.setEnabled(available)
        if not available:
            self.lot_cb.setChecked(False)
            self.lot_cb.setToolTip("Sheet has no 'Lot ID' column")
        else:
            self.lot_cb.setToolTip("")

    def reset(self):
        self.date_preset.setCurrentText(PRESET_ALL)
        self.lot_cb.setChecked(False)
        self.changed.emit()

    def enable_controls(self, enabled: bool):
        for w in (self.date_preset, self.lot_cb, self.lot_combo,
                  self.reset_btn):
            w.setEnabled(enabled)
        if enabled:
            self._sync_enabled()


class SettingsDialog(QtWidgets.QDialog):
    def __init__(self, cfg: dict, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Settings")
        self.setModal(True)
        self.setMinimumWidth(520)

        form = QtWidgets.QFormLayout()

        self.interval_spin = QtWidgets.QSpinBox()
        self.interval_spin.setRange(1, 1440)
        self.interval_spin.setSuffix(" min")
        self.interval_spin.setValue(cfg["interval_minutes"])
        form.addRow("Watchdog interval:", self.interval_spin)

        self.autostart_cb = QtWidgets.QCheckBox("Start with Windows")
        self.autostart_cb.setChecked(cfg["autostart"])
        form.addRow("", self.autostart_cb)

        folder_row = QtWidgets.QHBoxLayout()
        self.data_folder_edit = QtWidgets.QLineEdit()
        self.data_folder_edit.setText(cfg.get("data_folder") or "")
        self.data_folder_edit.setPlaceholderText(
            f"(empty = default: {core.DATA_DIR})")
        browse_btn = QtWidgets.QPushButton("Browse…")
        browse_btn.clicked.connect(self._browse_folder)
        folder_row.addWidget(self.data_folder_edit, 1)
        folder_row.addWidget(browse_btn)
        form.addRow("Data folder:", folder_row)

        hint = QtWidgets.QLabel(
            "Folder containing the per-PCBA JSON files (filenames like "
            "<i>1234.json</i> or <i>8K7.json</i>).")
        hint.setStyleSheet(f"color:{theme.FG_MUTED};")
        hint.setWordWrap(True)
        form.addRow("", hint)

        buttons = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.StandardButton.Ok |
            QtWidgets.QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)

        layout = QtWidgets.QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(buttons)

    def _browse_folder(self):
        start = self.data_folder_edit.text().strip() or str(core.DATA_DIR)
        chosen = QtWidgets.QFileDialog.getExistingDirectory(
            self, "Select SIPHOG SAA data folder", start)
        if chosen:
            self.data_folder_edit.setText(chosen)

    def result_config(self) -> dict:
        return {
            "interval_minutes": self.interval_spin.value(),
            "autostart": self.autostart_cb.isChecked(),
            "data_folder": self.data_folder_edit.text().strip(),
        }


class MainWindow(QtWidgets.QMainWindow):
    toggle_watchdog_requested = QtCore.pyqtSignal()
    dry_run_requested = QtCore.pyqtSignal()
    upload_requested = QtCore.pyqtSignal()
    settings_requested = QtCore.pyqtSignal()
    open_folder_requested = QtCore.pyqtSignal()
    refresh_tabs_requested = QtCore.pyqtSignal()
    tab_changed = QtCore.pyqtSignal(str)
    quit_requested = QtCore.pyqtSignal()

    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_NAME)
        self.resize(1150, 680)

        menubar = self.menuBar()
        file_menu = menubar.addMenu("&File")
        self.act_settings = file_menu.addAction("Settings…")
        file_menu.addSeparator()
        self.act_open_folder = file_menu.addAction("Open Data Folder")
        self.act_refresh_tabs = file_menu.addAction("Refresh Sheet Tabs")
        file_menu.addSeparator()
        self.act_quit = file_menu.addAction("Quit")
        self.act_settings.triggered.connect(self.settings_requested)
        self.act_open_folder.triggered.connect(self.open_folder_requested)
        self.act_refresh_tabs.triggered.connect(self.refresh_tabs_requested)
        self.act_quit.triggered.connect(self.quit_requested)

        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        vbox = QtWidgets.QVBoxLayout(central)
        vbox.setContentsMargins(10, 10, 10, 10)
        vbox.setSpacing(8)

        top = QtWidgets.QHBoxLayout()
        top.setSpacing(10)

        self.state_label = QtWidgets.QLabel()
        self.state_label.setFont(theme.ui_font(11))
        top.addWidget(self.state_label)

        self.next_run_label = QtWidgets.QLabel()
        self.next_run_label.setStyleSheet(f"color: {theme.FG_MUTED};")
        top.addWidget(self.next_run_label)

        top.addSpacing(12)

        tab_lbl = QtWidgets.QLabel("Target sheet:")
        tab_lbl.setStyleSheet(f"color: {theme.FG_MUTED};")
        top.addWidget(tab_lbl)

        self.tab_combo = QtWidgets.QComboBox()
        self.tab_combo.setMinimumWidth(180)
        self.tab_combo.currentTextChanged.connect(self._on_tab_changed)
        top.addWidget(self.tab_combo)

        self.tab_badge = QtWidgets.QLabel()
        self.tab_badge.setVisible(False)
        top.addWidget(self.tab_badge)

        top.addStretch()

        self.wd_btn = QtWidgets.QPushButton()
        self.dry_btn = QtWidgets.QPushButton("Dry Run")
        self.apply_btn = QtWidgets.QPushButton("Upload Now")
        for b in (self.wd_btn, self.dry_btn, self.apply_btn):
            top.addWidget(b)
        vbox.addLayout(top)

        self.wd_btn.clicked.connect(self.toggle_watchdog_requested)
        self.dry_btn.clicked.connect(self.dry_run_requested)
        self.apply_btn.clicked.connect(self.upload_requested)

        self.scope_panel = ScopePanel()
        vbox.addWidget(self.scope_panel)

        self.summary_label = QtWidgets.QLabel("<span style='color:%s'>No run yet.</span>"
                                              % theme.FG_MUTED)
        self.summary_label.setFont(theme.mono_font(10))
        self.summary_label.setTextFormat(QtCore.Qt.TextFormat.RichText)
        vbox.addWidget(self.summary_label)

        self.table = QtWidgets.QTableWidget(0, 7)
        self.table.setHorizontalHeaderLabels(
            ["PCBA", "Row", "Status", "Field", "Current", "New", "Changed"])
        self.table.horizontalHeader().setSectionResizeMode(
            QtWidgets.QHeaderView.ResizeMode.ResizeToContents)
        self.table.verticalHeader().setDefaultSectionSize(22)
        self.table.setFont(theme.mono_font(10))
        self.table.setSortingEnabled(True)
        self.table.setAlternatingRowColors(True)
        self.table.setEditTriggers(
            QtWidgets.QAbstractItemView.EditTrigger.NoEditTriggers)
        vbox.addWidget(self.table, 1)

        self.log = QtWidgets.QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(500)
        self.log.setMinimumHeight(140)
        self.log.setFont(theme.mono_font(10))
        vbox.addWidget(self.log)

        self._suppress_tab_signal = False

    def set_available_tabs(self, names: list[str], current: str) -> None:
        self._suppress_tab_signal = True
        self.tab_combo.clear()
        self.tab_combo.addItems(names)
        if current in names:
            self.tab_combo.setCurrentText(current)
        elif names:
            self.tab_combo.setCurrentIndex(0)
        self._suppress_tab_signal = False
        self._update_tab_badge(self.tab_combo.currentText())

    def _on_tab_changed(self, name: str) -> None:
        if self._suppress_tab_signal:
            return
        self._update_tab_badge(name)
        self.tab_changed.emit(name)

    def _update_tab_badge(self, name: str) -> None:
        if name and name != SAFE_TAB:
            self.tab_badge.setText(f"  ⚠ PROD: {name}  ")
            self.tab_badge.setStyleSheet(
                f"background:{theme.ERROR}; color:white; "
                f"border-radius:4px; padding:2px 6px; font-weight:700;")
            self.tab_badge.setVisible(True)
            self.setWindowTitle(f"{APP_NAME} — {name.upper()}")
        else:
            self.tab_badge.setVisible(False)
            self.setWindowTitle(APP_NAME)

    def log_line(self, text: str, level: str = "INFO") -> None:
        self.log.appendHtml(theme.log_html(level, text))

    def set_state_display(self, status: str, color: str, wd_on: bool,
                          busy: bool, next_run_text: str) -> None:
        self.state_label.setText(
            f'<span style="color:{color}">●</span> '
            f'<span style="color:{theme.FG};font-weight:600">{status}</span>')
        self.wd_btn.setText("Disable Watchdog" if wd_on else "Enable Watchdog")
        self.dry_btn.setEnabled(not busy)
        self.apply_btn.setEnabled(not busy)
        self.tab_combo.setEnabled(not busy)
        self.scope_panel.enable_controls(not busy)
        self.next_run_label.setText(next_run_text)

    def show_results(self, counts: dict, rows: list,
                     cells: list, warnings: list, mode: str) -> None:
        mode_color = theme.SUCCESS if mode == "upload" else theme.INFO
        oos = counts.get("skipped_out_of_scope", 0)
        oos_color = theme.WARN if oos else theme.FG_MUTED
        summary = (
            f'<span style="color:{mode_color};font-weight:700">{mode}</span>'
            f'  <span style="color:{theme.FG_MUTED}">scanned=</span>'
            f'<span style="color:{theme.FG}">{counts["scanned"]}</span>'
            f'  <span style="color:{theme.FG_MUTED}">not_in_sheet=</span>'
            f'<span style="color:{theme.FG}">{counts["skipped_not_in_sheet"]}</span>'
            f'  <span style="color:{oos_color}">out_of_scope=</span>'
            f'<span style="color:{oos_color};font-weight:600">{oos}</span>'
            f'  <span style="color:{theme.FG_MUTED}">no_data=</span>'
            f'<span style="color:{theme.FG}">{counts["skipped_no_data"]}</span>'
            f'  <span style="color:{theme.FG_MUTED}">not_newer=</span>'
            f'<span style="color:{theme.FG}">{counts["skipped_not_newer"]}</span>'
            f'  <span style="color:{theme.ACCENT};font-weight:700">'
            f'to_update={counts["updated"]}</span>'
            f'  <span style="color:{theme.FG_MUTED}">cells=</span>'
            f'<span style="color:{theme.FG}">{len(cells)}</span>'
        )
        self.summary_label.setText(summary)

        self.table.setSortingEnabled(False)
        self.table.setRowCount(0)
        color_muted = QtGui.QColor(theme.FG_MUTED)
        color_accent = QtGui.QColor(theme.ACCENT)
        color_success = QtGui.QColor(theme.SUCCESS)
        color_warn = QtGui.QColor(theme.WARN)
        color_dim = QtGui.QColor(theme.FG_DIM)

        for change in rows:
            if change["status"] != "UPDATE":
                continue
            for field in SAA_FIELDS:
                old = str(change["old"].get(field, ""))
                new = str(change["new"].get(field, ""))
                changed = old != new
                r = self.table.rowCount()
                self.table.insertRow(r)
                cells_row = [
                    (change["pcba"], color_muted),
                    (str(change["row"]), color_muted),
                    (change["status"], color_accent),
                    (field, color_muted),
                    (old, color_dim if not old else color_warn
                        if changed and old else color_muted),
                    (new, color_success if changed else color_muted),
                    ("yes" if changed else "", color_success),
                ]
                for c, (text, col) in enumerate(cells_row):
                    item = QtWidgets.QTableWidgetItem(text)
                    item.setForeground(col)
                    self.table.setItem(r, c, item)
        self.table.setSortingEnabled(True)

        for w in warnings:
            self.log_line(w, "WARN")

    def closeEvent(self, event: QtGui.QCloseEvent) -> None:
        event.ignore()
        self.hide()


class AppState(QtCore.QObject):
    def __init__(self, app: QtWidgets.QApplication):
        super().__init__()
        self.app = app
        self.cfg = load_config()
        self.status = "idle_on" if self.cfg["watchdog_enabled"] else "idle_off"
        self.worker: UploadWorker | None = None
        self.tab_worker: TabListWorker | None = None
        self.lot_worker: LotIDWorker | None = None
        self.last_mode = "dry-run"

        self.window = MainWindow()
        self.window.set_available_tabs([self.cfg["tab_name"]], self.cfg["tab_name"])
        self.window.scope_panel.set_from_config(self.cfg.get("scope") or {})
        self.window.scope_panel.changed.connect(self.on_scope_changed)
        self.window.toggle_watchdog_requested.connect(self.toggle_watchdog)
        self.window.dry_run_requested.connect(lambda: self.start_run(False))
        self.window.upload_requested.connect(lambda: self.start_run(True))
        self.window.settings_requested.connect(self.open_settings)
        self.window.open_folder_requested.connect(self.open_folder)
        self.window.refresh_tabs_requested.connect(self.refresh_tabs)
        self.window.tab_changed.connect(self.on_tab_changed)
        self.window.quit_requested.connect(self.quit)

        self.tray = QtWidgets.QSystemTrayIcon()
        self.tray.setToolTip(APP_NAME)
        self.menu = QtWidgets.QMenu()
        self.act_show = self.menu.addAction("Show Window")
        self.act_toggle = self.menu.addAction("Enable Watchdog")
        self.menu.addSeparator()
        self.act_dry = self.menu.addAction("Dry Run")
        self.act_run = self.menu.addAction("Upload Now")
        self.menu.addSeparator()
        self.act_folder = self.menu.addAction("Open Data Folder")
        self.act_settings = self.menu.addAction("Settings…")
        self.menu.addSeparator()
        self.act_quit = self.menu.addAction("Quit")
        self.tray.setContextMenu(self.menu)

        self.act_show.triggered.connect(self.show_window)
        self.act_toggle.triggered.connect(self.toggle_watchdog)
        self.act_dry.triggered.connect(lambda: self.start_run(False))
        self.act_run.triggered.connect(lambda: self.start_run(True))
        self.act_folder.triggered.connect(self.open_folder)
        self.act_settings.triggered.connect(self.open_settings)
        self.act_quit.triggered.connect(self.quit)
        self.tray.activated.connect(self.on_tray_activated)

        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.scheduled_run)

        self.tick_timer = QtCore.QTimer()
        self.tick_timer.setInterval(1000)
        self.tick_timer.timeout.connect(self.refresh_next_run)
        self.tick_timer.start()

        self.refresh_ui()
        self.tray.show()
        resolved = resolve_data_folder(self.cfg)
        self.window.log_line(f"Data folder: {resolved}",
                             "OK" if resolved.is_dir() else "WARN")
        if not resolved.is_dir():
            self.window.log_line(
                "Data folder not found; set it in File → Settings…",
                "WARN")
        self.window.log_line("Ready.", "OK")
        self.refresh_tabs()

    def color_for_status(self) -> str:
        return {"idle_off": theme.FG_MUTED,
                "idle_on":  theme.SUCCESS,
                "running":  theme.WARN,
                "error":    theme.ERROR}.get(self.status, theme.FG_MUTED)

    def status_text(self) -> str:
        return {"idle_off": "Watchdog OFF",
                "idle_on":  "Watchdog ON (idle)",
                "running":  "Running…",
                "error":    "Error (see log)"}.get(self.status, self.status)

    def refresh_ui(self) -> None:
        self.tray.setIcon(make_tray_icon(self.color_for_status()))
        self.act_toggle.setText(
            "Disable Watchdog" if self.cfg["watchdog_enabled"]
            else "Enable Watchdog")
        busy = self.status == "running"
        self.act_dry.setEnabled(not busy)
        self.act_run.setEnabled(not busy)
        self.window.set_state_display(
            self.status_text(), self.color_for_status(),
            self.cfg["watchdog_enabled"], busy,
            self._next_run_text())
        if self.cfg["watchdog_enabled"] and self.status != "running":
            interval_ms = self.cfg["interval_minutes"] * 60 * 1000
            if not self.timer.isActive() or self.timer.interval() != interval_ms:
                self.timer.start(interval_ms)
        else:
            self.timer.stop()

    def _next_run_text(self) -> str:
        if not self.cfg["watchdog_enabled"] or self.status == "running":
            return ""
        remaining = self.timer.remainingTime()
        if remaining < 0:
            return ""
        mins, secs = divmod(remaining // 1000, 60)
        return f"Next run in {mins:02d}:{secs:02d}"

    def refresh_next_run(self) -> None:
        self.window.next_run_label.setText(self._next_run_text())

    def on_tray_activated(self, reason) -> None:
        if reason == QtWidgets.QSystemTrayIcon.ActivationReason.DoubleClick:
            self.show_window()

    def show_window(self) -> None:
        self.window.showNormal()
        self.window.raise_()
        self.window.activateWindow()

    def toggle_watchdog(self) -> None:
        self.cfg["watchdog_enabled"] = not self.cfg["watchdog_enabled"]
        save_config(self.cfg)
        if self.status != "running":
            self.status = ("idle_on" if self.cfg["watchdog_enabled"]
                           else "idle_off")
        on = self.cfg["watchdog_enabled"]
        self.window.log_line(
            f"Watchdog {'enabled' if on else 'disabled'}",
            "OK" if on else "INFO")
        self.refresh_ui()

    def on_tab_changed(self, name: str) -> None:
        if not name or name == self.cfg["tab_name"]:
            return
        self.cfg["tab_name"] = name
        save_config(self.cfg)
        level = "WARN" if name != SAFE_TAB else "INFO"
        self.window.log_line(f"Target sheet changed to '{name}'", level)
        self.refresh_lot_ids()

    def on_scope_changed(self) -> None:
        self.cfg["scope"] = self.window.scope_panel.to_config()
        save_config(self.cfg)

    def refresh_lot_ids(self) -> None:
        if self.lot_worker and self.lot_worker.isRunning():
            return
        self.lot_worker = LotIDWorker(self.cfg["tab_name"])
        self.lot_worker.ok.connect(self.on_lots_ok)
        self.lot_worker.failed.connect(self.on_lots_fail)
        self.lot_worker.start()

    def on_lots_ok(self, lots: list, available: bool) -> None:
        self.window.scope_panel.set_lot_ids(lots, available)
        if available:
            self.window.log_line(
                f"Lot IDs loaded: {len(lots)} distinct values", "INFO")
        else:
            self.window.log_line(
                "Sheet has no 'Lot ID' column; lot filter disabled", "WARN")

    def on_lots_fail(self, err: str) -> None:
        first = err.splitlines()[0] if err else "unknown error"
        self.window.log_line(f"Could not load Lot IDs: {first}", "WARN")

    def refresh_tabs(self) -> None:
        if self.tab_worker and self.tab_worker.isRunning():
            return
        self.window.log_line("Loading worksheet list…", "INFO")
        self.tab_worker = TabListWorker()
        self.tab_worker.ok.connect(self.on_tabs_ok)
        self.tab_worker.failed.connect(self.on_tabs_fail)
        self.tab_worker.start()

    def on_tabs_ok(self, names: list) -> None:
        if not names:
            self.window.log_line("Spreadsheet has no worksheets", "ERROR")
            return
        current = self.cfg["tab_name"] if self.cfg["tab_name"] in names else names[0]
        if current != self.cfg["tab_name"]:
            self.window.log_line(
                f"Saved tab '{self.cfg['tab_name']}' not found; "
                f"falling back to '{current}'", "WARN")
            self.cfg["tab_name"] = current
            save_config(self.cfg)
        self.window.set_available_tabs(names, current)
        self.window.log_line(
            f"Worksheets: {', '.join(names)}", "INFO")
        self.refresh_lot_ids()

    def on_tabs_fail(self, err: str) -> None:
        self.window.log_line(
            f"Could not load worksheet list: {err.splitlines()[0]}", "ERROR")

    def open_folder(self) -> None:
        QtGui.QDesktopServices.openUrl(
            QtCore.QUrl.fromLocalFile(str(resolve_data_folder(self.cfg))))

    def open_settings(self) -> None:
        dlg = SettingsDialog(self.cfg, self.window)
        if dlg.exec() != QtWidgets.QDialog.DialogCode.Accepted:
            return
        new = dlg.result_config()
        old_autostart = self.cfg["autostart"]
        old_folder = (self.cfg.get("data_folder") or "").strip()
        self.cfg.update(new)
        save_config(self.cfg)
        if new["autostart"] != old_autostart:
            try:
                set_autostart(new["autostart"])
                self.window.log_line(
                    f"Autostart {'enabled' if new['autostart'] else 'disabled'}",
                    "OK")
            except Exception as e:
                QtWidgets.QMessageBox.warning(
                    self.window, "Autostart failed", str(e))
        if new["data_folder"] != old_folder:
            resolved = resolve_data_folder(self.cfg)
            lvl = "OK" if resolved.is_dir() else "WARN"
            self.window.log_line(f"Data folder set to: {resolved}", lvl)
            if not resolved.is_dir():
                self.window.log_line(
                    "Warning: data folder does not exist", "WARN")
        self.window.log_line(
            f"Settings saved (interval={new['interval_minutes']} min)", "OK")
        self.refresh_ui()

    def scheduled_run(self) -> None:
        if self.status == "running":
            return
        self.window.log_line("Scheduled run triggered", "INFO")
        self.start_run(True)

    def start_run(self, apply_mode: bool) -> None:
        if self.status == "running":
            return
        self.status = "running"
        self.last_mode = "upload" if apply_mode else "dry-run"
        tab = self.cfg["tab_name"]
        tag = "[PROD]" if tab != SAFE_TAB else ""
        scope = self.window.scope_panel.to_scope()
        scope_desc = self._describe_scope(scope)
        self.window.log_line(
            f"Starting {self.last_mode} on '{tab}' {tag}{scope_desc}".strip(),
            "INFO")
        self.refresh_ui()
        self.worker = UploadWorker(
            apply_mode, tab, resolve_data_folder(self.cfg),
            scope=scope or None)
        self.worker.finished_ok.connect(self.on_ok)
        self.worker.failed.connect(self.on_fail)
        self.worker.start()

    def _describe_scope(self, scope: dict) -> str:
        if not scope:
            return ""
        parts = []
        if "date_from" in scope:
            preset = self.window.scope_panel.date_preset.currentText()
            parts.append(f"date={preset} ({scope['date_from']}..{scope['date_to']})")
        if "lot_id" in scope:
            parts.append(f"lot={scope['lot_id']}")
        return f"  scope[{', '.join(parts)}]" if parts else ""

    def on_ok(self, counts, rows, cells, warnings) -> None:
        self.status = ("idle_on" if self.cfg["watchdog_enabled"]
                       else "idle_off")
        suffix = (f"{counts['updated']} rows, {len(cells)} cells"
                  if self.last_mode == "upload"
                  else f"{counts['updated']} rows pending")
        self.window.log_line(f"{self.last_mode} complete: {suffix}", "OK")
        self.window.show_results(counts, rows, cells, warnings, self.last_mode)
        self.tray.setToolTip(
            f"{APP_NAME}\nTab: {self.cfg['tab_name']}\n"
            f"Last {self.last_mode}: {suffix}\n"
            f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        self.refresh_ui()

    def on_fail(self, err: str) -> None:
        self.status = "error"
        first = err.splitlines()[0] if err else "unknown error"
        self.window.log_line(first, "ERROR")
        for line in err.splitlines()[1:]:
            if line.strip():
                self.window.log_line(line, "DEBUG")
        self.tray.showMessage(
            APP_NAME, "Run failed — see window for details.",
            QtWidgets.QSystemTrayIcon.MessageIcon.Critical, 5000)
        self.refresh_ui()

    def quit(self) -> None:
        for w in (self.worker, self.tab_worker, self.lot_worker):
            if w and w.isRunning():
                w.wait(5000)
        self.tray.hide()
        self.app.quit()


def main() -> int:
    app = QtWidgets.QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)
    app.setApplicationName(APP_NAME)
    theme.apply(app, base_pt=10)
    if not QtWidgets.QSystemTrayIcon.isSystemTrayAvailable():
        QtWidgets.QMessageBox.critical(
            None, APP_NAME, "System tray is not available on this system.")
        return 1
    state = AppState(app)
    state.show_window()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())

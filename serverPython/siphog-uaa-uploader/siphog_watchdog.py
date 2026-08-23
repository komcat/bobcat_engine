"""PyQt6 system-tray watchdog / GUI for upload_uaa_to_sheet.

Install:
    pip install PyQt6

Run:
    python siphog_watchdog.py
"""

from __future__ import annotations

import getpass
import json
import re
import sys
import traceback
from datetime import datetime
from pathlib import Path

from PyQt6 import QtCore, QtGui, QtNetwork, QtWidgets

import kumgit_api
import upload_uaa_to_sheet as core
import theme


APP_NAME = "SIPHOG UAA Watchdog"
CONFIG_FILE = Path(__file__).parent / "watchdog_config.json"
DEFAULT_CONFIG = {
    "watchdog_enabled": False,
    "interval_minutes": 15,
    "autostart": False,
    "tab_name": "test",
    "data_folder": "",  # empty string = fall back to script directory
    "data_folder_2": "",  # optional second location (e.g. FireDog products dir)
    # Backup database on kumgit.com. On by default: the sheet is the primary
    # store, and a mirror is only useful if nobody has to remember to turn it
    # on. machine_id says which UAA station this PC is - it must be unique
    # across the stations that share the database.
    "kumgit_enabled": True,
    "kumgit_api_key": "",
    "kumgit_machine_id": kumgit_api.DEFAULT_MACHINE_ID,
    "kumgit_operator_id": "",
    "kumgit_url": kumgit_api.DEFAULT_URL,
    "scope": {
        "bench_enabled": False,
        "bench_min": 1000,
        "bench_max": 9999,
        "date_enabled": False,
        "date_from": "",   # ISO date str
        "date_to": "",
        "lot_enabled": False,
        "lot_id": "",
    },
}
SAFE_TAB = "test"
REGISTRY_RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
REGISTRY_VALUE_NAME = "SIPHOGUAAWatchdog"


def _single_instance_key() -> str:
    """Per-user name for the single-instance endpoint.

    Windows named pipes live in a machine-wide namespace, so a bare name
    would let one logged-in user block another from starting the app. Suffix
    it with the account name, sanitised to alphanumerics (the pipe
    namespace rejects the separator in DOMAIN-qualified names).
    """
    try:
        user = getpass.getuser()
    except (OSError, KeyError):
        user = "unknown"
    user = re.sub(r"[^A-Za-z0-9_-]", "_", user) or "unknown"
    return f"SIPHOG-UAA-Watchdog-single-instance-{user}"


SINGLE_INSTANCE_KEY = _single_instance_key()


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
    """Primary folder - what 'Open Data Folder' opens."""
    df = (cfg.get("data_folder") or "").strip()
    return Path(df) if df else core.DATA_DIR


def resolve_data_folders(cfg: dict) -> list[Path]:
    """Every folder a run scans: the primary plus the optional second one."""
    folders = [resolve_data_folder(cfg)]
    second = (cfg.get("data_folder_2") or "").strip()
    if second and Path(second) not in folders:
        folders.append(Path(second))
    return folders


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

    def __init__(self, apply_mode: bool, tab_name: str,
                 data_folders: list[Path], scope: dict | None = None,
                 parent=None):
        super().__init__(parent)
        self.apply_mode = apply_mode
        self.tab_name = tab_name
        self.data_folders = data_folders
        self.scope = scope

    def run(self):
        try:
            present = [d for d in self.data_folders if d.is_dir()]
            if not present:
                raise RuntimeError(
                    "No data folder exists: "
                    + ", ".join(str(d) for d in self.data_folders))
            gc = core.authorize()
            ws = gc.open_by_key(core.SHEET_ID).worksheet(self.tab_name)
            all_values, col_idx = core.fetch_sheet(ws)
            counts, rows, cells, warnings = core.compute_plan(
                all_values, col_idx, present, scope=self.scope)
            # A configured-but-absent folder is a silent data loss otherwise:
            # the run still "succeeds", just with fewer benches.
            for d in self.data_folders:
                if not d.is_dir():
                    warnings.append(f"data folder not found, skipped: {d}")
            if self.apply_mode and cells:
                core.apply_updates(ws, cells)
            self.finished_ok.emit(counts, rows, cells, warnings)
        except Exception as e:
            self.failed.emit(f"{e}\n\n{traceback.format_exc()}")


class KumgitWorker(QtCore.QThread):
    """Off-thread talker to the kumgit.com backup database.

    Runs after the sheet upload has already been reported, so a slow or dead
    backup server never delays the primary result. Failures are surfaced as
    warnings, not run failures.
    """

    pushed = QtCore.pyqtSignal(dict)
    pinged = QtCore.pyqtSignal(str)
    failed = QtCore.pyqtSignal(str)

    def __init__(self, cfg: dict, records: list | None = None,
                 ping_only: bool = False, parent=None):
        super().__init__(parent)
        self.api_key = (cfg.get("kumgit_api_key") or "").strip()
        self.url = (cfg.get("kumgit_url") or "").strip() or kumgit_api.DEFAULT_URL
        self.records = records or []
        self.ping_only = ping_only

    def run(self):
        try:
            client = kumgit_api.KumgitClient(self.api_key, self.url)
            if self.ping_only:
                self.pinged.emit(client.ping())
            else:
                self.pushed.emit(client.push(self.records))
        except kumgit_api.KumgitError as e:
            self.failed.emit(str(e))
        except Exception as e:
            self.failed.emit(f"{e}\n\n{traceback.format_exc()}")


class ScopePanel(QtWidgets.QFrame):
    """Filter scope controls: bench range, date range, lot ID."""
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

        self.bench_cb = QtWidgets.QCheckBox("Bench")
        self.bench_min = QtWidgets.QSpinBox()
        self.bench_min.setRange(0, 999999)
        self.bench_max = QtWidgets.QSpinBox()
        self.bench_max.setRange(0, 999999)
        self.bench_to = QtWidgets.QLabel("to")
        self.bench_to.setStyleSheet(f"color:{theme.FG_MUTED};")
        row.addWidget(self.bench_cb)
        row.addWidget(self.bench_min)
        row.addWidget(self.bench_to)
        row.addWidget(self.bench_max)

        row.addSpacing(8)
        self.date_cb = QtWidgets.QCheckBox("Date")
        self.date_from = QtWidgets.QDateEdit()
        self.date_from.setCalendarPopup(True)
        self.date_from.setDisplayFormat("M/d/yyyy")
        self.date_to = QtWidgets.QDateEdit()
        self.date_to.setCalendarPopup(True)
        self.date_to.setDisplayFormat("M/d/yyyy")
        self.date_to_lbl = QtWidgets.QLabel("to")
        self.date_to_lbl.setStyleSheet(f"color:{theme.FG_MUTED};")
        row.addWidget(self.date_cb)
        row.addWidget(self.date_from)
        row.addWidget(self.date_to_lbl)
        row.addWidget(self.date_to)

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

        for w in (self.bench_cb, self.date_cb, self.lot_cb):
            w.toggled.connect(self._sync_enabled)
            w.toggled.connect(self.changed)
        for w in (self.bench_min, self.bench_max):
            w.valueChanged.connect(self.changed)
        for w in (self.date_from, self.date_to):
            w.dateChanged.connect(self.changed)
        self.lot_combo.currentTextChanged.connect(self.changed)

        self._sync_enabled()

    def _sync_enabled(self):
        self.bench_min.setEnabled(self.bench_cb.isChecked())
        self.bench_max.setEnabled(self.bench_cb.isChecked())
        self.date_from.setEnabled(self.date_cb.isChecked())
        self.date_to.setEnabled(self.date_cb.isChecked())
        self.lot_combo.setEnabled(self.lot_cb.isChecked())

    def set_from_config(self, s: dict):
        b = self.blockSignals(True)
        try:
            self.bench_cb.setChecked(bool(s.get("bench_enabled")))
            self.bench_min.setValue(int(s.get("bench_min") or 1000))
            self.bench_max.setValue(int(s.get("bench_max") or 9999))
            self.date_cb.setChecked(bool(s.get("date_enabled")))
            today = QtCore.QDate.currentDate()
            df = s.get("date_from") or ""
            dt = s.get("date_to") or ""
            qd_from = QtCore.QDate.fromString(df, "yyyy-MM-dd")
            qd_to = QtCore.QDate.fromString(dt, "yyyy-MM-dd")
            self.date_from.setDate(qd_from if qd_from.isValid()
                                   else today.addDays(-30))
            self.date_to.setDate(qd_to if qd_to.isValid() else today)
            self.lot_cb.setChecked(bool(s.get("lot_enabled")))
            self.lot_combo.setCurrentText(s.get("lot_id") or "")
        finally:
            self.blockSignals(b)
        self._sync_enabled()

    def to_config(self) -> dict:
        return {
            "bench_enabled": self.bench_cb.isChecked(),
            "bench_min": self.bench_min.value(),
            "bench_max": self.bench_max.value(),
            "date_enabled": self.date_cb.isChecked(),
            "date_from": self.date_from.date().toString("yyyy-MM-dd"),
            "date_to": self.date_to.date().toString("yyyy-MM-dd"),
            "lot_enabled": self.lot_cb.isChecked(),
            "lot_id": self.lot_combo.currentText().strip(),
        }

    def to_scope(self) -> dict:
        from datetime import date as _date
        s = {}
        if self.bench_cb.isChecked():
            s["bench_min"] = self.bench_min.value()
            s["bench_max"] = self.bench_max.value()
        if self.date_cb.isChecked():
            qf, qt = self.date_from.date(), self.date_to.date()
            s["date_from"] = _date(qf.year(), qf.month(), qf.day())
            s["date_to"] = _date(qt.year(), qt.month(), qt.day())
        if self.lot_cb.isChecked() and self.lot_combo.currentText().strip():
            s["lot_id"] = self.lot_combo.currentText().strip()
        return s

    def is_any_active(self) -> bool:
        return (self.bench_cb.isChecked() or self.date_cb.isChecked()
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
        self.bench_cb.setChecked(False)
        self.date_cb.setChecked(False)
        self.lot_cb.setChecked(False)
        self.changed.emit()

    def enable_controls(self, enabled: bool):
        for w in (self.bench_cb, self.date_cb, self.lot_cb,
                  self.bench_min, self.bench_max, self.date_from,
                  self.date_to, self.lot_combo, self.reset_btn):
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
            f"(empty = use script directory: {core.DATA_DIR})")
        browse_btn = QtWidgets.QPushButton("Browse\u2026")
        browse_btn.clicked.connect(lambda: self._browse_folder(
            self.data_folder_edit))
        folder_row.addWidget(self.data_folder_edit, 1)
        folder_row.addWidget(browse_btn)
        form.addRow("Data folder:", folder_row)

        folder2_row = QtWidgets.QHBoxLayout()
        self.data_folder2_edit = QtWidgets.QLineEdit()
        self.data_folder2_edit.setText(cfg.get("data_folder_2") or "")
        self.data_folder2_edit.setPlaceholderText("(optional - leave empty to skip)")
        browse2_btn = QtWidgets.QPushButton("Browse\u2026")
        browse2_btn.clicked.connect(lambda: self._browse_folder(
            self.data_folder2_edit))
        folder2_row.addWidget(self.data_folder2_edit, 1)
        folder2_row.addWidget(browse2_btn)
        form.addRow("Second folder:", folder2_row)

        hint = QtWidgets.QLabel(
            "Folders holding the per-bench data. Both layouts are read: "
            "<i>1234.json</i> (bobcat_engine) and <i>1234/dut.json</i> "
            "(FireDog). If the same bench appears in both folders the newest "
            "reading wins.")
        hint.setStyleSheet(f"color:{theme.FG_MUTED};")
        hint.setWordWrap(True)
        form.addRow("", hint)

        section = QtWidgets.QLabel("Backup database (kumgit.com)")
        section.setStyleSheet("font-weight:700;")
        form.addRow(section)

        self.kumgit_cb = QtWidgets.QCheckBox(
            "Mirror every upload to kumgit.com")
        self.kumgit_cb.setChecked(bool(cfg.get("kumgit_enabled", True)))
        form.addRow("", self.kumgit_cb)

        self.machine_edit = QtWidgets.QLineEdit(
            cfg.get("kumgit_machine_id") or kumgit_api.DEFAULT_MACHINE_ID)
        self.machine_edit.setMaxLength(50)
        form.addRow("Machine ID:", self.machine_edit)

        self.operator_edit = QtWidgets.QLineEdit(
            cfg.get("kumgit_operator_id") or "")
        self.operator_edit.setMaxLength(50)
        self.operator_edit.setPlaceholderText("(optional)")
        form.addRow("Operator ID:", self.operator_edit)

        key_row = QtWidgets.QHBoxLayout()
        self.apikey_edit = QtWidgets.QLineEdit(cfg.get("kumgit_api_key") or "")
        self.apikey_edit.setEchoMode(QtWidgets.QLineEdit.EchoMode.Password)
        self.apikey_edit.setPlaceholderText("X-API-Key with UAA scope")
        show_key = QtWidgets.QCheckBox("show")
        show_key.toggled.connect(lambda on: self.apikey_edit.setEchoMode(
            QtWidgets.QLineEdit.EchoMode.Normal if on
            else QtWidgets.QLineEdit.EchoMode.Password))
        key_row.addWidget(self.apikey_edit, 1)
        key_row.addWidget(show_key)
        form.addRow("API key:", key_row)

        self.kumgit_url_edit = QtWidgets.QLineEdit(cfg.get("kumgit_url") or "")
        self.kumgit_url_edit.setPlaceholderText(kumgit_api.DEFAULT_URL)
        form.addRow("Endpoint:", self.kumgit_url_edit)

        kumgit_hint = QtWidgets.QLabel(
            "<b>Machine ID</b> names this station in the shared database, so "
            "every UAA PC needs its own (<i>UAA1</i>, <i>UAA2</i>, \u2026). "
            "The backup runs in the background after the sheet is written; "
            "records the server could not take are kept in "
            "<i>kumgit_pending.json</i> and retried on the next run.")
        kumgit_hint.setStyleSheet(f"color:{theme.FG_MUTED};")
        kumgit_hint.setWordWrap(True)
        form.addRow("", kumgit_hint)

        buttons = QtWidgets.QDialogButtonBox(
            QtWidgets.QDialogButtonBox.StandardButton.Ok |
            QtWidgets.QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)

        layout = QtWidgets.QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(buttons)

    def _browse_folder(self, edit: QtWidgets.QLineEdit):
        start = (edit.text().strip()
                 or self.data_folder_edit.text().strip()
                 or str(core.DATA_DIR))
        chosen = QtWidgets.QFileDialog.getExistingDirectory(
            self, "Select SIPHOG data folder", start)
        if chosen:
            edit.setText(chosen)

    def result_config(self) -> dict:
        return {
            "interval_minutes": self.interval_spin.value(),
            "autostart": self.autostart_cb.isChecked(),
            "data_folder": self.data_folder_edit.text().strip(),
            "data_folder_2": self.data_folder2_edit.text().strip(),
            "kumgit_enabled": self.kumgit_cb.isChecked(),
            "kumgit_machine_id": (self.machine_edit.text().strip()
                                  or kumgit_api.DEFAULT_MACHINE_ID),
            "kumgit_operator_id": self.operator_edit.text().strip(),
            "kumgit_api_key": self.apikey_edit.text().strip(),
            "kumgit_url": self.kumgit_url_edit.text().strip(),
        }


class MainWindow(QtWidgets.QMainWindow):
    toggle_watchdog_requested = QtCore.pyqtSignal()
    dry_run_requested = QtCore.pyqtSignal()
    upload_requested = QtCore.pyqtSignal()
    settings_requested = QtCore.pyqtSignal()
    open_folder_requested = QtCore.pyqtSignal()
    refresh_tabs_requested = QtCore.pyqtSignal()
    toggle_kumgit_requested = QtCore.pyqtSignal(bool)
    kumgit_test_requested = QtCore.pyqtSignal()
    kumgit_flush_requested = QtCore.pyqtSignal()
    tab_changed = QtCore.pyqtSignal(str)
    quit_requested = QtCore.pyqtSignal()

    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_NAME)
        self.resize(1150, 680)

        menubar = self.menuBar()
        file_menu = menubar.addMenu("&File")
        self.act_settings = file_menu.addAction("Settings\u2026")
        file_menu.addSeparator()
        self.act_open_folder = file_menu.addAction("Open Data Folder")
        self.act_refresh_tabs = file_menu.addAction("Refresh Sheet Tabs")
        file_menu.addSeparator()
        self.act_quit = file_menu.addAction("Quit")
        backup_menu = menubar.addMenu("&Backup")
        self.act_kumgit = backup_menu.addAction("Mirror to kumgit.com")
        self.act_kumgit.setCheckable(True)
        backup_menu.addSeparator()
        self.act_kumgit_test = backup_menu.addAction("Test Connection")
        self.act_kumgit_flush = backup_menu.addAction("Send Pending Records Now")

        self.act_settings.triggered.connect(self.settings_requested)
        self.act_open_folder.triggered.connect(self.open_folder_requested)
        self.act_refresh_tabs.triggered.connect(self.refresh_tabs_requested)
        self.act_quit.triggered.connect(self.quit_requested)
        self.act_kumgit.toggled.connect(self.toggle_kumgit_requested)
        self.act_kumgit_test.triggered.connect(self.kumgit_test_requested)
        self.act_kumgit_flush.triggered.connect(self.kumgit_flush_requested)

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

        # Several stations upload to the same backup database, so which one
        # this PC claims to be has to be visible without opening Settings.
        self.machine_label = QtWidgets.QLabel()
        self.machine_label.setFont(theme.mono_font(10))
        top.addWidget(self.machine_label)

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
            ["Bench", "Row", "Status", "Field", "Current", "New", "Changed"])
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

    def set_backup_display(self, machine_id: str, enabled: bool) -> None:
        self.act_kumgit.blockSignals(True)
        self.act_kumgit.setChecked(enabled)
        self.act_kumgit.blockSignals(False)
        self.act_kumgit_test.setEnabled(enabled)
        self.act_kumgit_flush.setEnabled(enabled)
        if enabled:
            self.machine_label.setText(
                f'<span style="color:{theme.FG_MUTED}">backup </span>'
                f'<span style="color:{theme.SUCCESS};font-weight:700">'
                f'{machine_id}</span>'
                f'<span style="color:{theme.FG_MUTED}"> \u2192 kumgit</span>')
            self.machine_label.setToolTip(
                f"Uploads are mirrored to kumgit.com as machine {machine_id}")
        else:
            self.machine_label.setText(
                f'<span style="color:{theme.FG_MUTED}">backup </span>'
                f'<span style="color:{theme.WARN};font-weight:700">off</span>')
            self.machine_label.setToolTip(
                "kumgit.com backup is disabled (Backup menu)")

    def _on_tab_changed(self, name: str) -> None:
        if self._suppress_tab_signal:
            return
        self._update_tab_badge(name)
        self.tab_changed.emit(name)

    def _update_tab_badge(self, name: str) -> None:
        if name and name != SAFE_TAB:
            self.tab_badge.setText(f"  \u26a0 PROD: {name}  ")
            self.tab_badge.setStyleSheet(
                f"background:{theme.ERROR}; color:white; "
                f"border-radius:4px; padding:2px 6px; font-weight:700;")
            self.tab_badge.setVisible(True)
            self.setWindowTitle(f"{APP_NAME} \u2014 {name.upper()}")
        else:
            self.tab_badge.setVisible(False)
            self.setWindowTitle(APP_NAME)

    def log_line(self, text: str, level: str = "INFO") -> None:
        self.log.appendHtml(theme.log_html(level, text))

    def set_state_display(self, status: str, color: str, wd_on: bool,
                          busy: bool, next_run_text: str) -> None:
        self.state_label.setText(
            f'<span style="color:{color}">\u25cf</span> '
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
            for field in ["uaa_date", "uaa_dry", "uaa_uv", "uaa_bake",
                          "uaa_completed"]:
                old = str(change["old"].get(field, ""))
                new = str(change["new"].get(field, ""))
                changed = old != new
                r = self.table.rowCount()
                self.table.insertRow(r)
                cells_row = [
                    (change["bench"], color_muted),
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
        self.kumgit_worker: KumgitWorker | None = None
        # Records that arrived while a backup push was already in flight.
        # Queued in memory rather than written to the spool, which the worker
        # owns for the duration of a push.
        self.kumgit_queue: list[dict] = []
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
        self.window.toggle_kumgit_requested.connect(self.set_kumgit_enabled)
        self.window.kumgit_test_requested.connect(self.test_kumgit)
        self.window.kumgit_flush_requested.connect(self.flush_kumgit)
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
        self.act_kumgit = self.menu.addAction("Mirror to kumgit.com")
        self.act_kumgit.setCheckable(True)
        self.menu.addSeparator()
        self.act_folder = self.menu.addAction("Open Data Folder")
        self.act_settings = self.menu.addAction("Settings\u2026")
        self.menu.addSeparator()
        self.act_quit = self.menu.addAction("Quit")
        self.tray.setContextMenu(self.menu)

        self.act_show.triggered.connect(self.show_window)
        self.act_toggle.triggered.connect(self.toggle_watchdog)
        self.act_dry.triggered.connect(lambda: self.start_run(False))
        self.act_run.triggered.connect(lambda: self.start_run(True))
        self.act_kumgit.toggled.connect(self.set_kumgit_enabled)
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
        folders = resolve_data_folders(self.cfg)
        for f in folders:
            self.window.log_line(f"Data folder: {f}",
                                 "OK" if f.is_dir() else "WARN")
        if not any(f.is_dir() for f in folders):
            self.window.log_line(
                "Data folder not found; set it in File \u2192 Settings\u2026",
                "WARN")
        self.window.log_line("Ready.", "OK")
        self.refresh_tabs()
        self.report_kumgit_state()

    def color_for_status(self) -> str:
        return {"idle_off": theme.FG_MUTED,
                "idle_on":  theme.SUCCESS,
                "running":  theme.WARN,
                "error":    theme.ERROR}.get(self.status, theme.FG_MUTED)

    def status_text(self) -> str:
        return {"idle_off": "Watchdog OFF",
                "idle_on":  "Watchdog ON (idle)",
                "running":  "Running\u2026",
                "error":    "Error (see log)"}.get(self.status, self.status)

    def refresh_ui(self) -> None:
        self.tray.setIcon(make_tray_icon(self.color_for_status()))
        self.act_toggle.setText(
            "Disable Watchdog" if self.cfg["watchdog_enabled"]
            else "Enable Watchdog")
        busy = self.status == "running"
        self.act_dry.setEnabled(not busy)
        self.act_run.setEnabled(not busy)
        backup_on = bool(self.cfg.get("kumgit_enabled", True))
        self.act_kumgit.blockSignals(True)
        self.act_kumgit.setChecked(backup_on)
        self.act_kumgit.blockSignals(False)
        self.window.set_backup_display(self.machine_id(), backup_on)
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
        self.window.log_line("Loading worksheet list\u2026", "INFO")
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
        old_folders = [(self.cfg.get("data_folder") or "").strip(),
                       (self.cfg.get("data_folder_2") or "").strip()]
        old_machine = self.machine_id()
        old_key = (self.cfg.get("kumgit_api_key") or "").strip()
        old_url = (self.cfg.get("kumgit_url") or "").strip()
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
        if [new["data_folder"], new["data_folder_2"]] != old_folders:
            for resolved in resolve_data_folders(self.cfg):
                lvl = "OK" if resolved.is_dir() else "WARN"
                self.window.log_line(f"Data folder set to: {resolved}", lvl)
                if not resolved.is_dir():
                    self.window.log_line(
                        f"Warning: {resolved} does not exist", "WARN")
        if new["kumgit_machine_id"] != old_machine:
            self.window.log_line(
                f"Backup machine ID set to {new['kumgit_machine_id']}", "OK")
        self.window.log_line(
            f"Settings saved (interval={new['interval_minutes']} min)", "OK")
        self.refresh_ui()
        if (new["kumgit_enabled"]
                and (new["kumgit_api_key"] or "").strip()
                and (new["kumgit_api_key"] != old_key
                     or new["kumgit_url"] != old_url)):
            # A key arriving for the first time usually has a backlog waiting
            # behind it. Sending that backlog proves the key works, so do it
            # instead of a bare ping.
            if kumgit_api.load_pending():
                self.flush_kumgit()
            else:
                self.test_kumgit()

    # -- kumgit.com backup database ---------------------------------------

    def machine_id(self) -> str:
        return (self.cfg.get("kumgit_machine_id") or "").strip() \
            or kumgit_api.DEFAULT_MACHINE_ID

    def report_kumgit_state(self) -> None:
        """Log what the backup will do, and prove the key works if it can."""
        if not self.cfg.get("kumgit_enabled", True):
            self.window.log_line("kumgit.com backup: disabled", "INFO")
            return
        pending = len(kumgit_api.load_pending())
        tail = f", {pending} record(s) waiting to send" if pending else ""
        self.window.log_line(
            f"kumgit.com backup: on as machine {self.machine_id()}{tail}", "OK")
        if not (self.cfg.get("kumgit_api_key") or "").strip():
            self.window.log_line(
                "kumgit.com backup: no API key set \u2014 uploads will be held "
                "until one is entered in File \u2192 Settings\u2026", "WARN")
            return
        self.start_kumgit(ping_only=True)

    def set_kumgit_enabled(self, enabled: bool) -> None:
        if bool(self.cfg.get("kumgit_enabled", True)) == bool(enabled):
            return
        self.cfg["kumgit_enabled"] = bool(enabled)
        save_config(self.cfg)
        self.window.log_line(
            f"kumgit.com backup {'enabled' if enabled else 'disabled'}",
            "OK" if enabled else "WARN")
        self.refresh_ui()

    def start_kumgit(self, records: list | None = None,
                     ping_only: bool = False) -> bool:
        """Start a backup thread unless one is already running."""
        if self.kumgit_worker is not None and self.kumgit_worker.isRunning():
            if records:
                self.kumgit_queue.extend(records)
            return False
        self.kumgit_worker = KumgitWorker(
            self.cfg, records=records, ping_only=ping_only)
        self.kumgit_worker.pushed.connect(self.on_kumgit_pushed)
        self.kumgit_worker.pinged.connect(self.on_kumgit_pinged)
        self.kumgit_worker.failed.connect(self.on_kumgit_failed)
        self.kumgit_worker.start()
        return True

    def backup_to_kumgit(self, rows: list) -> None:
        """Mirror an applied upload to the backup database."""
        if self.last_mode != "upload" or not self.cfg.get("kumgit_enabled", True):
            return
        records = kumgit_api.build_records(
            rows, self.machine_id(),
            self.cfg.get("kumgit_operator_id") or "")
        if not (self.cfg.get("kumgit_api_key") or "").strip():
            if records:
                # Spool rather than drop: once the sheet row is written this
                # bench never shows up as an update again.
                kumgit_api.save_pending(kumgit_api.merge_pending(
                    kumgit_api.load_pending(), records))
                self.window.log_line(
                    f"kumgit.com backup: {len(records)} record(s) held, "
                    f"no API key set", "WARN")
            return
        if not records and not kumgit_api.load_pending():
            return
        self.window.log_line(
            f"kumgit.com backup: sending {len(records)} record(s) as "
            f"{self.machine_id()}\u2026", "INFO")
        self.start_kumgit(records)

    def flush_kumgit(self) -> None:
        """Backup menu: retry whatever the spool still owes."""
        pending = len(kumgit_api.load_pending())
        if not pending:
            self.window.log_line("kumgit.com backup: nothing pending", "INFO")
            return
        self.window.log_line(
            f"kumgit.com backup: retrying {pending} pending record(s)\u2026",
            "INFO")
        if not self.start_kumgit([]):
            self.window.log_line("kumgit.com backup: already running", "INFO")

    def test_kumgit(self) -> None:
        if not (self.cfg.get("kumgit_api_key") or "").strip():
            self.window.log_line(
                "kumgit.com backup: no API key set (File \u2192 Settings\u2026)",
                "WARN")
            return
        self.window.log_line("kumgit.com backup: testing connection\u2026", "INFO")
        if not self.start_kumgit(ping_only=True):
            self.window.log_line("kumgit.com backup: already running", "INFO")

    def on_kumgit_pinged(self, message: str) -> None:
        self.window.log_line(f"kumgit.com backup: {message}", "OK")
        self._drain_kumgit_queue()

    def on_kumgit_pushed(self, summary: dict) -> None:
        saved = summary.get("saved", 0)
        pending = summary.get("pending", 0)
        dropped = summary.get("dropped", 0)
        errors = summary.get("errors") or []
        level = "OK" if not (pending or dropped or errors) else "WARN"
        self.window.log_line(
            f"kumgit.com backup: {saved} saved, {pending} pending, "
            f"{dropped} rejected", level)
        for err in errors[:5]:
            self.window.log_line(f"kumgit.com backup: {err}", "WARN")
        if len(errors) > 5:
            self.window.log_line(
                f"kumgit.com backup: {len(errors) - 5} more error(s) suppressed",
                "WARN")
        self._drain_kumgit_queue()

    def on_kumgit_failed(self, err: str) -> None:
        first = err.splitlines()[0] if err else "unknown error"
        self.window.log_line(
            f"kumgit.com backup failed: {first} (data kept for retry)", "WARN")
        for line in err.splitlines()[1:]:
            if line.strip():
                self.window.log_line(line, "DEBUG")
        self._drain_kumgit_queue()

    def _drain_kumgit_queue(self) -> None:
        if not self.kumgit_queue:
            return
        queued, self.kumgit_queue = self.kumgit_queue, []
        self.start_kumgit(queued)

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
            apply_mode, tab, resolve_data_folders(self.cfg),
            scope=scope or None)
        self.worker.finished_ok.connect(self.on_ok)
        self.worker.failed.connect(self.on_fail)
        self.worker.start()

    @staticmethod
    def _describe_scope(scope: dict) -> str:
        if not scope:
            return ""
        parts = []
        if "bench_min" in scope:
            parts.append(f"bench={scope['bench_min']}..{scope['bench_max']}")
        if "date_from" in scope:
            parts.append(f"date={scope['date_from']}..{scope['date_to']}")
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
        self.backup_to_kumgit(rows)
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
            APP_NAME, "Run failed \u2014 see window for details.",
            QtWidgets.QSystemTrayIcon.MessageIcon.Critical, 5000)
        self.refresh_ui()

    def quit(self) -> None:
        for w in (self.worker, self.tab_worker, self.lot_worker,
                  self.kumgit_worker):
            if w and w.isRunning():
                w.wait(5000)
        self.tray.hide()
        self.app.quit()


class SingleInstanceGuard(QtCore.QObject):
    """Allow only one running copy; a second launch wakes the first one.

    Uses a named local socket (a named pipe on Windows). The first process
    listens on SINGLE_INSTANCE_KEY; later processes connect to it, send a
    wake-up byte and exit, which makes the already-running tray app pop its
    window to the front instead of starting a duplicate watchdog.
    """

    activated = QtCore.pyqtSignal()

    def __init__(self, key: str = SINGLE_INSTANCE_KEY,
                 parent: QtCore.QObject | None = None):
        super().__init__(parent)
        self.key = key
        self.server: QtNetwork.QLocalServer | None = None

    def try_acquire(self) -> bool:
        """True if this process owns the single-instance slot."""
        probe = QtNetwork.QLocalSocket()
        probe.connectToServer(self.key)
        if probe.waitForConnected(500):
            probe.write(b"SHOW")
            probe.waitForBytesWritten(500)
            probe.disconnectFromServer()
            return False

        # No live owner. Clear any stale endpoint left by a crashed process
        # (matters on Unix sockets; harmless on Windows named pipes).
        QtNetwork.QLocalServer.removeServer(self.key)
        self.server = QtNetwork.QLocalServer(self)
        self.server.newConnection.connect(self._on_new_connection)
        if not self.server.listen(self.key):
            # Cannot claim the name - fail open rather than blocking startup.
            self.server = None
        return True

    def _on_new_connection(self) -> None:
        if self.server is None:
            return
        conn = self.server.nextPendingConnection()
        if conn is None:
            return
        conn.readyRead.connect(conn.readAll)
        conn.disconnected.connect(conn.deleteLater)
        self.activated.emit()

    def release(self) -> None:
        if self.server is not None:
            self.server.close()
            self.server = None


def main() -> int:
    app = QtWidgets.QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)
    app.setApplicationName(APP_NAME)
    theme.apply(app, base_pt=10)

    guard = SingleInstanceGuard()
    if not guard.try_acquire():
        # Another copy is already running; it has been told to show itself.
        return 0

    if not QtWidgets.QSystemTrayIcon.isSystemTrayAvailable():
        QtWidgets.QMessageBox.critical(
            None, APP_NAME, "System tray is not available on this system.")
        return 1
    state = AppState(app)
    guard.activated.connect(state.show_window)
    state.show_window()
    rc = app.exec()
    guard.release()
    return rc


if __name__ == "__main__":
    sys.exit(main())

"""PyQt6 system-tray watchdog / GUI for upload_uaa_to_sheet.

Install:
    pip install PyQt6

Run:
    python siphog_watchdog.py
"""

from __future__ import annotations

import json
import sys
import traceback
from datetime import datetime
from pathlib import Path

from PyQt6 import QtCore, QtGui, QtWidgets

import upload_uaa_to_sheet as core
import theme


APP_NAME = "SIPHOG UAA Watchdog"
CONFIG_FILE = Path(__file__).parent / "watchdog_config.json"
DEFAULT_SCOPE = {
    "bench_enabled": False,
    "bench_min": 1000,
    "bench_max": 9999,
    "date_enabled": False,
    "date_from": "",   # ISO date str
    "date_to": "",
    "lot_enabled": False,
    "lot_id": "",
}
DEFAULT_CONFIG = {
    "watchdog_enabled": False,
    "interval_minutes": 15,
    "autostart": False,
    "tab_name": "test",
    "data_folder": "",  # empty string = fall back to script directory
    "run_detail_on_schedule": True,  # also push the detail tab on each timer tick
    "scope": dict(DEFAULT_SCOPE),
    "detail_scope": dict(DEFAULT_SCOPE),
}
SAFE_TAB = "test"
DETAIL_TAB = core.DETAIL_TAB_NAME
REGISTRY_RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
REGISTRY_VALUE_NAME = "SIPHOGUAAWatchdog"


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
                 mode: str = "summary", scope: dict | None = None, parent=None):
        super().__init__(parent)
        self.apply_mode = apply_mode
        self.tab_name = tab_name
        self.data_folder = data_folder
        self.mode = mode  # "summary" | "detail"
        self.scope = scope

    def run(self):
        try:
            if not self.data_folder.is_dir():
                raise RuntimeError(
                    f"Data folder does not exist: {self.data_folder}")
            gc = core.authorize()
            ws = gc.open_by_key(core.SHEET_ID).worksheet(self.tab_name)
            if self.mode == "detail":
                all_values, col_idx = core.fetch_sheet(ws, core.DETAIL_COLS)
                counts, rows, cells, warnings = core.compute_detail_plan(
                    all_values, col_idx, self.data_folder, scope=self.scope)
                if self.apply_mode and cells:
                    core.apply_updates(ws, cells, value_input_option="RAW")
            else:
                all_values, col_idx = core.fetch_sheet(ws)
                counts, rows, cells, warnings = core.compute_plan(
                    all_values, col_idx, self.data_folder, scope=self.scope)
                if self.apply_mode and cells:
                    core.apply_updates(ws, cells)
            self.finished_ok.emit(counts, rows, cells, warnings)
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


class UploadPanel(QtWidgets.QWidget):
    """One upload mode (summary or detail): target header, scope filters,
    Dry Run / Upload Now buttons, a summary line, and a results table."""

    dry_run_requested = QtCore.pyqtSignal()
    upload_requested = QtCore.pyqtSignal()
    scope_changed = QtCore.pyqtSignal()
    tab_changed = QtCore.pyqtSignal(str)      # summary only
    schedule_toggled = QtCore.pyqtSignal(bool)  # detail only

    SUMMARY_HEADERS = ["Bench", "Row", "Status", "Field",
                       "Current", "New", "Changed"]
    DETAIL_HEADERS = ["Bench", "Row", "Status", "Field", "New"]
    SUMMARY_FIELDS = ["uaa_date", "uaa_dry", "uaa_uv", "uaa_bake",
                      "uaa_completed"]
    DETAIL_FIELDS = core.DETAIL_COLS[1:]  # all but 'bench number'

    def __init__(self, mode: str, parent=None):
        super().__init__(parent)
        self.mode = mode  # "summary" | "detail"
        v = QtWidgets.QVBoxLayout(self)
        v.setContentsMargins(8, 8, 8, 8)
        v.setSpacing(8)

        header = QtWidgets.QHBoxLayout()
        header.setSpacing(8)

        if mode == "summary":
            lbl = QtWidgets.QLabel("Target sheet:")
            lbl.setStyleSheet(f"color: {theme.FG_MUTED};")
            header.addWidget(lbl)
            self.tab_combo = QtWidgets.QComboBox()
            self.tab_combo.setMinimumWidth(180)
            self.tab_combo.currentTextChanged.connect(self._on_tab_changed)
            header.addWidget(self.tab_combo)
            self.tab_badge = QtWidgets.QLabel()
            self.tab_badge.setVisible(False)
            header.addWidget(self.tab_badge)
            self._suppress_tab_signal = False
            self.sched_cb = None
        else:
            self.tab_combo = None
            self.tab_badge = None
            lbl = QtWidgets.QLabel(f"Target sheet: {DETAIL_TAB}")
            lbl.setStyleSheet(f"color: {theme.FG};")
            header.addWidget(lbl)
            badge = QtWidgets.QLabel("  DETAIL  ")
            badge.setStyleSheet(
                f"background:{theme.INFO}; color:white; border-radius:4px; "
                f"padding:2px 6px; font-weight:700;")
            header.addWidget(badge)
            header.addSpacing(12)
            self.sched_cb = QtWidgets.QCheckBox("Run with watchdog schedule")
            self.sched_cb.setToolTip(
                "When the watchdog timer fires, also upsert this detail tab")
            self.sched_cb.toggled.connect(self.schedule_toggled)
            header.addWidget(self.sched_cb)

        header.addStretch()
        self.dry_btn = QtWidgets.QPushButton("Dry Run")
        self.apply_btn = QtWidgets.QPushButton("Upload Now")
        self.dry_btn.clicked.connect(self.dry_run_requested)
        self.apply_btn.clicked.connect(self.upload_requested)
        header.addWidget(self.dry_btn)
        header.addWidget(self.apply_btn)
        v.addLayout(header)

        self.scope_panel = ScopePanel()
        self.scope_panel.changed.connect(self.scope_changed)
        v.addWidget(self.scope_panel)
        if mode == "detail":
            # Detail tab has no 'Lot ID' column; disable that filter.
            self.scope_panel.set_lot_ids([], False)

        self.summary_label = QtWidgets.QLabel(
            "<span style='color:%s'>No run yet.</span>" % theme.FG_MUTED)
        self.summary_label.setFont(theme.mono_font(10))
        self.summary_label.setTextFormat(QtCore.Qt.TextFormat.RichText)
        v.addWidget(self.summary_label)

        headers = (self.SUMMARY_HEADERS if mode == "summary"
                   else self.DETAIL_HEADERS)
        self.table = QtWidgets.QTableWidget(0, len(headers))
        self.table.setHorizontalHeaderLabels(headers)
        self.table.horizontalHeader().setSectionResizeMode(
            QtWidgets.QHeaderView.ResizeMode.ResizeToContents)
        self.table.verticalHeader().setDefaultSectionSize(22)
        self.table.setFont(theme.mono_font(10))
        self.table.setSortingEnabled(True)
        self.table.setAlternatingRowColors(True)
        self.table.setEditTriggers(
            QtWidgets.QAbstractItemView.EditTrigger.NoEditTriggers)
        v.addWidget(self.table, 1)

    # -- summary-mode tab combo ------------------------------------------
    def set_available_tabs(self, names: list[str], current: str) -> None:
        if self.tab_combo is None:
            return
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
        if self.tab_badge is None:
            return
        if name and name != SAFE_TAB:
            self.tab_badge.setText(f"  ⚠ PROD: {name}  ")
            self.tab_badge.setStyleSheet(
                f"background:{theme.ERROR}; color:white; "
                f"border-radius:4px; padding:2px 6px; font-weight:700;")
            self.tab_badge.setVisible(True)
        else:
            self.tab_badge.setVisible(False)

    # -- shared ----------------------------------------------------------
    def enable_controls(self, enabled: bool) -> None:
        self.dry_btn.setEnabled(enabled)
        self.apply_btn.setEnabled(enabled)
        if self.tab_combo is not None:
            self.tab_combo.setEnabled(enabled)
        if self.sched_cb is not None:
            self.sched_cb.setEnabled(enabled)
        self.scope_panel.enable_controls(enabled)

    def show_results(self, counts: dict, rows: list,
                     cells: list, warnings: list, mode_label: str) -> None:
        if self.mode == "summary":
            self._show_summary(counts, rows, cells, mode_label)
        else:
            self._show_detail(counts, rows, cells, mode_label)
        for w in warnings:
            pass  # warnings are logged by AppState

    def _show_summary(self, counts, rows, cells, mode_label) -> None:
        mode_color = theme.SUCCESS if mode_label == "upload" else theme.INFO
        oos = counts.get("skipped_out_of_scope", 0)
        oos_color = theme.WARN if oos else theme.FG_MUTED
        self.summary_label.setText(
            f'<span style="color:{mode_color};font-weight:700">{mode_label}</span>'
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
            f'<span style="color:{theme.FG}">{len(cells)}</span>')

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
            for field in self.SUMMARY_FIELDS:
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

    def _show_detail(self, counts, rows, cells, mode_label) -> None:
        mode_color = theme.SUCCESS if mode_label == "upload" else theme.INFO
        oos = counts.get("skipped_out_of_scope", 0)
        oos_color = theme.WARN if oos else theme.FG_MUTED
        self.summary_label.setText(
            f'<span style="color:{mode_color};font-weight:700">{mode_label}</span>'
            f'  <span style="color:{theme.FG_MUTED}">scanned=</span>'
            f'<span style="color:{theme.FG}">{counts["scanned"]}</span>'
            f'  <span style="color:{oos_color}">out_of_scope=</span>'
            f'<span style="color:{oos_color};font-weight:600">{oos}</span>'
            f'  <span style="color:{theme.FG_MUTED}">no_data=</span>'
            f'<span style="color:{theme.FG}">{counts["skipped_no_data"]}</span>'
            f'  <span style="color:{theme.FG_MUTED}">unchanged=</span>'
            f'<span style="color:{theme.FG}">{counts["unchanged"]}</span>'
            f'  <span style="color:{theme.ACCENT};font-weight:700">'
            f'updated={counts["updated"]}</span>'
            f'  <span style="color:{theme.SUCCESS};font-weight:700">'
            f'appended={counts["appended"]}</span>'
            f'  <span style="color:{theme.FG_MUTED}">rows=</span>'
            f'<span style="color:{theme.FG}">{len(cells)}</span>')

        self.table.setSortingEnabled(False)
        self.table.setRowCount(0)
        color_muted = QtGui.QColor(theme.FG_MUTED)
        color_accent = QtGui.QColor(theme.ACCENT)
        color_success = QtGui.QColor(theme.SUCCESS)
        color_fg = QtGui.QColor(theme.FG)

        for change in rows:
            if change["status"] not in ("APPEND", "UPDATE"):
                continue
            status_col = color_success if change["status"] == "APPEND" \
                else color_accent
            for field in self.DETAIL_FIELDS:
                val = change["new"].get(field, "")
                r = self.table.rowCount()
                self.table.insertRow(r)
                cells_row = [
                    (change["bench"], color_muted),
                    (str(change["row"]), color_muted),
                    (change["status"], status_col),
                    (field, color_muted),
                    ("" if val == "" else str(val),
                     color_muted if val == "" else color_fg),
                ]
                for c, (text, col) in enumerate(cells_row):
                    item = QtWidgets.QTableWidgetItem(text)
                    item.setForeground(col)
                    self.table.setItem(r, c, item)
        self.table.setSortingEnabled(True)


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
        browse_btn.clicked.connect(self._browse_folder)
        folder_row.addWidget(self.data_folder_edit, 1)
        folder_row.addWidget(browse_btn)
        form.addRow("Data folder:", folder_row)

        hint = QtWidgets.QLabel(
            "Folder containing the per-bench JSON files (filenames like "
            "<i>1234.json</i>).")
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
            self, "Select SIPHOG data folder", start)
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
    settings_requested = QtCore.pyqtSignal()
    open_folder_requested = QtCore.pyqtSignal()
    refresh_tabs_requested = QtCore.pyqtSignal()
    quit_requested = QtCore.pyqtSignal()

    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_NAME)
        self.resize(1180, 720)

        menubar = self.menuBar()
        file_menu = menubar.addMenu("&File")
        self.act_settings = file_menu.addAction("Settings\u2026")
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

        top.addStretch()

        self.wd_btn = QtWidgets.QPushButton()
        self.wd_btn.clicked.connect(self.toggle_watchdog_requested)
        top.addWidget(self.wd_btn)
        vbox.addLayout(top)

        self.summary_panel = UploadPanel("summary")
        self.detail_panel = UploadPanel("detail")
        self.tabs = QtWidgets.QTabWidget()
        self.tabs.addTab(self.summary_panel, "Summary  (test)")
        self.tabs.addTab(self.detail_panel, f"Detail  ({DETAIL_TAB})")
        vbox.addWidget(self.tabs, 1)

        self.log = QtWidgets.QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(500)
        self.log.setMinimumHeight(140)
        self.log.setFont(theme.mono_font(10))
        vbox.addWidget(self.log)

    def log_line(self, text: str, level: str = "INFO") -> None:
        self.log.appendHtml(theme.log_html(level, text))

    def set_state_display(self, status: str, color: str, wd_on: bool,
                          busy: bool, next_run_text: str) -> None:
        self.state_label.setText(
            f'<span style="color:{color}">\u25cf</span> '
            f'<span style="color:{theme.FG};font-weight:600">{status}</span>')
        self.wd_btn.setText("Disable Watchdog" if wd_on else "Enable Watchdog")
        self.summary_panel.enable_controls(not busy)
        self.detail_panel.enable_controls(not busy)
        self.next_run_label.setText(next_run_text)

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
        self._busy = False               # a worker is currently running
        self._queue: list = []           # pending (panel, apply_mode) tuples
        self._active_panel = None
        self._active_apply = False

        self.window = MainWindow()
        sp = self.window.summary_panel
        dp = self.window.detail_panel
        sp.set_available_tabs([self.cfg["tab_name"]], self.cfg["tab_name"])
        sp.scope_panel.set_from_config(self.cfg.get("scope") or {})
        dp.scope_panel.set_from_config(self.cfg.get("detail_scope") or {})
        dp.scope_panel.set_lot_ids([], False)  # detail tab has no Lot ID column
        dp.sched_cb.setChecked(bool(self.cfg.get("run_detail_on_schedule", True)))
        sp.scope_changed.connect(self.on_summary_scope_changed)
        dp.scope_changed.connect(self.on_detail_scope_changed)
        sp.tab_changed.connect(self.on_tab_changed)
        sp.dry_run_requested.connect(lambda: self.run_panel(sp, False))
        sp.upload_requested.connect(lambda: self.run_panel(sp, True))
        dp.dry_run_requested.connect(lambda: self.run_panel(dp, False))
        dp.upload_requested.connect(lambda: self.run_panel(dp, True))
        dp.schedule_toggled.connect(self.on_detail_schedule_toggled)
        self.window.toggle_watchdog_requested.connect(self.toggle_watchdog)
        self.window.settings_requested.connect(self.open_settings)
        self.window.open_folder_requested.connect(self.open_folder)
        self.window.refresh_tabs_requested.connect(self.refresh_tabs)
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
        self.act_settings = self.menu.addAction("Settings\u2026")
        self.menu.addSeparator()
        self.act_quit = self.menu.addAction("Quit")
        self.tray.setContextMenu(self.menu)

        self.act_show.triggered.connect(self.show_window)
        self.act_toggle.triggered.connect(self.toggle_watchdog)
        self.act_dry.triggered.connect(lambda: self.run_both(False))
        self.act_run.triggered.connect(lambda: self.run_both(True))
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
                "Data folder not found; set it in File \u2192 Settings\u2026",
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

    def on_summary_scope_changed(self) -> None:
        self.cfg["scope"] = self.window.summary_panel.scope_panel.to_config()
        save_config(self.cfg)

    def on_detail_scope_changed(self) -> None:
        self.cfg["detail_scope"] = \
            self.window.detail_panel.scope_panel.to_config()
        save_config(self.cfg)

    def on_detail_schedule_toggled(self, on: bool) -> None:
        self.cfg["run_detail_on_schedule"] = bool(on)
        save_config(self.cfg)
        self.window.log_line(
            f"Detail upload {'included in' if on else 'excluded from'} "
            f"watchdog schedule", "INFO")

    def refresh_lot_ids(self) -> None:
        if self.lot_worker and self.lot_worker.isRunning():
            return
        self.lot_worker = LotIDWorker(self.cfg["tab_name"])
        self.lot_worker.ok.connect(self.on_lots_ok)
        self.lot_worker.failed.connect(self.on_lots_fail)
        self.lot_worker.start()

    def on_lots_ok(self, lots: list, available: bool) -> None:
        self.window.summary_panel.scope_panel.set_lot_ids(lots, available)
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
        self.window.summary_panel.set_available_tabs(names, current)
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
        if self._busy:
            return
        self.window.log_line("Scheduled run triggered", "INFO")
        items = [(self.window.summary_panel, True)]
        if self.cfg.get("run_detail_on_schedule", True):
            items.append((self.window.detail_panel, True))
        self._enqueue(items)

    def run_panel(self, panel, apply_mode: bool) -> None:
        """Run a single tab (its own Dry Run / Upload Now button)."""
        self._enqueue([(panel, apply_mode)])

    def run_both(self, apply_mode: bool) -> None:
        """Run summary then detail back-to-back (tray Dry Run / Upload Now)."""
        self._enqueue([(self.window.summary_panel, apply_mode),
                       (self.window.detail_panel, apply_mode)])

    def _enqueue(self, items: list) -> None:
        self._queue.extend(items)
        self._start_next()

    def _start_next(self) -> None:
        if self._busy or not self._queue:
            return
        panel, apply_mode = self._queue.pop(0)
        self._busy = True
        self._active_panel = panel
        self._active_apply = apply_mode
        self.status = "running"
        self.last_mode = "upload" if apply_mode else "dry-run"
        if panel.mode == "detail":
            tab = DETAIL_TAB
            tag = ""
        else:
            tab = self.cfg["tab_name"]
            tag = "[PROD] " if tab != SAFE_TAB else ""
        scope = panel.scope_panel.to_scope()
        scope_desc = self._describe_scope(scope)
        self.window.log_line(
            f"Starting {panel.mode} {self.last_mode} on '{tab}' "
            f"{tag}{scope_desc}".strip(), "INFO")
        self.refresh_ui()
        self.worker = UploadWorker(
            apply_mode, tab, resolve_data_folder(self.cfg),
            mode=panel.mode, scope=scope or None)
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
        panel = self._active_panel
        mode_label = self.last_mode
        pmode = panel.mode if panel else "?"
        if pmode == "detail":
            covered = counts.get("updated", 0) + counts.get("appended", 0)
            suffix = (f"{covered} rows, {len(cells)} writes"
                      if mode_label == "upload"
                      else f"{covered} rows pending")
        else:
            updated = counts.get("updated", 0)
            suffix = (f"{updated} rows, {len(cells)} cells"
                      if mode_label == "upload"
                      else f"{updated} rows pending")
        self.window.log_line(f"{pmode} {mode_label} complete: {suffix}", "OK")
        if panel is not None:
            panel.show_results(counts, rows, cells, warnings, mode_label)
        for w in warnings:
            self.window.log_line(w, "WARN")
        self.tray.setToolTip(
            f"{APP_NAME}\nLast {pmode} {mode_label}: {suffix}\n"
            f"{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")

        self._busy = False
        self._active_panel = None
        self.worker = None
        if self._queue:
            self._start_next()
        else:
            self.status = ("idle_on" if self.cfg["watchdog_enabled"]
                           else "idle_off")
            self.refresh_ui()

    def on_fail(self, err: str) -> None:
        self._queue.clear()
        self._busy = False
        self._active_panel = None
        self.worker = None
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

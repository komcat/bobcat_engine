"""Serial Number Data Viewer.

Left panel: entries sorted newest-first (timestamp, step, location).
Right panel: B0/B1 poynt X/Y for MOE-A1 vs MOE-A2.
  - Missing A1/A2 falls back to the most recent previous one; blank if none yet.
  - Each cell is colored green if |value| < 0.5, red otherwise.

On startup, if no file is passed on the command line, the most recently
modified *.json under the script's directory is opened automatically. The file
is re-read whenever its mtime changes so the display stays live.

Usage:
    python view_serial_data.py [path/to/7074.json]
"""

import json
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, ttk

BASE_DIR = Path(__file__).parent
SETTINGS_PATH = BASE_DIR / "view_serial_data.local.json"
POLL_MS = 1000
POYNT_THRESHOLD = 0.5
COLOR_OK = "#0a7e07"     # green: |v| < 0.5
COLOR_BAD = "#c62828"    # red:   |v| >= 0.5
COLOR_NEUTRAL = "#222"


def load_settings():
    try:
        return json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def save_settings(settings):
    try:
        SETTINGS_PATH.write_text(
            json.dumps(settings, indent=2), encoding="utf-8"
        )
    except OSError:
        pass

SERIES = [
    # (label,   side, section, color)
    ("A1 B0 (Rx)", "a1", "b0", "#d32f2f"),
    ("A1 B1 (Tx)", "a1", "b1", "#f57c00"),
    ("A2 B0 (Rx)", "a2", "b0", "#1976d2"),
    ("A2 B1 (Tx)", "a2", "b1", "#388e3c"),
]
AXIS_MIN = -5.0
AXIS_MAX = 5.0
ACCEPT_R = 0.5

# Hex-right response rules — see docs/M4/hex_right_response_table.md
# Cell layout (B0/B1 rows × A1.X / A1.Y / A2.X / A2.Y columns):
#   B0: 1=A1.Px  2=A1.Py  3=A2.Px  4=A2.Py
#   B1: 5=A1.Px  6=A1.Py  7=A2.Px  8=A2.Py
CELL_NUMBERS = {
    ("b0", "a1", "poynt_x_deg"): 1,
    ("b0", "a1", "poynt_y_deg"): 2,
    ("b0", "a2", "poynt_x_deg"): 3,
    ("b0", "a2", "poynt_y_deg"): 4,
    ("b1", "a1", "poynt_x_deg"): 5,
    ("b1", "a1", "poynt_y_deg"): 6,
    ("b1", "a2", "poynt_x_deg"): 7,
    ("b1", "a2", "poynt_y_deg"): 8,
}
CELL_NAMES = {
    1: "A1.B0.Px", 2: "A1.B0.Py", 3: "A2.B0.Px", 4: "A2.B0.Py",
    5: "A1.B1.Px", 6: "A1.B1.Py", 7: "A2.B1.Px", 8: "A2.B1.Py",
}

# Per cell: axis moves that drive the value up/down, tagged by gain magnitude.
# Tag legend: lg = dominant gain; med = sm-lg cross-coupling (~½ of dominant);
# sm = small (~5–10× smaller than dominant). Derived from the master table
# (with U/X corrections from the SN 7112b verification ladders).
CELL_RULES = {
    1: {"up":   [("Y+", "lg"), ("U+", "lg"), ("W+", "med")],
        "down": [("Y-", "lg"), ("U-", "lg"), ("W-", "med")]},
    2: {"up":   [("X+", "lg"), ("Z+", "lg"), ("V-", "lg"), ("W+", "lg")],
        "down": [("X-", "lg"), ("Z-", "lg"), ("V+", "lg"), ("W-", "lg")]},
    3: {"up":   [("Y+", "lg"), ("U+", "lg"), ("W+", "sm")],
        "down": [("Y-", "lg"), ("U-", "lg"), ("W-", "sm")]},
    4: {"up":   [("X+", "lg"), ("Z-", "lg"), ("V-", "lg"), ("W-", "lg")],
        "down": [("X-", "lg"), ("Z+", "lg"), ("V+", "lg"), ("W+", "lg")]},
    5: {"up":   [("Y+", "lg"), ("U-", "sm"), ("W+", "med")],
        "down": [("Y-", "lg"), ("U+", "sm"), ("W-", "med")]},
    6: {"up":   [("X-", "lg"), ("Z+", "lg"), ("W-", "lg"), ("V-", "sm")],
        "down": [("X+", "lg"), ("Z-", "lg"), ("W+", "lg"), ("V+", "sm")]},
    7: {"up":   [("Y+", "lg"), ("W+", "sm")],
        "down": [("Y-", "lg"), ("W-", "sm")]},
    8: {"up":   [("X-", "lg"), ("Z+", "lg"), ("W+", "lg"), ("V-", "sm")],
        "down": [("X+", "lg"), ("Z-", "lg"), ("W-", "lg"), ("V+", "sm")]},
}
TAG_LABEL = {"lg": "dominant", "med": "medium", "sm": "small"}

# Per-cell, per-axis response gain from the v2 Jacobian (Theil-Sen, 2026-06-24;
# docs/M4/hex_right_response_table.md). X/Y in deg-of-poynt per um; U/V/W in
# deg/deg. Used to estimate how far to move an axis to null a cell. Z is not
# characterized (no span) -> no magnitude shown for Z.
CELL_GAINS = {
    1: {"X": -0.003, "Y": 0.221, "U": 5.558, "V": 1.176, "W": 0.708},
    2: {"X": 0.214, "Y": 0.000, "U": 0.039, "V": -6.544, "W": 5.947},
    3: {"X": 0.005, "Y": 0.196, "U": 5.241, "V": 0.854, "W": 0.918},
    4: {"X": 0.211, "Y": -0.008, "U": 1.202, "V": -5.841, "W": -5.425},
    5: {"X": 0.005, "Y": 0.212, "U": 0.241, "V": 1.125, "W": 0.774},
    6: {"X": -0.185, "Y": -0.014, "U": -0.397, "V": -0.909, "W": -5.097},
    7: {"X": 0.012, "Y": 0.155, "U": 0.285, "V": 0.780, "W": 0.653},
    8: {"X": -0.162, "Y": -0.003, "U": -0.234, "V": -1.037, "W": 4.547},
}
_AXIS_UNIT = {"X": "um", "Y": "um", "U": "deg", "V": "deg", "W": "deg"}


def move_magnitude(cell_num, axis, value):
    """Estimated single-axis move to drive |cell| to 0, or '' if not
    characterized (Z) or the gain is too small to be a useful lever."""
    letter = axis[0]
    gain = CELL_GAINS.get(cell_num, {}).get(letter)
    if gain is None or abs(gain) < 1e-3:
        return ""
    mag = abs(value) / abs(gain)
    if _AXIS_UNIT[letter] == "deg":
        return f"~{mag:.3f} deg"
    return f"~{mag:.1f} um"


def fmt_number(v):
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return "" if v is None else str(v)
    return f"{v:.4f}"


def poynt_color(v):
    if not isinstance(v, (int, float)) or isinstance(v, bool):
        return COLOR_NEUTRAL
    return COLOR_OK if abs(v) < POYNT_THRESHOLD else COLOR_BAD


def cell_tip(cell_num, value):
    """Return a tooltip string for a poynt cell, or None if no value."""
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return None
    rules = CELL_RULES.get(cell_num)
    if not rules:
        return None
    in_spec = abs(value) < POYNT_THRESHOLD
    if value >= 0:
        direction, arrow = "down", "↓"
    else:
        direction, arrow = "up", "↑"
    grouped = {"lg": [], "med": [], "sm": []}
    for axis, tag in rules[direction]:
        mag = move_magnitude(cell_num, axis, value)
        grouped[tag].append(f"{axis} {mag}" if mag else axis)
    if in_spec:
        status = f"In spec (|v| < {POYNT_THRESHOLD})"
    else:
        sign_word = "too high" if value > 0 else "too low"
        status = f"Out of spec (|v| ≥ {POYNT_THRESHOLD}); {sign_word}"
    lines = [
        f"Cell {cell_num}: {CELL_NAMES[cell_num]} = {value:+.4f}",
        f"{status}; toward 0 needs {arrow}",
        "",
        "Hex-right moves that drive this cell toward 0:",
    ]
    for tag in ("lg", "med", "sm"):
        if grouped[tag]:
            lines.append(f"  {TAG_LABEL[tag]:>8}: {', '.join(grouped[tag])}")
    if not in_spec:
        lines += [
            "",
            "Est. single-axis move to null this cell (axes couple;",
            "use Auto-correct v2 for a combined solve). Z not characterized.",
        ]
    return "\n".join(lines)


class Tooltip:
    """Lightweight hover tooltip; .set_text(None) disables it for the widget."""
    def __init__(self, widget):
        self.widget = widget
        self.tip = None
        self._text = None
        widget.bind("<Enter>", self._on_enter)
        widget.bind("<Leave>", self._on_leave)
        widget.bind("<Motion>", self._on_motion)

    def set_text(self, text):
        self._text = text
        if not text:
            self._hide()

    def _on_enter(self, event):
        if self._text:
            self._show(event.x_root + 16, event.y_root + 16)

    def _on_motion(self, event):
        if self.tip and self._text:
            self.tip.wm_geometry(f"+{event.x_root + 16}+{event.y_root + 16}")

    def _on_leave(self, _event):
        self._hide()

    def _show(self, x, y):
        if self.tip is not None:
            return
        self.tip = tk.Toplevel(self.widget)
        self.tip.wm_overrideredirect(True)
        self.tip.wm_geometry(f"+{x}+{y}")
        tk.Label(
            self.tip, text=self._text, justify=tk.LEFT,
            background="#fff8c5", relief=tk.SOLID, borderwidth=1,
            font=("Consolas", 9), padx=8, pady=6,
        ).pack()

    def _hide(self):
        if self.tip is not None:
            self.tip.destroy()
            self.tip = None


def find_latest_json(root):
    latest = None
    latest_mtime = -1.0
    for p in root.rglob("*.json"):
        try:
            m = p.stat().st_mtime
        except OSError:
            continue
        if m > latest_mtime:
            latest_mtime = m
            latest = p
    return latest


class SerialDataViewer(tk.Tk):
    def __init__(self, initial_file=None):
        super().__init__()
        self.title("Serial Number Data Viewer")
        self.geometry("1180x640")

        menubar = tk.Menu(self)
        filemenu = tk.Menu(menubar, tearoff=0)
        filemenu.add_command(label="Open JSON...", command=self.open_file)
        filemenu.add_separator()
        filemenu.add_command(label="Quit", command=self.destroy)
        menubar.add_cascade(label="File", menu=filemenu)
        self.config(menu=menubar)

        self.header = ttk.Label(self, text="No file loaded",
                                font=("Segoe UI", 10, "bold"))
        self.header.pack(fill=tk.X, padx=8, pady=(6, 2))

        self.settings = load_settings()
        saved_dir = self.settings.get("watch_dir")
        self.watch_dir = (
            Path(saved_dir) if saved_dir and Path(saved_dir).is_dir()
            else BASE_DIR
        )

        controls = ttk.Frame(self)
        controls.pack(fill=tk.X, padx=8, pady=(0, 4))
        ttk.Label(controls, text="Folder watch:").pack(side=tk.LEFT)
        self.start_btn = ttk.Button(controls, text="Start",
                                    command=self.start_monitoring, width=8)
        self.start_btn.pack(side=tk.LEFT, padx=(6, 2))
        self.stop_btn = ttk.Button(controls, text="Stop",
                                   command=self.stop_monitoring,
                                   width=8, state=tk.DISABLED)
        self.stop_btn.pack(side=tk.LEFT, padx=2)
        ttk.Button(controls, text="Folder...",
                   command=self.choose_watch_folder,
                   width=10).pack(side=tk.LEFT, padx=(6, 2))
        self.status_label = ttk.Label(controls, text="● idle",
                                      foreground="#888",
                                      font=("Segoe UI", 9, "bold"))
        self.status_label.pack(side=tk.LEFT, padx=(12, 0))
        self.folder_caption_var = tk.StringVar()
        ttk.Label(controls, textvariable=self.folder_caption_var,
                  foreground="#666").pack(side=tk.LEFT, padx=(8, 0))
        self._update_folder_caption()

        paned = ttk.PanedWindow(self, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=8, pady=6)

        # ---- Left: flat list sorted newest-first ----
        left = ttk.Frame(paned)
        ttk.Label(left, text="Entries (newest first)",
                  font=("Segoe UI", 9, "bold")).pack(anchor=tk.W)
        self.tree = ttk.Treeview(left, columns=("time", "step", "loc"),
                                 show="headings", selectmode="browse")
        self.tree.heading("time", text="Timestamp")
        self.tree.heading("step", text="Step")
        self.tree.heading("loc", text="Loc")
        self.tree.column("time", width=180, stretch=False)
        self.tree.column("step", width=210, stretch=True)
        self.tree.column("loc", width=60, stretch=False, anchor=tk.CENTER)
        tscroll = ttk.Scrollbar(left, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=tscroll.set)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        tscroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.tree.bind("<<TreeviewSelect>>", self.on_select)
        paned.add(left, weight=2)

        # ---- Right: A1/A2 poynt comparison (Label grid for per-cell color) ----
        right = ttk.Frame(paned)
        ttk.Label(right, text="Poynt X / Y  (deg)",
                  font=("Segoe UI", 9, "bold")).pack(anchor=tk.W)
        self.detail_header = ttk.Label(right, text="Select an entry on the left.",
                                       justify=tk.LEFT)
        self.detail_header.pack(anchor=tk.W, pady=(0, 6))

        grid = ttk.Frame(right, padding=8)
        grid.pack(anchor=tk.NW, fill=tk.X)

        header_font = ("Segoe UI", 10, "bold")
        row_label_font = ("Segoe UI", 10, "bold")
        value_font = ("Consolas", 14, "bold")

        # column headers
        headers = ["", "A1 Poynt.X", "A1 Poynt.Y", "A2 Poynt.X", "A2 Poynt.Y"]
        for col, text in enumerate(headers):
            ttk.Label(grid, text=text, font=header_font).grid(
                row=0, column=col, padx=12, pady=(0, 6), sticky=tk.W)

        self.value_labels = {}  # (section, side, axis_key) -> tk.Label
        self.value_tooltips = {}  # same key -> Tooltip
        rows = [
            ("B0 (Rx)", "b0"),
            ("B1 (Tx)", "b1"),
        ]
        cells = [
            ("a1", "poynt_x_deg"),
            ("a1", "poynt_y_deg"),
            ("a2", "poynt_x_deg"),
            ("a2", "poynt_y_deg"),
        ]
        for r_idx, (label, section) in enumerate(rows, start=1):
            ttk.Label(grid, text=label, font=row_label_font).grid(
                row=r_idx, column=0, padx=12, pady=6, sticky=tk.W)
            for c_idx, (side, key) in enumerate(cells, start=1):
                lbl = tk.Label(grid, text="", font=value_font,
                               fg=COLOR_NEUTRAL, anchor=tk.E,
                               width=12)
                lbl.grid(row=r_idx, column=c_idx, padx=12, pady=6, sticky=tk.E)
                self.value_labels[(section, side, key)] = lbl
                self.value_tooltips[(section, side, key)] = Tooltip(lbl)

        # scatter plot below the table
        scatter_wrap = ttk.Frame(right)
        scatter_wrap.pack(fill=tk.BOTH, expand=True, padx=8, pady=(6, 8))
        self.scatter = tk.Canvas(scatter_wrap, bg="white",
                                 highlightthickness=1,
                                 highlightbackground="#999")
        self.scatter.pack(fill=tk.BOTH, expand=True)
        self.scatter.bind("<Configure>", lambda e: self._draw_scatter())
        self.current_points = []  # [(label, x, y, color)]

        paned.add(right, weight=3)

        self.data = None
        self.entries = []
        self.current_path = None
        self.current_mtime = None
        self._watch_job = None
        self.monitoring = False

        if initial_file:
            p = Path(initial_file)
            self.watch_dir = p.parent
            self.settings["watch_dir"] = str(self.watch_dir)
            save_settings(self.settings)
            self._update_folder_caption()
            self.load_file(p)
        else:
            latest = find_latest_json(self.watch_dir)
            if latest:
                self.load_file(latest)

        # Start monitoring by default so the UI is live out of the box.
        self.start_monitoring()

    # -------- file handling --------
    def open_file(self):
        init_dir = str(BASE_DIR) if BASE_DIR.exists() else "."
        path = filedialog.askopenfilename(
            title="Open serial-number JSON",
            initialdir=init_dir,
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")],
        )
        if path:
            self.load_file(path)

    def load_file(self, path, force_latest=False):
        path = Path(path)
        try:
            mtime = path.stat().st_mtime
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            # File may be mid-write; leave existing state in place.
            if self.data is None:
                self.header.config(text=f"Failed to load {path}: {e}")
            return

        # Remember selection so we can restore it after reload, unless the
        # caller requested we always jump to the newest entry.
        prev_key = None if force_latest else self._selection_key()

        self.data = data
        self.entries = data.get("entries", [])
        self.current_path = path
        self.current_mtime = mtime

        sn = data.get("serial_number", "?")
        prod = data.get("product_name", "?")
        ct = data.get("creation_timestamp", "?")
        self.header.config(
            text=(f"Serial: {sn}   |   Product: {prod}   |   "
                  f"Created: {ct}   |   File: {path.name}")
        )
        self.populate_list()
        self._restore_selection(prev_key)

    # -------- watch folder --------
    def _update_folder_caption(self):
        self.folder_caption_var.set(
            f"(scans {self.watch_dir} every {POLL_MS}ms)"
        )

    def choose_watch_folder(self):
        init_dir = str(self.watch_dir) if self.watch_dir.exists() else str(BASE_DIR)
        chosen = filedialog.askdirectory(
            title="Choose product folder to watch",
            initialdir=init_dir,
            mustexist=True,
        )
        if not chosen:
            return
        self.watch_dir = Path(chosen)
        self.settings["watch_dir"] = str(self.watch_dir)
        save_settings(self.settings)
        self._update_folder_caption()
        latest = find_latest_json(self.watch_dir)
        if latest is not None:
            self.load_file(latest, force_latest=True)
        else:
            # No JSON in the chosen folder yet; clear view and wait for one.
            self.data = None
            self.entries = []
            self.current_path = None
            self.current_mtime = None
            self.header.config(
                text=f"No JSON found in {self.watch_dir} — waiting..."
            )
            self.tree.delete(*self.tree.get_children())
            self._clear_right()

    # -------- folder monitoring --------
    def start_monitoring(self):
        self.monitoring = True
        self.start_btn.config(state=tk.DISABLED)
        self.stop_btn.config(state=tk.NORMAL)
        self.status_label.config(text="● monitoring", foreground=COLOR_OK)
        if self._watch_job is None:
            self._watch_job = self.after(POLL_MS, self._tick)

    def stop_monitoring(self):
        self.monitoring = False
        self.start_btn.config(state=tk.NORMAL)
        self.stop_btn.config(state=tk.DISABLED)
        self.status_label.config(text="● idle", foreground="#888")
        if self._watch_job is not None:
            self.after_cancel(self._watch_job)
            self._watch_job = None

    def _tick(self):
        try:
            if not self.monitoring:
                self._watch_job = None
                return
            latest = find_latest_json(self.watch_dir)
            if latest is not None:
                try:
                    m = latest.stat().st_mtime
                except OSError:
                    m = None
                if latest != self.current_path:
                    # A newer JSON file appeared — switch to it.
                    self.load_file(latest, force_latest=True)
                elif m is not None and m != self.current_mtime:
                    # Same file was rewritten — refresh and jump to newest.
                    self.load_file(latest, force_latest=True)
        finally:
            if self.monitoring:
                self._watch_job = self.after(POLL_MS, self._tick)

    # -------- selection preservation --------
    def _selection_key(self):
        sel = self.tree.selection()
        if not sel or not sel[0].isdigit():
            return None
        idx = int(sel[0])
        if 0 <= idx < len(self.entries):
            e = self.entries[idx]
            v = e.get("value", {})
            return (e.get("timestamp"), e.get("key"), v.get("location"))
        return None

    def _restore_selection(self, key):
        kids = self.tree.get_children()
        if not kids:
            self._clear_right()
            return
        target = None
        if key is not None:
            for iid in kids:
                idx = int(iid)
                e = self.entries[idx]
                v = e.get("value", {})
                if (e.get("timestamp"), e.get("key"), v.get("location")) == key:
                    target = iid
                    break
        if target is None:
            target = kids[0]  # newest-first, so this is the latest entry
        self.tree.selection_set(target)
        self.tree.focus(target)
        self.tree.see(target)
        self._update_right(int(target))

    # -------- left list --------
    def populate_list(self):
        self.tree.delete(*self.tree.get_children())
        # Display newest first; iid is the original chronological index.
        for idx in range(len(self.entries) - 1, -1, -1):
            entry = self.entries[idx]
            val = entry.get("value", {})
            ts = entry.get("timestamp", "")
            key = entry.get("key", "")
            loc = val.get("location", "")
            loc_short = loc.replace("MOE-", "") if loc else ""
            self.tree.insert("", "end", iid=str(idx),
                             values=(ts, key, loc_short))

    # -------- right table --------
    def latest_by_location(self, upto_idx, target_loc):
        for i in range(upto_idx, -1, -1):
            e = self.entries[i]
            if e.get("value", {}).get("location") == target_loc:
                return e, i
        return None, -1

    def on_select(self, event):
        sel = self.tree.selection()
        if not sel or not sel[0].isdigit():
            return
        self._update_right(int(sel[0]))

    def _clear_right(self):
        self.detail_header.config(text="Select an entry on the left.")
        for lbl in self.value_labels.values():
            lbl.config(text="", fg=COLOR_NEUTRAL)
        for tip in self.value_tooltips.values():
            tip.set_text(None)
        self.current_points = []
        self._draw_scatter()

    def _update_right(self, sel_idx):
        if not (0 <= sel_idx < len(self.entries)):
            self._clear_right()
            return
        a1, a1_idx = self.latest_by_location(sel_idx, "MOE-A1")
        a2, a2_idx = self.latest_by_location(sel_idx, "MOE-A2")

        sel_entry = self.entries[sel_idx]
        sel_ts = sel_entry.get("timestamp", "")
        sel_key = sel_entry.get("key", "")

        def ts_label(entry, idx):
            if not entry:
                return "—"
            ts = entry.get("timestamp", "")
            return f"{ts}{'  (prev)' if idx != sel_idx else ''}"

        self.detail_header.config(
            text=(f"Selected: {sel_ts}   {sel_key}\n"
                  f"A1: {ts_label(a1, a1_idx)}   |   "
                  f"A2: {ts_label(a2, a2_idx)}")
        )

        sides = {"a1": a1, "a2": a2}
        for section in ("b0", "b1"):
            for side in ("a1", "a2"):
                src = (sides[side] or {}).get("value", {}).get(section) or {}
                for axis in ("poynt_x_deg", "poynt_y_deg"):
                    v = src.get(axis)
                    key = (section, side, axis)
                    self.value_labels[key].config(
                        text=fmt_number(v), fg=poynt_color(v)
                    )
                    cell_num = CELL_NUMBERS.get(key)
                    self.value_tooltips[key].set_text(
                        cell_tip(cell_num, v) if cell_num else None
                    )

        pts = []
        for label, side, section, color in SERIES:
            src = (sides[side] or {}).get("value", {}).get(section) or {}
            x = src.get("poynt_x_deg")
            y = src.get("poynt_y_deg")
            if isinstance(x, (int, float)) and isinstance(y, (int, float)) \
                    and not isinstance(x, bool) and not isinstance(y, bool):
                pts.append((label, float(x), float(y), color))
        self.current_points = pts
        self._draw_scatter()

    def _draw_scatter(self):
        c = self.scatter
        c.delete("all")
        w = c.winfo_width()
        h = c.winfo_height()
        if w <= 20 or h <= 20:
            return
        margin_l, margin_r, margin_t, margin_b = 44, 14, 14, 30
        plot_size = min(w - margin_l - margin_r, h - margin_t - margin_b)
        if plot_size <= 0:
            return
        ox = margin_l
        oy = margin_t

        def to_px(x, y):
            px = ox + (x - AXIS_MIN) / (AXIS_MAX - AXIS_MIN) * plot_size
            py = oy + (AXIS_MAX - y) / (AXIS_MAX - AXIS_MIN) * plot_size
            return px, py

        # plot frame
        c.create_rectangle(ox, oy, ox + plot_size, oy + plot_size,
                           outline="#bbb", fill="#fafafa")

        # minor grid every 1 unit
        for v in range(int(AXIS_MIN) + 1, int(AXIS_MAX)):
            if v == 0:
                continue
            px, _ = to_px(v, 0)
            c.create_line(px, oy, px, oy + plot_size, fill="#ececec")
            _, py = to_px(0, v)
            c.create_line(ox, py, ox + plot_size, py, fill="#ececec")

        # axes
        px0, py0 = to_px(0, 0)
        c.create_line(px0, oy, px0, oy + plot_size, fill="#888")
        c.create_line(ox, py0, ox + plot_size, py0, fill="#888")

        # acceptance circle r=0.5
        r_px = ACCEPT_R / (AXIS_MAX - AXIS_MIN) * plot_size
        c.create_oval(px0 - r_px, py0 - r_px, px0 + r_px, py0 + r_px,
                      outline=COLOR_OK, width=2, dash=(4, 3))
        c.create_text(px0 + r_px + 4, py0 - r_px - 4,
                      text=f"r={ACCEPT_R}", fill=COLOR_OK,
                      anchor=tk.W, font=("Segoe UI", 8, "bold"))

        # tick labels
        for v in (int(AXIS_MIN), 0, int(AXIS_MAX)):
            px, _ = to_px(v, 0)
            c.create_text(px, oy + plot_size + 10, text=str(v),
                          fill="#555", font=("Segoe UI", 8))
            _, py = to_px(0, v)
            c.create_text(ox - 6, py, text=str(v),
                          fill="#555", font=("Segoe UI", 8), anchor=tk.E)

        # axis titles
        c.create_text(ox + plot_size / 2, oy + plot_size + 22,
                      text="Poynt.X (deg)", fill="#333",
                      font=("Segoe UI", 9, "bold"))
        try:
            c.create_text(ox - 30, oy + plot_size / 2,
                          text="Poynt.Y (deg)", fill="#333",
                          font=("Segoe UI", 9, "bold"), angle=90)
        except tk.TclError:
            c.create_text(ox - 30, oy + plot_size / 2,
                          text="Y (deg)", fill="#333",
                          font=("Segoe UI", 9, "bold"))

        # legend
        legend_x = ox + plot_size - 110
        for i, (label, _side, _sec, color) in enumerate(SERIES):
            ly = oy + 8 + i * 15
            c.create_rectangle(legend_x, ly, legend_x + 10, ly + 10,
                               fill=color, outline="")
            c.create_text(legend_x + 14, ly + 5, text=label,
                          anchor=tk.W, fill="#333",
                          font=("Segoe UI", 8))

        # points (skip any outside visible range)
        for label, x, y, color in self.current_points:
            if not (AXIS_MIN <= x <= AXIS_MAX and AXIS_MIN <= y <= AXIS_MAX):
                continue
            px, py = to_px(x, y)
            c.create_oval(px - 5, py - 5, px + 5, py + 5,
                          fill=color, outline="white", width=1)


def main():
    initial = sys.argv[1] if len(sys.argv) > 1 else None
    app = SerialDataViewer(initial)
    app.mainloop()


if __name__ == "__main__":
    main()

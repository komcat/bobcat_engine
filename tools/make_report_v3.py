#!/usr/bin/env python3
"""Generate a self-contained HTML report for a beam-capture run that includes
the per-Z beam SLICES ("Z-Propagation"), in the style of the vendor report
(Result table + Pointing / Waist / Pair-offset scatters + Z-Propagation strips).

This is the slice-aware successor to make_report_v2.py. It reads the same
"beam_capture_all_channels" data layout, plus the per-slice images written by
beam_capture_all_channels.lua:

    <root>/<serial>.json                                   # all entries
    <root>/<serial>/<run_tag>/<STATION>/beam_*.png          # final centred capture
    <root>/<serial>/<run_tag>/<STATION>/slices/             # per-Z slices:
        <STATION>_slice_<NN>_z<offset>um_{raw|overlay|B0|B1|collage}.png

Each entry in <serial>.json (key == "beam_capture_all_channels") carries:
    station, run_tag, axis, status, temperature_c,
    b0/b1 {poynt_x/y_deg, loc_x/y_um, waist_x/y_um, m2_x/y, z0_um,
           ee_pct, peak_intensity, cx/cy_px, d4_x/y_um, label, image, slot},
    power [{device, channel, setpoint_mA, measured_mA}],
    slices {dir, count, z_start_um, z_step_um, b0_crop, b1_crop},
    image_dir  (".../<run_tag>/<station>")

Usage:
    python make_report_v3.py [RUN_FOLDER] [-o report.html] [--open]

RUN_FOLDER is the <run_tag> directory (e.g. "test 0615/20260615_141058").
If omitted, the current directory is used. The serial is the parent folder
name and <serial>.json is read from the data root (run's grandparent).

The report bundles every image as base64 so the .html is fully portable.
The Z-Propagation strips use the per-beam crops (_B0/_B1) to keep the file
small; the full-frame _raw slices stay on disk.
"""

import argparse
import base64
import html
import json
import re
import sys
from pathlib import Path

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp"}

# Pass/fail limits as (center, tolerance): value passes within [c-tol, c+tol].
LIMITS = {
    "poynt_x_deg": (0.0, 0.5),   # Poynt.X: 0 +/- 0.5 deg
    "poynt_y_deg": (0.0, 0.5),   # Poynt.Y: 0 +/- 0.5 deg
}

# Final centred beam images we know how to label, in display order.
BEAM_VIEWS = [
    ("beam_collage.png", "Collage"),
    ("beam_overlay.png", "Overlay"),
    ("beam_raw.png", "Raw"),
    ("beam_B0.png", "B0"),
    ("beam_B1.png", "B1"),
]

PALETTE = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf",
           "#8c564b", "#e377c2", "#bcbd22", "#7f7f7f"]

RX_COLOR = "#1f77b4"   # blue
TX_COLOR = "#ff7f0e"   # orange

# Filename pattern for slice images, e.g. "SA 03 04_slice_04_z+0um_B0.png".
SLICE_RE = re.compile(r"_slice_(\d+)_z([+-]?\d+)um_([A-Za-z0-9]+)\.png$", re.IGNORECASE)


# --------------------------------------------------------------------------- #
# Data loading
# --------------------------------------------------------------------------- #
def load_json(path: Path):
    try:
        with path.open(encoding="utf-8") as fh:
            return json.load(fh)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"  ! could not read {path}: {exc}", file=sys.stderr)
        return None


def station_of(value: dict):
    """Station/location name for an entry: prefer 'station', else last
    segment of image_dir."""
    st = value.get("station")
    if st:
        return st
    idir = value.get("image_dir", "").replace("\\", "/").rstrip("/")
    return idir.rsplit("/", 1)[-1] if idir else "—"


def _entry_run_tag(val: dict):
    """The captured run_tag for an entry: prefer 'run_tag', else the
    second-to-last segment of image_dir (.../<run_tag>/<station>)."""
    tag = val.get("run_tag") or ""
    if tag:
        return tag
    idir = (val.get("image_dir") or "").replace("\\", "/").rstrip("/")
    parts = idir.rsplit("/", 2)
    return parts[-2] if len(parts) >= 2 else ""


def _run_tag_matches(stored: str, requested: str):
    """True if an entry's captured run_tag belongs to the requested run
    folder. The folder may have been renamed with a trailing suffix beyond
    the captured tag (e.g. '20260617_093226' -> '20260617_093226_after_bake'),
    so accept the stored tag as a leading prefix of the folder name."""
    if not stored:
        return False
    if stored == requested:
        return True
    return requested.startswith(stored) and (
        len(requested) == len(stored) or requested[len(stored)] in " _-.")


def load_run_entries(serial_json: Path, run_tag: str):
    """Return [entry_value, ...] for the given run_tag, in capture order."""
    data = load_json(serial_json)
    if not data:
        return [], None
    out = []
    for entry in data.get("entries", []):
        if entry.get("key") != "beam_capture_all_channels":
            continue
        val = entry.get("value")
        if not isinstance(val, dict):
            continue
        if _run_tag_matches(_entry_run_tag(val), run_tag):
            out.append({"timestamp": entry.get("timestamp", ""), **val})
    return out, data


def b64_img(path: Path):
    if not path.is_file():
        return None
    ext = path.suffix.lower().lstrip(".")
    mime = "jpeg" if ext in ("jpg", "jpeg") else ext
    return f"data:image/{mime};base64," + base64.b64encode(path.read_bytes()).decode("ascii")


# --------------------------------------------------------------------------- #
# Beam helpers
# --------------------------------------------------------------------------- #
def beam_label(b):
    """Clean display label for a beam: 'A1.RX' -> 'RX', 'ch.2' -> 'ch.2'."""
    raw = (b.get("label") or "").strip()
    if "." in raw:
        head, tail = raw.rsplit(".", 1)
        if tail.upper() in ("RX", "TX"):
            return tail.upper()
    return raw


def beam_side(b):
    """'RX' / 'TX' / '' for a beam, from its label."""
    raw = (b.get("label") or "").strip().upper()
    if raw.endswith("RX"):
        return "RX"
    if raw.endswith("TX"):
        return "TX"
    return ""


def beam_tag(b, station):
    """Short marker text for scatter points: channel number, or RX/TX side,
    falling back to a trimmed station name."""
    raw = (b.get("label") or "").strip()
    m = re.search(r"(\d+)\s*$", raw)
    if raw.lower().startswith("ch") and m:
        return m.group(1)
    side = beam_side(b)
    st = station.upper().replace("MOE-", "").replace(" ", "")
    return f"{st}.{side}" if side else st


def beam_order_key(b):
    """Sort key so RX precedes TX, and channels go ascending (ch.1, ch.2...)."""
    raw = (b.get("label") or "").strip().lower()
    if raw.endswith("rx"):
        return (0, 0)
    if raw.endswith("tx"):
        return (0, 1)
    m = re.search(r"(\d+)\s*$", raw)
    if m:
        return (1, int(m.group(1)))
    return (2, raw)


def sorted_beams(e):
    """b0/b1 of an entry, ordered RX->TX / channel-ascending."""
    beams = [b for bk in ("b0", "b1") if isinstance((b := e.get(bk)), dict)]
    beams.sort(key=beam_order_key)
    return beams


# --------------------------------------------------------------------------- #
# Generic scatter (inline SVG, no matplotlib dependency)
# --------------------------------------------------------------------------- #
def scatter_svg(points, xlabel, ylabel, center=None, rings=(), pad=0.18,
                equal=True):
    """points = [(x, y, tooltip, color, tag)] -> SVG string ('' if no points).

    center/rings draw dashed tolerance circles around `center`.
    `equal` forces a square data aspect so rings render as circles.
    """
    pts = [p for p in points if p[0] is not None and p[1] is not None]
    if not pts:
        return ""

    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    minx, maxx = min(xs), max(xs)
    miny, maxy = min(ys), max(ys)
    if center is not None and rings:
        cx0, cy0 = center
        rmax = max(rings)
        minx, maxx = min(minx, cx0 - rmax), max(maxx, cx0 + rmax)
        miny, maxy = min(miny, cy0 - rmax), max(maxy, cy0 + rmax)

    midx, midy = (minx + maxx) / 2, (miny + maxy) / 2
    spanx = (maxx - minx) or 1.0
    spany = (maxy - miny) or 1.0
    if equal:
        spanx = spany = max(spanx, spany)
    halfx, halfy = spanx * (0.5 + pad), spany * (0.5 + pad)
    lox, hix = midx - halfx, midx + halfx
    loy, hiy = midy - halfy, midy + halfy

    W = H = 360
    M = 50
    span = W - 2 * M

    def sx(v):
        return M + (v - lox) / (hix - lox) * span

    def sy(v):
        return M + (hiy - v) / (hiy - loy) * span

    parts = [f'<svg width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
             f'xmlns="http://www.w3.org/2000/svg" font-family="monospace" font-size="11">']
    parts.append(f'<rect x="{M}" y="{M}" width="{span}" height="{span}" '
                 f'fill="#fafafa" stroke="#bbb"/>')

    # 5 evenly spaced ticks on each axis.
    for i in range(5):
        vx = lox + i * (hix - lox) / 4
        vy = loy + i * (hiy - loy) / 4
        gx, gy = sx(vx), sy(vy)
        parts.append(f'<line x1="{gx:.1f}" y1="{M}" x2="{gx:.1f}" y2="{M+span}" stroke="#e6e6e6"/>')
        parts.append(f'<line x1="{M}" y1="{gy:.1f}" x2="{M+span}" y2="{gy:.1f}" stroke="#e6e6e6"/>')
        parts.append(f'<text x="{gx:.1f}" y="{M+span+16}" text-anchor="middle" fill="#555">{vx:.2f}</text>')
        parts.append(f'<text x="{M-8}" y="{gy+4:.1f}" text-anchor="end" fill="#555">{vy:.2f}</text>')

    # Axes through the origin if it is in range.
    if lox <= 0 <= hix:
        parts.append(f'<line x1="{sx(0):.1f}" y1="{M}" x2="{sx(0):.1f}" y2="{M+span}" stroke="#999"/>')
    if loy <= 0 <= hiy:
        parts.append(f'<line x1="{M}" y1="{sy(0):.1f}" x2="{M+span}" y2="{sy(0):.1f}" stroke="#999"/>')

    # Tolerance rings.
    if center is not None:
        cx0, cy0 = center
        rpx_per_unit = span / (hix - lox)
        for r in rings:
            parts.append(f'<circle cx="{sx(cx0):.1f}" cy="{sy(cy0):.1f}" '
                         f'r="{r*rpx_per_unit:.1f}" fill="none" stroke="#2a9d2a" '
                         f'stroke-dasharray="5 4"/>')
            parts.append(f'<text x="{sx(cx0)+r*rpx_per_unit+3:.1f}" '
                         f'y="{sy(cy0)-r*rpx_per_unit+2:.1f}" fill="#2a9d2a">{r:g}</text>')

    # Points + tags.
    for x, y, tip, color, tag in pts:
        cx, cy = sx(x), sy(y)
        parts.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="5" fill="{color}" '
                     f'stroke="#333" stroke-width="0.6"><title>{html.escape(str(tip))}</title></circle>')
        if tag:
            parts.append(f'<text x="{cx+6:.1f}" y="{cy-5:.1f}" fill="#333" font-size="10">{html.escape(str(tag))}</text>')

    parts.append(f'<text x="{M+span/2:.1f}" y="{H-6}" text-anchor="middle" fill="#333">{html.escape(xlabel)}</text>')
    parts.append(f'<text x="14" y="{M+span/2:.1f}" text-anchor="middle" fill="#333" '
                 f'transform="rotate(-90 14 {M+span/2:.1f})">{html.escape(ylabel)}</text>')
    parts.append("</svg>")
    return "".join(parts)


# --------------------------------------------------------------------------- #
# HTML rendering helpers
# --------------------------------------------------------------------------- #
def esc(v):
    return html.escape(str(v))


def fmt(v, nd=3):
    if isinstance(v, (int, float)):
        return f"{v:.{nd}f}"
    return esc(v) if v not in (None, "") else "—"


def verdict(field, value):
    if field not in LIMITS or not isinstance(value, (int, float)):
        return ""
    center, tol = LIMITS[field]
    return "pass" if (center - tol) <= value <= (center + tol) else "fail"


def cell(field, value, nd=3):
    return (fmt(value, nd), verdict(field, value))


def table(headers, rows, cls="data"):
    out = [f'<table class="{cls}"><thead><tr>']
    out += [f"<th>{esc(h)}</th>" for h in headers]
    out.append("</tr></thead><tbody>")
    for r in rows:
        cells = []
        for c in r:
            if isinstance(c, tuple):
                txt, css = c
                cells.append(f'<td class="{css}">{txt}</td>' if css else f"<td>{txt}</td>")
            else:
                cells.append(f"<td>{c}</td>")
        out.append("<tr>" + "".join(cells) + "</tr>")
    out.append("</tbody></table>")
    return "".join(out)


def meas_rows(entries):
    rows = []
    for e in entries:
        st = station_of(e)
        expo = e.get("swir_exposure_us")
        for b in sorted_beams(e):
            rows.append([
                esc(st), esc(b.get("label") or beam_label(b)), esc(beam_side(b) or "—"),
                fmt(expo, 0),
                cell("poynt_x_deg", b.get("poynt_x_deg")),
                cell("poynt_y_deg", b.get("poynt_y_deg")),
                fmt(b.get("loc_x_um"), 2), fmt(b.get("loc_y_um"), 2),
                fmt(b.get("waist_x_um"), 2), fmt(b.get("waist_y_um"), 2),
                fmt(b.get("m2_x")), fmt(b.get("m2_y")),
                fmt(b.get("z0_um"), 1),
                fmt(b.get("ee_pct"), 1),
                fmt(b.get("peak_intensity"), 0),
            ])
    return rows


def power_rows(entries):
    rows = []
    for e in entries:
        st = station_of(e)
        for p in e.get("power", []):
            sp = p.get("setpoint_mA")
            ms = p.get("measured_mA")
            delta = (ms - sp) if isinstance(sp, (int, float)) and isinstance(ms, (int, float)) else None
            rows.append([esc(st), esc(p.get("device", "")), fmt(p.get("channel"), 0),
                         fmt(sp, 1), fmt(ms, 2), fmt(delta, 2)])
    return rows


def offset_rows(entries):
    """Per-station TX-RX delta (dx, dy) computed as B0 − B1, matching the
    'beam details' window. sorted_beams orders RX(b0) before TX(b1), so
    beams[0]=B0(Rx), beams[1]=B1(Tx) and the delta is RX − TX. Stations
    without exactly two beams are skipped."""
    rows = []
    for e in entries:
        if not station_of(e).upper().startswith("MOE"):
            continue
        beams = sorted_beams(e)
        if len(beams) != 2:
            continue
        b0, b1 = beams[0], beams[1]
        if not all(isinstance(b.get(k), (int, float))
                   for b in (b0, b1) for k in ("loc_x_um", "loc_y_um")):
            continue
        dx = b0["loc_x_um"] - b1["loc_x_um"]
        dy = b0["loc_y_um"] - b1["loc_y_um"]
        mag = (dx * dx + dy * dy) ** 0.5
        rows.append([esc(station_of(e)),
                     esc(b0.get("label") or beam_label(b0)),
                     esc(b1.get("label") or beam_label(b1)),
                     fmt(dx, 3), fmt(dy, 3), fmt(mag, 3)])
    return rows


def summary_rows(entries):
    rows = []
    for e in entries:
        status = e.get("status", "—")
        css = "" if status == "ok" else "fail"
        rows.append([esc(station_of(e)), esc(e.get("axis", "—")),
                     (esc(status), css), fmt(e.get("temperature_c"), 2),
                     fmt(e.get("swir_exposure_us"), 0)])
    return rows


# --------------------------------------------------------------------------- #
# Slice (Z-propagation) discovery + rendering
# --------------------------------------------------------------------------- #
def slice_files(slices_dir: Path, station: str, view: str):
    """Return [(slice_idx, z_offset_um, Path)] for one view (e.g. 'B0'),
    sorted by slice index."""
    if not slices_dir.is_dir():
        return []
    out = []
    for p in slices_dir.iterdir():
        if not p.is_file():
            continue
        m = SLICE_RE.search(p.name)
        if not m:
            continue
        if m.group(3).lower() != view.lower():
            continue
        out.append((int(m.group(1)), int(m.group(2)), p))
    out.sort(key=lambda t: t[0])
    return out


def zprop_section(run: Path, serial: str, entries):
    """One Z-Propagation strip per beam (per station), using the _B<slot> crops."""
    s = ['<h3>Z-Propagation (beam slices before fitting)</h3>']
    rendered = 0
    for e in entries:
        st = station_of(e)
        slices_dir = run / st / "slices"
        for b in sorted_beams(e):
            slot = b.get("slot")
            if slot is None:
                continue
            files = slice_files(slices_dir, st, f"B{slot}")
            if not files:
                continue
            side = beam_side(b)
            lbl = b.get("label") or beam_label(b)
            title = f"Z-Propagation | {serial} | {st} | {lbl}"
            s.append('<div class="zrow">')
            s.append(f'<div class="ztitle">{esc(title)}</div>')
            s.append('<div class="zstrip">')
            any_img = False
            for _idx, off, p in files:
                uri = b64_img(p)
                if not uri:
                    continue
                any_img = True
                s.append(f'<figure class="zfig"><img src="{uri}" alt="{esc(p.name)}">'
                         f'<figcaption>@{off}um</figcaption></figure>')
            s.append('</div></div>')
            if any_img:
                rendered += 1
    if rendered == 0:
        s.append('<p class="muted">no slice images found under &lt;station&gt;/slices/</p>')
    return "".join(s)


def overview_section(run: Path, entries):
    """A compact 'beams at focus' row: each station's final per-beam crops."""
    s = ['<h3>Beams at focus</h3><div class="overview">']
    for e in entries:
        st = station_of(e)
        img_dir = run / st
        cards = []
        for b in sorted_beams(e):
            slot = b.get("slot")
            if slot is None:
                continue
            uri = b64_img(img_dir / f"beam_B{slot}.png")
            if not uri:
                continue
            lbl = b.get("label") or beam_label(b)
            cards.append(f'<figure class="ovfig"><img src="{uri}" alt="{esc(lbl)}">'
                         f'<figcaption>{esc(lbl)}</figcaption></figure>')
        if cards:
            s.append(f'<div class="ovstation"><div class="ovname">{esc(st)}</div>'
                     f'<div class="ovbeams">{"".join(cards)}</div></div>')
    s.append('</div>')
    return "".join(s)


# --------------------------------------------------------------------------- #
# Rendering
# --------------------------------------------------------------------------- #
def render_run(run: Path, serial: str, run_tag: str, entries):
    ts = entries[0].get("timestamp", "") if entries else ""
    s = [f'<section class="run"><h2>Serial {esc(serial)} '
         f'<span class="ts">{esc(run_tag)}</span></h2>']
    s.append(f'<p class="sub">Captured: <b>{esc(ts)}</b> &nbsp;·&nbsp; '
             f'Stations: <b>{len(entries)}</b> &nbsp;·&nbsp; '
             f'Run folder: <code>{esc(run.name)}</code></p>')

    # ---- scatter points ----
    poynt_pts, waist_pts, offset_pts = [], [], []
    for i, e in enumerate(entries):
        st = station_of(e)
        beams = sorted_beams(e)
        for b in beams:
            side = beam_side(b)
            color = RX_COLOR if side == "RX" else TX_COLOR if side == "TX" else PALETTE[i % len(PALETTE)]
            tag = beam_tag(b, st)
            lbl = b.get("label") or beam_label(b)
            px, py = b.get("poynt_x_deg"), b.get("poynt_y_deg")
            poynt_pts.append((px, py, f"{st} {lbl}: ({fmt(px)}, {fmt(py)})°", color, tag))
            wx, wy = b.get("waist_x_um"), b.get("waist_y_um")
            waist_pts.append((wx, wy, f"{st} {lbl}: waist ({fmt(wx,2)}, {fmt(wy,2)})µm", color, tag))
        # TX-RX delta: B0 - B1 per station (matches the beam-details window).
        # Only meaningful for the MOE RX/TX pairs; skip SA channel pairs.
        if len(beams) == 2 and st.upper().startswith("MOE"):
            a, c = beams[0], beams[1]
            if all(isinstance(a.get(k), (int, float)) and isinstance(c.get(k), (int, float))
                   for k in ("loc_x_um", "loc_y_um")):
                dx = a["loc_x_um"] - c["loc_x_um"]
                dy = a["loc_y_um"] - c["loc_y_um"]
                offset_pts.append((dx, dy, f"{st}: Δ({fmt(dx,2)}, {fmt(dy,2)})µm",
                                   PALETTE[i % len(PALETTE)], st.upper().replace("MOE-", "").replace(" ", "")))

    poynt = scatter_svg(poynt_pts, "Poynt.X [deg]", "Poynt.Y [deg]",
                        center=(0.0, 0.0), rings=(0.5, 1.0))
    waist = scatter_svg(waist_pts, "x Waist Size [um]", "y Waist Size [um]")
    offset = scatter_svg(offset_pts, "TX-RX delta dx [um]", "TX-RX delta dy [um]")

    side_legend = (f'<div class="legend">'
                   f'<span class="lg"><i style="background:{RX_COLOR}"></i>RX</span>'
                   f'<span class="lg"><i style="background:{TX_COLOR}"></i>TX</span></div>')

    s.append('<div class="cols">')
    if poynt:
        s.append(f'<div class="card"><h3>Pointing (Poynt)</h3>{poynt}{side_legend}</div>')
    if waist:
        s.append(f'<div class="card"><h3>Beam waist size</h3>{waist}{side_legend}</div>')
    if offset:
        otable = table(["Station", "dx µm", "dy µm", "|Δ| µm"],
                       [[r[0], r[3], r[4], r[5]] for r in offset_rows(entries)])
        s.append(f'<div class="card"><h3>TX-RX delta (B0 − B1)</h3>{offset}{otable}</div>')
    s.append('<div class="card"><h3>Station summary</h3>'
             + table(["Station", "Axis", "Status", "Temp °C", "Exposure µs"], summary_rows(entries))
             + '</div>')
    s.append("</div>")  # cols

    # ---- result / measurement table ----
    mrows = meas_rows(entries)
    if mrows:
        s.append("<h3>Result table</h3>")
        s.append(table(["Station", "Label", "Side", "Exposure µs",
                        "Poynt.X ° (0±0.5)", "Poynt.Y ° (0±0.5)",
                        "loc X µm", "loc Y µm", "waist X µm", "waist Y µm",
                        "M²x", "M²y", "z0 µm", "EE %", "Peak"], mrows))

    # ---- power table ----
    prows = power_rows(entries)
    if prows:
        s.append("<h3>Drive current</h3>")
        s.append(table(["Station", "Device", "Ch", "Setpoint mA", "Measured mA", "Δ mA"], prows))

    # ---- beams at focus overview ----
    s.append(overview_section(run, entries))

    # ---- Z-propagation slice strips (the multi-slice section) ----
    s.append(zprop_section(run, serial, entries))

    # ---- full image gallery per station ----
    s.append('<h3>Captured images (final, per station)</h3>')
    for e in entries:
        st = station_of(e)
        img_dir = run / st
        files = sorted(p.name for p in img_dir.iterdir()
                       if p.is_file() and p.suffix.lower() in IMAGE_EXTS) if img_dir.is_dir() else []
        s.append(f'<h4 class="loc">{esc(st)}</h4><div class="gallery">')
        ordered = [(fn, lbl) for fn, lbl in BEAM_VIEWS if fn in files]
        ordered += [(fn, fn) for fn in files if fn not in {f for f, _ in BEAM_VIEWS}]
        any_img = False
        for fn, lbl in ordered:
            uri = b64_img(img_dir / fn)
            if uri:
                any_img = True
                s.append(f'<figure><img src="{uri}" alt="{esc(fn)}">'
                         f'<figcaption>{esc(lbl)}</figcaption></figure>')
        if not any_img:
            s.append('<p class="muted">no images found</p>')
        s.append("</div>")

    s.append("</section>")
    return "".join(s)


CSS = """
:root{color-scheme:light}
body{font-family:'Segoe UI',Roboto,sans-serif;margin:0;padding:24px;background:#f4f5f7;color:#1c1c1e}
h1{margin:0 0 4px}
.meta{color:#666;margin-bottom:24px;font-size:14px}
section.run{background:#fff;border:1px solid #e0e0e0;border-radius:10px;padding:20px 24px;margin-bottom:28px;box-shadow:0 1px 3px rgba(0,0,0,.06)}
h2{margin:0 0 2px;font-size:20px}
h2 .ts{font-weight:400;color:#888;font-size:14px;margin-left:8px}
.sub{margin:0 0 16px;color:#555;font-size:14px}
h3{margin:22px 0 8px;font-size:15px;border-bottom:2px solid #f0f0f0;padding-bottom:4px}
h4.loc{margin:16px 0 6px;color:#1f6fbf}
.cols{display:flex;gap:20px;flex-wrap:wrap;align-items:flex-start}
.card{background:#fcfcfd;border:1px solid #ececec;border-radius:8px;padding:12px 16px}
.card h3{margin-top:6px}
.legend{margin-top:6px;font-size:12px;max-width:320px}
.lg{margin-right:12px;white-space:nowrap;display:inline-block}
.lg i{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:4px;vertical-align:middle}
table.data{border-collapse:collapse;font-size:12.5px;margin:4px 0 8px}
table.data th{background:#f3f4f6;text-align:right;padding:5px 9px;border:1px solid #e3e3e3;font-weight:600}
table.data td{text-align:right;padding:4px 9px;border:1px solid #ececec;font-variant-numeric:tabular-nums}
table.data td:first-child,table.data th:first-child{text-align:left}
table.data td.pass{background:#d7f5d7;color:#0a6b0a;font-weight:600}
table.data td.fail{background:#fbd5d5;color:#a40000;font-weight:600}
.gallery{display:flex;flex-wrap:wrap;gap:14px;margin-bottom:8px}
figure{margin:0;text-align:center}
figure img{max-width:220px;max-height:220px;border:1px solid #ddd;border-radius:6px;background:#000;display:block}
figcaption{font-size:12px;color:#666;margin-top:3px}
.overview{display:flex;flex-wrap:wrap;gap:16px;margin-bottom:8px}
.ovstation{border:1px solid #ececec;border-radius:8px;padding:8px 10px;background:#fcfcfd}
.ovname{font-size:12px;color:#1f6fbf;font-weight:600;margin-bottom:4px;text-align:center}
.ovbeams{display:flex;gap:8px}
.ovfig img{width:84px;height:84px;object-fit:contain;border:1px solid #ddd;border-radius:4px;background:#000;display:block}
.ovfig figcaption{font-size:11px}
.zrow{margin:10px 0 18px}
.ztitle{font-size:13px;color:#333;font-weight:600;text-align:center;margin-bottom:6px}
.zstrip{display:flex;flex-wrap:nowrap;gap:6px;overflow-x:auto;padding-bottom:6px}
.zfig{flex:0 0 auto}
.zfig img{width:92px;height:92px;object-fit:contain;border:1px solid #444;border-radius:4px;background:#000;display:block}
.zfig figcaption{font-size:11px;color:#555}
.muted{color:#999;font-style:italic}
code{background:#f0f0f0;padding:1px 5px;border-radius:4px;font-size:12px}
footer{color:#999;font-size:12px;text-align:center;margin-top:8px}
"""


def build_report(run, serial, run_tag, entries, title):
    body = ['<!doctype html><html lang="en"><head><meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width,initial-scale=1">',
            f"<title>{esc(title)}</title><style>{CSS}</style></head><body>",
            f"<h1>{esc(title)}</h1>",
            f'<div class="meta">{len(entries)} station(s) · generated by make_report_v3.py</div>']
    body.append(render_run(run, serial, run_tag, entries))
    body.append('<footer>- Fabrinet Confidential -</footer></body></html>')
    return "".join(body)


def main():
    ap = argparse.ArgumentParser(
        description="Build an HTML report (with Z-propagation slices) for one beam-capture run.")
    ap.add_argument("path", nargs="?", default=".", help="run folder (<serial>/<run_tag>)")
    ap.add_argument("-o", "--output", help="output .html path (default: <run>/report_slices.html)")
    ap.add_argument("--serial-json", help="path to <serial>.json (default: data-root/<serial>.json)")
    ap.add_argument("--open", action="store_true", help="open the report when done")
    args = ap.parse_args()

    run = Path(args.path).resolve()
    if not run.is_dir():
        print(f"error: run folder not found: {run}", file=sys.stderr)
        return 1

    run_tag = run.name
    serial = run.parent.name
    root = run.parent.parent
    serial_json = Path(args.serial_json).resolve() if args.serial_json else (root / f"{serial}.json")
    if not serial_json.is_file():
        print(f"error: serial json not found: {serial_json}", file=sys.stderr)
        return 1

    entries, _ = load_run_entries(serial_json, run_tag)
    if not entries:
        print(f"error: no beam_capture entries for run_tag '{run_tag}' in {serial_json}", file=sys.stderr)
        return 1

    # Location order: MOE stations first, then SA stations by number (names are
    # zero-padded so lexicographic == numeric).
    entries.sort(key=lambda e: (0 if station_of(e).upper().startswith("MOE") else 1,
                                station_of(e)))

    out = Path(args.output) if args.output else (run / "report_slices.html")
    title = f"Beam Slice Report — {serial} / {run_tag}"
    print(f"Building report: serial={serial} run_tag={run_tag} stations={len(entries)}")
    for e in entries:
        st = station_of(e)
        n_b0 = len(slice_files(run / st / "slices", st, "B0"))
        n_b1 = len(slice_files(run / st / "slices", st, "B1"))
        print(f"  + {st}  (slices B0={n_b0}, B1={n_b1})")
    out.write_text(build_report(run, serial, run_tag, entries, title), encoding="utf-8")
    print(f"Wrote {out}")

    if args.open:
        import webbrowser
        webbrowser.open(out.as_uri())
    return 0


if __name__ == "__main__":
    sys.exit(main())

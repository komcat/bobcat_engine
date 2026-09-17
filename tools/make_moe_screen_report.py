#!/usr/bin/env python3
"""Generate a self-contained HTML report for one MOE-Screening capture-all run.

This is the DUT-less, MOE-ID-keyed sibling of make_report_v3.py. The MOE Screening
tab (MoeScreeningWidget) has no DUT and no <serial>.json — moe_screen_capture_all.lua
writes a per-run CSV summary plus per-station SWIR image packages + Z-scan slices:

    <run>/capture_summary.csv                              # one row per CHANNEL
    <run>/<station>/beam_*.png                             # final centred capture
    <run>/<station>/slices/                                # per-Z slices:
        <station>_slice_<NN>_z<offset>um_{raw|overlay|B0|B1|collage}.png

`<run>` is the timestamped run folder (data/<product>/moe-screen/<MOE_ID>/<run_tag>).
Each CSV row is one channel with a Poynt/Rx (b0, top) + Tx (b1, bottom) pair; scr-chN
stations contribute two rows (right = odd channel, left = even), the two IR stations
one row each.

The report bundles every image as base64 so the .html is fully portable.

Usage:
    python make_moe_screen_report.py [RUN_FOLDER] [-o report.html] [--open]

RUN_FOLDER defaults to the current directory. Output defaults to
<run>/report_slices.html (same filename the widget looks for).
"""

import argparse
import base64
import csv
import html
import re
import sys
from pathlib import Path

IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".bmp"}

# Pass/fail limits as (center, tolerance): value passes within [c-tol, c+tol].
# Poynt ±0.5° matches the tab's green band (|value| < 0.5).
LIMITS = {
    "poynt_x_deg": (0.0, 0.5),
    "poynt_y_deg": (0.0, 0.5),
}

# Final centred beam images we know how to label, in display order.
BEAM_VIEWS = [
    ("beam_collage.png", "Collage"),
    ("beam_overlay.png", "Overlay"),
    ("beam_raw.png", "Raw"),
    ("beam_B0.png", "B0"),
    ("beam_B1.png", "B1"),
]

RX_COLOR = "#1f77b4"   # blue
TX_COLOR = "#ff7f0e"   # orange
PALETTE = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf",
           "#8c564b", "#e377c2", "#bcbd22", "#7f7f7f"]

# Filename pattern for slice images, e.g. "scr-ch1_slice_04_z+0um_B0.png".
SLICE_RE = re.compile(r"_slice_(\d+)_z([+-]?\d+)um_([A-Za-z0-9]+)\.png$", re.IGNORECASE)


# --------------------------------------------------------------------------- #
# CSV loading
# --------------------------------------------------------------------------- #
def _f(row, key):
    """Parse a numeric CSV field to float, or None if empty/invalid."""
    v = row.get(key, "")
    if v is None or str(v).strip() == "":
        return None
    try:
        return float(v)
    except ValueError:
        return None


def _i(row, key):
    v = _f(row, key)
    return int(v) if v is not None else None


def _beam(row, prefix):
    """Build a beam dict from the b0_/b1_ columns of a CSV row."""
    return {
        "label": (row.get(prefix + "label") or "").strip(),
        "slot": _i(row, prefix + "slot"),
        "image": (row.get(prefix + "image") or "").strip(),
        "cx_px": _f(row, prefix + "cx_px"),
        "cy_px": _f(row, prefix + "cy_px"),
        "poynt_x_deg": _f(row, prefix + "poynt_x_deg"),
        "poynt_y_deg": _f(row, prefix + "poynt_y_deg"),
        "loc_x_um": _f(row, prefix + "loc_x_um"),
        "loc_y_um": _f(row, prefix + "loc_y_um"),
        "z0_um": _f(row, prefix + "z0_um"),
        "waist_x_um": _f(row, prefix + "waist_x_um"),
        "waist_y_um": _f(row, prefix + "waist_y_um"),
        "m2_x": _f(row, prefix + "m2_x"),
        "m2_y": _f(row, prefix + "m2_y"),
        "ee_pct": _f(row, prefix + "ee_pct"),
        "peak_intensity": _f(row, prefix + "peak"),
    }


def load_entries(csv_path: Path):
    """Return [entry, ...] in capture order. One entry per channel row."""
    entries = []
    with csv_path.open(encoding="utf-8", newline="") as fh:
        for row in csv.DictReader(fh):
            entries.append({
                "station": (row.get("station") or "—").strip(),
                "channel_label": (row.get("channel_label") or "").strip(),
                "kind": (row.get("kind") or "").strip(),
                "status": (row.get("status") or "—").strip(),
                "timestamp": (row.get("timestamp") or "").strip(),
                "temperature_c": _f(row, "temperature_c"),
                "swir_exposure_us": _f(row, "swir_exposure_us"),
                "b0": _beam(row, "b0_"),
                "b1": _beam(row, "b1_"),
            })
    return entries


def b64_img(path: Path):
    if not path.is_file():
        return None
    ext = path.suffix.lower().lstrip(".")
    mime = "jpeg" if ext in ("jpg", "jpeg") else ext
    return f"data:image/{mime};base64," + base64.b64encode(path.read_bytes()).decode("ascii")


# --------------------------------------------------------------------------- #
# Beam helpers
# --------------------------------------------------------------------------- #
def beam_side(b):
    """'RX' / 'TX' / '' from a beam label like 'ch1.Rx' / 'A1.Tx'."""
    raw = (b.get("label") or "").strip().upper()
    if raw.endswith("RX"):
        return "RX"
    if raw.endswith("TX"):
        return "TX"
    return ""


def beam_label(b):
    return (b.get("label") or "").strip()


def entry_beams(e):
    """(b0, b1) as a list, skipping beams with no detection (slot is None)."""
    return [b for bk in ("b0", "b1")
            if isinstance((b := e.get(bk)), dict) and b.get("slot") is not None]


# --------------------------------------------------------------------------- #
# Generic scatter (inline SVG, no matplotlib dependency)
# --------------------------------------------------------------------------- #
def scatter_svg(points, xlabel, ylabel, center=None, rings=(), pad=0.18, equal=True):
    """points = [(x, y, tooltip, color, tag)] -> SVG string ('' if no points)."""
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
    parts.append(f'<rect x="{M}" y="{M}" width="{span}" height="{span}" fill="#fafafa" stroke="#bbb"/>')

    for i in range(5):
        vx = lox + i * (hix - lox) / 4
        vy = loy + i * (hiy - loy) / 4
        gx, gy = sx(vx), sy(vy)
        parts.append(f'<line x1="{gx:.1f}" y1="{M}" x2="{gx:.1f}" y2="{M+span}" stroke="#e6e6e6"/>')
        parts.append(f'<line x1="{M}" y1="{gy:.1f}" x2="{M+span}" y2="{gy:.1f}" stroke="#e6e6e6"/>')
        parts.append(f'<text x="{gx:.1f}" y="{M+span+16}" text-anchor="middle" fill="#555">{vx:.2f}</text>')
        parts.append(f'<text x="{M-8}" y="{gy+4:.1f}" text-anchor="end" fill="#555">{vy:.2f}</text>')

    if lox <= 0 <= hix:
        parts.append(f'<line x1="{sx(0):.1f}" y1="{M}" x2="{sx(0):.1f}" y2="{M+span}" stroke="#999"/>')
    if loy <= 0 <= hiy:
        parts.append(f'<line x1="{M}" y1="{sy(0):.1f}" x2="{M+span}" y2="{sy(0):.1f}" stroke="#999"/>')

    if center is not None:
        cx0, cy0 = center
        rpx_per_unit = span / (hix - lox)
        for r in rings:
            parts.append(f'<circle cx="{sx(cx0):.1f}" cy="{sy(cy0):.1f}" '
                         f'r="{r*rpx_per_unit:.1f}" fill="none" stroke="#2a9d2a" stroke-dasharray="5 4"/>')
            parts.append(f'<text x="{sx(cx0)+r*rpx_per_unit+3:.1f}" '
                         f'y="{sy(cy0)-r*rpx_per_unit+2:.1f}" fill="#2a9d2a">{r:g}</text>')

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
        st = e["station"]
        ch = e["channel_label"]
        expo = e.get("swir_exposure_us")
        for b in entry_beams(e):
            rows.append([
                esc(st), esc(ch), esc(beam_side(b) or "—"), fmt(expo, 0),
                cell("poynt_x_deg", b.get("poynt_x_deg")),
                cell("poynt_y_deg", b.get("poynt_y_deg")),
                fmt(b.get("loc_x_um"), 2), fmt(b.get("loc_y_um"), 2),
                fmt(b.get("waist_x_um"), 2), fmt(b.get("waist_y_um"), 2),
                fmt(b.get("m2_x")), fmt(b.get("m2_y")),
                fmt(b.get("z0_um"), 1), fmt(b.get("ee_pct"), 1),
                fmt(b.get("peak_intensity"), 0),
            ])
    return rows


def offset_rows(entries):
    """Per-channel TX-RX delta = Rx(b0) − Tx(b1)."""
    rows = []
    for e in entries:
        b0, b1 = e.get("b0"), e.get("b1")
        if not (b0 and b1 and b0.get("slot") is not None and b1.get("slot") is not None):
            continue
        if not all(isinstance(b.get(k), (int, float))
                   for b in (b0, b1) for k in ("loc_x_um", "loc_y_um")):
            continue
        dx = b0["loc_x_um"] - b1["loc_x_um"]
        dy = b0["loc_y_um"] - b1["loc_y_um"]
        mag = (dx * dx + dy * dy) ** 0.5
        rows.append([esc(e["station"]), esc(e["channel_label"]),
                     fmt(dx, 3), fmt(dy, 3), fmt(mag, 3)])
    return rows


def summary_rows(entries):
    rows = []
    for e in entries:
        status = e.get("status", "—")
        css = "" if status == "ok" else "fail"
        rows.append([esc(e["station"]), esc(e["channel_label"]),
                     (esc(status), css), fmt(e.get("temperature_c"), 2),
                     fmt(e.get("swir_exposure_us"), 0)])
    return rows


# --------------------------------------------------------------------------- #
# Slice (Z-propagation) discovery + rendering
# --------------------------------------------------------------------------- #
def slice_files(slices_dir: Path, view: str):
    """Return [(slice_idx, z_offset_um, Path)] for one view (e.g. 'B0')."""
    if not slices_dir.is_dir():
        return []
    out = []
    for p in slices_dir.iterdir():
        if not p.is_file():
            continue
        m = SLICE_RE.search(p.name)
        if not m or m.group(3).lower() != view.lower():
            continue
        out.append((int(m.group(1)), int(m.group(2)), p))
    out.sort(key=lambda t: t[0])
    return out


def zprop_section(run: Path, entries):
    """One Z-Propagation strip per beam (per channel), using the _B<slot> crops."""
    s = ['<h3>Z-Propagation (beam slices before fitting)</h3>']
    rendered = 0
    for e in entries:
        st = e["station"]
        slices_dir = run / st / "slices"
        for b in entry_beams(e):
            slot = b.get("slot")
            files = slice_files(slices_dir, f"B{slot}")
            if not files:
                continue
            lbl = beam_label(b) or f"B{slot}"
            title = f"Z-Propagation | {st} | {e['channel_label']} | {lbl}"
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
    """A compact 'beams at focus' row per channel: the final per-beam crops."""
    s = ['<h3>Beams at focus</h3><div class="overview">']
    for e in entries:
        st = e["station"]
        img_dir = run / st
        cards = []
        for b in entry_beams(e):
            slot = b.get("slot")
            uri = b64_img(img_dir / f"beam_B{slot}.png")
            if not uri:
                continue
            lbl = beam_label(b) or f"B{slot}"
            cards.append(f'<figure class="ovfig"><img src="{uri}" alt="{esc(lbl)}">'
                         f'<figcaption>{esc(lbl)}</figcaption></figure>')
        if cards:
            name = e["channel_label"] if e["channel_label"] == st else f"{st} · {e['channel_label']}"
            s.append(f'<div class="ovstation"><div class="ovname">{esc(name)}</div>'
                     f'<div class="ovbeams">{"".join(cards)}</div></div>')
    s.append('</div>')
    return "".join(s)


def gallery_section(run: Path, entries):
    """Full image gallery, one block per unique station (not per channel)."""
    s = ['<h3>Captured images (final, per station)</h3>']
    seen = set()
    for e in entries:
        st = e["station"]
        if st in seen:
            continue
        seen.add(st)
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
    return "".join(s)


# --------------------------------------------------------------------------- #
# Rendering
# --------------------------------------------------------------------------- #
def render_run(run: Path, moe_id: str, run_tag: str, entries):
    ts = entries[0].get("timestamp", "") if entries else ""
    n_stations = len({e["station"] for e in entries})
    s = [f'<section class="run"><h2>MOE {esc(moe_id)} '
         f'<span class="ts">{esc(run_tag)}</span></h2>']
    s.append(f'<p class="sub">Captured: <b>{esc(ts)}</b> &nbsp;·&nbsp; '
             f'Stations: <b>{n_stations}</b> &nbsp;·&nbsp; '
             f'Channels: <b>{len(entries)}</b> &nbsp;·&nbsp; '
             f'Run folder: <code>{esc(run.name)}</code></p>')

    # ---- scatter points ----
    poynt_pts, waist_pts, offset_pts = [], [], []
    for i, e in enumerate(entries):
        st = e["station"]
        ch = e["channel_label"]
        for b in entry_beams(e):
            side = beam_side(b)
            color = RX_COLOR if side == "RX" else TX_COLOR if side == "TX" else PALETTE[i % len(PALETTE)]
            lbl = beam_label(b)
            px, py = b.get("poynt_x_deg"), b.get("poynt_y_deg")
            poynt_pts.append((px, py, f"{st} {lbl}: ({fmt(px)}, {fmt(py)})°", color, ch))
            wx, wy = b.get("waist_x_um"), b.get("waist_y_um")
            waist_pts.append((wx, wy, f"{st} {lbl}: waist ({fmt(wx,2)}, {fmt(wy,2)})µm", color, ch))
        b0, b1 = e.get("b0"), e.get("b1")
        if (b0 and b1 and b0.get("slot") is not None and b1.get("slot") is not None
                and all(isinstance(b.get(k), (int, float))
                        for b in (b0, b1) for k in ("loc_x_um", "loc_y_um"))):
            dx = b0["loc_x_um"] - b1["loc_x_um"]
            dy = b0["loc_y_um"] - b1["loc_y_um"]
            offset_pts.append((dx, dy, f"{ch}: Δ({fmt(dx,2)}, {fmt(dy,2)})µm",
                               PALETTE[i % len(PALETTE)], ch))

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
        otable = table(["Station", "Ch", "dx µm", "dy µm", "|Δ| µm"], offset_rows(entries))
        s.append(f'<div class="card"><h3>TX-RX delta (Rx − Tx)</h3>{offset}{otable}</div>')
    s.append('<div class="card"><h3>Channel summary</h3>'
             + table(["Station", "Ch", "Status", "Temp °C", "Exposure µs"], summary_rows(entries))
             + '</div>')
    s.append("</div>")  # cols

    mrows = meas_rows(entries)
    if mrows:
        s.append("<h3>Result table</h3>")
        s.append(table(["Station", "Ch", "Side", "Exposure µs",
                        "Poynt.X ° (0±0.5)", "Poynt.Y ° (0±0.5)",
                        "loc X µm", "loc Y µm", "waist X µm", "waist Y µm",
                        "M²x", "M²y", "z0 µm", "EE %", "Peak"], mrows))

    s.append(overview_section(run, entries))
    s.append(zprop_section(run, entries))
    s.append(gallery_section(run, entries))
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


def build_report(run, moe_id, run_tag, entries, title):
    body = ['<!doctype html><html lang="en"><head><meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width,initial-scale=1">',
            f"<title>{esc(title)}</title><style>{CSS}</style></head><body>",
            f"<h1>{esc(title)}</h1>",
            f'<div class="meta">{len(entries)} channel(s) · generated by make_moe_screen_report.py</div>']
    body.append(render_run(run, moe_id, run_tag, entries))
    body.append('<footer>- Fabrinet Confidential -</footer></body></html>')
    return "".join(body)


def main():
    ap = argparse.ArgumentParser(
        description="Build an HTML report (with Z-propagation slices) for one MOE-screening capture run.")
    ap.add_argument("path", nargs="?", default=".", help="run folder (<MOE_ID>/<run_tag>)")
    ap.add_argument("-o", "--output", help="output .html path (default: <run>/report_slices.html)")
    ap.add_argument("--open", action="store_true", help="open the report when done")
    args = ap.parse_args()

    run = Path(args.path).resolve()
    if not run.is_dir():
        print(f"error: run folder not found: {run}", file=sys.stderr)
        return 1

    run_tag = run.name
    moe_id = run.parent.name
    csv_path = run / "capture_summary.csv"
    if not csv_path.is_file():
        print(f"error: capture_summary.csv not found in {run}", file=sys.stderr)
        return 1

    entries = load_entries(csv_path)
    if not entries:
        print(f"error: no channel rows in {csv_path}", file=sys.stderr)
        return 1

    out = Path(args.output) if args.output else (run / "report_slices.html")
    title = f"MOE Screen Report — {moe_id} / {run_tag}"
    print(f"Building report: moe_id={moe_id} run_tag={run_tag} channels={len(entries)}")
    for e in entries:
        st = e["station"]
        n_b0 = len(slice_files(run / st / "slices", f"B{e['b0'].get('slot')}")) if e["b0"].get("slot") is not None else 0
        n_b1 = len(slice_files(run / st / "slices", f"B{e['b1'].get('slot')}")) if e["b1"].get("slot") is not None else 0
        print(f"  + {st}  {e['channel_label']}  (slices B0={n_b0}, B1={n_b1})")
    out.write_text(build_report(run, moe_id, run_tag, entries, title), encoding="utf-8")
    print(f"Wrote {out}")

    if args.open:
        import webbrowser
        webbrowser.open(out.as_uri())
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Generate a self-contained HTML report for a single beam-capture run
(new "beam_capture_all_channels" data layout — no metadata.json).

Data layout produced by the engine (new format):

    <root>/<serial>.json                          # all entries for the serial
    <root>/<serial>/<run_tag>/<STATION>/*.png      # per-station beam images

Each entry in <serial>.json (key == "beam_capture_all_channels") carries:
    station, run_tag, axis, status, temperature_c,
    b0/b1 {poynt_x/y_deg, loc_x/y_um, waist_x/y_um, m2_x/y, z0_um,
           ee_pct, peak_intensity, cx/cy_px, d4_x/y_um, label, image},
    power [{device, channel, setpoint_mA, measured_mA}],
    image_dir  (".../<run_tag>/<station>")

Usage:
    python make_report_v2.py [RUN_FOLDER] [-o report.html] [--open]

RUN_FOLDER is the <run_tag> directory (e.g. "1737-2/20260612_215909").
If omitted, the current directory is used. The serial is the parent folder
name and <serial>.json is read from the data root (run's grandparent).
Entries are filtered to the run_tag of the given folder.

The report bundles every image as base64 so the .html is fully portable.
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

# Beam images we know how to label, in display order.
BEAM_VIEWS = [
    ("beam_collage.png", "Collage"),
    ("beam_overlay.png", "Overlay"),
    ("beam_raw.png", "Raw"),
    ("beam_B0.png", "B0 (RX)"),
    ("beam_B1.png", "B1 (TX)"),
]

PALETTE = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf",
           "#8c564b", "#e377c2"]


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
        tag = val.get("run_tag") or ""
        if not tag:
            tag = run_tag if run_tag in val.get("image_dir", "") else ""
        if tag == run_tag:
            out.append({"timestamp": entry.get("timestamp", ""), **val})
    return out, data


def b64_img(path: Path):
    if not path.is_file():
        return None
    ext = path.suffix.lower().lstrip(".")
    mime = "jpeg" if ext in ("jpg", "jpeg") else ext
    return f"data:image/{mime};base64," + base64.b64encode(path.read_bytes()).decode("ascii")


# --------------------------------------------------------------------------- #
# Poynt scatter (inline SVG, no matplotlib dependency)
# --------------------------------------------------------------------------- #
def poynt_svg(points, limit=2.0, ring=0.5):
    """points = [(x_deg, y_deg, label, color)] -> SVG string. None -> ''."""
    pts = [p for p in points if p[0] is not None and p[1] is not None]
    if not pts:
        return ""
    # auto-expand axis so all points fit
    m = max(abs(v) for p in pts for v in (p[0], p[1]))
    limit = max(limit, (int(m) + 1))
    W = H = 340
    M = 44
    span = W - 2 * M

    def sx(v):
        return M + (v + limit) / (2 * limit) * span

    def sy(v):
        return M + (limit - v) / (2 * limit) * span

    parts = [f'<svg width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
             f'xmlns="http://www.w3.org/2000/svg" font-family="monospace" font-size="11">']
    parts.append(f'<rect x="{M}" y="{M}" width="{span}" height="{span}" '
                 f'fill="#fafafa" stroke="#bbb"/>')

    t = -int(limit)
    while t <= int(limit):
        gx, gy = sx(t), sy(t)
        parts.append(f'<line x1="{gx:.1f}" y1="{M}" x2="{gx:.1f}" y2="{M+span}" stroke="#e3e3e3"/>')
        parts.append(f'<line x1="{M}" y1="{gy:.1f}" x2="{M+span}" y2="{gy:.1f}" stroke="#e3e3e3"/>')
        if t in (-int(limit), 0, int(limit)):
            parts.append(f'<text x="{gx:.1f}" y="{M+span+16}" text-anchor="middle" fill="#555">{t}</text>')
            parts.append(f'<text x="{M-8}" y="{gy+4:.1f}" text-anchor="end" fill="#555">{t}</text>')
        t += 1

    parts.append(f'<line x1="{sx(0):.1f}" y1="{M}" x2="{sx(0):.1f}" y2="{M+span}" stroke="#888"/>')
    parts.append(f'<line x1="{M}" y1="{sy(0):.1f}" x2="{M+span}" y2="{sy(0):.1f}" stroke="#888"/>')

    r_px = ring / (2 * limit) * span
    parts.append(f'<circle cx="{sx(0):.1f}" cy="{sy(0):.1f}" r="{r_px:.1f}" '
                 f'fill="none" stroke="#2a9d2a" stroke-dasharray="5 4"/>')
    parts.append(f'<text x="{sx(0)+r_px+4:.1f}" y="{sy(0)-r_px:.1f}" fill="#2a9d2a">r={ring}</text>')

    for x, y, label, color in pts:
        cx, cy = sx(x), sy(y)
        parts.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="5" fill="{color}" '
                     f'stroke="#333" stroke-width="0.6"><title>{html.escape(label)} '
                     f'({x:.3f}, {y:.3f})</title></circle>')

    parts.append(f'<text x="{M+span/2:.1f}" y="{H-4}" text-anchor="middle" fill="#333">Poynt.X (deg)</text>')
    parts.append(f'<text x="14" y="{M+span/2:.1f}" text-anchor="middle" fill="#333" '
                 f'transform="rotate(-90 14 {M+span/2:.1f})">Poynt.Y (deg)</text>')
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


def beam_label(b):
    """Clean display label for a beam: 'A1.RX' -> 'RX', 'ch.2' -> 'ch.2'."""
    raw = (b.get("label") or "").strip()
    if "." in raw:
        head, tail = raw.rsplit(".", 1)
        if tail.upper() in ("RX", "TX"):
            return tail.upper()
    return raw


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


def meas_rows(entries):
    rows = []
    for e in entries:
        st = station_of(e)
        for b in sorted_beams(e):
            rows.append([
                esc(st), esc(beam_label(b)),
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


def summary_rows(entries):
    rows = []
    for e in entries:
        rows.append([esc(station_of(e)), esc(e.get("axis", "—")),
                     esc(e.get("status", "—")), fmt(e.get("temperature_c"), 2)])
    return rows


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

    # ---- Poynt scatter ----
    pts = []
    for i, e in enumerate(entries):
        st = station_of(e)
        color = PALETTE[i % len(PALETTE)]
        for b in sorted_beams(e):
            pts.append((b.get("poynt_x_deg"), b.get("poynt_y_deg"),
                        f"{st} {beam_label(b)}", color))
    svg = poynt_svg(pts)

    s.append('<div class="cols">')
    if svg:
        legend = " ".join(
            f'<span class="lg"><i style="background:{PALETTE[i % len(PALETTE)]}"></i>{esc(station_of(e))}</span>'
            for i, e in enumerate(entries))
        s.append(f'<div class="card"><h3>Pointing (Poynt)</h3>{svg}<div class="legend">{legend}</div></div>')

    # ---- station summary card ----
    side = ['<div class="card"><h3>Station summary</h3>']
    side.append(table(["Station", "Axis", "Status", "Temp °C"], summary_rows(entries)))
    side.append("</div>")
    s.append("".join(side))
    s.append("</div>")  # cols

    # ---- measurement table ----
    mrows = meas_rows(entries)
    if mrows:
        s.append("<h3>Measurements</h3>")
        s.append(table(["Station", "Beam", "Poynt.X ° (0±0.5)", "Poynt.Y ° (0±0.5)",
                        "loc X µm", "loc Y µm", "waist X µm", "waist Y µm",
                        "M²x", "M²y", "z0 µm", "EE %", "Peak"], mrows))

    # ---- power table ----
    prows = power_rows(entries)
    if prows:
        s.append("<h3>Drive current</h3>")
        s.append(table(["Station", "Device", "Ch", "Setpoint mA", "Measured mA", "Δ mA"], prows))

    # ---- images per station ----
    s.append('<h3>Images</h3>')
    for e in entries:
        st = station_of(e)
        img_dir = run / st
        files = sorted(p.name for p in img_dir.iterdir()
                       if p.suffix.lower() in IMAGE_EXTS) if img_dir.is_dir() else []
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
.legend{margin-top:6px;font-size:12px;max-width:300px}
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
.muted{color:#999;font-style:italic}
code{background:#f0f0f0;padding:1px 5px;border-radius:4px;font-size:12px}
footer{color:#999;font-size:12px;text-align:center;margin-top:8px}
"""


def build_report(run, serial, run_tag, entries, title):
    body = ['<!doctype html><html lang="en"><head><meta charset="utf-8">',
            '<meta name="viewport" content="width=device-width,initial-scale=1">',
            f"<title>{esc(title)}</title><style>{CSS}</style></head><body>",
            f"<h1>{esc(title)}</h1>",
            f'<div class="meta">{len(entries)} station(s) · generated by make_report_v2.py</div>']
    body.append(render_run(run, serial, run_tag, entries))
    body.append('<footer>- Fabrinet Confidential -</footer></body></html>')
    return "".join(body)


def main():
    ap = argparse.ArgumentParser(description="Build an HTML report for one beam-capture run (new format).")
    ap.add_argument("path", nargs="?", default=".", help="run folder (<serial>/<run_tag>)")
    ap.add_argument("-o", "--output", help="output .html path (default: <run>/report.html)")
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

    out = Path(args.output) if args.output else (run / "report.html")
    title = f"Beam Report — {serial} / {run_tag}"
    print(f"Building report: serial={serial} run_tag={run_tag} stations={len(entries)}")
    for e in entries:
        print(f"  + {station_of(e)}")
    out.write_text(build_report(run, serial, run_tag, entries, title), encoding="utf-8")
    print(f"Wrote {out}")

    if args.open:
        import webbrowser
        webbrowser.open(out.as_uri())
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Upload SIPHOG SAA measurement data from per-PCBA JSON files into the
shared Google Sheet ('SiPhog Build data' tab) under the saa_* columns.

First-time setup:
    pip install gspread google-auth-oauthlib python-dateutil
    Place credentials.json (OAuth Desktop client) next to this script.
    Run once; a browser window authorizes access and caches token.json.

Usage:
    python upload_saa_to_sheet.py              # apply updates
    python upload_saa_to_sheet.py --dry-run    # print intended updates, no writes
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from dateutil import parser as date_parser
from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
import gspread


SHEET_ID = "1JTJBXtzsS-YDFxDPkxo9o1DczHtMozSvclQTYMj_jaU"
TAB_NAME = "test"
SCOPES = ["https://www.googleapis.com/auth/spreadsheets"]

SCRIPT_DIR = Path(__file__).parent
CRED_FILE = SCRIPT_DIR / "credentials.json"
TOKEN_FILE = SCRIPT_DIR / "token.json"

# SAA bench writes per-PCBA JSON test logs here.
DATA_DIR = Path(
    r"C:\Users\Fabrinet\source\repos\acs_solution_saa\acs_solution_saa\bin\Debug\data"
)

STEP_DISPENSE = "Step 4 - Move to Dispense"
STEP_WET = "Step 4.1 - Dispense and Move to UV"
STEP_UV = "Step 5 - Complete UV and Unload"

KEY_COL = "PCBA Number"
REQUIRED_COLS = [
    KEY_COL,
    "saa_date",
    "saa_Sled_current",
    "saa_PD_uA",
    "saa_Target_V",
    "saa_Actual_V",
    "saa_Wet_V",
    "saa_UV_V",
    "saa_completed",
]


def authorize() -> gspread.Client:
    creds: Credentials | None = None
    if TOKEN_FILE.exists():
        creds = Credentials.from_authorized_user_file(str(TOKEN_FILE), SCOPES)
    if not creds or not creds.valid:
        if creds and creds.expired and creds.refresh_token:
            creds.refresh(Request())
        else:
            if not CRED_FILE.exists():
                sys.exit(
                    f"ERROR: {CRED_FILE} not found. See setup notes in file header."
                )
            flow = InstalledAppFlow.from_client_secrets_file(str(CRED_FILE), SCOPES)
            creds = flow.run_local_server(port=0)
        TOKEN_FILE.write_text(creds.to_json())
    return gspread.authorize(creds)


def col_to_a1(col_idx_zero_based: int) -> str:
    n = col_idx_zero_based + 1
    s = ""
    while n:
        n, r = divmod(n - 1, 26)
        s = chr(ord("A") + r) + s
    return s


def extract_saa(json_path: Path):
    """Return dict of SAA measurements pulled from a per-PCBA JSON file, or
    None if none of the source steps (Dispense / Wet / UV) are present.
    """
    try:
        with json_path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"  WARN: could not parse {json_path.name}: {exc}")
        return None

    steps = data.get("steps") or []
    step_by_name = {s.get("step_name"): s for s in steps if s.get("step_name")}

    disp = step_by_name.get(STEP_DISPENSE)
    wet = step_by_name.get(STEP_WET)
    uv = step_by_name.get(STEP_UV)

    if not any([disp, wet, uv]):
        return None

    def _ts(s):
        return s.get("timestamp") if s else None

    timestamps = [_ts(s) for s in (disp, wet, uv) if s and _ts(s)]
    if not timestamps:
        return None
    latest_ts = max(timestamps)

    def _r(step, field):
        if not step:
            return None
        return (step.get("sensor_readings") or {}).get(field)

    return {
        "sled_current": _r(disp, "sled_current_mA"),
        "pd_ua":        _r(disp, "photo_current_uA"),
        "target_v":     _r(disp, "target_sag_pwr_V"),
        "actual_v":     _r(disp, "sag_pwr_V"),
        "wet_v":        _r(wet, "sag_pwr_V"),
        "uv_v":         _r(uv, "sag_pwr_V"),
        "completed":    uv is not None,
        "latest_ts":    latest_ts,
    }


def parse_date(s: str):
    try:
        return date_parser.isoparse(s).date()
    except (ValueError, TypeError):
        return None


def fetch_sheet(ws):
    all_values = ws.get_all_values()
    if not all_values:
        raise RuntimeError("sheet is empty")
    header = all_values[0]
    col_idx = {name: i for i, name in enumerate(header)}
    missing = [c for c in REQUIRED_COLS if c not in col_idx]
    if missing:
        raise RuntimeError(f"sheet missing required columns: {missing}")
    return all_values, col_idx


def _fmt_num(v, ndigits: int = 3):
    return "" if v is None else round(v, ndigits)


def collect_lot_ids(all_values, col_idx):
    """Return sorted list of unique non-empty Lot ID values from the sheet."""
    i = col_idx.get("Lot ID")
    if i is None:
        return []
    lots = set()
    for row in all_values[1:]:
        if i < len(row):
            v = row[i].strip()
            if v:
                lots.add(v)
    return sorted(lots)


def compute_plan(all_values, col_idx, data_dir, scope=None):
    """Scan data_dir and return (counts, row_changes, cell_updates, warnings).

    row_changes is a list of dicts: {pcba, row, old, new, status} where
    status is UPDATE or SKIP_NOT_NEWER. Only UPDATE entries appear in
    cell_updates.

    scope (optional dict) filters which files are considered:
        date_from, date_to : date - inclusive range on JSON latest entry date
        lot_id             : str  - exact match on sheet 'Lot ID' column
    Any missing/None key is ignored.
    """
    scope = scope or {}
    date_from = scope.get("date_from")
    date_to = scope.get("date_to")
    lot_id = scope.get("lot_id")

    pcba_col = col_idx[KEY_COL]
    lot_col = col_idx.get("Lot ID")

    warnings: list[str] = []
    if lot_id and lot_col is None:
        warnings.append("Lot ID filter requested but 'Lot ID' column not in sheet; "
                        "ignoring lot filter")
        lot_id = None

    pcba_to_row: dict[str, int] = {}
    for r, row in enumerate(all_values[1:], start=2):
        if pcba_col >= len(row):
            continue
        key = row[pcba_col].strip()
        if not key:
            continue
        if key in pcba_to_row:
            warnings.append(f"duplicate PCBA Number {key} "
                            f"(rows {pcba_to_row[key]} and {r}); keeping first")
            continue
        pcba_to_row[key] = r

    counts = {
        "scanned": 0,
        "skipped_not_in_sheet": 0,
        "skipped_out_of_scope": 0,
        "skipped_no_data": 0,
        "skipped_not_newer": 0,
        "updated": 0,
    }
    row_changes = []
    cell_updates = []

    for jf in sorted(Path(data_dir).glob("*.json")):
        counts["scanned"] += 1
        stem = jf.stem.strip()
        if stem not in pcba_to_row:
            counts["skipped_not_in_sheet"] += 1
            continue

        row_num = pcba_to_row[stem]
        existing_row = all_values[row_num - 1]

        if lot_id:
            sheet_lot = (existing_row[lot_col] if lot_col < len(existing_row)
                         else "").strip()
            if sheet_lot != lot_id:
                counts["skipped_out_of_scope"] += 1
                continue

        saa = extract_saa(jf)
        if saa is None:
            counts["skipped_no_data"] += 1
            continue

        new_date = parse_date(saa["latest_ts"])
        if new_date is None:
            counts["skipped_no_data"] += 1
            continue

        if date_from is not None and new_date < date_from:
            counts["skipped_out_of_scope"] += 1
            continue
        if date_to is not None and new_date > date_to:
            counts["skipped_out_of_scope"] += 1
            continue

        def _get(col_name):
            i = col_idx[col_name]
            return existing_row[i] if i < len(existing_row) else ""

        existing_date_str = _get("saa_date").strip()
        existing_date = parse_date(existing_date_str) if existing_date_str else None

        new_vals = {
            "saa_date":         f"{new_date.month}/{new_date.day}/{new_date.year}",
            "saa_Sled_current": _fmt_num(saa["sled_current"]),
            "saa_PD_uA":        _fmt_num(saa["pd_ua"]),
            "saa_Target_V":     _fmt_num(saa["target_v"]),
            "saa_Actual_V":     _fmt_num(saa["actual_v"]),
            "saa_Wet_V":        _fmt_num(saa["wet_v"]),
            "saa_UV_V":         _fmt_num(saa["uv_v"]),
            "saa_completed":    "Completed" if saa["completed"] else "Not completed",
        }
        old_vals = {k: _get(k) for k in new_vals}

        if existing_date is not None and new_date <= existing_date:
            counts["skipped_not_newer"] += 1
            row_changes.append({
                "pcba": stem, "row": row_num,
                "old": old_vals, "new": new_vals,
                "status": "SKIP_NOT_NEWER",
            })
            continue

        for col_name, value in new_vals.items():
            a1 = f"{col_to_a1(col_idx[col_name])}{row_num}"
            cell_updates.append({"range": a1, "values": [[value]]})

        counts["updated"] += 1
        row_changes.append({
            "pcba": stem, "row": row_num,
            "old": old_vals, "new": new_vals,
            "status": "UPDATE",
        })

    return counts, row_changes, cell_updates, warnings


def apply_updates(ws, cell_updates):
    if not cell_updates:
        return
    ws.batch_update(cell_updates, value_input_option="USER_ENTERED")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true",
                    help="print intended updates without writing to the sheet")
    ap.add_argument("--data-dir", type=Path, default=DATA_DIR,
                    help=f"folder containing per-PCBA JSON files "
                         f"(default: {DATA_DIR})")
    ap.add_argument("--tab", default=TAB_NAME,
                    help=f"worksheet tab name (default: {TAB_NAME})")
    args = ap.parse_args()

    gc = authorize()
    ws = gc.open_by_key(SHEET_ID).worksheet(args.tab)
    try:
        all_values, col_idx = fetch_sheet(ws)
    except RuntimeError as e:
        sys.exit(f"ERROR: {e}")

    counts, row_changes, cell_updates, warnings = compute_plan(
        all_values, col_idx, args.data_dir)

    for w in warnings:
        print(f"  WARN: {w}")

    print("Summary:")
    for k, v in counts.items():
        print(f"  {k:24s} {v}")

    previews = [c for c in row_changes if c["status"] == "UPDATE"][:5]
    if previews:
        print("\nFirst updates:")
        for c in previews:
            print(f"  PCBA {c['pcba']} (row {c['row']}): {c['new']}")

    if not cell_updates:
        print("\nNo updates to apply.")
        return 0

    if args.dry_run:
        print(f"\n[dry-run] Would send {len(cell_updates)} cell updates "
              f"covering {counts['updated']} rows.")
        return 0

    apply_updates(ws, cell_updates)
    print(f"\nApplied {len(cell_updates)} cell updates "
          f"covering {counts['updated']} rows.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

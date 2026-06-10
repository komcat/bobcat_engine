"""Upload SIPHOG888 UAA measurement data from per-bench JSON files into the
shared Google Sheet.

Two upload modes:
  * Summary  (tab 'test')              - one row per bench, uA-scaled values,
                                         only writes when the JSON is newer.
  * Detail   (tab 'detail_uaa_upload') - raw per-measurement values WITH their
                                         own timestamps; upsert by bench number.

First-time setup:
    pip install gspread google-auth-oauthlib python-dateutil
    Place credentials.json (OAuth Desktop client) next to this script.
    Run once; a browser window authorizes access and caches token.json.

Usage:
    python upload_uaa_to_sheet.py                       # apply summary updates
    python upload_uaa_to_sheet.py --dry-run             # preview, no writes
    python upload_uaa_to_sheet.py --detail              # apply detail upsert
    python upload_uaa_to_sheet.py --detail --dry-run    # preview detail, no writes
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from dateutil import parser as date_parser
from google.auth.exceptions import RefreshError
from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
import gspread


SHEET_ID = "1JTJBXtzsS-YDFxDPkxo9o1DczHtMozSvclQTYMj_jaU"
TAB_NAME = "test"
DETAIL_TAB_NAME = "detail_uaa_upload"
SCOPES = ["https://www.googleapis.com/auth/spreadsheets"]

DATA_DIR = Path(__file__).parent
CRED_FILE = DATA_DIR / "credentials.json"
TOKEN_FILE = DATA_DIR / "token.json"

KEY_STATUS = "$Status"
KEY_DRY = "pre_uv_value"
KEY_UV = "post_uv_value"
KEY_BAKE = "After Bake measurement"
KEY_BAKE_START = "Timestamp_Start_AFBake"
KEY_BAKE_END = "Timestamp_END_AFBake"

VALUE_SCALE = 1_000_000  # A -> uA

REQUIRED_COLS = [
    "Bench Number",
    "uaa_date",
    "uaa_dry",
    "uaa_uv",
    "uaa_bake",
    "uaa_completed",
]

# Header of the 'detail_uaa_upload' tab. The detail uploader writes raw,
# per-measurement values (with their own timestamps) rather than the summary
# row written to the 'test' tab. Values are RAW amps (no uA scaling) and
# timestamps are the exact ISO strings from the JSON.
DETAIL_COLS = [
    "bench number",
    "product_name",
    "$Status",
    "started_timestamp",
    "pre_uv_value",
    "pre_uv_value_timestamp",
    "post_uv_value",
    "Timestamp_Start_AFBake",
    "After Bake measurement",
    "Timestamp_END_AFBake",
]


def authorize() -> gspread.Client:
    creds: Credentials | None = None
    if TOKEN_FILE.exists():
        creds = Credentials.from_authorized_user_file(str(TOKEN_FILE), SCOPES)
    if not creds or not creds.valid:
        refreshed = False
        if creds and creds.expired and creds.refresh_token:
            try:
                creds.refresh(Request())
                refreshed = True
            except RefreshError:
                # Refresh token revoked or expired (Google "Testing" apps expire
                # refresh tokens after 7 days). Fall through to re-auth.
                creds = None
        if not refreshed:
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


def pick_latest(entries, key):
    """Return the entry with the latest timestamp for the given key, or None."""
    best = None
    for e in entries:
        if e.get("key") != key:
            continue
        ts = e.get("timestamp")
        if ts is None:
            continue
        if best is None or ts > best["timestamp"]:
            best = e
    return best


def extract_uaa(json_path: Path):
    """Return dict with dry/uv/bake floats (or None) and the latest timestamp string,
    or None if the file has no relevant entries."""
    try:
        with json_path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"  WARN: could not parse {json_path.name}: {exc}")
        return None

    entries = data.get("entries", [])
    dry = pick_latest(entries, KEY_DRY)
    uv = pick_latest(entries, KEY_UV)
    bake = pick_latest(entries, KEY_BAKE)

    if not any([dry, uv, bake]):
        return None

    latest_ts = max(
        (e["timestamp"] for e in (dry, uv, bake) if e is not None)
    )
    return {
        "dry": dry["value"] * VALUE_SCALE if dry else None,
        "uv": uv["value"] * VALUE_SCALE if uv else None,
        "bake": bake["value"] * VALUE_SCALE if bake else None,
        "latest_ts": latest_ts,
    }


def parse_date(s: str):
    try:
        return date_parser.isoparse(s).date()
    except (ValueError, TypeError):
        return None


def fetch_sheet(ws, required_cols=None):
    required = REQUIRED_COLS if required_cols is None else required_cols
    all_values = ws.get_all_values()
    if not all_values:
        raise RuntimeError("sheet is empty")
    header = all_values[0]
    col_idx = {name: i for i, name in enumerate(header)}
    missing = [c for c in required if c not in col_idx]
    if missing:
        raise RuntimeError(f"sheet missing required columns: {missing}")
    return all_values, col_idx


def _as_int(v):
    return "" if v is None else round(v)


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

    row_changes is a list of dicts: {bench, row, old, new, status} where
    status is UPDATE or SKIP_NOT_NEWER. Only UPDATE entries appear in
    cell_updates.

    scope (optional dict) filters which benches are considered:
        bench_min, bench_max : int    - inclusive range on bench number
        date_from, date_to   : date   - inclusive range on JSON latest entry date
        lot_id               : str    - exact match on sheet 'Lot ID' column
    Any missing/None key is ignored.
    """
    scope = scope or {}
    bench_min = scope.get("bench_min")
    bench_max = scope.get("bench_max")
    date_from = scope.get("date_from")
    date_to = scope.get("date_to")
    lot_id = scope.get("lot_id")

    bench_col = col_idx["Bench Number"]
    lot_col = col_idx.get("Lot ID")

    warnings: list[str] = []
    if lot_id and lot_col is None:
        warnings.append("Lot ID filter requested but 'Lot ID' column not in sheet; "
                        "ignoring lot filter")
        lot_id = None

    bench_to_row: dict[str, int] = {}
    for r, row in enumerate(all_values[1:], start=2):
        if bench_col >= len(row):
            continue
        key = row[bench_col].strip()
        if not key:
            continue
        if key in bench_to_row:
            warnings.append(f"duplicate Bench Number {key} "
                            f"(rows {bench_to_row[key]} and {r}); keeping first")
            continue
        bench_to_row[key] = r

    counts = {
        "scanned": 0,
        "skipped_not_digit": 0,
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
        if not stem.isdigit():
            counts["skipped_not_digit"] += 1
            continue
        if stem not in bench_to_row:
            counts["skipped_not_in_sheet"] += 1
            continue

        bench_int = int(stem)
        if bench_min is not None and bench_int < bench_min:
            counts["skipped_out_of_scope"] += 1
            continue
        if bench_max is not None and bench_int > bench_max:
            counts["skipped_out_of_scope"] += 1
            continue

        row_num = bench_to_row[stem]
        existing_row = all_values[row_num - 1]

        if lot_id:
            sheet_lot = (existing_row[lot_col] if lot_col < len(existing_row)
                         else "").strip()
            if sheet_lot != lot_id:
                counts["skipped_out_of_scope"] += 1
                continue

        uaa = extract_uaa(jf)
        if uaa is None:
            counts["skipped_no_data"] += 1
            continue

        new_date = parse_date(uaa["latest_ts"])
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

        existing_date_str = _get("uaa_date").strip()
        existing_date = parse_date(existing_date_str) if existing_date_str else None

        new_vals = {
            "uaa_date": f"{new_date.month}/{new_date.day}/{new_date.year}",
            "uaa_dry": _as_int(uaa["dry"]),
            "uaa_uv": _as_int(uaa["uv"]),
            "uaa_bake": _as_int(uaa["bake"]),
            "uaa_completed": "Completed" if uaa["uv"] is not None else "Not completed",
        }
        old_vals = {k: _get(k) for k in new_vals}

        if existing_date is not None and new_date <= existing_date:
            counts["skipped_not_newer"] += 1
            row_changes.append({
                "bench": stem, "row": row_num,
                "old": old_vals, "new": new_vals,
                "status": "SKIP_NOT_NEWER",
            })
            continue

        for col_name, value in new_vals.items():
            a1 = f"{col_to_a1(col_idx[col_name])}{row_num}"
            cell_updates.append({"range": a1, "values": [[value]]})

        counts["updated"] += 1
        row_changes.append({
            "bench": stem, "row": row_num,
            "old": old_vals, "new": new_vals,
            "status": "UPDATE",
        })

    return counts, row_changes, cell_updates, warnings


# ---------------------------------------------------------------------------
# Detail tab ('detail_uaa_upload'): raw per-measurement values + timestamps.
# Upsert by bench number (update in place if present, append if new).
# ---------------------------------------------------------------------------

def extract_detail(json_path: Path):
    """Return a dict of raw per-measurement values + their timestamps for the
    'detail_uaa_upload' tab, or None if the file carries no relevant entries.

    Values are RAW amps (no scaling). Timestamps are the exact ISO strings
    from each entry. Where a key appears multiple times (e.g. an original bake
    plus a re-bake), the latest entry wins.
    """
    try:
        with json_path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"  WARN: could not parse {json_path.name}: {exc}")
        return None

    entries = data.get("entries", [])
    status = pick_latest(entries, KEY_STATUS)
    pre = pick_latest(entries, KEY_DRY)
    post = pick_latest(entries, KEY_UV)
    bake = pick_latest(entries, KEY_BAKE)
    bake_start = pick_latest(entries, KEY_BAKE_START)
    bake_end = pick_latest(entries, KEY_BAKE_END)

    relevant = [status, pre, post, bake, bake_start, bake_end]
    if not any(relevant):
        return None

    latest_ts = max(e["timestamp"] for e in relevant if e is not None)
    return {
        "product_name": data.get("product_name", ""),
        "status": status["value"] if status else "",
        "started_ts": status["timestamp"] if status else "",
        "pre": pre["value"] if pre else None,
        "pre_ts": pre["timestamp"] if pre else "",
        "post": post["value"] if post else None,
        "bake_start_ts": bake_start["timestamp"] if bake_start else "",
        "bake": bake["value"] if bake else None,
        "bake_end_ts": bake_end["timestamp"] if bake_end else "",
        "latest_ts": latest_ts,
    }


def _detail_field_values(stem, detail):
    """Map detail dict -> {header name: cell value} for the detail tab."""
    def _num(v):
        return "" if v is None else v
    return {
        "bench number": stem,
        "product_name": detail["product_name"],
        "$Status": detail["status"],
        "started_timestamp": detail["started_ts"],
        "pre_uv_value": _num(detail["pre"]),
        "pre_uv_value_timestamp": detail["pre_ts"],
        "post_uv_value": _num(detail["post"]),
        "Timestamp_Start_AFBake": detail["bake_start_ts"],
        "After Bake measurement": _num(detail["bake"]),
        "Timestamp_END_AFBake": detail["bake_end_ts"],
    }


def _cells_equal(existing_val, new_val):
    """True if the sheet's existing string cell already matches new_val.

    Numbers are compared with tolerance (the sheet round-trips floats as
    formatted strings); everything else by trimmed string equality.
    """
    e = (existing_val or "").strip()
    if isinstance(new_val, (int, float)):
        try:
            nf = float(new_val)
            return abs(float(e) - nf) <= 1e-12 + 1e-9 * abs(nf)
        except (ValueError, TypeError):
            return False
    n = "" if new_val is None else str(new_val)
    return e == n.strip()


def compute_detail_plan(all_values, col_idx, data_dir, scope=None):
    """Scan data_dir and return (counts, row_changes, cell_updates, warnings)
    for the detail tab. Upsert semantics: an in-scope bench already present in
    the sheet is updated in place (status UPDATE), a new bench is appended
    (status APPEND); a present bench whose values already match is skipped
    (status UNCHANGED). cell_updates contains only APPEND/UPDATE rows.

    scope filters which benches are considered:
        bench_min, bench_max : int  - inclusive range on bench number
        date_from, date_to   : date - inclusive range on latest entry date
    (The detail tab has no 'Lot ID' column, so a lot_id filter is ignored.)
    """
    scope = scope or {}
    bench_min = scope.get("bench_min")
    bench_max = scope.get("bench_max")
    date_from = scope.get("date_from")
    date_to = scope.get("date_to")

    header = all_values[0]
    n_cols = len(header)
    bench_col = col_idx["bench number"]

    warnings: list[str] = []
    if scope.get("lot_id"):
        warnings.append("Lot ID filter ignored: detail tab has no 'Lot ID' column")

    bench_to_row: dict[str, int] = {}
    for r, row in enumerate(all_values[1:], start=2):
        if bench_col >= len(row):
            continue
        key = row[bench_col].strip()
        if not key:
            continue
        if key in bench_to_row:
            warnings.append(f"duplicate bench number {key} "
                            f"(rows {bench_to_row[key]} and {r}); keeping first")
            continue
        bench_to_row[key] = r

    next_row = len(all_values) + 1  # first empty row for appends

    counts = {
        "scanned": 0,
        "skipped_not_digit": 0,
        "skipped_out_of_scope": 0,
        "skipped_no_data": 0,
        "unchanged": 0,
        "updated": 0,
        "appended": 0,
    }
    row_changes = []
    cell_updates = []
    last_col_a1 = col_to_a1(n_cols - 1)

    for jf in sorted(Path(data_dir).glob("*.json")):
        counts["scanned"] += 1
        stem = jf.stem.strip()
        if not stem.isdigit():
            counts["skipped_not_digit"] += 1
            continue

        bench_int = int(stem)
        if bench_min is not None and bench_int < bench_min:
            counts["skipped_out_of_scope"] += 1
            continue
        if bench_max is not None and bench_int > bench_max:
            counts["skipped_out_of_scope"] += 1
            continue

        detail = extract_detail(jf)
        if detail is None:
            counts["skipped_no_data"] += 1
            continue

        new_date = parse_date(detail["latest_ts"])
        if new_date is None:
            counts["skipped_no_data"] += 1
            continue
        if date_from is not None and new_date < date_from:
            counts["skipped_out_of_scope"] += 1
            continue
        if date_to is not None and new_date > date_to:
            counts["skipped_out_of_scope"] += 1
            continue

        field_values = _detail_field_values(stem, detail)
        ordered = [field_values.get(h, "") for h in header]

        if stem in bench_to_row:
            row_num = bench_to_row[stem]
            existing = all_values[row_num - 1]
            same = all(
                _cells_equal(existing[i] if i < len(existing) else "", ordered[i])
                for i in range(n_cols)
            )
            if same:
                counts["unchanged"] += 1
                row_changes.append({"bench": stem, "row": row_num,
                                    "status": "UNCHANGED", "new": field_values})
                continue
            status = "UPDATE"
            counts["updated"] += 1
        else:
            row_num = next_row
            next_row += 1
            status = "APPEND"
            counts["appended"] += 1

        cell_updates.append({
            "range": f"A{row_num}:{last_col_a1}{row_num}",
            "values": [ordered],
        })
        row_changes.append({"bench": stem, "row": row_num,
                            "status": status, "new": field_values})

    return counts, row_changes, cell_updates, warnings


def apply_updates(ws, cell_updates, value_input_option="USER_ENTERED"):
    if not cell_updates:
        return
    ws.batch_update(cell_updates, value_input_option=value_input_option)


def _print_plan(counts, row_changes, preview_statuses):
    print("Summary:")
    for k, v in counts.items():
        print(f"  {k:24s} {v}")

    previews = [c for c in row_changes
                if c["status"] in preview_statuses][:5]
    if previews:
        print("\nFirst changes:")
        for c in previews:
            print(f"  bench {c['bench']} (row {c['row']}) "
                  f"[{c['status']}]: {c['new']}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true",
                    help="print intended updates without writing to the sheet")
    ap.add_argument("--data-dir", type=Path, default=DATA_DIR,
                    help=f"folder containing per-bench JSON files "
                         f"(default: {DATA_DIR})")
    ap.add_argument("--detail", action="store_true",
                    help=f"upload raw per-measurement detail to the "
                         f"'{DETAIL_TAB_NAME}' tab (upsert by bench number) "
                         f"instead of the '{TAB_NAME}' summary")
    ap.add_argument("--tab", default=None,
                    help=f"worksheet tab name (default: '{TAB_NAME}', or "
                         f"'{DETAIL_TAB_NAME}' with --detail)")
    args = ap.parse_args()

    tab = args.tab or (DETAIL_TAB_NAME if args.detail else TAB_NAME)

    gc = authorize()
    ws = gc.open_by_key(SHEET_ID).worksheet(tab)
    try:
        if args.detail:
            all_values, col_idx = fetch_sheet(ws, DETAIL_COLS)
        else:
            all_values, col_idx = fetch_sheet(ws)
    except RuntimeError as e:
        sys.exit(f"ERROR: {e}")

    if args.detail:
        counts, row_changes, cell_updates, warnings = compute_detail_plan(
            all_values, col_idx, args.data_dir)
        preview_statuses = ("APPEND", "UPDATE")
        value_input_option = "RAW"
        covered = counts["updated"] + counts["appended"]
    else:
        counts, row_changes, cell_updates, warnings = compute_plan(
            all_values, col_idx, args.data_dir)
        preview_statuses = ("UPDATE",)
        value_input_option = "USER_ENTERED"
        covered = counts["updated"]

    for w in warnings:
        print(f"  WARN: {w}")

    _print_plan(counts, row_changes, preview_statuses)

    if not cell_updates:
        print("\nNo updates to apply.")
        return 0

    if args.dry_run:
        print(f"\n[dry-run] Would send {len(cell_updates)} row updates "
              f"covering {covered} rows on '{tab}'.")
        return 0

    apply_updates(ws, cell_updates, value_input_option=value_input_option)
    print(f"\nApplied {len(cell_updates)} row updates "
          f"covering {covered} rows on '{tab}'.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

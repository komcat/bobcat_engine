"""Upload SIPHOG888 UAA measurement data from per-bench JSON files into the
shared Google Sheet (tab 'test').

First-time setup:
    pip install gspread google-auth-oauthlib python-dateutil

    Preferred - service account (no browser, no token expiry):
        1. GCP Console -> IAM -> Service Accounts -> create -> Keys -> add JSON key.
        2. Save the key as service_account.json next to this script.
        3. Share the sheet with the service account's client_email as Editor.

    Fallback - OAuth desktop client:
        Place credentials.json (OAuth Desktop client) next to this script.
        Run once; a browser window authorizes access and caches token.json.
        Note: while the OAuth app is in "Testing" status Google expires the
        refresh token after 7 days, forcing a browser re-auth. Publish the
        consent screen (or use the service account above) to avoid that.

Two on-disk layouts are supported, and a run may scan several folders at once:

    legacy (bobcat_engine)   <data dir>/<bench>.json
                             entries: {"key", "value", "timestamp", "data_type"}

    firedog                  <data dir>/<bench>/dut.json
                             entries: {"key", "value", "t", "type", "seq"}

A second target, the "uaasaa data 2026" sheet (UAASAA_SHEET_ID, tab 'data'),
gets the same readings in its own narrow layout (Bench, uaa_date, uaa_machine,
uaa_dry_uA, uaa_uv_uA, uaa_bake_date, uaa_bake_uA). Benches with no row there
are appended. It runs after the primary sheet and a failure on it never fails
the run. Pass --no-uaasaa to skip it.

Usage:
    python upload_uaa_to_sheet.py              # apply updates
    python upload_uaa_to_sheet.py --dry-run    # print intended updates, no writes
    python upload_uaa_to_sheet.py --data-dir A --data-dir B   # scan both
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
from datetime import datetime
from pathlib import Path

from dateutil import parser as date_parser
from google.auth.exceptions import RefreshError
from google.auth.transport.requests import Request
from google.oauth2.credentials import Credentials
from google.oauth2.service_account import Credentials as ServiceAccountCredentials
from google_auth_oauthlib.flow import InstalledAppFlow
import gspread


SHEET_ID = "1JTJBXtzsS-YDFxDPkxo9o1DczHtMozSvclQTYMj_jaU"
TAB_NAME = "test"
SCOPES = ["https://www.googleapis.com/auth/spreadsheets"]

DATA_DIR = Path(__file__).parent
CRED_FILE = DATA_DIR / "credentials.json"
TOKEN_FILE = DATA_DIR / "token.json"
SERVICE_ACCOUNT_FILE = DATA_DIR / "service_account.json"

# Set once service_account.json is loaded, so callers can tell the operator
# which address the sheet must be shared with when a 403 comes back.
SERVICE_ACCOUNT_EMAIL: str | None = None

KEY_DRY = "pre_uv_value"
KEY_UV = "post_uv_value"
KEY_BAKE = "After Bake measurement"

VALUE_SCALE = 1_000_000  # A -> uA

REQUIRED_COLS = [
    "Bench Number",
    "uaa_date",
    "uaa_dry",
    "uaa_uv",
    "uaa_bake",
    "uaa_completed",
]

# Filled when the tab has them; a tab without them (e.g. 'test') still uploads
# the required columns, with a warning naming what was skipped.
OPTIONAL_COLS = [
    "uaa_uv_machine",
    "uaa_bake_date",
    "uaa_measure_machine",
]

# Second target: "uaasaa data 2026", one row per bench shared with the SAA
# uploader. UAA owns only these columns there.
UAASAA_SHEET_ID = "136wNsymuzDvab1NZWqVyRY6ZgQGr1e_zLWDJPmzIh-4"
UAASAA_TAB = "data"
UAASAA_BENCH_COL = "Bench"
UAASAA_COLS = [
    "uaa_date",
    "uaa_machine",
    "uaa_dry_uA",
    "uaa_uv_uA",
    "uaa_bake_date",
    "uaa_bake_uA",
]


def authorize() -> gspread.Client:
    """Return an authorized gspread client.

    Prefers service_account.json (unattended, never expires); falls back to the
    OAuth desktop flow with token.json when no service account key is present.
    """
    if SERVICE_ACCOUNT_FILE.exists():
        return gspread.authorize(_service_account_creds())
    return gspread.authorize(_oauth_creds())


def _service_account_creds() -> ServiceAccountCredentials:
    global SERVICE_ACCOUNT_EMAIL
    try:
        creds = ServiceAccountCredentials.from_service_account_file(
            str(SERVICE_ACCOUNT_FILE), scopes=SCOPES
        )
    except (ValueError, KeyError) as e:
        raise RuntimeError(
            f"{SERVICE_ACCOUNT_FILE} is not a valid service account key: {e}\n"
            "Download a fresh JSON key from GCP Console -> IAM -> Service "
            "Accounts -> Keys, or delete the file to fall back to OAuth."
        ) from e
    if SERVICE_ACCOUNT_EMAIL is None:
        # Printed once per process: a 403 from the sheet almost always means
        # this address has not been given Editor access.
        print(f"Auth: service account {creds.service_account_email}")
    SERVICE_ACCOUNT_EMAIL = creds.service_account_email
    return creds


def _oauth_creds() -> Credentials:
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
                    f"ERROR: neither {SERVICE_ACCOUNT_FILE.name} nor "
                    f"{CRED_FILE.name} found in {DATA_DIR}. "
                    "See setup notes in file header."
                )
            flow = InstalledAppFlow.from_client_secrets_file(str(CRED_FILE), SCOPES)
            creds = flow.run_local_server(port=0)
        TOKEN_FILE.write_text(creds.to_json())
    return creds


def col_to_a1(col_idx_zero_based: int) -> str:
    n = col_idx_zero_based + 1
    s = ""
    while n:
        n, r = divmod(n - 1, 26)
        s = chr(ord("A") + r) + s
    return s


def entry_ts(entry) -> str | None:
    """Timestamp of one entry, whichever layout wrote it.

    Legacy bobcat_engine files use "timestamp"; FireDog files use "t". Both are
    ISO-8601 with a trailing Z, so the strings stay comparable either way.
    """
    ts = entry.get("timestamp")
    if ts is None:
        ts = entry.get("t")
    return ts if isinstance(ts, str) else None


def pick_latest(entries, key):
    """Return the entry with the latest timestamp for the given key, or None."""
    best = None
    best_ts = None
    for e in entries:
        if e.get("key") != key:
            continue
        ts = entry_ts(e)
        if ts is None:
            continue
        if best_ts is None or ts > best_ts:
            best, best_ts = e, ts
    return best


def _numeric(entry):
    """Entry value as a float, or None when it is not a plain number.

    FireDog stores structured values (dicts) under bookkeeping keys such as
    $run, so a matched key is no guarantee of a scalar.
    """
    v = entry["value"]
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return None
    return float(v) * VALUE_SCALE


def extract_uaa(json_path: Path):
    """Return dict with dry/uv/bake floats (or None) and the latest timestamp string,
    or None if the file has no relevant entries."""
    try:
        with json_path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"  WARN: could not parse {json_path}: {exc}")
        return None

    entries = data.get("entries", [])
    dry = pick_latest(entries, KEY_DRY)
    uv = pick_latest(entries, KEY_UV)
    bake = pick_latest(entries, KEY_BAKE)

    if not any([dry, uv, bake]):
        return None

    vals = {
        "dry": _numeric(dry) if dry else None,
        "uv": _numeric(uv) if uv else None,
        "bake": _numeric(bake) if bake else None,
    }
    if all(v is None for v in vals.values()):
        # Keys were present but none carried a number - nothing to write, and
        # writing a date with three blanks would poison the "is it newer" check.
        return None

    # The sheet gets one uaa_date (the newest of the three); the kumgit backup
    # database has a date column per measurement, so keep them all. A reading
    # that failed _numeric contributes no timestamp - its value is None too.
    for name, entry in (("dry", dry), ("uv", uv), ("bake", bake)):
        vals[f"{name}_ts"] = (entry_ts(entry)
                              if entry is not None and vals[name] is not None
                              else None)

    vals["latest_ts"] = max(entry_ts(e) for e in (dry, uv, bake) if e is not None)
    return vals


def discover_dut_files(data_dirs):
    """Map bench number -> list of candidate JSON files across all data_dirs.

    Picks up both layouts: <dir>/<bench>.json (legacy) and <dir>/<bench>/dut.json
    (FireDog). Returns (bench_to_paths, scanned, skipped_not_digit); the two
    counters keep the CLI/GUI summary meaning what it did before.
    """
    bench_to_paths: dict[str, list[Path]] = {}
    scanned = 0
    skipped_not_digit = 0

    for d in data_dirs:
        d = Path(d)
        for jf in sorted(d.glob("*.json")):
            scanned += 1
            stem = jf.stem.strip()
            if not stem.isdigit():
                skipped_not_digit += 1
                continue
            bench_to_paths.setdefault(stem, []).append(jf)
        for sub in sorted(p for p in d.glob("*") if p.is_dir()):
            dut = sub / "dut.json"
            if not dut.is_file():
                continue
            scanned += 1
            name = sub.name.strip()
            if not name.isdigit():
                skipped_not_digit += 1
                continue
            bench_to_paths.setdefault(name, []).append(dut)

    return bench_to_paths, scanned, skipped_not_digit


def _best_reading(bench: str, paths, warnings: list[str]):
    """Extract from every candidate file for a bench, keep the newest reading."""
    best = None
    best_path = None
    for p in paths:
        uaa = extract_uaa(p)
        if uaa is None:
            continue
        if best is None or uaa["latest_ts"] > best["latest_ts"]:
            best, best_path = uaa, p
    if best is not None and len(paths) > 1:
        warnings.append(f"bench {bench}: {len(paths)} source files "
                        f"({', '.join(str(p) for p in paths)}); "
                        f"using newest ({best_path})")
    return best


def parse_date(s: str):
    """Parse a JSON entry timestamp (strict ISO-8601)."""
    try:
        return date_parser.isoparse(s).date()
    except (ValueError, TypeError):
        return None


def parse_sheet_date(s: str):
    """Parse a date as displayed in the sheet.

    The sheet holds US-style M/D/YYYY (that is what this script writes; see
    new_vals below), which isoparse rejects - using it here would make every
    existing date unreadable and re-flag every row as an update forever.
    dayfirst=False keeps 8/4/2026 as August 4th.
    """
    try:
        return date_parser.parse(s, dayfirst=False).date()
    except (ValueError, TypeError, OverflowError):
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


def _as_int(v):
    return "" if v is None else round(v)


def _sheet_date(d) -> str:
    """date -> US-style M/D/YYYY, the format parse_sheet_date reads back."""
    return f"{d.month}/{d.day}/{d.year}"


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


def as_dir_list(data_dir) -> list[Path]:
    """Accept a single folder or an iterable of folders; return a de-duplicated list."""
    if isinstance(data_dir, (str, Path)):
        data_dir = [data_dir]
    out: list[Path] = []
    for d in data_dir:
        p = Path(d)
        if p not in out:
            out.append(p)
    return out


def _bench_in_scope(bench_int: int, scope: dict) -> bool:
    """Inclusive bench_min/bench_max check; a missing bound is no bound."""
    lo = scope.get("bench_min")
    hi = scope.get("bench_max")
    return not ((lo is not None and bench_int < lo)
                or (hi is not None and bench_int > hi))


def _date_in_scope(d, scope: dict) -> bool:
    """Inclusive date_from/date_to check on a reading's (UTC) date."""
    lo = scope.get("date_from")
    hi = scope.get("date_to")
    return not ((lo is not None and d < lo) or (hi is not None and d > hi))


def compute_plan(all_values, col_idx, data_dir, scope=None, machine_name=""):
    """Scan the data folder(s) and return (counts, row_changes, cell_updates, warnings).

    data_dir is one folder or an iterable of folders; each is scanned for both
    the legacy <bench>.json and the FireDog <bench>/dut.json layouts.

    machine_name is this station's name (pc_name()) - the data files carry no
    machine name of their own. It goes into uaa_uv_machine when the file has a
    pre_uv_value, and into uaa_measure_machine when it has a post_uv_value;
    each is left blank otherwise, like the reading it describes.

    uaa_bake_date is the date of the post_uv_value timestamp ("t").

    row_changes is a list of dicts: {bench, row, old, new, status} where
    status is UPDATE, FILL or SKIP_NOT_NEWER. UPDATE rows are written whole;
    FILL rows are not newer but have empty OPTIONAL_COLS cells this file can
    vouch for (see _fill_cells), and carry {fill, fill_mismatch} naming the
    cells written and the ones left blank. Only those cells appear in
    cell_updates.

    scope (optional dict) filters which benches are considered:
        bench_min, bench_max : int    - inclusive range on bench number
        date_from, date_to   : date   - inclusive range on JSON latest entry date
        lot_id               : str    - exact match on sheet 'Lot ID' column
    Any missing/None key is ignored.
    """
    scope = scope or {}
    lot_id = scope.get("lot_id")

    bench_col = col_idx["Bench Number"]
    lot_col = col_idx.get("Lot ID")

    warnings: list[str] = []
    if lot_id and lot_col is None:
        warnings.append("Lot ID filter requested but 'Lot ID' column not in sheet; "
                        "ignoring lot filter")
        lot_id = None

    missing_optional = [c for c in OPTIONAL_COLS if c not in col_idx]
    if missing_optional:
        warnings.append(f"sheet has no {', '.join(missing_optional)} column(s); "
                        "not writing them")

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

    bench_to_paths, scanned, not_digit = discover_dut_files(as_dir_list(data_dir))

    counts = {
        "scanned": scanned,
        "skipped_not_digit": not_digit,
        "skipped_not_in_sheet": 0,
        "skipped_out_of_scope": 0,
        "skipped_no_data": 0,
        "skipped_not_newer": 0,
        "updated": 0,
        "filled": 0,
        "fill_mismatch": 0,
    }
    row_changes = []
    cell_updates = []

    for stem in sorted(bench_to_paths, key=int):
        if stem not in bench_to_row:
            counts["skipped_not_in_sheet"] += 1
            continue

        if not _bench_in_scope(int(stem), scope):
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

        uaa = _best_reading(stem, bench_to_paths[stem], warnings)
        if uaa is None:
            counts["skipped_no_data"] += 1
            continue

        new_date = parse_date(uaa["latest_ts"])
        if new_date is None:
            counts["skipped_no_data"] += 1
            continue

        if not _date_in_scope(new_date, scope):
            counts["skipped_out_of_scope"] += 1
            continue

        def _get(col_name):
            i = col_idx[col_name]
            return existing_row[i] if i < len(existing_row) else ""

        existing_date_str = _get("uaa_date").strip()
        existing_date = (parse_sheet_date(existing_date_str)
                         if existing_date_str else None)
        if existing_date_str and existing_date is None:
            warnings.append(f"bench {stem} (row {row_num}): unparseable uaa_date "
                            f"{existing_date_str!r}; treating as no date")

        bake_date = parse_date(uaa["uv_ts"]) if uaa["uv_ts"] else None
        # Same left-to-right order as the sheet, so the GUI's change table
        # reads like the row it describes.
        new_vals = {
            "uaa_date": _sheet_date(new_date),
            "uaa_dry": _as_int(uaa["dry"]),
            "uaa_uv": _as_int(uaa["uv"]),
            "uaa_uv_machine": machine_name if uaa["dry"] is not None else "",
            "uaa_bake_date": _sheet_date(bake_date) if bake_date else "",
            "uaa_bake": _as_int(uaa["bake"]),
            "uaa_measure_machine": machine_name if uaa["uv"] is not None else "",
            "uaa_completed": "Completed" if uaa["uv"] is not None else "Not completed",
        }
        for col_name in missing_optional:
            del new_vals[col_name]
        old_vals = {k: _get(k) for k in new_vals}

        if existing_date is not None and new_date <= existing_date:
            # Not newer, so the readings stay. Rows uploaded before the
            # OPTIONAL_COLS existed would otherwise keep those cells blank
            # forever, since they never count as newer again.
            to_set, mismatch = _fill_cells(old_vals, new_vals)
            for col_name, value in to_set.items():
                a1 = f"{col_to_a1(col_idx[col_name])}{row_num}"
                cell_updates.append({"range": a1, "values": [[value]]})
            counts["filled" if to_set else "skipped_not_newer"] += 1
            if mismatch:
                counts["fill_mismatch"] += 1
            row_changes.append({
                "bench": stem, "row": row_num,
                "old": old_vals, "new": new_vals,
                "status": "FILL" if to_set else "SKIP_NOT_NEWER",
                "fill": to_set, "fill_mismatch": mismatch,
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
            # Source material for the kumgit backup database, which wants the
            # raw readings plus sheet-side context rather than the formatted
            # cell values. Carried here so both targets describe one plan.
            "uaa": uaa,
            "lot_id": (existing_row[lot_col].strip()
                       if lot_col is not None and lot_col < len(existing_row)
                       else ""),
            "note": _get("uaa_note").strip() if "uaa_note" in col_idx else "",
        })

    return counts, row_changes, cell_updates, warnings


def _same_reading(sheet_value: str, file_value) -> bool:
    """True when a sheet cell shows the integer the uploader would write.

    Off-by-one counts as the same reading: older uploads truncated where this
    one rounds, so 1615 in the sheet and 1616 from the file are one value.
    """
    if file_value == "":
        return False
    try:
        return abs(round(float(sheet_value.replace(",", ""))) - file_value) <= 1
    except ValueError:
        return False


def _fill_cells(old, new):
    """Empty OPTIONAL_COLS cells of a not-newer row that this file can fill.

    A cell is filled only when the sheet still shows the reading this PC's
    file holds - otherwise the row was written from somewhere else (another
    station, a hand edit) and naming this machine, or this file's date, would
    be a guess:

        uaa_uv_machine       sheet uaa_dry (pre_uv_value) matches the file
        uaa_measure_machine  sheet uaa_uv (post_uv_value) matches the file
        uaa_bake_date        sheet uaa_uv matches the file (date comes from
                             post_uv_value's timestamp)

    Non-empty cells are never touched. Returns (to_set, mismatch): the cells
    to write, and the empty ones left blank because the reading differs.
    """
    has_uv = new["uaa_uv"] != ""
    evidence = {
        "uaa_uv_machine": ["uaa_dry"] if new["uaa_dry"] != "" else [],
        "uaa_measure_machine": ["uaa_uv"] if has_uv else [],
        "uaa_bake_date": ["uaa_uv"] if has_uv else [],
    }
    to_set = {}
    mismatch = []
    for col_name, keys in evidence.items():
        if col_name not in new or old[col_name].strip() or new[col_name] == "":
            continue
        if keys and all(_same_reading(old[k], new[k]) for k in keys):
            to_set[col_name] = new[col_name]
        else:
            mismatch.append(col_name)
    return to_set, mismatch


def apply_updates(ws, cell_updates):
    if not cell_updates:
        return
    ws.batch_update(cell_updates, value_input_option="USER_ENTERED")


# --- Second target: "uaasaa data 2026" -------------------------------------
#
# One row per bench, shared with the SAA uploader, keyed on 'Bench'. Dates are
# written as local "M/D/YYYY H:MM" to match the SAA columns beside them; the
# files carry UTC ("...Z") timestamps, so they are converted first.


def _local_dt(ts: str | None) -> datetime | None:
    """File timestamp -> naive local datetime, truncated to the minute."""
    if not ts:
        return None
    try:
        dt = date_parser.isoparse(ts)
    except (ValueError, TypeError):
        return None
    if dt.tzinfo is not None:
        dt = dt.astimezone().replace(tzinfo=None)
    return dt.replace(second=0, microsecond=0)


def _sheet_datetime(dt: datetime) -> str:
    """datetime -> "M/D/YYYY H:MM", the format the SAA columns use."""
    return f"{dt.month}/{dt.day}/{dt.year} {dt.hour}:{dt.minute:02d}"


def parse_sheet_datetime(s: str) -> datetime | None:
    """Read a sheet date or date+time back (US order), minute resolution."""
    try:
        dt = date_parser.parse(s, dayfirst=False)
    except (ValueError, TypeError, OverflowError):
        return None
    return dt.replace(tzinfo=None, second=0, microsecond=0)


def _is_newer(new_dt: datetime, sheet_str: str, sheet_dt: datetime) -> bool:
    """True when the file reading is newer than what the sheet shows.

    A cell formatted to show the date only reads back as midnight; comparing
    a timed reading against that would re-upload the bench on every run, so
    such a cell is compared by date.
    """
    if ":" not in sheet_str:
        return new_dt.date() > sheet_dt.date()
    return new_dt > sheet_dt


def _is_same_reading(new_dt: datetime, sheet_str: str, sheet_dt: datetime) -> bool:
    """True when the sheet's uaa_date is this file's reading (same minute, or
    same day for a date-only cell) - the condition for filling blank cells."""
    if ":" not in sheet_str:
        return new_dt.date() == sheet_dt.date()
    return new_dt == sheet_dt


def _cell(row, i: int) -> str:
    """Stripped cell text; get_all_values drops trailing blanks per row."""
    return row[i].strip() if i < len(row) else ""


def _bench_key(s: str) -> str:
    """Normalise a bench number for matching: '2941', '2941.0', '2,941' and
    '02941' are one bench. Non-numeric text is kept as-is (stripped)."""
    s = s.strip().replace(",", "")
    try:
        f = float(s)
    except ValueError:
        return s
    return str(int(f)) if f.is_integer() else s


def fetch_uaasaa(ws):
    """Return (all_values, col_idx) for the uaasaa tab, checking its header."""
    all_values = ws.get_all_values()
    if not all_values:
        raise RuntimeError(f"uaasaa tab '{ws.title}' is empty")
    col_idx: dict[str, int] = {}
    for i, name in enumerate(all_values[0]):
        col_idx.setdefault(name.strip(), i)
    missing = [c for c in [UAASAA_BENCH_COL, *UAASAA_COLS] if c not in col_idx]
    if missing:
        raise RuntimeError(f"uaasaa tab '{ws.title}' missing columns: {missing}")
    return all_values, col_idx


def benches_with_lot(all_values, col_idx, lot_id: str) -> set[str] | None:
    """Benches whose primary-sheet 'Lot ID' is lot_id.

    The uaasaa sheet has no Lot ID column, so a lot filter is resolved against
    the primary sheet. None means "no lot column there", i.e. no filter -
    the same thing compute_plan does in that case.
    """
    lot_col = col_idx.get("Lot ID")
    bench_col = col_idx.get("Bench Number")
    if lot_col is None or bench_col is None:
        return None
    out = set()
    for row in all_values[1:]:
        if max(lot_col, bench_col) < len(row) and row[lot_col].strip() == lot_id:
            bench = _bench_key(row[bench_col])
            if bench:
                out.add(bench)
    return out


def compute_uaasaa_plan(all_values, col_idx, data_dir, scope=None,
                        machine_name="", lot_benches=None):
    """Plan the uaasaa upload.

    Returns (counts, changes, cell_updates, new_rows, warnings).

    Rows are matched on Bench (see _bench_key), so a row the SAA uploader
    created first is reused and its SAA columns are never touched:
      UPDATE  file reading newer than the row's uaa_date (or none yet) -
              all of UAASAA_COLS are written
      FILL    same reading as uaa_date - only the UAA cells still blank
      APPEND  no row for the bench - new row with only Bench + UAA filled

    scope takes the same bench/date keys as compute_plan; lot_benches (from
    benches_with_lot) stands in for its lot filter.
    """
    scope = scope or {}
    bench_col = col_idx[UAASAA_BENCH_COL]
    date_col = col_idx["uaa_date"]
    width = len(all_values[0])
    warnings: list[str] = []

    bench_to_row: dict[str, int] = {}
    for r, row in enumerate(all_values[1:], start=2):
        key = _bench_key(_cell(row, bench_col))
        if not key:
            continue
        if key in bench_to_row:
            warnings.append(f"duplicate Bench {key} (rows {bench_to_row[key]} "
                            f"and {r}); keeping first")
            continue
        bench_to_row[key] = r

    bench_to_paths, scanned, _ = discover_dut_files(as_dir_list(data_dir))

    counts = {
        "scanned": scanned,
        "skipped_out_of_scope": 0,
        "skipped_no_data": 0,
        "skipped_not_newer": 0,
        "updated": 0,
        "filled": 0,
        "appended": 0,
    }
    changes = []
    cell_updates = []
    new_rows = []

    for stem in sorted(bench_to_paths, key=int):
        if (not _bench_in_scope(int(stem), scope)
                or (lot_benches is not None
                    and _bench_key(stem) not in lot_benches)):
            counts["skipped_out_of_scope"] += 1
            continue

        # The primary plan already reported multi-file benches; don't repeat.
        uaa = _best_reading(stem, bench_to_paths[stem], [])
        new_date = parse_date(uaa["latest_ts"]) if uaa else None
        latest = _local_dt(uaa["latest_ts"]) if uaa else None
        if new_date is None or latest is None:
            counts["skipped_no_data"] += 1
            continue
        if not _date_in_scope(new_date, scope):
            counts["skipped_out_of_scope"] += 1
            continue

        bake_dt = _local_dt(uaa["bake_ts"])
        new_vals = {
            "uaa_date": _sheet_datetime(latest),
            "uaa_machine": machine_name,
            "uaa_dry_uA": _as_int(uaa["dry"]),
            "uaa_uv_uA": _as_int(uaa["uv"]),
            "uaa_bake_date": _sheet_datetime(bake_dt) if bake_dt else "",
            "uaa_bake_uA": _as_int(uaa["bake"]),
        }

        # A row the SAA uploader already made is matched by bench number and
        # kept: only its UAA columns are written, never a second row.
        row_num = bench_to_row.get(_bench_key(stem))
        if row_num is None:
            row = [""] * width
            row[bench_col] = stem
            for col_name, value in new_vals.items():
                row[col_idx[col_name]] = value
            while row and row[-1] == "":
                row.pop()
            new_rows.append(row)
            counts["appended"] += 1
            changes.append({"bench": stem, "row": None, "new": new_vals,
                            "status": "APPEND"})
            continue

        existing_row = all_values[row_num - 1]
        old_str = _cell(existing_row, date_col)
        old_dt = parse_sheet_datetime(old_str) if old_str else None
        if old_str and old_dt is None:
            warnings.append(f"bench {stem} (row {row_num}): unparseable "
                            f"uaa_date {old_str!r}; treating as no date")
        if old_dt is not None and not _is_newer(latest, old_str, old_dt):
            # Same reading already in the row: fill only the UAA cells still
            # blank there. A sheet date *newer* than the file means another
            # station wrote this row - mixing this file's values in would be
            # a guess, so that row is left alone.
            fill = {}
            if _is_same_reading(latest, old_str, old_dt):
                fill = {k: v for k, v in new_vals.items()
                        if v != "" and not _cell(existing_row, col_idx[k])}
            if not fill:
                counts["skipped_not_newer"] += 1
                continue
            for col_name, value in fill.items():
                a1 = f"{col_to_a1(col_idx[col_name])}{row_num}"
                cell_updates.append({"range": a1, "values": [[value]]})
            counts["filled"] += 1
            changes.append({"bench": stem, "row": row_num, "new": fill,
                            "status": "FILL"})
            continue

        for col_name, value in new_vals.items():
            a1 = f"{col_to_a1(col_idx[col_name])}{row_num}"
            cell_updates.append({"range": a1, "values": [[value]]})
        counts["updated"] += 1
        changes.append({"bench": stem, "row": row_num, "new": new_vals,
                        "status": "UPDATE"})

    return counts, changes, cell_updates, new_rows, warnings


def apply_uaasaa(ws, cell_updates, new_rows) -> None:
    apply_updates(ws, cell_updates)
    if new_rows:
        # INSERT_ROWS grows the grid as needed and never overwrites a row
        # the SAA uploader may have written meanwhile.
        ws.append_rows(new_rows, value_input_option="USER_ENTERED",
                       insert_data_option="INSERT_ROWS", table_range="A1")


def run_uaasaa(gc, data_dirs, scope=None, machine_name="", apply=False,
               lot_benches=None, tab=UAASAA_TAB) -> dict:
    """Plan (and with apply=True, write) the uaasaa sheet; return a summary."""
    ws = gc.open_by_key(UAASAA_SHEET_ID).worksheet(tab)
    all_values, col_idx = fetch_uaasaa(ws)
    counts, changes, cells, new_rows, warnings = compute_uaasaa_plan(
        all_values, col_idx, data_dirs, scope=scope,
        machine_name=machine_name, lot_benches=lot_benches)
    if apply:
        apply_uaasaa(ws, cells, new_rows)
    return {"tab": tab, "counts": counts, "changes": changes,
            "cells": len(cells), "applied": apply, "warnings": warnings}


def describe_uaasaa(summary: dict) -> str:
    """One-line result for logs, shared by the CLI and the tray app."""
    c = summary["counts"]
    head = f"uaasaa sheet '{summary['tab']}'"
    if summary["applied"]:
        return (f"{head}: {c['updated']} updated, {c['filled']} filled, "
                f"{c['appended']} appended, {c['skipped_not_newer']} not newer")
    return (f"{head}: would update {c['updated']}, fill {c['filled']}, "
            f"append {c['appended']} ({c['skipped_not_newer']} not newer)")


WATCHDOG_CONFIG = DATA_DIR / "watchdog_config.json"


def backup_settings() -> dict:
    """kumgit.com settings, read from the tray app's config file.

    The CLI and the tray app write to the same two databases, so they read the
    backup credentials from one place rather than growing a second set of
    flags to keep in sync.
    """
    try:
        with WATCHDOG_CONFIG.open("r", encoding="utf-8") as f:
            cfg = json.load(f)
    except (OSError, json.JSONDecodeError):
        return {}
    return cfg if isinstance(cfg, dict) else {}


def pc_name() -> str:
    """This PC's Windows name (Settings -> System -> About), e.g. DESKTOP-6U3ITU5.

    Used for the sheet's machine columns: unlike the Machine ID setting it is
    already unique per PC and needs no configuring on a new station.
    """
    return socket.gethostname()


def mirror_to_kumgit(row_changes) -> None:
    """Best-effort push of an applied plan to the backup database."""
    import kumgit_api

    cfg = backup_settings()
    machine_id = (cfg.get("kumgit_machine_id") or "").strip() \
        or kumgit_api.DEFAULT_MACHINE_ID
    records = kumgit_api.build_records(
        row_changes, machine_id, cfg.get("kumgit_operator_id") or "")
    api_key = (cfg.get("kumgit_api_key") or "").strip()
    if not api_key:
        if records:
            kumgit_api.save_pending(kumgit_api.merge_pending(
                kumgit_api.load_pending(), records))
            print(f"kumgit backup: {len(records)} record(s) held - no API key "
                  f"in {WATCHDOG_CONFIG.name}")
        return
    if not records and not kumgit_api.load_pending():
        return
    client = kumgit_api.KumgitClient(
        api_key, (cfg.get("kumgit_url") or "").strip() or kumgit_api.DEFAULT_URL)
    summary = client.push(records)
    print(f"kumgit backup ({machine_id}): {summary['saved']} saved, "
          f"{summary['pending']} pending, {summary['dropped']} rejected")
    for err in summary["errors"][:5]:
        print(f"  WARN: kumgit backup: {err}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true",
                    help="print intended updates without writing to the sheet")
    ap.add_argument("--data-dir", type=Path, action="append", dest="data_dirs",
                    metavar="DIR",
                    help=f"folder holding per-bench data, either <bench>.json or "
                         f"<bench>/dut.json; repeat to scan several "
                         f"(default: {DATA_DIR})")
    ap.add_argument("--tab", default=TAB_NAME,
                    help=f"worksheet tab name (default: {TAB_NAME})")
    ap.add_argument("--no-kumgit", dest="kumgit", action="store_false",
                    help="skip the kumgit.com backup database (it is mirrored "
                         "by default on a real run, using the credentials in "
                         "watchdog_config.json)")
    ap.add_argument("--no-uaasaa", dest="uaasaa", action="store_false",
                    help="skip the 'uaasaa data 2026' sheet (updated by "
                         "default, after the primary sheet)")
    ap.set_defaults(kumgit=True, uaasaa=True)
    args = ap.parse_args()
    data_dirs = args.data_dirs or [DATA_DIR]

    missing = [d for d in data_dirs if not d.is_dir()]
    if missing:
        sys.exit("ERROR: data folder(s) not found: "
                 + ", ".join(str(d) for d in missing))

    gc = authorize()
    ws = gc.open_by_key(SHEET_ID).worksheet(args.tab)
    try:
        all_values, col_idx = fetch_sheet(ws)
    except RuntimeError as e:
        sys.exit(f"ERROR: {e}")

    counts, row_changes, cell_updates, warnings = compute_plan(
        all_values, col_idx, data_dirs, machine_name=pc_name())

    for w in warnings:
        print(f"  WARN: {w}")

    for d in data_dirs:
        print(f"Data folder: {d}")
    print("Summary:")
    for k, v in counts.items():
        print(f"  {k:24s} {v}")

    previews = [c for c in row_changes if c["status"] == "UPDATE"][:5]
    if previews:
        print("\nFirst updates:")
        for c in previews:
            print(f"  bench {c['bench']} (row {c['row']}): {c['new']}")

    fills = [c for c in row_changes if c["status"] == "FILL"][:5]
    if fills:
        print("\nFirst empty-cell fills:")
        for c in fills:
            print(f"  bench {c['bench']} (row {c['row']}): {c['fill']}")
    if counts["fill_mismatch"]:
        print(f"\n{counts['fill_mismatch']} row(s) keep some empty cells: the "
              "sheet reading differs from this PC's file")

    rows = (f"{counts['updated']} updated + {counts['filled']} filled rows")
    if not cell_updates:
        print("\nNo updates to apply.")
    elif args.dry_run:
        print(f"\n[dry-run] Would send {len(cell_updates)} cell updates "
              f"covering {rows}.")
    else:
        apply_updates(ws, cell_updates)
        print(f"\nApplied {len(cell_updates)} cell updates covering {rows}.")
        if args.kumgit:
            mirror_to_kumgit(row_changes)

    if args.uaasaa:
        _uaasaa_cli(gc, data_dirs, apply=not args.dry_run)
    return 0


def _uaasaa_cli(gc, data_dirs, apply: bool) -> None:
    """Second-sheet pass for the CLI; a failure here is a warning only."""
    print(f"\n--- {UAASAA_TAB} tab of uaasaa data 2026 ---")
    try:
        summary = run_uaasaa(gc, data_dirs, machine_name=pc_name(), apply=apply)
    except Exception as e:  # noqa: BLE001 - must not fail the primary run
        print(f"  WARN: uaasaa sheet failed: {e}")
        return
    for w in summary["warnings"]:
        print(f"  WARN: {w}")
    for k, v in summary["counts"].items():
        print(f"  {k:24s} {v}")
    for c in summary["changes"][:5]:
        where = f"row {c['row']}" if c["row"] else "new row"
        print(f"  {c['status']} bench {c['bench']} ({where}): {c['new']}")
    print(describe_uaasaa(summary))


if __name__ == "__main__":
    sys.exit(main())

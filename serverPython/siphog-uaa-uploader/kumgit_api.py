"""Backup-database client for kumgit.com (UAA test records).

API reference: https://kumgit.com/Projects/UaaSaaData/api-docs.html

This is a *secondary* target for the SIPHOG UAA uploader. The Google Sheet
stays the primary database; every row the uploader writes there is mirrored
here so the data survives a sheet mishap.

Two things make that mirror trustworthy:

* Records are built from the same ``row_changes`` that
  ``upload_uaa_to_sheet.compute_plan`` produces, so the two targets can never
  disagree about what was uploaded.
* Anything the server refuses for a transient reason is written to a spool
  file and retried on the next run. Without it a single network blip would
  lose a bench forever: once the sheet row is updated the bench stops being
  "newer" and never appears in a later plan again.

The module deliberately avoids importing gspread/Google auth so it can be
exercised on its own.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

from dateutil import parser as date_parser

try:
    import requests
except ImportError:  # pragma: no cover - requests ships with gspread
    requests = None


DEFAULT_URL = "https://kumgit.com/Projects/UaaSaaData/api.php"

# Every UAA station uploads to the same database, so each record has to say
# which machine produced it. This is the value for *this* PC; other stations
# run the same app with UAA2, UAA3, ... set in Settings.
DEFAULT_MACHINE_ID = "UAA1"

# The API accepts up to 500 records per call, all-or-nothing.
BATCH_LIMIT = 500

HTTP_TIMEOUT = 30.0
RETRY_ON_500 = 2
RETRY_BACKOFF_S = 2.0

PENDING_FILE = Path(__file__).parent / "kumgit_pending.json"
MAX_PENDING = 5000

# Server rejected the payload itself; retrying the same bytes cannot help.
_PERMANENT = (400, 404, 405)


# --------------------------------------------------------------------------
# record building
# --------------------------------------------------------------------------

def _date_only(ts: str | None) -> str:
    """ISO-8601 entry timestamp -> 'YYYY-MM-DD', or '' when unusable."""
    if not ts:
        return ""
    try:
        return date_parser.isoparse(ts).date().isoformat()
    except (ValueError, TypeError):
        return ""


def _number(v):
    """uA reading -> a JSON number the API accepts, or '' when absent."""
    if v is None:
        return ""
    return round(float(v), 3)


def build_records(row_changes, machine_id: str = DEFAULT_MACHINE_ID,
                  operator_id: str = "") -> list[dict]:
    """Turn compute_plan row_changes into uaa_save records.

    Only UPDATE rows are mirrored - those are the ones actually written to
    the sheet. Rows carrying no extracted reading (older plans, or a change
    dict built before this field existed) are skipped rather than sent as
    empty records.
    """
    records = []
    for change in row_changes:
        if change.get("status") != "UPDATE":
            continue
        uaa = change.get("uaa")
        if not uaa:
            continue
        records.append({
            "serial_number": str(change["bench"])[:50],
            "lot_id": str(change.get("lot_id") or "")[:60],
            "machine_id": str(machine_id or DEFAULT_MACHINE_ID)[:50],
            "operator_id": str(operator_id or "")[:50],
            "dry_ua": _number(uaa.get("dry")),
            "dry_date": _date_only(uaa.get("dry_ts")),
            "uv_ua": _number(uaa.get("uv")),
            "uv_date": _date_only(uaa.get("uv_ts")),
            "bake_ua": _number(uaa.get("bake")),
            "bake_date": _date_only(uaa.get("bake_ts")),
            # The sheet tracks completion, not pass/fail - there is no PASS /
            # FAIL source column for UAA, so this stays blank rather than
            # inventing a verdict.
            "result": "",
            "note": str(change.get("note") or "")[:255],
        })
    return records


# --------------------------------------------------------------------------
# spool of records still owed to the server
# --------------------------------------------------------------------------

def load_pending() -> list[dict]:
    if not PENDING_FILE.exists():
        return []
    try:
        data = json.loads(PENDING_FILE.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return []
    return data if isinstance(data, list) else []


def save_pending(records: list[dict]) -> None:
    try:
        if not records:
            PENDING_FILE.unlink(missing_ok=True)
            return
        PENDING_FILE.write_text(json.dumps(records, indent=1), encoding="utf-8")
    except OSError:
        pass


def merge_pending(older: list[dict], newer: list[dict]) -> list[dict]:
    """Combine two record lists, newest wins per serial, oldest dropped at cap.

    uaa_save is an upsert keyed on serial_number, so holding two records for
    the same bench would just send the stale one first for nothing.
    """
    by_serial: dict[str, dict] = {}
    for rec in list(older) + list(newer):
        serial = str(rec.get("serial_number") or "")
        if serial:
            by_serial[serial] = rec
    merged = list(by_serial.values())
    if len(merged) > MAX_PENDING:
        merged = merged[-MAX_PENDING:]
    return merged


# --------------------------------------------------------------------------
# client
# --------------------------------------------------------------------------

class KumgitError(Exception):
    """Transport or server failure; str() is safe to show an operator."""


class KumgitClient:
    def __init__(self, api_key: str, url: str = DEFAULT_URL,
                 timeout: float = HTTP_TIMEOUT):
        self.api_key = (api_key or "").strip()
        self.url = (url or DEFAULT_URL).strip() or DEFAULT_URL
        self.timeout = timeout

    # -- low level ---------------------------------------------------------

    def _post(self, action: str, payload: dict | None):
        if requests is None:
            raise KumgitError("the 'requests' package is not installed")
        if not self.api_key:
            raise KumgitError("no API key configured")
        try:
            return requests.post(
                self.url, params={"action": action},
                headers={"X-API-Key": self.api_key,
                         "Content-Type": "application/json"},
                json=payload if payload is not None else {},
                timeout=self.timeout)
        except requests.RequestException as exc:
            raise KumgitError(f"{type(exc).__name__}: {exc}") from exc

    @staticmethod
    def _describe(resp) -> str:
        """Server message when there is one, else the bare status line."""
        try:
            body = resp.json()
        except ValueError:
            body = None
        if isinstance(body, dict):
            msg = body.get("message") or body.get("error")
            if msg:
                return f"HTTP {resp.status_code}: {msg}"
        text = (resp.text or "").strip().replace("\n", " ")
        return f"HTTP {resp.status_code}" + (f": {text[:160]}" if text else "")

    # -- public ------------------------------------------------------------

    def ping(self) -> str:
        """Verify key and server. Returns a human-readable status line."""
        resp = self._post("ping", None)
        if resp.ok:
            return "reachable, key accepted"
        if resp.status_code == 401:
            raise KumgitError("API key rejected (401)")
        if resp.status_code == 403:
            raise KumgitError("API key lacks UAA permission (403)")
        raise KumgitError(self._describe(resp))

    def _send_batch(self, records: list[dict]):
        """Send one batch. Returns (still_owed, dropped, error_messages).

        A 500 is retried; the docs mark it safe. A 400 means the batch holds
        something the server will never accept - since saves are
        all-or-nothing, the batch is bisected to find the offending record(s)
        instead of letting one bad bench block every other one behind it.
        Records isolated that way are *dropped*: retrying them forever would
        wedge the spool.
        """
        last_error = ""
        resp = None
        for attempt in range(RETRY_ON_500 + 1):
            resp = self._post("uaa_save", {"records": records})
            if resp.ok:
                return [], [], []
            last_error = self._describe(resp)
            if resp.status_code == 500 and attempt < RETRY_ON_500:
                time.sleep(RETRY_BACKOFF_S * (attempt + 1))
                continue
            break

        if resp.status_code in _PERMANENT:
            if len(records) == 1:
                serial = records[0].get("serial_number", "?")
                return [], records, [f"bench {serial} rejected - {last_error}"]
            half = len(records) // 2
            owed_a, drop_a, errs_a = self._send_batch(records[:half])
            owed_b, drop_b, errs_b = self._send_batch(records[half:])
            return owed_a + owed_b, drop_a + drop_b, errs_a + errs_b

        # 401/403/500/anything else: the records are still owed.
        return records, [], [last_error]

    def push(self, records: list[dict], include_pending: bool = True) -> dict:
        """Upload records, retrying whatever the spool still owes.

        Returns {"saved", "pending", "dropped", "errors"}. Never raises for a
        server-side problem - the caller treats the backup as best-effort and
        the spool keeps the data for the next attempt.
        """
        owed = merge_pending(load_pending() if include_pending else [], records)
        summary = {"saved": 0, "pending": 0, "dropped": 0, "errors": []}
        if not owed:
            return summary

        still_owed: list[dict] = []
        dropped: list[dict] = []
        errors: list[str] = []
        for start in range(0, len(owed), BATCH_LIMIT):
            batch = owed[start:start + BATCH_LIMIT]
            try:
                batch_owed, batch_dropped, batch_errors = self._send_batch(batch)
            except KumgitError as exc:
                # A dead connection or a bad key fails every later batch the
                # same way; keep the rest and stop hammering it.
                still_owed.extend(owed[start:])
                errors.append(str(exc))
                break
            still_owed.extend(batch_owed)
            dropped.extend(batch_dropped)
            errors.extend(batch_errors)

        summary["saved"] = len(owed) - len(still_owed) - len(dropped)
        summary["pending"] = len(still_owed)
        summary["dropped"] = len(dropped)
        summary["errors"] = errors
        save_pending(still_owed)
        return summary

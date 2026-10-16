# SIPHOG UAA Uploader — Setup

Pushes UAA measurement data from per-bench JSON files into the shared Google
Sheet. Two entry points, same core logic:

| File | What it is |
|------|-----------|
| `upload_uaa_to_sheet.py` | Core logic + one-shot CLI |
| `siphog_watchdog.py` | PyQt6 tray app that runs the upload on a timer |
| `siphog_watchdog.bat` | Launch the tray app (no console) |
| `siphog_watchdog_console.bat` | Launch with a console so tracebacks are visible |

## 1. Install dependencies

```
pip install gspread google-auth-oauthlib python-dateutil PyQt6
```

`PyQt6` is only needed for the tray watchdog; the CLI runs without it.

## 2. Authentication

The script picks its auth method automatically: if `service_account.json`
exists next to the script it uses that, otherwise it falls back to the OAuth
desktop flow with `credentials.json` + `token.json`.

**Use the service account.** The OAuth path is kept only as a fallback — while
the OAuth app sits in "Testing" status Google expires the refresh token after
7 days, which forces a browser re-auth. On an unattended bench that means the
uploader silently stops working every week, and under the tray app the browser
prompt would block a worker thread.

### Creating the service account (one time)

1. Open <https://console.cloud.google.com> and select project
   **bamboo-almanac-441203-n3** in the top bar.
2. **APIs & Services → Enabled APIs & services** — confirm *Google Sheets API*
   is listed. If not, **+ ENABLE APIS AND SERVICES** → search for it → Enable.
3. **IAM & Admin → Service Accounts → + CREATE SERVICE ACCOUNT**
   - Name: `siphog-uaa-uploader`
   - **CREATE AND CONTINUE**
   - *Grant this service account access to project*: **skip it** → CONTINUE.
     It needs no project roles; its only access comes from step 5.
   - *Grant users access*: skip → **DONE**
4. Click the new account → **KEYS** tab → **ADD KEY** → *Create new key* →
   **JSON** → **CREATE**. The file downloads once and Google keeps no copy.
   Rename it to `service_account.json` and put it next to the script.
5. Copy the `client_email` from that JSON — it looks like
   `siphog-uaa-uploader@bamboo-almanac-441203-n3.iam.gserviceaccount.com`.
   Open the sheet → **Share** → paste the address → **Editor** → untick
   *Notify people* (it is not a mailbox) → **Share**.

Step 5 is the one people skip. Without it auth succeeds and the sheet read
fails with `403 PERMISSION_DENIED`.

Once a service-account run works, delete `token.json` — it is no longer used.

`service_account.json`, `credentials.json`, `token.json`, and
`watchdog_config.json` are all gitignored. Treat the key file like a password.

### If GCP blocks you

- *"Service account key creation is disabled"* — the org policy
  `iam.disableServiceAccountKeyCreation` is set. Either get the project
  exempted, or publish the OAuth consent screen to **Production**
  (APIs & Services → OAuth consent screen → **Publish App**), which removes
  the 7-day refresh-token expiry on the fallback path.
- *Sharing rejects the address* — the sheet lives in a Workspace that blocks
  sharing outside the domain. Same two options.

## 3. Running it

CLI, dry run (prints the plan, writes nothing):

```powershell
python upload_uaa_to_sheet.py --dry-run `
  --data-dir "C:\...\out\build\x64-release\data\SIPHOG888" `
  --tab database_anl
```

`--data-dir` may be repeated to scan several locations in one pass — e.g. the
bobcat_engine folder plus the FireDog one:

```powershell
python upload_uaa_to_sheet.py --dry-run `
  --data-dir "C:\...\komcat\bobcat_engine\out\build\x64-release\data\SIPHOG888" `
  --data-dir "C:\...\FireDog\out\build\x64-debug\products\siphog888\data" `
  --tab database_anl
```

A folder that does not exist aborts the run rather than silently narrowing it.

Drop `--dry-run` to apply. Defaults are `--data-dir` = the script's own folder
and `--tab` = `test`, so **always pass both** for a real run — with the
defaults it scans its own config files and reports
`skipped_not_digit`, which looks like a failure but is just an empty scan.

The CLI also ignores the `scope` filters in `watchdog_config.json`, so its
counts are broader than a watchdog run over the same folder.

The tray app reads everything from `watchdog_config.json` instead.

Only one tray app can run at a time. Launching `siphog_watchdog.py` (or the
`.bat`) again while a copy is already running does not start a second
watchdog — the new process hands off to the running one, which pops its
window to the front, and then exits with code 0. The lock is a named local
socket, `SIPHOG-UAA-Watchdog-single-instance-<user>` (per Windows
account, so two logged-in users each get their own copy). It is released
automatically if the app crashes or the machine reboots.

## 4. Data and sheet contract

Input: one file per bench. Two on-disk layouts are read, and both may be
present in the same run:

| Writer | Path | Bench number from |
|--------|------|-------------------|
| bobcat_engine (legacy) | `<data dir>/<bench>.json` | filename stem |
| FireDog | `<data dir>/<bench>/dut.json` | the containing folder name |

Either way the bench part must be all digits — anything else counts as
`skipped_not_digit`. The two shapes differ only in the timestamp field name
(`timestamp` vs `t`); both are ISO-8601 with a trailing `Z`:

```json
{ "entries": [ { "key": "...", "value": 1.23e-6, "timestamp": "2026-07-14T09:00:00Z" } ] }
{ "entries": [ { "key": "...", "value": 1.23e-6, "t":         "2026-07-14T09:00:00Z" } ] }
```

FireDog also writes bookkeeping entries whose `value` is an object (`$run`,
`$open`, `$close`); non-numeric values are ignored rather than uploaded.

If the same bench turns up in more than one folder or layout, the file with the
newest reading wins and the run logs a warning naming both.

Three keys are read, latest timestamp per key wins:

| Key | Sheet column |
|-----|-------------|
| `pre_uv_value` | `uaa_dry` |
| `post_uv_value` | `uaa_uv` |
| `After Bake measurement` | `uaa_bake` |

Values are scaled A → µA (×1,000,000) and rounded to an integer.

The sheet tab must have a header row containing at least: `Bench Number`,
`uaa_date`, `uaa_dry`, `uaa_uv`, `uaa_bake`, `uaa_completed`. `Lot ID` is
optional and enables lot-scoped filtering. A missing column aborts the run
with `sheet missing required columns`.

Three more columns are filled when the tab has them, and skipped with a
warning when it does not:

| Column | Value |
|--------|-------|
| `uaa_uv_machine` | this PC's Windows name (e.g. `DESKTOP-6U3ITU5`) when a `pre_uv_value` exists, else blank |
| `uaa_bake_date` | date of the `post_uv_value` entry's own timestamp (`t`), `M/D/YYYY`; blank when there is no post-UV reading |
| `uaa_measure_machine` | this PC's Windows name when a `post_uv_value` exists, else blank |

The data files carry no machine name, so both machine columns use the PC name
from **Settings → System → About** (rename it there to change what the sheet
shows). It needs no configuring, unlike the backup database's **Machine ID**
(`UAA1`, below), which is a separate, hand-set value.

Rows uploaded before these columns existed are never "newer" again, so every
run (CLI and tray app) also fills their empty cells, counted as `filled`. It
only writes cells that are empty, and only when the sheet still shows the
reading this PC's file holds (`uaa_dry` for the UV machine, `uaa_uv` for the
measure machine and the bake date; off by one counts as the same, since older
uploads truncated instead of rounding). A row whose sheet values came from
elsewhere keeps its blanks and is counted as `fill_mismatch`, instead of being
labelled with this machine. Rows whose bench has no data file on this PC can
only be filled by the station that holds it.

Write rules:
- A row is updated only when the JSON's latest timestamp is **newer** than the
  existing `uaa_date`; otherwise it counts as `skipped_not_newer`.
- `uaa_date` is written US-style `M/D/YYYY`. Timestamps *in the JSON* are
  parsed as strict ISO-8601; dates already *in the sheet* are parsed with
  `dayfirst=False`. Do not "fix" this to `isoparse` — it would make every
  existing date unreadable and re-flag every row forever.
- `uaa_completed` is `Completed` when a `post_uv_value` exists, else
  `Not completed`.
- Duplicate bench numbers in the sheet: the first row wins, with a warning.

## 5. Watchdog config

`watchdog_config.json` (gitignored, written by the GUI):

| Field | Meaning |
|-------|---------|
| `watchdog_enabled` / `autostart` | run on a timer / start the timer at launch |
| `interval_minutes` | period between runs |
| `tab_name` | worksheet tab to write |
| `data_folders` | list of per-bench data folders, all scanned in one pass; the first is what *Open Data Folder* opens (empty list = script directory). Edited from the **Data folders** panel on the main window (Add / Remove / Up / Down, double-click to edit a path). Old `data_folder` / `data_folder_2` entries are folded into it on load |
| `scope` | bench-number range, date range, and lot filters |
| `run_detail_on_schedule` + `detail_scope` | second scoped pass per cycle |
| `kumgit_enabled` | mirror uploads to the kumgit.com backup database (default `true`) |
| `kumgit_machine_id` | which UAA station this PC is — `UAA1` here, unique per machine |
| `kumgit_api_key` | `X-API-Key` value, UAA scope |
| `kumgit_operator_id` | optional, stamped on every record |
| `kumgit_url` | endpoint override (empty = the documented one) |

## 6. Backup database (kumgit.com)

The Google Sheet stays the primary database. Every row the uploader writes
there is also mirrored to <https://kumgit.com/Projects/UaaSaaData/api.php>
(`?action=uaa_save`, [API docs][api]), so the data survives a sheet mishap.
This is **on by default** — toggle it under **Backup → Mirror to kumgit.com**,
in the tray menu, or in Settings.

[api]: https://kumgit.com/Projects/UaaSaaData/api-docs.html

### Machine ID

Several UAA stations share one database, so every record carries a
`machine_id`. **This PC is `UAA1`.** Any other station running this app must
be given its own (`UAA2`, `UAA3`, …) in **File → Settings → Machine ID**, or
its rows will be filed under the wrong machine. The active ID is shown in the
top bar next to the run status, so it is visible without opening Settings.

### API key

Paste the UAA-scoped key into **File → Settings → API key**; it is stored in
`watchdog_config.json` (gitignored) and sent as the `X-API-Key` header.
**Backup → Test Connection** pings the server and reports whether the key was
accepted. Without a key the app still runs — uploads are held (see below)
rather than lost — and logs a warning on every run.

### What gets sent, and when

Only rows the uploader actually wrote to the sheet (status `UPDATE`) are
mirrored, in batches of up to 500. A dry run never sends anything. The push
runs on its own thread *after* the sheet result is reported, so a slow or
dead backup server never delays or fails the sheet upload — backup problems
appear as warnings in the log.

Field mapping, per bench:

| API field | Source |
|-----------|--------|
| `serial_number` | bench number (the JSON file stem, and `serial_number` inside it) |
| `lot_id` | sheet `Lot ID` column |
| `machine_id` | `kumgit_machine_id` (`UAA1`) |
| `operator_id` | `kumgit_operator_id`, blank unless set |
| `dry_ua` / `uv_ua` / `bake_ua` | `pre_uv_value` / `post_uv_value` / `After Bake measurement`, in µA |
| `dry_date` / `uv_date` / `bake_date` | each measurement's **own** entry timestamp, not the single `uaa_date` the sheet gets |
| `result` | always blank — the sheet tracks completion, not PASS/FAIL, and there is no UAA verdict column to read |
| `note` | sheet `uaa_note` column |

### Nothing is lost when the server is down

Once a sheet row is updated the bench is no longer "newer", so it never
appears in a later plan — a failed backup push would lose it permanently.
Records the server could not take are therefore written to
`kumgit_pending.json` (gitignored) and retried at the start of every later
push, including on the next scheduled run. **Backup → Send Pending Records
Now** retries them on demand, and the count is logged at startup.

Records the server rejects with a `400` are a different case: retrying cannot
help. Because a save is all-or-nothing, the batch is bisected to find the
offending bench, the rest still land, and only the offender is dropped — with
its bench number and the server's message in the log.

### CLI

A real `upload_uaa_to_sheet.py` run mirrors to kumgit too, reading the same
credentials from `watchdog_config.json`. Pass `--no-kumgit` to skip it.

## 7. Troubleshooting

| Symptom | Cause |
|---------|-------|
| `403 PERMISSION_DENIED` | sheet not shared with the service account (step 5) |
| Browser opens on startup | no `service_account.json`; running the OAuth fallback |
| `... is not a valid service account key` | wrong JSON downloaded — use the key from the **KEYS** tab, not the OAuth client secret |
| `neither service_account.json nor credentials.json found` | no auth file present |
| `scanned N / skipped_not_digit N` | wrong `--data-dir`, or the bench part is not all digits (`<digits>.json` / `<digits>/dut.json`) |
| `sheet missing required columns` | wrong `--tab`, or the header row was edited |
| `kumgit backup: API key rejected (401)` | wrong/empty key in Settings |
| `kumgit backup: API key lacks UAA permission (403)` | key is SAA-scoped; UAA needs a `UAA-…` key |
| `kumgit backup: N pending` | server unreachable; records are in `kumgit_pending.json` and retry automatically |
| `kumgit backup: bench NNNN rejected` | that record breaks a field rule (see API docs); every other bench still went through |
| Two machines' rows collide in kumgit | both PCs left `Machine ID` at `UAA1` — give each its own |
| Tray app fails silently | relaunch with `siphog_watchdog_console.bat` to see the traceback — `siphog_watchdog.bat` uses `pythonw.exe` and has no console, so the `Auth: service account ...` line and any print output are invisible |

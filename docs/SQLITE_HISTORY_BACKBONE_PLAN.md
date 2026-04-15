# SQLite History Backbone - Design Plan

**Status:** Deferred. Revisit after machine event log improvements.
**Scope:** New general-purpose SQLite module as a reusable history/telemetry backbone for bobcat_engine.
**Coexists with:** `DeviceUnderTest` JSON (`data/{product}/{serial}.json`), `SafetyEventLog`, `AccuracyTrainerLogger` (separate, domain-specific).

---

## Goals

Fill four real gaps in current persistence:

1. **Manual-adjust position capture** - Operator tweaks a device position, saves to a named position. Each save recorded with process UID + recipe program UID + serial number. Over a year this becomes a drift dataset ("lensgrip position drifted 5um/month").
2. **Measurement capture** - Checkbox on measurement commands to persist `(name, value, unit, timestamp, program context)`.
3. **Run / operation history** - Queryable log of recipe runs and per-step execution, cross-run queries (e.g. "average cycle time last week").
4. **Image capture index** - Central catalog mapping UIDs to locally saved image files with run/step/serial context.

**Non-goals:**
- Not a replacement for `DeviceUnderTest` JSON (stays as the per-serial pass/fail record).
- Not a replacement for `SafetyEventLog` JSONL (stays as the tamper-evident audit trail).
- Not an MES / historian integration. Leave hooks open but do not build.

---

## Existing Infrastructure (do not duplicate)

| Subsystem | Role | File |
|---|---|---|
| `AccuracyTrainerLogger` | Domain-specific SQLite (hex corner deltas). Ad-hoc, keep separate. | `include/AppUI/AccuracyTrainer/AccuracyTrainerLogger.cpp` |
| `SafetyEventLog` | JSONL audit trail, 10 MB rotation, ring buffer | `include/safety/SafetyEventLog.cpp` |
| `SequenceContext` | In-memory execution values, `ExportToDataStore()`, `SaveToFile()` | `include/sequence/SequenceContext.h` |
| `DeviceUnderTest` | Per-serial JSON results, consumed by `tools/data_explorer` | `include/dut/DeviceUnderTest.h` |
| `LogManager` | spdlog text logs, daily rotated | `include/logger/LogManager.h` |
| `DataStore` | In-memory key-value channel cache (not history) | `include/data/DataStore.h` |
| `sqlite3` (bundled) | Available, already linked for accuracy trainer | `external/sqlite3/` |

---

## Key Decisions

| Decision | Choice | Rationale |
|---|---|---|
| DB file | Single `logs/bobcat.db` | One history backbone, multiple tables |
| Keep accuracy_trainer_log.db separate? | Yes | Already shipping, different lifecycle |
| Threading | Async batched writer | Mandatory for position snapshots and motion-thread callers |
| Station ID | `station_id TEXT NOT NULL` on every table | Forward-compat; default `"bobcat-01"` from `applicationSettings.ini` |
| Retention | 1 year, prune on startup | Configurable; walks `image_captures` to delete image files before row delete |
| Consumer tier | Tier 1 (local viewer) + Tier 2 (CSV export for offline analysis) | SPC / drift analysis by process engineers |
| Replace DUT JSON? | No, coexist | DUT JSON keeps pass/fail role; DB holds cross-run history |
| UUID format | UUID v4 (36-char) | Matches `AccuracyTrainerStore` |
| SQLite pragmas | WAL + `synchronous=NORMAL` | Telemetry, not financial data |
| Qt dependency in core | None | Core is plain C++17 so CLI/headless tools can link |

---

## Architecture (Two-Layer)

### Layer 1 - Core (generic, written once)

`include/database/` (new directory)

```
SqliteDb.h/.cpp        RAII sqlite3 handle, prepared-statement cache,
                       Execute/Query/Transaction helpers, no exceptions,
                       returns ErrorCode

AsyncDbWriter.h/.cpp   Batching writer. Owns the only sqlite3* handle.
                       Enqueue(WriteJob), Flush(), Stop().
                       One writer thread. Bounded queue, drop-oldest policy.

DbMigrator.h/.cpp      schema_version table + registered migration steps.

IRecorder.h            Interface every domain-specific recorder implements:
                         virtual const char* TableName() = 0;
                         virtual int CurrentSchemaVersion() = 0;
                         virtual void ApplyMigrations(SqliteDb&) = 0;

DatabaseManager.h/.cpp Owns SqliteDb + AsyncDbWriter + registered recorders.
                       Lifetime tied to ApplicationContext.
                       Handles startup prune (1-year retention) and VACUUM.
```

**Core never knows** what a position save or measurement is. It only knows: open DB, run migrations, batch writes, flush, prune.

### Layer 2 - Recorders (one per use case)

`include/database/recorders/`

```
RunRecorder            runs table
PositionSaveRecorder   position_saves table
MeasurementRecorder    measurements table
ImageCaptureRecorder   image_captures table        (Phase 2)
ContextSnapshotRecorder                            (Phase 3, if needed)
```

Each recorder is ~150-250 lines and does three things only:
1. Declares schema (CREATE TABLE + indexes + migrations).
2. Exposes typed `Record(struct)` that turns the struct into a `WriteJob` and pushes to the shared `AsyncDbWriter`.
3. Exposes typed `Query*()` methods for the viewer/exporter (synchronous, direct through `SqliteDb`; WAL mode means reads do not block the writer).

**Adding a new use case** = write one recorder file, register in `DatabaseManager`. No changes to core.

---

## Async Batched Writer - Mechanics

### The problem with synchronous writes

Every per-row SQLite commit does a `fsync` (~1-15 ms). If the caller is a motion command handler in the middle of a move, that latency becomes motion jitter. A single recipe run can easily produce 80+ rows, stacking latency on threads that should not wait.

### The batching insight

SQLite is 50-100x faster when many rows share a single transaction:

```
BEGIN
INSERT row 1
INSERT row 2
...
INSERT row 100
COMMIT    <- one fsync for all 100 rows
```

100 individual commits: ~100-500 ms. 100 in one commit: ~2-5 ms.

### The async pattern

```
Caller (any thread)                         Writer thread
  recorder.Record(row) ---push to queue---> 1. Pop N rows from queue
  returns immediately                       2. BEGIN TXN
                                            3. Insert all N
Caller (motion thread)                      4. COMMIT
  recorder.Record(row) ---push to queue---> 5. Wait for next row or timer
  returns immediately
```

Caller never touches `sqlite3*`. Only the writer thread does.

### Parameters

| Parameter | Value | Controls |
|---|---|---|
| `MAX_QUEUE` | 10 000 | Memory ceiling if writer falls behind |
| `BATCH_MAX` | 500 | Max rows per transaction |
| Flush timer | 250 ms | Max latency before a partial batch is written |
| Backpressure | Drop-oldest + WARNING to `SafetyEventLog` | Newest data is most interesting |

### Shutdown / E-stop

- Destructor sets `m_stop`, notifies CV, joins thread.
- Writer loop detects stop, drains remaining rows, commits, exits.
- `Flush()` hooked into `Application::performEStopCascade()` so even a crash-triggering E-stop leaves the DB consistent.
- `Flush()` also called at end of every recipe run - blocks the calling thread briefly but nobody cares at that point.

### Crash safety trade-off

Async batching can lose up to the last unflushed batch (~500 rows or 250 ms). Acceptable for telemetry/history. For irreplaceable data, keep it in `DeviceUnderTest` JSON (already file-flushed) or call `Flush()` explicitly after writing.

### Skeleton

```cpp
class AsyncDbWriter {
    std::thread              m_writerThread;
    std::mutex               m_queueMutex;
    std::condition_variable  m_cv;
    std::deque<WriteJob>     m_queue;
    std::atomic<bool>        m_stop{false};
    sqlite3*                 m_db;

public:
    void Enqueue(WriteJob job) {
        std::lock_guard lk(m_queueMutex);
        if (m_queue.size() >= MAX_QUEUE) {
            m_queue.pop_front();   // drop oldest
        }
        m_queue.push_back(std::move(job));
        m_cv.notify_one();
    }

    void WriterLoop() {
        for (int iter = 0; iter < INT_MAX && !m_stop; ++iter) {   // bounded per MISRA
            std::vector<WriteJob> batch;
            {
                std::unique_lock lk(m_queueMutex);
                m_cv.wait_for(lk, std::chrono::milliseconds(250),
                              [&]{ return !m_queue.empty() || m_stop; });
                while (!m_queue.empty() && batch.size() < BATCH_MAX) {
                    batch.push_back(std::move(m_queue.front()));
                    m_queue.pop_front();
                }
            }
            if (batch.empty()) continue;

            sqlite3_exec(m_db, "BEGIN", 0, 0, 0);
            for (auto& job : batch) job.Execute(m_db);
            sqlite3_exec(m_db, "COMMIT", 0, 0, 0);
        }
        FlushRemaining();
    }
};
```

---

## Recipe / Program UID Backfill (Pre-requisite)

**Current state (verified):**

| Entity | Has UID? | File |
|---|---|---|
| `ProcessInstance.uniqueId` | Yes - 7-char alnum, persisted as `"id"`, preserved on load | `include/recipe/RecipeManager.h:12-26` |
| `Recipe` | NO | `include/recipe/RecipeManager.h:31-39` |
| `Program` | NO | `include/ProgramManager/ProgramManager.h:108-120` |
| `RecipeReference` | NO | `include/ProgramManager/ProgramManager.h:59` |

Recipes and Programs are identified by filename only. Without stable UIDs, cross-year statistics fragment every time someone renames or edits.

### The fix - mirror `AccuracyTrainerStore` v1->v2 pattern

1. Add `std::string recipe_uid;` to `Recipe` (`RecipeManager.h:31`).
2. Add `std::string program_uid;` to `Program` (`ProgramManager.h:108`).
3. Add `std::string recipe_ref_uid;` to `RecipeReference` (`ProgramManager.h:59`).
4. In `RecipeManager::loadFromJson()` (~`RecipeManager.cpp:618`) and `ProgramManager::loadFromJson()` (~`ProgramManager.cpp:1142, 1174`):
   ```cpp
   std::string uid = j.value("recipe_uid", "");
   if (uid.empty()) {
       uid = GenerateUUID();   // v4
       // mark dirty so next save writes it back
   }
   ```
5. Save paths always write the field.
6. **Do not** rewrite old files behind the user's back. Keep the generated UID in memory, persist to DB immediately (DB becomes the UID registry), write back to JSON on the next save.

### Rename/copy edge case

If an operator copies `recipeA.json` to `recipeB.json`, both share the same UID. Handle on load:

```
ReconcileRecipeUid(filePath, uidFromFile) -> finalUid
  if UID exists in DB under a different path: regenerate
  else: use uidFromFile (or generate if empty)
```

Strict reconciliation keeps statistics meaningful.

### Order

UID backfill ships as its own commit **before** the DB module, so `git bisect` stays clean.

---

## Phase 1 Schema (Minimal Core)

```sql
CREATE TABLE schema_version (
    module TEXT PRIMARY KEY,
    version INTEGER NOT NULL
);

CREATE TABLE runs (
    run_id TEXT PRIMARY KEY,              -- UUID v4
    start_ts TEXT NOT NULL,               -- ISO-8601 UTC
    end_ts TEXT,
    station_id TEXT NOT NULL,
    product TEXT,
    serial_number TEXT,
    recipe_uid TEXT,
    recipe_name TEXT,
    program_uid TEXT,
    program_name TEXT,
    status TEXT,                          -- running / ok / fail / aborted
    operator_name TEXT,
    app_version TEXT
);
CREATE INDEX idx_runs_serial ON runs(serial_number);
CREATE INDEX idx_runs_start  ON runs(start_ts);

CREATE TABLE position_saves (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT NOT NULL,
    station_id TEXT NOT NULL,
    position_name TEXT NOT NULL,          -- operator-chosen label, free-form
    device TEXT NOT NULL,
    x REAL, y REAL, z REAL, u REAL, v REAL, w REAL,
    process_uid TEXT,
    recipe_uid TEXT,
    program_uid TEXT,
    run_id TEXT,
    serial_number TEXT,
    operator_name TEXT
);
CREATE INDEX idx_pos_name   ON position_saves(position_name);
CREATE INDEX idx_pos_run    ON position_saves(run_id);
CREATE INDEX idx_pos_ts     ON position_saves(ts);

CREATE TABLE measurements (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ts TEXT NOT NULL,
    station_id TEXT NOT NULL,
    measurement_name TEXT NOT NULL,       -- operator-chosen label
    value REAL NOT NULL,
    unit TEXT,
    device TEXT,
    process_uid TEXT,
    recipe_uid TEXT,
    program_uid TEXT,
    run_id TEXT,
    serial_number TEXT,
    -- SPC-friendly nullable hooks for future quality integration:
    nominal REAL,
    usl REAL,
    lsl REAL
);
CREATE INDEX idx_meas_name ON measurements(measurement_name);
CREATE INDEX idx_meas_run  ON measurements(run_id);
CREATE INDEX idx_meas_ts   ON measurements(ts);
```

Later phases add: `image_captures`, `position_snapshots` (periodic), `context_snapshots`, `run_steps`.

---

## Integration Points

- `ApplicationContext` - add `DatabaseManager* dbManager` raw pointer, owned by `Application.cpp`.
- `SequenceEngine` - call `dbManager->runs->StartRun(...)` / `EndRun(...)` on sequence lifecycle hooks.
- Manual adjust process step - call `dbManager->positions->Record(...)` on save-to-named-position.
- Measurement command handlers (PSU/SMU/TEC/Vision) - if checkbox set, call `dbManager->measurements->Record(...)`.
- `Application::performEStopCascade()` - call `dbManager->Flush()`.
- `applicationSettings.ini` - new `[Database]` section: `station_id`, `retention_days`, `enabled`.
- Core is plain C++17 - no Qt include in `include/database/`.

---

## Tier 2 CSV Export Commitment

Ship `tools/db_export/` CLI in Phase 1c or early Phase 2:
- Args: date range + table + product filter
- Output: CSV to stdout or file
- ~200 lines, one file
- Saves the process engineer from writing SQL for drift studies

---

## Reusability Rules

- **No string-keyed blob table** for "generic events". Every use case defines typed columns. `SafetyEventLog` already fills the generic-event role.
- **No Qt in core.** Qt viewer UI lives in `tools/` next to `data_explorer`, linking against the core.
- **Every recorder table includes `station_id`** as a convention documented in `IRecorder.h`.
- **New use case = new recorder file + register in `DatabaseManager`**. Nothing else.

---

## Recommended Order of Work

1. **Pre-req**: Recipe/Program UID backfill (self-contained commit).
2. **Phase 1a**: Core - `SqliteDb`, `AsyncDbWriter`, `DbMigrator`, `IRecorder`, `DatabaseManager`. No recorders. Verify open/migrate/enqueue/flush lifecycle.
3. **Phase 1b**: `RunRecorder` + `PositionSaveRecorder` + `MeasurementRecorder`. Hook into `SequenceEngine`, manual-adjust step, measurement handlers.
4. **Phase 1c**: `tools/db_export/` CSV CLI.
5. **Phase 1d**: Small Qt viewer in `tools/` (table view + date filter + export button).
6. **Phase 2+**: `ImageCaptureRecorder`, then `PositionSnapshotRecorder` (periodic/checkpoint), then `ContextSnapshotRecorder` if still needed after 1-3.

---

## Open Items to Revisit

- Machine event log improvements come first - this module is deferred until that work lands.
- Whether `ContextSnapshotRecorder` is actually needed after Phase 1 ships (may be redundant with run/position/measurement recorders).
- Viewer UI scope - start with read-only table + filter, expand based on user feedback.
- Whether to expose an optional sync-on-record path for critical rows, or rely on explicit `Flush()` at run end.

---

## Consumer Tiers Reference

| Tier | Example | Implication for this design |
|---|---|---|
| 1 - Local viewer | `tools/data_explorer` style | Primary target. SQLite direct read. |
| 2 - Offline analysis (CSV/Parquet -> Excel/Python/JMP/Minitab) | Process engineer SPC charts | Primary target. Provide `tools/db_export/` CLI and flat `VIEW`s. |
| 3 - MES | Siemens Opcenter, Tulip, etc. | Not built. Keep `serial_number` on every row. |
| 4 - Historian | OSIsoft PI, InfluxDB, Ignition | Not built. If site has one, prefer publishing to it over duplicating in SQLite. |
| 5 - Quality/SPC | InfinityQS, Q-DAS | Not built. `measurements.nominal/usl/lsl` nullable hooks reserved now. |

# Data Backup System

`include/AppUI/DataBackupPage.h/.cpp`

Product-scoped file backup with SHA-256 integrity verification. Accessed via "Data Backup" button on main menu (F8).

## Backup Storage

Each backup folder is named `{timestamp}_{type}[_{name}]/` and contains two subfolders plus a manifest:

```
{timestamp}_{type}/
├── manifest.json   # SHA-256 + size for every file below (keys prefixed config/ or data/)
├── config/         # the product directory (positions, recipes, programs, trainer datasets) minus backups/
└── data/           # the product's serial results: <exe>/data/{productName}/ (DUT JSON + images)
```

> Backups created before this layout stored config files at the folder root with no `config/`/`data/` split. Restore still handles those (root files route to the product config dir).

### Destinations (user-selectable checklist)

The "Backup to:" row lets the user tick any combination of destinations; every backup is written to **all enabled** ones:

| Destination | Path | Default |
|-------------|------|---------|
| Local | `config/{productName}/backups/{folder}/` | on |
| AppData | `%APPDATA%/BobcatEngine/backups/{productName}/{folder}/` | on |
| Custom | `{customPath}/{productName}/{folder}/` — `customPath` defaults to `D:\data_config_backup`, auto-created if missing | on |

Selections persist in QSettings (`Backup/DestLocal`, `Backup/DestAppData`, `Backup/DestCustom`, `Backup/CustomPath`). The visible backup list, detail/restore, and pruning operate on the **first enabled** destination (`getListBasePath()`), so disabling Local still surfaces backups stored elsewhere. Restore writes back to the right place per file: `config/` → product dir, `data/` → `<exe>/data/{product}/`.

## Backup Types

| Type | Trigger | Retention |
|------|---------|-----------|
| `save_point` | User clicks "Create Save Point" | Never auto-pruned |
| `auto` | QTimer at configurable interval (default 30 min) | Keep last 10 |
| `pre_revert` | Automatically before any restore | Keep last 3 |

## Key Features

- **SHA-256 manifest** (`manifest.json`) stores per-file hash and size for integrity verification
- **Smart auto-backup** skips if no files changed since last backup (hash comparison)
- **Show Changes** compares current product files against any backup: reports `[M]` modified, `[+]` added, `[-]` deleted files
- **Pre-revert safety** always snapshots current state before restoring (to all enabled destinations)
- Auto-backup settings persist in QSettings (`Backup/AutoEnabled`, `Backup/AutoIntervalMin`)
- **Serial data included** — each backup also captures `<exe>/data/{product}/` (DUT result JSON + captured images) under the backup's `data/` subfolder

## Related

- Root/system config backup (separate from product backup): `include/AppUI/SettingsPage/ConfigBackupManager.h` — dialog-based, lives in Settings page.

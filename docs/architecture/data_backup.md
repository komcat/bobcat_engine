# Data Backup System

`include/AppUI/DataBackupPage.h/.cpp`

Product-scoped file backup with SHA-256 integrity verification. Accessed via "Data Backup" button on main menu (F8).

## Backup Storage

Backups are stored per-product in `config/{productName}/backups/{timestamp}_{type}[_{name}]/`. Each backup copies the entire product directory (positions, recipes, programs, trainer datasets) except the `backups/` subfolder itself.

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
- **Pre-revert safety** always snapshots current state before restoring
- Auto-backup settings persist in QSettings (`Backup/AutoEnabled`, `Backup/AutoIntervalMin`)

## Related

- Root/system config backup (separate from product backup): `include/AppUI/SettingsPage/ConfigBackupManager.h` — dialog-based, lives in Settings page.

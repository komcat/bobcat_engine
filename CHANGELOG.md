# Changelog

All notable changes to bobcat_engine, tracked by git commit.

---

## v1.7.0 — 2026-04-07

### Highlights

- **Sub-Recipe System** — Recipes can now inline other recipes as sub-steps, with folder-based categorization, drag-drop organization, and usage-aware deletion
- **Config Backup & Restore** — One-click backup of all config/SYSTEM/product folders with timestamped restore points
- **Lighting System Overhaul** — Grouped channels, two-column UI, improved sequence plugins
- **Motion Page Redesign** — Isometric 3D viewport with CCW/CW rotation, graph edge visualization
- **PSU & SMU Page Redesign** — Per-channel UI template, multi-channel SCPI commands, external connection detection
- **Placement Correction Trainer** — Teach reference/target line angles, compute hex U-axis correction, retrace validation, recipe-executable step
- **Roll Correction Widget** — Dual-channel peak measurement, arctan-based W-axis correction, fine grid scan with background stop control
- **Dual-Channel Grid Scan** — GridScanDualChannel plugin with data logging, SI formatting, and delta tracking
- **Motion Interlock System** — Position-based safety rules (up to 3), configurable operators, persisted to interlocks.json
- **Beam Detection Pipeline** — SWIR beam processing, encircled energy (EE) metric, D4sigma detection (Halcon), beam focus optimizer, CE/EE charts
- **Qt Camera Monitor** — Qt-based camera window with side panel, window persistence, dual instances, click-to-move calibration
- **Accuracy Trainer Enhancements** — SQLite corner logging, unique point UIDs, reference image crop+crosshair, vision profile detect & center, Z offset trainer, yaw trainer, 2-3 corner hex teach
- **Safety Infrastructure (MISRA Phases 1-5)** — Bounded loops, [[nodiscard]], /W4, SocketGuard RAII, exception removal, unified ErrorCode enum, SafetyEventLog, thread priority, E-stop cascade, command timeout watchdog, concurrency hardening, health monitor, file rotation

### All Changes (by date)

## 2026-04-07

- **uprev to v1.7: main menu redesign, changelog viewer, embedded icons, product image**
  - Version bumped from 1.6 to 1.7 with build number reset
  - Added in-app Changelog button next to version title, opens scrollable dialog with CHANGELOG.md
  - Main menu buttons reorganized to 4-column x 2-row grid layout with F1-F7 shortcut tooltips
  - Embedded menu icons via Qt resource system (.qrc) — no runtime file dependency
  - New icons: RunProgramPage.png (Process), accuracy_trainer.png, visionmanager.png, logo.png (app icon)
  - Application window icon set to logo.png via QApplication::setWindowIcon
  - Product image placeholder (250x250): auto-loads product.png from active product folder
  - Removed Delete Product button from main menu

## 2026-04-05

- `fb97882` — **add sub-recipe plugin, recipe folder categories, and recipe management UI**
  - Sub-Recipe plugin: inlines another recipe's steps at build time via ProcessSequenceFactory
  - Recipe list refactored from flat QListWidget to categorized QTreeWidget with physical subfolders
  - Drag-drop moves recipe files with confirmation warning about program references
  - Action buttons: create/remove folder, copy recipe, delete with usage checking (scans programs and sub-recipe references)
  - Recipe selector widget (addRecipeSelector) with recursive FileSelectorDialog for subfolder support
  - Edit Program table: added File column showing recipe filename alongside display name
  - Recipe name field now shows filename for consistency

- `9538f0e` — **add config backup & restore feature to settings page**
  - ConfigBackupManager module for timestamped backups of config root, SYSTEM, and product folders
  - Backup destination user-configurable via QFileDialog, persisted in QSettings across restarts
  - Restore dialog lists available backups with scope selection and delete option
  - New Backup section in application settings tree

- `f5f40dd` — **overhaul lighting system: grouped channels, two-column UI, improved sequence plugins**

## 2026-04-04

- `239c3ef` — **fix CCW/CW buttons to rotate viewport around screen center instead of camera azimuth**

## 2026-04-03

- `d60923a` — **redesign motion page: isometric 3D view, UI layout improvements, graph edge visualization**
- `033b135` — **redesign PSU and SMU control pages with per-channel UI template**
- `0c2dc4f` — add documentation comments to placement correction trainer and sequence plugin
- `8b3d333` — add test server v08
- `8aec14f` — **add axis scan plugin, recipe filter, edit-recipe navigation, and multiple bug fixes**

## 2026-04-02

- `a518c90` — fix shutdown hang caused by AuxViewWindow refusing to close
- `9426f76` — add placement correction retrace as recipe-executable step in RunProgramPage
- `6526269` — add shutdown confirm dialog, app icon update, camera monitor geometry persistence, and aux window close fix
- `85a87ee` — add fine grid scan buttons and stop scan to roll correction tab
- `692bd8a` — add grid scan file logging helper, dual-channel delta tracking, and user stop handling
- `f20a93f` — add roll correction tab with move-to-peak and W correction

## 2026-04-01

- `f87a359` — **add dual-channel grid scan plugin, grid scan data logging, SI formatting, and log level tuning**
- `2916ed6` — **add placement correction trainer, recipe file rename, scan timeout increase, and quick move grid expansion**

## 2026-03-31

- `1e5899b` — **add motion interlock system, device-colored UI, and velocity presets**
- `47ecf71` — add jog step sizes JSON config with local-first loading and per-device colors
- `4123fde` — update jog panel 0.2u, 0.3u and UI 3-column button layout
- `d73a54c` — add relative move shortcut in Qt window
- `d5db4ef` — fix bug dispense autosave navigation file

## 2026-03-30

- `db32b9a` — update plugin and trainer for auto navigate
- `4973d76` — fix XYZ reteach

## 2026-03-28

- `05f9f19` — add window persistence and second Qt camera monitor instance
- `d601dc3` — add Qt-based camera monitor window with side panel controls
- `245c6f9` — extract GDI camera window overlays into standalone files
- `9ed811e` — add FPS overlay to GDI camera window, set default camera settings (10fps, auto gain OFF)
- `318df7f` — add blob detection mode

## 2026-03-25

- `264a073` — add Ultra Fine (0.5/0.2/0.1 um) scan mode to hexapod scanning
- `10e0723` — fix 60s timeout issue in scanning

## 2026-03-20

- `b0e7d42` — change total slot limit to 20 from 5

## 2026-03-19

- `1db3624` — **add SQLite logging for accuracy trainer corners, unique point UIDs, and reference image crop+crosshair**

## 2026-03-17

- `b34b1b2` — split detection into single-frame scoring and EMA-smoothed display
- `ac9efd8` — refactor beam optimizer, add TEC controls, improve beam detection
- `725f00a` — add beam focus optimizer, CE/EE charts, and 3-column tools layout
- `797c109` — add pneumatic RetractAll command and Slide Retract All recipe plugin
- `f462b7d` — improve beam detection stability and PSU current tracking

## 2026-03-15

- `c2069f9` — fix splash screen hang
- `a876850` — add encircled energy (EE) metric to beam detection pipeline
- `24593ad` — create placeholder for beam detection, implement beam detection

## 2026-03-14

- `241251c` — add camera calibration fields to camera_config.json for config_editor

## 2026-03-13

- `cb8c51d` — add auto focus in jog
- `9a75c35` — **add and test Yaw trainer**

## 2026-03-12

- `1b2cc4b` — add trainer documents
- `391c23c` — **add Z Offset Trainer with global-to-local Z correction**
- `dc7e646` — add channel selection to SMU plugins, fix Save As duplicate IDs, remove copy suffix

## 2026-03-10

- `dea77b8` — add accuracy mode
- `ebb17ff` — fix bug 60 sec timeout for plugins waiting user interaction

## 2026-03-06

- `a056f4b` — fix bug 60 sec wait timeout

## 2026-03-05

- `2326d04` — apply 2-3 corner teach for hex devices for positioning instead of single point
- `f627106` — add planar adjustment

## 2026-03-04

- `eaefc7c` — add open config button
- `a1e1991` — adjust safety guard polling and update jog UI

## 2026-03-03

- `9ae0e17` — fix custom server for filtering noise beams parameters
- `f6420ec` — add pneumatic camera offset

## 2026-03-02

- `762453b` — fix GDI camera window menu responsiveness and fix UI error on SMU page

## 2026-02-25

- `66292fe` — **add calibration cam offset trainer and plugin for move to high/low mag positions**

## 2026-02-24

- `13885d0` — update UI trainer, fix issue closing main app but GDI window still active, update real-world measurement

## 2026-02-23

- `cdb970b` — update click-to-move to correct mm/pixel value automatically with different image sizes

## 2026-02-21

- `49ed466` — add 2nd GDI and inject BS2 beam stream into sequence context
- `a997616` — enable 2 realtime camera monitor windows to support beam stream
- `1018dd8` — fix inpos logic for ACS controller

## 2026-02-20

- `07132be` — fix save template to autosave when working PR matching, add accuracy save file UID
- `0320ad7` — merge Accuracy Trainer

## 2026-02-19

- `3332577` — camera settle time 500ms, remove excess delay 500ms per axis in ACS wait for motion complete
- `45b6bcb` — **add accuracy trainer: taught position of reference, taught bonding position, apply manual or vision alignment**

## 2026-02-18

- `fcf835c` — update UI and GDI camera beam streaming optimize

## 2026-02-14

- `af4baee` — add beam server analyzer

## 2026-02-13

- `62c9bd4` — create vision tester

## 2026-02-12

- `4f0b943` — tested the loop for d4sigma, update RunProgramPage UI, move progress bar
- `3ade49d` — add d4sigma beam detection and update documentation
- `9836bbc` — add Halcon d4sigma beam vision profile
- `b874456` — add custom beam server

## 2026-02-10

- `a37f6dd` — add "Refresh CV" button to Context Tab
- `91c2ad9` — fix parallel move error after security code update

## 2026-02-09

- `45e0d38` — add UserPromptWidget alignment enum (Center, TopLeft) and setAlignment methods

## 2026-02-07

- `d62debb` — enable DoorOpen safety input
- `e897da3` — **add SafetyWatchManager health monitor (heartbeat, stall detection, watchdog timeout)**
- `f8a2b0f` — add motion E-stop cascade
- `3cb417a` — **Phase 4 MISRA compliance: E-stop broadcast, command timeout watchdog, concurrency hardening, connection pre-checks**
- `ed7c2b0` — **Phase 3 MISRA compliance: unified ErrorCode enum, SafetyEventLog, thread priority, clang-tidy**

## 2026-02-06

- `1d96425` — **Phase 2 MISRA compliance: zero-warning /W4 build, SocketGuard RAII, exception removal**
- `5d50081` — **Phase 1 MISRA compliance: bounded loops, [[nodiscard]] attributes**

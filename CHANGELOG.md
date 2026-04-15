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

## 2026-04-14

- **fix VisionCommandHandler reporting success on no-match**
  - Halcon pattern-match plugins (Gray, NCC, Edge, Descriptor) intentionally return `num_matches=0` with `success=true` on no-match; this caused `execute_profile` to leave `vision.result.success=true` while skipping `world_x_mm`/`world_y_mm`, so callers reading defaults saw `(0, 0)` and downstream consumers (e.g. `PlacementRetraceWidget::runAutoDetect`) could issue `MoveToAbs(0, 0, currentZ)` — a dangerous garbage move
  - Added `ApplyNoMatchGuardToContext()` helper in `VisionCommandHandler.cpp` (anonymous namespace) — flips `vision.result.success=false` and sets `vision.result.error` when either `vision.result.num_matches == 0` or none of `match_0_anchor_{x,y}`/`corner_{x,y}`/`center_{x,y}` keys are present
  - Guard wired into `Execute()` right after `StoreResultInContext`, covering all three command paths (`execute`, `capture_and_execute`, `execute_profile`) via the common `CaptureAndExecute` → `Execute` chain
  - `Execute()` now returns `false` when the guard flips success, aligning Halcon behavior with the OpenCV plugin convention where no-match is a dispatch-level failure; `execute_profile` still returns `true` from `ExecuteCommand` per the existing "dispatch OK" pattern, but context state is now correct
  - `capture_as` prefix copies propagate the corrected `success=false`/`error` automatically via the existing `vision.*` → `<prefix>.*` copy loop
  - Removed the now-redundant inline success-flip in the `calculate_world_position` / `!foundReference` branch; kept the diagnostic `⚠️ No reference point found` logs

## 2026-04-11

- **runtime theme switcher with CC-29 palette and live reload**
  - New `Theme::ThemeId` enum (`Default`, `CC29`) and `Theme::applyPalette()` in `Theme.h` — bulk-reassigns all `Color::*` pointers to the chosen palette at runtime (constexpr dropped to mutable `inline const char*`)
  - Added CC-29 palette (`Palette::CC29`) — 23 colors mapped to semantic roles: navy/purple backgrounds, cream text, cyan primary, sage green success, tan warning, coral danger, pink/mint accents; background `#212123`
  - New `ThemeManager` QObject singleton (`include/AppUI/ThemeManager.h/.cpp`) — `setTheme()` reassigns palette pointers, re-applies global QSS via `qApp->setStyleSheet(DarkTheme::getStyleSheet())`, emits `themeChanged()` signal
  - `RecipePageStyles::Colors::*` converted from `const QString` to `inline QString()` functions so they return fresh palette values after a theme switch (3 consumer files updated)
  - New `UI/Theme` combo setting in Application Settings page (`Default (Dark)` / `CC-29 (Retro)`) — uses existing combo infrastructure, no new UI code
  - Boot-time palette read in `Application.cpp` — reads `QSettings("UI/Theme")` before first `setStyleSheet()` so the initial render uses the saved theme
  - Live reload wiring in `MainWindow.cpp` — `settingsChanged` → lambda reads QSettings → `ThemeManager::setTheme()` → `themeChanged()` → widget `applyTheme()` slots
  - `applyTheme()` public slots added to `TopBar` (10 styled elements), `RunProgramPage` (status badge, buttons, progress bar), `RecipePage` (tabs, tree, bezel buttons, vision group, recipe name edit) — re-apply local QSS from current `Theme::Color::*` values

- **compact settings page property editor**
  - Value editor field resized from full-width/tall to compact 420px-wide fixed-height with top-left alignment
  - Section badge (`m_propSectionLabel`) now `Maximum/Fixed` size policy — shrinks to text content
  - `QStackedWidget` vertical policy set to `Fixed`, wrapped in `QHBoxLayout` with trailing stretch for flush-left alignment
  - All 4 editor widgets (LineEdit, SpinBox, CheckBox, ComboBox) constrained to `kEditorMaxWidth`
  - Property panel root layout set to `AlignTop | AlignLeft`

- **Theme palette system and Phase 2 migration**
  - New `Theme.h` — header-only `Theme::Palette::*` (raw hex constants) + `Theme::Color::*` (semantic role aliases) single source of truth for the entire app's color scheme
  - New `ThemeButtonStyles.h` — `Theme::Buttons::bezel()` builder + 5 convenience variants (`bezelPrimary/Success/Warning/Danger/Neutral`) + `Theme::Boxes::groupBoxDark()`; eliminates ~30-line duplicated bezel QSS at every call site
  - `DarkTheme.cpp` rewritten to use `__PLACEHOLDER__` template substituted from `Theme::Color::*` at runtime
  - Phase 2 migration of TopBar (5 buttons + labels), RunProgramPage execution view + Edit Program subpage, RollCorrectionWidget (all buttons + labels), RecipePage + RecipePageStyles.h — ~550 lines of duplicated QSS removed
  - `RecipePageStyles.h` Colors:: layer rebound to `Theme::Color::*` references

- **scan-busy indicator mirrored to RunProgramPage status badge**
  - Wired `ScanActivityTracker::activeChanged` to RunProgramPage execution badge via `QueuedConnection`; shows amber `SCANNING` overlay while any scan is active, reverts to base state (Idle/Running/Stopped/Loop) when scan ends
  - Status badge resized to 250x150; `setProgramStatus()`/`applyProgramStatusLabel()` helpers centralize the 5 status set-sites

- **scan-busy indicator in top bar, Scanning tab cleanup, grid-scan log demotion**
  - New `ScanActivityTracker` QObject (`include/common/`) — thread-safe, reference-counted broadcaster with `activeChanged(bool)` signal; scan operations register via RAII `ScanActivityGuard` so every return path (success, early failure, user stop) correctly pairs Begin/End
  - `⚡ SCANNING` amber badge added to TopBar next to the safety LED; hidden when idle, visible whenever any scan is in progress (tab-initiated OR recipe-initiated), driven via `Qt::QueuedConnection` for cross-thread safety
  - `MotionCommandHandler` — RAII guards at all 5 scan command entry points: `XCAN`, `XCANExtended`, `GridScan`, `GridScanExtended`, `GridScanDualChannel`; covers recipe-driven grid scans
  - `ScanExecutor` — flag-guarded Begin/End around `executeScan()` + `executeRotationScan()` with pairing in `cleanup()`/`cleanupRotation()` so async completion and stop paths clear correctly
  - `ApplicationContext.scanActivityTracker` field; instance owned by `Application`, created after settings load but before SequenceEngine init so MotionCommandHandler can be wired at registration time
  - Scanning tab (`ScanControlWidget`): removed the "Rotation Optimization" group entirely (~200 LOC — device/axis/step/threshold/Start Rotation Scan button); `RotationScanController` and `ScanExecutor::executeRotationScan` left in place but no longer reachable from this UI
  - Scanning tab compact layout: Hexapod Scanning group top-left anchored via `QSizePolicy::Fixed` + trailing horizontal/vertical stretches; all 6 Coarse/Fine/Ultra Fine buttons `setFixedSize(140, 26)`; STOP button `setFixedSize(140, 90)`; Data Channel combo `setFixedWidth(320)`; big value label `setFixedWidth(500)`
  - Grid-scan log noise reduction (19 sites): demoted per-point "New peak", per-20-points "Progress", context-capture echoes, and redundant "Scan completed successfully" trailers across XCAN/XCANExtended/GridScan/GridScanExtended/GridScanDualChannel; KEPT at INFO: scan start, peak-value callbacks, returning-to-peak, final summary, exit threshold reached, stop requested

- `8022a4a` — **add Logging.DebugEnabled application setting to suppress verbose developer logs**
  - New `[Logging] DebugEnabled` bool in `config/system/applicationSettings.ini`, default `false` (quiet operator mode); editable via Application Settings → Logging section with a bool toggle
  - `LogManager::SetMinimumLevel` moved out of header to sync both `m_minimumLevel` and spdlog's own level — fixes latent bug where spdlog's hard-coded info level silently dropped DEBUG messages even when `m_minimumLevel` allowed them
  - `LogManager` constructor default minimum level changed from `DEBUG` to `INFO`
  - Fixed 3 `MotionCommandHandler` messages literally prefixed `"[DEBUG]"` but emitted via `m_logger->Info` — stripped the prefix and demoted the calls
  - Demoted ~80 call sites from INFO to DEBUG across `SequenceEngine` (22 "Registered X handler" startup lines + `⚙️ Executing`, `Custom timeout`, `Building device registry`), `MotionCommandHandler` (param-resolution echoes, `WaitForMotion` diagnostics, `ParallelMove` per-device progress), `ACSController`/`PIController`/`UnifiedMotionManager` (init/shutdown, thread start/stop, per-axis velocity echoes, `MoveToAbs`/`MoveRelative` routing), `SystemCommandHandler` (`Waiting for Xms` / `Wait completed` pairs), `IOCommandHandler` (pin-name fallback, `Delaying`), `DUTCommandHandler` (product-name loads), `CameraManager` (exposure setting echoes), `AdaptiveScanController` (ctor/dtor), `JogPanelWindow` (device-not-connected, panel-refreshed, applied-exposure)
  - Outcome: trivial 8-step recipe log dropped from ~60 lines to ~10 operator-meaningful lines when the setting is off; scans no longer flood the log with 100+ per-iteration move traces

- **placement correction manual offset tuner on review and view-results pages**
  - Added Manual Offset Tuner group box to PlacementRetraceWidget (Review state): nudge the corrected hex target position by XYZ (mm) and UVW (°) before saving; offsets are applied on top of the computed retrace correction and written to `motor_devices.local.json`
  - Added "Re-save Target Position" tuner workflow to PlacementResultWidget (View Results state): adjust an already-saved target position by XYZ/UVW offsets and re-persist without requiring a full retrace cycle
  - New persisted config fields in `PlacementCorrectionConfig`: `tuneOffsetX/Y/Z` (mm) and `tuneOffsetU/V/W` (°), serialized in PlacementCorrectionStore so offsets survive session restarts
  - New signals `PlacementRetraceWidget::offsetsPersistRequested` and `PlacementResultWidget::targetResaved` wired in both PlacementCorrectionPage (F8 trainer flow) and RunProgramPage (recipe execution flow)
  - `RunProgramPage` gains `m_activePlacementStoreBasePath` member so offsets tuned during a recipe run are saved to the correct per-product placement_correction directory
  - UI convention: tuner group boxes use `setFixedWidth` + `Qt::AlignLeft | Qt::AlignTop` layout; spinboxes are fixed width; range ±1, step 0.001, 3 decimals

## 2026-04-09

- **stepwise roll W correction with channel tracking and per-substep XCANExtended re-peak**
  - Added "Tracking Channel" dropdown to RollCorrectionWidget; populated from DataStore via existing periodic refresh + one-shot populate at construction
  - Correct W button now requires both CH1/CH2 registration AND a selected tracking channel
  - Replaced single-shot W rotation with stepwise procedure: split total W into N sub-steps no larger than 0.085 deg each (hard cap 0.1 deg), e.g. 0.3 deg → 4 × 0.075 deg
  - Velocity reduced from 1.0 to 0.1 mm/s for all correction moves
  - After every W sub-step, runs XCANExtended (axes=XY, step=0.5 µm, settling=300 ms) on the selected channel to re-peak optical power before applying the next sub-step
  - Background-threaded with SequenceContext stop control; STOP Scan button reused to abort mid-correction (halts before next W sub-step or scan)
  - Original system velocity restored on every exit path including failure and stop

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

# RecipePage Layout

`include/AppUI/RecipePage/`

The F2 "Recipe" page is composed from focused helpers — touch the helper for the area you're working in, **not** the orchestrator.

| File | Owns |
|------|------|
| `RecipePage.{h,cpp}` | Top-level page, three-panel splitter, dropdown menus, recipe step tree (`m_recipeTree`), file ops (new/load/save/save-as), `setPageState`, theme refresh, signal wiring between helpers. The orchestrator — keep it thin. |
| `ProcessCatalogWidget` | Left panel "Available Processes" tree: search box, gear/visibility dialog, "+" add button, hidden-process set. Emits `addProcessRequested(processType)`. |
| `RecipeBrowserWidget` | "Recipes" tab in the middle panel: lists recipe JSON files in `config/{product}/recipes/`, folder CRUD, drag-drop move, usage-finder (programs + sub-recipes) on delete. Emits `loadRecipeRequested(filePath)`. |
| `RecipeExecutionController` | Owns `RecipeWorker` lifetime; forwards its 11 output signals so the page connects once instead of re-wiring per execution; installs the safety-stop `DirectConnection`; `Start()` / `RequestStop()` / `IsRunning()` / `ShutdownSync()`. |
| `QuickAddController` | Ctrl+B two-key UX: floating overlay shows numbered category list (1-9), then numbered process list. Emits `processSelected(processType)`. Filters by `ProcessCatalogWidget::hiddenProcesses()` so quick-add indexing matches the visible catalog. |
| `RecipePageSettingsStore` | Pure QSettings I/O for `RecipePage/lastRecipe` (auto-load target on startup). |
| `ProcessConfigWidget` | Right panel: dynamic config form for the selected step. |
| `RecipeProgressWidget` | Right panel during execution: progress bar + status text. Hosted in `RecipeRunWindow`. |
| `RecipeRunWindow` | Floating window that hosts execution-time widgets (progress / manual adjust / manual align / user input). Reused across runs. |
| `RecipeTreeItemFactory` | Builds the `m_recipeTree` step rows (number, enable checkbox, breakpoint, run-once button). |
| `RecipeStepDelegate` | Custom QStyledItemDelegate for the step tree. |
| `RecipeClipboard` | In-process clipboard for copy/paste of process steps. |
| `ContextViewerWindow` + `ContextVariablePreviewWidget` | Live `SequenceContext` inspection (variables captured during execution) and a static preview of variables a recipe will produce. |
| `ProcessVisibilityDialog` | Modal dialog managing `config/hidden_processes(.local).json`. |
| `PropertyTableWidget`, `DeviceSelectorWidgets`, `MotionDeviceSelectorDialog`, `PneumaticSlideSelectorDialog` | Form helpers consumed by `ProcessConfigWidget`. |
| `ProcessSummary`, `RecipePageStyles` | Shared formatters / themed QSS strings. |

When extending the recipe page, prefer adding/expanding a helper over enlarging `RecipePage.cpp`. The page should not own recipe-library file ops, process-catalog state, RecipeWorker callbacks, or quick-add state — those have homes already.

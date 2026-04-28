// ProgramEditPanel.h
// The editable recipe list embedded in the Run Program page's Edit view.
// Owns the Add/Add Script buttons, the recipe table, the preview pane,
// and the Remove / Move Up / Move Down / Toggle Enabled / Manage
// Dependencies row, plus the right-click context menu (rename, inspect
// motion, retrace edit, move-to-slot).
//
// All ProgramManager mutations stay inside the panel; the page is told
// to refresh other UI (run-side recipe list, slot info, save state)
// via the recipeListChanged() signal. Two cross-cutting concerns —
// "Inspect Motion" and "Retrace Edit" — touch widgets the page owns,
// so they go out as signals for the page to act on.
#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class ApplicationContext;
class ProgramManager;
class QPushButton;
class QTableWidget;
class QTextEdit;

class ProgramEditPanel : public QWidget {
    Q_OBJECT
public:
    struct Deps {
        ApplicationContext*       context = nullptr;   // for product checks
        ProgramManager*           manager = nullptr;
        // Callbacks that read page-owned state at the moment the panel
        // needs it — avoids the panel mirroring page state.
        std::function<QString()>  currentProgramPath;  // for auto-save after rename / deps edit
        std::function<bool()>     isExecuting;         // gates retrace-edit menu item
    };

    explicit ProgramEditPanel(Deps deps, QWidget* parent = nullptr);
    ~ProgramEditPanel() override;

    // Refresh the table from the ProgramManager. Call after any external
    // change to the program (slot switch, file load).
    void Reload();

    // Reset the table to empty + clear the preview pane.
    void Clear();

signals:
    // Emitted whenever the panel mutates the program through ProgramManager
    // (add / remove / move / rename / toggle / move-to-slot / dependency edit).
    void recipeListChanged();

    // Page-owned features that take a recipe row index.
    void inspectMotionRequested(int recipeIndex);
    void retraceEditRequested(int recipeIndex);

private slots:
    void onAddRecipe();
    void onAddScript();
    void onRemoveRecipe();
    void onMoveRecipeUp();
    void onMoveRecipeDown();
    void onToggleRecipeEnabled();
    void onManageDependencies();
    void onSelectionChanged();
    void onContextMenuRequested(const QPoint& pos);

private:
    void buildUi();
    void renameRecipe(int recipeIndex);
    void loadRecipePreview(const std::string& recipeFilePath);

    Deps m_deps;

    // Action buttons (top row + bottom row)
    QPushButton* m_addRecipeButton    = nullptr;
    QPushButton* m_addScriptButton    = nullptr;
    QPushButton* m_removeRecipeButton = nullptr;
    QPushButton* m_moveUpButton       = nullptr;
    QPushButton* m_moveDownButton     = nullptr;
    QPushButton* m_toggleEnableButton = nullptr;
    QPushButton* m_manageDepsButton   = nullptr;

    // Table + preview
    QTableWidget* m_recipeTable      = nullptr;
    QTextEdit*    m_previewText      = nullptr;
};

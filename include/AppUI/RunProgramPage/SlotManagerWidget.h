// SlotManagerWidget.h
// The Active Slot dropdown(s) the Run Program page exposes in two
// places — once on the execution side (a slim selector with a recipe
// count label), once on the edit side (the same selector plus
// Rename / Add / Copy / Delete buttons). Both render modes live in
// this single class; the page constructs one of each and bridges them
// via signals.
//
// The widget owns the ProgramManager mutation for slot operations
// (setActiveSlot, renameSlot, createSlot, deleteSlot, duplicateSlot)
// and the QInputDialog / QMessageBox flow around them. The page reacts
// to two outgoing signals:
//   - activeSlotChanged(int): the user picked a different slot
//   - slotsModified(): the slot list itself changed (rename / add /
//                      delete / duplicate)
#pragma once

#include <QWidget>

class ProgramManager;
class QComboBox;
class QLabel;
class QPushButton;

class SlotManagerWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode {
        Run,   // Slim: combobox + "X recipes in this slot" label
        Edit,  // Full: combobox + Rename / Add / Copy / Delete buttons
    };

    struct Deps {
        ProgramManager* manager      = nullptr;
        QWidget*        dialogParent = nullptr;  // for QInputDialog / QMessageBox parenting
    };

    SlotManagerWidget(Mode mode, Deps deps, QWidget* parent = nullptr);
    ~SlotManagerWidget() override;

    // Refresh both the dropdown contents and (Edit mode) the button
    // enabled-states from ProgramManager. Call after any external
    // change to the slot list (e.g. when a sibling instance modified
    // it, or after loadProgram).
    void Reload();

    // Programmatically select a slot without emitting activeSlotChanged.
    // Use this from the page when the *other* slot widget told us a
    // selection happened.
    void SetSelectedSlot(int index);

    // Update the "X recipes in this slot" label (Run mode only).
    void SetRecipeCount(int count);

signals:
    // The user picked a slot from the dropdown. ProgramManager's active
    // slot is already updated by the time this fires.
    void activeSlotChanged(int newSlot);

    // The slot list itself changed (rename / add / delete / duplicate).
    // Page should refresh sibling slot widget + recipe list.
    void slotsModified();

private slots:
    void onComboBoxChanged(int index);
    void onRenameClicked();
    void onAddClicked();
    void onDeleteClicked();
    void onDuplicateClicked();

private:
    void buildRunUi();
    void buildEditUi();

    Mode m_mode;
    Deps m_deps;

    QComboBox*   m_combo           = nullptr;

    // Run mode only
    QLabel*      m_infoLabel       = nullptr;

    // Edit mode only
    QPushButton* m_renameButton    = nullptr;
    QPushButton* m_addButton       = nullptr;
    QPushButton* m_duplicateButton = nullptr;
    QPushButton* m_deleteButton    = nullptr;
};

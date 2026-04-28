// RunPageLeftPanel.h
// The left side of RunProgramPage: back button, product banner with
// chain/loop indicators, and a 3-tab pane (File / Execute / Option)
// holding the program file actions, recent-programs list, execute
// button, recipe list, and the option checkboxes (auto-run, deps,
// chain, loop). Pure UI assembly — the page connects to the exposed
// widgets' signals and reads their state, exactly as before; this
// class just owns construction so RunProgramPage.cpp stops carrying
// ~700 lines of QSS strings.
//
// The Run-side SlotManagerWidget is constructed inside this panel
// (it's part of the Execute tab) and exposed for the page to wire
// its activeSlotChanged signal.
#pragma once

#include <QWidget>

class ProgramManager;
class SlotManagerWidget;
class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTabWidget;

class RunPageLeftPanel : public QWidget {
    Q_OBJECT
public:
    struct Deps {
        ProgramManager* manager      = nullptr;
        QWidget*        dialogParent = nullptr;  // for the SlotManagerWidget's dialogs
    };

    explicit RunPageLeftPanel(Deps deps, QWidget* parent = nullptr);
    ~RunPageLeftPanel() override;

    // ---- Buttons (page wires their clicked() signals) ----
    QPushButton* backButton()           const { return m_backButton; }
    QPushButton* newButton()            const { return m_newButton; }
    QPushButton* loadButton()           const { return m_loadButton; }
    QPushButton* saveButton()           const { return m_saveButton; }
    QPushButton* saveAsButton()         const { return m_saveAsButton; }
    QPushButton* editButton()           const { return m_editButton; }
    QPushButton* executeButton()        const { return m_executeButton; }
    QPushButton* configureLoopButton()  const { return m_configureLoopButton; }

    // ---- List widgets ----
    QListWidget* recipeList()           const { return m_recipeListWidget; }
    QListWidget* recentProgramsList()   const { return m_recentProgramsList; }

    // ---- Option checkboxes ----
    QCheckBox* autoRunNextCheckBox()        const { return m_autoRunNextCheckBox; }
    QCheckBox* useDependenciesCheckBox()    const { return m_useDependenciesCheckBox; }
    QCheckBox* chainExecutionCheckBox()     const { return m_chainExecutionCheckBox; }
    QCheckBox* loopEnabledCheckBox()        const { return m_loopEnabledCheckBox; }
    QCheckBox* ignoreDependenciesCheckBox() const { return m_ignoreDependenciesCheckBox; }

    // ---- Labels ----
    QLabel* productLabel()       const { return m_productLabel; }
    QLabel* programNameLabel()   const { return m_programNameLabel; }
    QLabel* programPathLabel()   const { return m_programPathLabel; }
    QLabel* loopStatusLabel()    const { return m_loopStatusLabel; }
    QLabel* chainIndicator()     const { return m_chainIndicator; }
    QLabel* loopIndicator()      const { return m_loopIndicator; }

    // ---- Tabs + slot widget ----
    QTabWidget*       tabWidget()  const { return m_tabWidget; }
    SlotManagerWidget* slotWidget() const { return m_slotWidget; }

private:
    void buildUi();

    Deps m_deps;

    // Header
    QPushButton* m_backButton       = nullptr;
    QLabel*      m_productLabel     = nullptr;
    QLabel*      m_chainIndicator   = nullptr;
    QLabel*      m_loopIndicator    = nullptr;

    // Tabs
    QTabWidget* m_tabWidget         = nullptr;

    // File tab
    QLabel*      m_programNameLabel = nullptr;
    QLabel*      m_programPathLabel = nullptr;
    QPushButton* m_newButton        = nullptr;
    QPushButton* m_loadButton       = nullptr;
    QPushButton* m_saveButton       = nullptr;
    QPushButton* m_saveAsButton     = nullptr;
    QPushButton* m_editButton       = nullptr;
    QListWidget* m_recentProgramsList = nullptr;

    // Execute tab
    QPushButton*       m_executeButton    = nullptr;
    SlotManagerWidget* m_slotWidget       = nullptr;
    QListWidget*       m_recipeListWidget = nullptr;

    // Option tab
    QCheckBox*  m_autoRunNextCheckBox        = nullptr;
    QCheckBox*  m_useDependenciesCheckBox    = nullptr;
    QCheckBox*  m_chainExecutionCheckBox     = nullptr;
    QCheckBox*  m_loopEnabledCheckBox        = nullptr;
    QCheckBox*  m_ignoreDependenciesCheckBox = nullptr;
    QPushButton* m_configureLoopButton        = nullptr;
    QLabel*      m_loopStatusLabel            = nullptr;
};

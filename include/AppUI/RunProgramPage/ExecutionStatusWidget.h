// ExecutionStatusWidget.h
// The top strip of the Run Program page's execution view: a 250x150
// status badge (Idle / Running / Stopped / SCANNING / Loop #N), a
// stacked program title + status line + current recipe label, a
// 4-cell command tracker (PREVIOUS / CURRENT / NEXT / AFTER — kept
// hidden by default per design but the labels still update), a
// single-line "action detail" tooltip, and a step-execution
// progress bar.
//
// Pure UI assembly. The page reads/writes the inner widgets via
// typed getters; it remains the source of truth for how the badge
// is painted (setProgramStatus + applyProgramStatusLabel + the
// scan-active overlay).
#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;

class ExecutionStatusWidget : public QWidget {
    Q_OBJECT
public:
    explicit ExecutionStatusWidget(QWidget* parent = nullptr);
    ~ExecutionStatusWidget() override;

    // Status badge (250x75) — page paints from its own m_baseStatus*
    // state via applyProgramStatusLabel().
    QLabel* programStatusLabel()    const { return m_programStatusLabel; }

    // "Init DUT" / "x" close button stack — pinned to the left of the badge.
    // Both clicks are wired by RunProgramPage to DUTCommandHandler dispatches
    // (LoadOrCreate / Close). Close is hidden by default; the page shows it
    // after a successful init and hides it again on close.
    QPushButton* initDutButton()    const { return m_initDutButton; }
    QPushButton* closeDutButton()   const { return m_closeDutButton; }

    // Active DUT serial number overlaid on the status badge. Empty hides
    // the SN line. Read by the page when composing the badge text.
    void          setDutSerial(const QString& serial) { m_dutSerial = serial; }
    const QString& dutSerial() const { return m_dutSerial; }

    // Header text labels.
    QLabel* executionTitleLabel()   const { return m_executionTitleLabel; }
    QLabel* executionStatusLabel()  const { return m_executionStatusLabel; }
    QLabel* currentRecipeLabel()    const { return m_currentRecipeLabel; }

    // 4-cell command tracker. The container row is hidden by default
    // (per current design); the labels still receive updates so the
    // tracker can be re-enabled by un-hiding the row.
    QWidget* commandTrackerRow()    const { return m_commandTrackerRow; }
    QLabel*  cmdPastLabel()         const { return m_cmdPastLabel; }
    QLabel*  cmdCurrentLabel()      const { return m_cmdCurrentLabel; }
    QLabel*  cmdNext1Label()        const { return m_cmdNext1Label; }
    QLabel*  cmdNext2Label()        const { return m_cmdNext2Label; }

    // Single-row human-readable description of the running action.
    QLabel* actionDetailLabel()     const { return m_actionDetailLabel; }

    // 0-100 step-execution progress bar.
    QProgressBar* progressBar()     const { return m_progressBar; }

private:
    void buildUi();

    QLabel*       m_programStatusLabel   = nullptr;
    QLabel*       m_executionTitleLabel  = nullptr;
    QLabel*       m_executionStatusLabel = nullptr;
    QLabel*       m_currentRecipeLabel   = nullptr;

    QWidget* m_commandTrackerRow = nullptr;
    QLabel*  m_cmdPastLabel      = nullptr;
    QLabel*  m_cmdCurrentLabel   = nullptr;
    QLabel*  m_cmdNext1Label     = nullptr;
    QLabel*  m_cmdNext2Label     = nullptr;

    QLabel*       m_actionDetailLabel = nullptr;
    QProgressBar* m_progressBar       = nullptr;

    QPushButton*  m_initDutButton     = nullptr;
    QPushButton*  m_closeDutButton    = nullptr;
    QString       m_dutSerial;
};

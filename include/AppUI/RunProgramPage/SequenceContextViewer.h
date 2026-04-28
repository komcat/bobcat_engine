// SequenceContextViewer.h
// Live SequenceContext variable tree (the body of the "Context" tab in
// RunProgramPage's right panel). Owns the filter, expand/collapse, font
// toggle, status label, copy-to-clipboard, export-to-CSV, and the
// "Refresh CV" auto-refresh toggle.
//
// Two render sources are supported:
//   1. Live polling: SetWorker() + StartLivePolling() reads
//      m_programWorker->getCurrentContext() every intervalMs.
//   2. CV refresh: SetCvPopulator() wires a callback that fills an
//      internally-owned standalone context (typically with cv.* / bs.*
//      data from continuous vision / beam streams). Toggle the button
//      to start/stop a 500ms auto-refresh.
//
// The two modes are mutually exclusive — starting one stops the other.
#pragma once

#include <QWidget>
#include <functional>
#include <memory>

class ProgramWorker;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class SequenceContext;

class SequenceContextViewer : public QWidget {
    Q_OBJECT
public:
    // Populator callback: fills the supplied target context with whatever
    // the caller wants displayed. Return false to surface an error message
    // (the viewer displays it in its status label).
    using CvPopulatorFn = std::function<bool(SequenceContext& target,
                                              QString& errorMsgIfFalse)>;

    explicit SequenceContextViewer(QWidget* parent = nullptr);
    ~SequenceContextViewer() override;

    // Wire the "Refresh CV" button to a populator. Without this set the
    // button does nothing; with it set, a click runs the populator and
    // renders, and a toggle starts/stops 500ms auto-refresh.
    void SetCvPopulator(CvPopulatorFn fn);

    // Set the worker we should poll while live polling is active. Pass
    // nullptr right before destroying the worker.
    void SetWorker(ProgramWorker* worker);

    void StartLivePolling(int intervalMs = 500);
    void StopLivePolling();

    // Immediate refresh from the worker (or, when the worker has stopped,
    // a "(last run)" badge update). Used right before worker deletion.
    void ForceRefresh();

    // Stop CV auto-refresh if the user left the toggle on.
    void StopCvAutoRefresh();

    // Whether the live-polling timer is currently active.
    bool IsLivePolling() const;

signals:
    // Emitted when CV auto-refresh starts via the user toggling the
    // button on. Live polling cancels this; the page can also call
    // StopCvAutoRefresh() to end it.
    void cvAutoRefreshStarted();
    void cvAutoRefreshStopped();

private slots:
    void onFilterChanged(const QString& filter);
    void onCopyToClipboard();
    void onExportToCsv();
    void onRefreshCvClicked(bool checked);

private:
    void buildUi();
    void applyFontSize();
    void refreshFromWorker(bool forceUpdate);
    void runCvPopulatorOnce();
    void renderTree(SequenceContext* context,
                    const QString& varLabelPrefix,
                    const QString& varLabelSuffix);

    // Widgets
    QTreeWidget* m_tree              = nullptr;
    QLineEdit*   m_filterEdit        = nullptr;
    QLabel*      m_statusLabel       = nullptr;
    QPushButton* m_copyButton        = nullptr;
    QPushButton* m_exportButton      = nullptr;
    QPushButton* m_refreshCvButton   = nullptr;
    QPushButton* m_expandAllButton   = nullptr;
    QPushButton* m_collapseAllButton = nullptr;
    QPushButton* m_fontToggleButton  = nullptr;

    // Timers
    QTimer* m_liveRefreshTimer       = nullptr;
    QTimer* m_cvAutoRefreshTimer     = nullptr;

    // State
    ProgramWorker* m_worker          = nullptr;
    CvPopulatorFn  m_cvPopulator;
    std::unique_ptr<SequenceContext> m_standaloneCvContext;
    bool m_largeFont                 = false;
};

// UserInteractionOverlay.h
// The QStackedWidget on the right side of RunProgramPage that flips
// between the "execution log + tabs" view and one of several blocking
// interaction widgets driven by the recipe worker. Each Request* method
// is callable from any thread (including the recipe worker thread) —
// it switches to the right page on the UI thread, runs a QEventLoop
// until the user responds, then restores the default page.
//
// The 5 built-in widgets that come with the overlay:
//   index 1 — UserPromptWidget        (yes/no confirmation)
//   index 2 — ManualAdjustWidget      (jog-and-confirm)
//   index 3 — ManualAlignPointWidget  (vision-assisted alignment)
//   index 4 — UserInputWidget         (text input)
//   index 5 — RetraceEditWidget       (per-position pause for retrace edit)
//
// Index 0 is reserved for the page's "default" view (the tabs widget).
// Pages that need to host additional embedded widgets (PlacementRetrace
// at index 6, EdgeTransferRetrace at index 7) call AddExtraPage() and
// drive show/hide + their own event loop on top of the overlay's stack.
#pragma once

#include <QStackedWidget>
#include <QString>
#include <QStringList>
#include <utility>

struct ApplicationContext;
class TransformationMatrixLoader;
class ManualAdjustWidget;
class ManualAlignPointWidget;
class RetraceEditWidget;
class UserInputWidget;
class UserPromptWidget;

class UserInteractionOverlay : public QStackedWidget {
    Q_OBJECT
public:
    struct Deps {
        ApplicationContext*         context      = nullptr;
        TransformationMatrixLoader* matrixLoader = nullptr;
    };

    struct RetraceEditResult {
        bool continueExecution = false;
        bool save              = false;
    };

    explicit UserInteractionOverlay(Deps deps, QWidget* parent = nullptr);
    ~UserInteractionOverlay() override;

    // Wire the page's tabs/log widget as the index-0 default view.
    // Must be called once after construction, before any Request* calls.
    void SetDefaultPage(QWidget* page);

    // Push another page (e.g. PlacementRetraceWidget) onto the stack
    // and return its index. The overlay does not manage these pages'
    // lifecycle or signal wiring — the page does.
    int AddExtraPage(QWidget* page);

    // Switch back to the index-0 default view.
    void ShowDefaultPage();

    // ---- Synchronous blocking calls (worker-thread safe) ----
    // Each marshals to the UI thread, switches to the matching index,
    // runs a QEventLoop until the user responds, then restores the
    // default page and returns the user's answer.
    bool RequestUserPrompt(const QString& title, const QString& message);

    bool RequestManualAdjust(const QString& message,
                             const QString& deviceName,
                             double stepSizeUm,
                             bool enableX, bool enableY, bool enableZ,
                             const QStringList& imagePaths = QStringList());

    bool RequestManualAlignPoint(const QString& pointName,
                                 const QString& message,
                                 bool enableX, bool enableY, bool enableZ);

    std::pair<bool, QString> RequestUserInput(const QString& title,
                                              const QString& prompt,
                                              const QString& defaultValue);

    RetraceEditResult RequestRetraceEdit(const QString& deviceName,
                                          const QString& positionName,
                                          double origX, double origY, double origZ,
                                          double origU, double origV, double origW,
                                          double currX, double currY, double currZ,
                                          double currU, double currV, double currW,
                                          int currentPosition,
                                          int totalPositions);

private:
    void buildOverlays();

    Deps m_deps;

    UserPromptWidget*       m_promptWidget          = nullptr;
    ManualAdjustWidget*     m_manualAdjustWidget    = nullptr;
    ManualAlignPointWidget* m_manualAlignPointWidget = nullptr;
    UserInputWidget*        m_userInputWidget       = nullptr;
    RetraceEditWidget*      m_retraceEditWidget     = nullptr;

    int m_idxDefault         = -1;
    int m_idxPrompt          = -1;
    int m_idxManualAdjust    = -1;
    int m_idxManualAlignPoint = -1;
    int m_idxUserInput       = -1;
    int m_idxRetraceEdit     = -1;
};

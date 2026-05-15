// UserInteractionOverlay.cpp
#include "UserInteractionOverlay.h"

#include "ApplicationContext.h"
#include "ManualAdjustWidget.h"
#include "ManualAlignPointWidget.h"
#include "RetraceEditWidget.h"
#include "UserInputWidget.h"
#include "UserPromptWidget.h"
#include "settings/ApplicationSettings.h"

#include <QEventLoop>
#include <QMetaObject>

UserInteractionOverlay::UserInteractionOverlay(Deps deps, QWidget* parent)
    : QStackedWidget(parent)
    , m_deps(std::move(deps))
{
    buildOverlays();
}

UserInteractionOverlay::~UserInteractionOverlay() = default;

void UserInteractionOverlay::buildOverlays()
{
    // index 1 — User prompt (yes/no). Honors the page's PromptAlignment setting.
    m_promptWidget = new UserPromptWidget();
    m_promptWidget->setStyleSheet(
        "UserPromptWidget { "
        "  background-color: #1e1e1e; "
        "}");
    if (m_deps.context && m_deps.context->applicationSettings) {
        std::string alignStr = m_deps.context->applicationSettings->GetString(
            "UI", "PromptAlignment", "Center");
        if (alignStr == "TopLeft") {
            m_promptWidget->setAlignment(UserPromptWidget::Alignment::TopLeft);
        }
    }

    // index 2 — Manual jog-and-confirm.
    m_manualAdjustWidget = new ManualAdjustWidget(m_deps.context, m_deps.matrixLoader);
    m_manualAdjustWidget->setStyleSheet(
        "ManualAdjustWidget { "
        "  background-color: #1e1e1e; "
        "}");

    // index 3 — Manual align point (vision-assisted).
    m_manualAlignPointWidget = new ManualAlignPointWidget(
        m_deps.context ? m_deps.context->motionManager : nullptr,
        m_deps.context);
    m_manualAlignPointWidget->setStyleSheet(
        "ManualAlignPointWidget { "
        "  background-color: #1e1e1e; "
        "}");

    // index 4 — User text input. Mirrors the prompt's alignment.
    m_userInputWidget = new UserInputWidget();
    m_userInputWidget->setStyleSheet(
        "UserInputWidget { "
        "  background-color: #1e1e1e; "
        "}");
    if (m_deps.context && m_deps.context->applicationSettings) {
        std::string alignStr = m_deps.context->applicationSettings->GetString(
            "UI", "PromptAlignment", "Center");
        if (alignStr == "TopLeft") {
            m_userInputWidget->setAlignment(UserInputWidget::Alignment::TopLeft);
        }
    }

    // index 5 — Retrace edit (per-position pause).
    m_retraceEditWidget = new RetraceEditWidget(
        m_deps.context ? m_deps.context->motionManager : nullptr,
        m_deps.matrixLoader);
    m_retraceEditWidget->setStyleSheet(
        "RetraceEditWidget { "
        "  background-color: #1e1e1e; "
        "}");
    // Stack indices are recorded as the page calls SetDefaultPage()
    // (index 0) followed by these widgets in order.
}

void UserInteractionOverlay::SetDefaultPage(QWidget* page)
{
    if (!page) return;
    m_idxDefault          = addWidget(page);
    m_idxPrompt           = addWidget(m_promptWidget);
    m_idxManualAdjust     = addWidget(m_manualAdjustWidget);
    m_idxManualAlignPoint = addWidget(m_manualAlignPointWidget);
    m_idxUserInput        = addWidget(m_userInputWidget);
    m_idxRetraceEdit      = addWidget(m_retraceEditWidget);
    setCurrentIndex(m_idxDefault);
}

int UserInteractionOverlay::AddExtraPage(QWidget* page)
{
    return addWidget(page);
}

void UserInteractionOverlay::ShowDefaultPage()
{
    if (m_idxDefault >= 0) setCurrentIndex(m_idxDefault);
}

bool UserInteractionOverlay::RequestUserPrompt(const QString& title,
                                                const QString& message)
{
    bool result = false;

    // Marshal to UI thread; the lambda spins a local QEventLoop until
    // the prompt widget signals a response, then restores the default page.
    QMetaObject::invokeMethod(this, [this, &title, &message, &result]() {
        setCurrentIndex(m_idxPrompt);
        m_promptWidget->showPrompt(title, message);

        QEventLoop loop;
        // Connect the response signal directly into the loop. The
        // disconnect when the loop returns is automatic since the
        // QMetaObject::Connection is local.
        auto conn = connect(m_promptWidget, &UserPromptWidget::responseReceived,
                            &loop, [&loop, &result](bool accepted) {
            result = accepted;
            loop.quit();
        });
        loop.exec();
        disconnect(conn);

        setCurrentIndex(m_idxDefault);
    }, Qt::BlockingQueuedConnection);

    return result;
}

bool UserInteractionOverlay::RequestManualAdjust(const QString& message,
                                                  const QString& deviceName,
                                                  double stepSizeUm,
                                                  bool enableX, bool enableY, bool enableZ,
                                                  const QStringList& imagePaths)
{
    bool result = false;

    QMetaObject::invokeMethod(this, [&]() {
        setCurrentIndex(m_idxManualAdjust);
        m_manualAdjustWidget->showAdjustment(message, deviceName, stepSizeUm,
                                              enableX, enableY, enableZ, imagePaths);

        QEventLoop loop;
        auto conn = connect(m_manualAdjustWidget, &ManualAdjustWidget::responseReceived,
                            &loop, [&loop, &result](bool completed) {
            result = completed;
            loop.quit();
        });
        loop.exec();
        disconnect(conn);

        setCurrentIndex(m_idxDefault);
    }, Qt::BlockingQueuedConnection);

    return result;
}

bool UserInteractionOverlay::RequestManualAlignPoint(const QString& pointName,
                                                      const QString& message,
                                                      bool enableX, bool enableY, bool enableZ)
{
    bool result = false;

    QMetaObject::invokeMethod(this, [&]() {
        setCurrentIndex(m_idxManualAlignPoint);
        m_manualAlignPointWidget->showAlignment(pointName, message,
                                                 enableX, enableY, enableZ);

        QEventLoop loop;
        auto conn = connect(m_manualAlignPointWidget, &ManualAlignPointWidget::responseReceived,
                            &loop, [&loop, &result](bool completed) {
            result = completed;
            loop.quit();
        });
        loop.exec();
        disconnect(conn);

        setCurrentIndex(m_idxDefault);
    }, Qt::BlockingQueuedConnection);

    return result;
}

std::pair<bool, QString> UserInteractionOverlay::RequestUserInput(
    const QString& title, const QString& prompt, const QString& defaultValue)
{
    bool accepted = false;
    QString text;

    QMetaObject::invokeMethod(this, [&]() {
        setCurrentIndex(m_idxUserInput);
        m_userInputWidget->showInput(title, prompt, defaultValue);

        QEventLoop loop;
        auto conn = connect(m_userInputWidget, &UserInputWidget::responseReceived,
                            &loop, [&loop, &accepted, &text](bool ok, QString inputText) {
            accepted = ok;
            text     = inputText;
            loop.quit();
        });
        loop.exec();
        disconnect(conn);

        setCurrentIndex(m_idxDefault);
    }, Qt::BlockingQueuedConnection);

    return {accepted, text};
}

UserInteractionOverlay::RetraceEditResult
UserInteractionOverlay::RequestRetraceEdit(const QString& deviceName,
                                            const QString& positionName,
                                            double origX, double origY, double origZ,
                                            double origU, double origV, double origW,
                                            double currX, double currY, double currZ,
                                            double currU, double currV, double currW,
                                            int currentPosition, int totalPositions)
{
    RetraceEditResult result;

    QMetaObject::invokeMethod(this, [&]() {
        setCurrentIndex(m_idxRetraceEdit);
        m_retraceEditWidget->showRetraceEdit(
            deviceName, positionName,
            origX, origY, origZ, origU, origV, origW,
            currX, currY, currZ, currU, currV, currW,
            currentPosition, totalPositions);

        QEventLoop loop;
        auto conn = connect(m_retraceEditWidget, &RetraceEditWidget::responseReceived,
                            &loop, [&loop, &result](bool continueExec, bool save) {
            result.continueExecution = continueExec;
            result.save              = save;
            loop.quit();
        });
        loop.exec();
        disconnect(conn);

        setCurrentIndex(m_idxDefault);
    }, Qt::BlockingQueuedConnection);

    return result;
}

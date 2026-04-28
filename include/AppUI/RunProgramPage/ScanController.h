// ScanController.h
// Owns the ScanExecutor and the wiring between the ScanControlWidget on
// the Run Program page's "Scanning" tab and that executor. The page
// keeps the widget (it's a tab body) and the status badge; this
// controller only coordinates "user clicks Scan -> executor runs ->
// finishes -> show result".
//
// The page provides via Deps:
//   - dialogParent: parent widget for QMessageBox dialogs
//   - context: managers needed by the executor (motion, datastore, ...)
//   - widget:  the ScanControlWidget the controller should drive
//   - appendLog: callback for surfacing executor messages in the page's
//                execution log pane
#pragma once

#include "ScanControlWidget.h"

#include <QObject>
#include <QString>
#include <functional>

class ApplicationContext;
class ScanExecutor;
class QWidget;

class ScanController : public QObject {
    Q_OBJECT
public:
    struct Deps {
        QWidget*            dialogParent = nullptr;
        ApplicationContext* context      = nullptr;
        ScanControlWidget*  widget       = nullptr;
        std::function<void(const QString&)> appendLog;
    };

    explicit ScanController(Deps deps, QObject* parent = nullptr);
    ~ScanController() override;

    ScanExecutor* executor() const { return m_executor; }

private slots:
    void onScanRequested(QString deviceName,
                         ScanControlWidget::ScanMode mode,
                         QString channel);
    void onStopScanRequested();
    void onScanLogMessage(QString message);
    void onScanCompleted(QString deviceName, double peakValue, bool success);
    void onScanError(QString errorMessage);

private:
    Deps m_deps;
    ScanExecutor* m_executor = nullptr;
};

// ScanController.cpp
#include "ScanController.h"

#include "ApplicationContext.h"
#include "ScanExecutor.h"
#include "DataStore.h"
#include "LogManager.h"
#include "motions/unified_motion_manager.h"
#include "safety/SafetyWatchManager.h"
#include "common/ScanActivityTracker.h"

#include <QMessageBox>

ScanController::ScanController(Deps deps, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(deps))
{
    // Build the executor and wire its dependencies from the application context.
    m_executor = new ScanExecutor(this);
    if (m_deps.context) {
        m_executor->setMotionManager(m_deps.context->motionManager);
        m_executor->setDataStore(m_deps.context->dataStore);
        if (m_deps.context->logManager) {
            m_executor->setLogger(m_deps.context->logManager);
        }
        if (m_deps.context->safetyWatchManager) {
            m_executor->setSafetyWatchManager(m_deps.context->safetyWatchManager);
        }
        // Wire the scan-busy tracker so tab-initiated scans light up the
        // TopBar indicator.
        m_executor->setScanActivityTracker(m_deps.context->scanActivityTracker);
    }

    // Hook scan widget user actions through the controller.
    if (m_deps.widget) {
        connect(m_deps.widget, &ScanControlWidget::scanRequested,
                this, &ScanController::onScanRequested);
        connect(m_deps.widget, &ScanControlWidget::stopRequested,
                this, &ScanController::onStopScanRequested);
    }

    // Surface executor signals into the controller's slots.
    connect(m_executor, &ScanExecutor::logMessage,
            this, &ScanController::onScanLogMessage);
    connect(m_executor, &ScanExecutor::scanCompleted,
            this, &ScanController::onScanCompleted);
    connect(m_executor, &ScanExecutor::scanError,
            this, &ScanController::onScanError);
}

ScanController::~ScanController() = default;

void ScanController::onScanRequested(QString deviceName,
                                     ScanControlWidget::ScanMode mode,
                                     QString channel)
{
    if (channel.isEmpty()) {
        QMessageBox::warning(m_deps.dialogParent, "Scan Error",
                             "Please select a data channel");
        return;
    }
    if (!m_deps.context || !m_deps.context->motionManager ||
        !m_deps.context->dataStore) {
        QMessageBox::warning(m_deps.dialogParent, "Scan Error",
                             "Motion manager or data store not available");
        return;
    }

    if (m_deps.widget) {
        m_deps.widget->setScanButtonsEnabled(false);
        m_deps.widget->setStopButtonEnabled(true);
    }

    // Map widget ScanMode -> executor ScanMode (same values, distinct enums).
    ScanMode execMode;
    switch (mode) {
        case ScanControlWidget::ScanMode::Coarse:    execMode = ScanMode::Coarse;    break;
        case ScanControlWidget::ScanMode::Fine:      execMode = ScanMode::Fine;      break;
        case ScanControlWidget::ScanMode::UltraFine: execMode = ScanMode::UltraFine; break;
    }

    bool success = m_executor->executeScan(deviceName, execMode, channel);
    if (!success && m_deps.widget) {
        m_deps.widget->setScanButtonsEnabled(true);
        m_deps.widget->setStopButtonEnabled(false);
    }
}

void ScanController::onStopScanRequested()
{
    if (m_deps.appendLog) {
        m_deps.appendLog("Stopping scan - returning to global peak position...");
    }
    m_executor->stopScan();
    if (m_deps.widget) {
        m_deps.widget->setScanButtonsEnabled(true);
        m_deps.widget->setStopButtonEnabled(false);
    }
    if (m_deps.appendLog) {
        m_deps.appendLog("Scan stopped - device returned to peak position");
    }
}

void ScanController::onScanLogMessage(QString message)
{
    if (m_deps.appendLog) m_deps.appendLog(message);
}

void ScanController::onScanCompleted(QString deviceName, double peakValue, bool success)
{
    if (m_deps.widget) {
        m_deps.widget->setScanButtonsEnabled(true);
        m_deps.widget->setStopButtonEnabled(false);
    }

    if (success) {
        QMessageBox::information(m_deps.dialogParent, "Scan Complete",
            QString("Scan completed on %1\nPeak value: %2")
                .arg(deviceName)
                .arg(peakValue, 0, 'f', 6));
    } else {
        QMessageBox::warning(m_deps.dialogParent, "Scan Complete",
            QString("Scan completed on %1, but no peak found").arg(deviceName));
    }
}

void ScanController::onScanError(QString errorMessage)
{
    if (m_deps.widget) {
        m_deps.widget->setScanButtonsEnabled(true);
        m_deps.widget->setStopButtonEnabled(false);
    }
    QMessageBox::critical(m_deps.dialogParent, "Scan Error", errorMessage);
}

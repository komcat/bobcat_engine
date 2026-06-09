// GlobalMaximizeController.cpp
#include "GlobalMaximizeController.h"

#include "GlobalMaximizeWidget.h"
#include "GlobalMaximizeExecutor.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "DataStore.h"
#include "motions/unified_motion_manager.h"

#include <QMessageBox>

GlobalMaximizeController::GlobalMaximizeController(Deps deps, QObject* parent)
  : QObject(parent)
  , m_deps(std::move(deps)) {
  // Build the executor and wire its dependencies from the application context.
  m_executor = new GlobalMaximizeExecutor(this);
  if (m_deps.context) {
    m_executor->setMotionManager(m_deps.context->motionManager);
    m_executor->setDataStore(m_deps.context->dataStore);
    if (m_deps.context->logManager) {
      m_executor->setLogger(m_deps.context->logManager);
    }
  }

  // Hook widget user actions through the controller.
  if (m_deps.widget) {
    connect(m_deps.widget, &GlobalMaximizeWidget::startRequested,
            this, &GlobalMaximizeController::onStartRequested);
    connect(m_deps.widget, &GlobalMaximizeWidget::stopRequested,
            this, &GlobalMaximizeController::onStopRequested);
  }

  // Surface executor signals into the controller's slots.
  connect(m_executor, &GlobalMaximizeExecutor::logMessage,
          this, &GlobalMaximizeController::onLogMessage);
  connect(m_executor, &GlobalMaximizeExecutor::completed,
          this, &GlobalMaximizeController::onCompleted);
  connect(m_executor, &GlobalMaximizeExecutor::errorOccurred,
          this, &GlobalMaximizeController::onError);
}

GlobalMaximizeController::~GlobalMaximizeController() = default;

void GlobalMaximizeController::onStartRequested() {
  if (!m_deps.widget) {
    return;
  }

  const QString channel = m_deps.widget->currentChannel();
  if (channel.isEmpty()) {
    QMessageBox::warning(m_deps.dialogParent, "Global Maximize",
                         "Please select a data channel");
    return;
  }
  if (!m_deps.context || !m_deps.context->motionManager ||
      !m_deps.context->dataStore) {
    QMessageBox::warning(m_deps.dialogParent, "Global Maximize",
                         "Motion manager or data store not available");
    return;
  }

  m_deps.widget->setRunningState(true);

  GlobalMaximizeExecutor::Params params;
  params.channel     = channel;
  params.zStepDevice = m_deps.widget->zStepDevice();
  params.gridDevice  = m_deps.widget->gridDevice();
  params.zStepMm     = m_deps.widget->zStepMm();
  params.gridStepMm  = m_deps.widget->gridStepMm();
  params.gridSize    = m_deps.widget->gridSize();
  params.settleMs    = m_deps.widget->settleMs();

  const bool ok = m_executor->execute(params);
  if (!ok) {
    m_deps.widget->setRunningState(false);
  }
}

void GlobalMaximizeController::onStopRequested() {
  if (m_deps.appendLog) {
    m_deps.appendLog("Stopping global maximize - returning to session peak...");
  }
  m_executor->stop();
  if (m_deps.widget) {
    m_deps.widget->setRunningState(false);
  }
}

void GlobalMaximizeController::onLogMessage(QString message) {
  if (m_deps.appendLog) {
    m_deps.appendLog(message);
  }
}

void GlobalMaximizeController::onCompleted(double peakValue, bool success) {
  if (m_deps.widget) {
    m_deps.widget->setRunningState(false);
  }
  if (success) {
    QMessageBox::information(m_deps.dialogParent, "Global Maximize Complete",
      QString("Routine complete\nPeak value: %1").arg(peakValue, 0, 'f', 6));
  } else {
    QMessageBox::warning(m_deps.dialogParent, "Global Maximize",
      "Routine finished, but no valid peak was found");
  }
}

void GlobalMaximizeController::onError(QString errorMessage) {
  if (m_deps.widget) {
    m_deps.widget->setRunningState(false);
  }
  QMessageBox::critical(m_deps.dialogParent, "Global Maximize Error", errorMessage);
}

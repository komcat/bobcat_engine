// GlobalMaximizeExecutor.cpp
#include "GlobalMaximizeExecutor.h"

#include "DualHexMaximizer.h"
#include "unified_motion_manager.h"
#include "DataStore.h"
#include "ILogger.h"

#include <QMetaObject>

GlobalMaximizeExecutor::GlobalMaximizeExecutor(QObject* parent)
  : QObject(parent) {
}

GlobalMaximizeExecutor::~GlobalMaximizeExecutor() {
  cleanup();
}

void GlobalMaximizeExecutor::setMotionManager(UnifiedMotionManager* manager) {
  m_motionManager = manager;
}

void GlobalMaximizeExecutor::setDataStore(DataStore* dataStore) {
  m_dataStore = dataStore;
}

void GlobalMaximizeExecutor::setLogger(ILogger* logger) {
  m_logger = logger;
}

bool GlobalMaximizeExecutor::execute(const Params& params) {
  if (!m_motionManager || !m_dataStore) {
    emit errorOccurred("Motion manager or data store not available");
    return false;
  }
  if (params.channel.isEmpty()) {
    emit errorOccurred("No data channel selected");
    return false;
  }

  if (m_maximizer) {
    cleanup();
  }

  m_maximizer = new DualHexMaximizer(*m_motionManager, *m_dataStore);
  if (m_logger) {
    m_maximizer->SetLogger(m_logger);
  }

  DualHexMaximizeParams p;
  p.channel     = params.channel.toStdString();
  p.zStepDevice = params.zStepDevice.toStdString();
  p.gridDevice  = params.gridDevice.toStdString();
  p.zStepMm     = params.zStepMm;
  p.gridStepMm  = params.gridStepMm;
  p.gridSize    = params.gridSize;
  p.settleMs    = params.settleMs;

  setupCallbacks();

  const bool ok = m_maximizer->Start(p);
  if (!ok) {
    cleanup();
    return false;
  }
  return true;
}

bool GlobalMaximizeExecutor::isActive() const {
  return m_maximizer != nullptr && m_maximizer->IsActive();
}

void GlobalMaximizeExecutor::stop() {
  if (m_maximizer && m_maximizer->IsActive()) {
    emit logMessage("Stopping global maximize - returning to session peak...");
    m_maximizer->Halt();  // blocks until return-to-peak completes
  }
  cleanup();
}

void GlobalMaximizeExecutor::setupCallbacks() {
  m_maximizer->SetProgressCallback(
    [this](const DualHexProgressEvent& event) {
      QMetaObject::invokeMethod(this, [this, event]() {
        emit logMessage(QString::fromStdString(event.message));
        emit progress(event.progress);
      }, Qt::QueuedConnection);
    });

  m_maximizer->SetCompletionCallback(
    [this](const DualHexResult& result) {
      QMetaObject::invokeMethod(this, [this, result]() {
        if (result.halted) {
          emit logMessage("Global maximize stopped - device at session peak");
        } else if (result.success) {
          emit logMessage(QString("Global maximize complete - peak value: %1 (%2 iterations)")
            .arg(result.peakValue, 0, 'f', 6)
            .arg(result.iterations));
        } else {
          emit logMessage("Global maximize finished - no valid peak found");
        }
        emit completed(result.peakValue, result.success);
        cleanup();
      }, Qt::QueuedConnection);
    });

  m_maximizer->SetErrorCallback(
    [this](const std::string& message) {
      QMetaObject::invokeMethod(this, [this, message]() {
        emit errorOccurred(QString::fromStdString(message));
        cleanup();
      }, Qt::QueuedConnection);
    });
}

void GlobalMaximizeExecutor::cleanup() {
  if (m_maximizer) {
    delete m_maximizer;  // destructor halts + joins the worker thread
    m_maximizer = nullptr;
  }
}

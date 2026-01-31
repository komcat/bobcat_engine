// SequenceWorker.cpp
#include "SequenceWorker.h"
#include "SequenceEngine.h"
#include "SequenceBuilder.h"
#include "SequenceContext.h"

SequenceWorker::SequenceWorker(
  SequenceEngine* engine,
  SequenceBuilder* sequence,
  QObject* parent)
  : QThread(parent)
  , m_engine(engine)
  , m_sequence(sequence)
  , m_stopRequested(false)
{
}

SequenceWorker::~SequenceWorker()
{
  // Clean up sequence if not already deleted
  if (m_sequence) {
    delete m_sequence;
    m_sequence = nullptr;
  }
}

void SequenceWorker::requestStop()
{
  m_stopRequested.store(true);
  emit logMessage("Sequence stop requested");

  // Forward stop request to SequenceEngine
  if (m_engine) {
    m_engine->RequestStop();
    emit logMessage("Stop forwarded to SequenceEngine");
  }
}

void SequenceWorker::run()
{
  try {
    emit executionStarted();
    emit logMessage("Starting sequence execution in background thread");

    // Check stop flag before starting
    if (m_stopRequested.load()) {
      emit executionStopped();
      emit logMessage("Execution cancelled before start");
      delete m_sequence;
      m_sequence = nullptr;
      return;
    }

    // Create execution context
    SequenceContext context;

    // Execute sequence (blocking in worker thread - UI stays responsive)
    bool success = m_engine->ExecuteSequence(m_sequence, &context);

    // Clean up sequence after execution
    delete m_sequence;
    m_sequence = nullptr;

    // Check if stopped
    if (m_stopRequested.load() || context.IsStopRequested()) {
      emit executionStopped();
      emit logMessage("Execution stopped by user");
      return;
    }

    // Report result
    if (success) {
      emit executionCompleted(true, "");
      emit logMessage("Sequence completed successfully");
    } else {
      emit executionCompleted(false, "Sequence execution failed - check console output");
      emit logMessage("Sequence execution failed");
    }

  } catch (const std::exception& e) {
    emit logMessage(QString("Exception during execution: %1").arg(e.what()));
    emit executionCompleted(false, QString("Exception: %1").arg(e.what()));

    // Clean up on exception
    if (m_sequence) {
      delete m_sequence;
      m_sequence = nullptr;
    }
  }
}

// SequenceWorker.h
#pragma once

#include <QThread>
#include <QString>
#include <atomic>

class SequenceBuilder;
class SequenceEngine;
class SequenceContext;

/**
 * @brief Worker thread for executing pre-built sequences asynchronously
 *
 * Unlike RecipeWorker (which loads recipes from files), this executes
 * sequences that are already constructed. Perfect for:
 * - Graph path execution
 * - UI-triggered motion sequences
 * - Any pre-built sequence that needs async execution
 *
 * Usage:
 *   SequenceBuilder* seq = engine->CreateSequence("MyPath");
 *   seq->AddAction(...);
 *
 *   SequenceWorker* worker = new SequenceWorker(engine, seq);
 *   connect(worker, &SequenceWorker::executionCompleted, ...);
 *   worker->start();
 */
class SequenceWorker : public QThread
{
  Q_OBJECT

public:
  /**
   * @brief Construct a sequence worker
   * @param engine SequenceEngine for execution
   * @param sequence Pre-built sequence to execute (worker takes ownership)
   * @param parent QObject parent
   */
  SequenceWorker(
    SequenceEngine* engine,
    SequenceBuilder* sequence,
    QObject* parent = nullptr
  );

  ~SequenceWorker();

  void requestStop();  // Request graceful stop

signals:
  void executionStarted();
  void executionCompleted(bool success, QString errorMessage);
  void executionStopped();
  void logMessage(QString message);

protected:
  void run() override;

private:
  SequenceEngine* m_engine;
  SequenceBuilder* m_sequence;
  std::atomic<bool> m_stopRequested;
};

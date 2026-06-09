// GlobalMaximizeExecutor.h
#pragma once

#include <QObject>
#include <QString>

class DualHexMaximizer;
class UnifiedMotionManager;
class DataStore;
class ILogger;

/**
 * @brief Qt wrapper around DualHexMaximizer for the dual-hex global maximize.
 *
 * Mirrors ScanExecutor: owns the (non-Qt) algorithm object, builds its
 * parameters from UI-friendly values, and marshals the algorithm's worker-thread
 * callbacks back onto the Qt thread as signals. Contains no UI.
 */
class GlobalMaximizeExecutor : public QObject {
  Q_OBJECT

public:
  /// UI-level parameters; device roles already resolved from beam direction.
  struct Params {
    QString channel;
    QString zStepDevice;   ///< collimating device (steps -Z)
    QString gridDevice;    ///< search device (XY grid)
    double  zStepMm    = 0.002;
    double  gridStepMm = 0.0002;
    int     gridSize   = 3;
    int     settleMs   = 200;
  };

  explicit GlobalMaximizeExecutor(QObject* parent = nullptr);
  ~GlobalMaximizeExecutor() override;

  void setMotionManager(UnifiedMotionManager* manager);
  void setDataStore(DataStore* dataStore);
  void setLogger(ILogger* logger);

  /// Start the routine. Returns true if it started successfully.
  bool execute(const Params& params);

  bool isActive() const;
  void stop();

signals:
  void logMessage(QString message);
  void completed(double peakValue, bool success);
  void errorOccurred(QString message);
  void progress(double value);

private:
  void setupCallbacks();
  void cleanup();

  UnifiedMotionManager* m_motionManager = nullptr;
  DataStore*            m_dataStore     = nullptr;
  ILogger*              m_logger        = nullptr;

  DualHexMaximizer* m_maximizer = nullptr;
};

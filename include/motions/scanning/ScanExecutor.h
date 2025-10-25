// ScanExecutor.h
#pragma once

#include <QObject>
#include <QString>
#include <string>
#include <memory>

// Forward declarations
class AdaptiveScanController;
class UnifiedMotionManager;
class DataStore;
class ILogger;
struct ScanParameters;
struct ScanResults;
struct ScanProgressEvent;
struct ScanError;

/**
 * @brief Handles all hexapod scanning business logic
 *
 * Responsibilities:
 * - Creates and manages AdaptiveScanController instances
 * - Configures scan parameters
 * - Sets up callbacks and signal routing
 * - Executes scans and handles cleanup
 *
 * Does NOT contain any UI - purely business logic
 */
class ScanExecutor : public QObject
{
  Q_OBJECT

public:
  explicit ScanExecutor(QObject* parent = nullptr);
  ~ScanExecutor();

  /**
   * @brief Set required dependencies
   * Must be called before executing scans
   */
  void setMotionManager(UnifiedMotionManager* manager);
  void setDataStore(DataStore* dataStore);
  void setLogger(ILogger* logger);

  /**
   * @brief Execute a hexapod scan
   * @param deviceName Device to scan (e.g. "hex-left", "hex-right")
   * @param isCoarse True for coarse scan (10→5→2→1 µm), false for fine (2→1→0.5 µm)
   * @param channel Data channel to optimize
   * @return True if scan started successfully
   */
  bool executeScan(const QString& deviceName, bool isCoarse, const QString& channel);

  /**
   * @brief Check if a scan is currently active
   */
  bool isScanActive() const;

  /**
   * @brief Stop any active scan
   */
  void stopScan();

signals:
  /**
   * @brief Emitted for progress updates and informational messages
   */
  void logMessage(QString message);

  /**
   * @brief Emitted when scan completes successfully
   */
  void scanCompleted(QString deviceName, double peakValue, bool success);

  /**
   * @brief Emitted when scan encounters an error
   */
  void scanError(QString errorMessage);

  /**
   * @brief Emitted for progress percentage (0.0 - 1.0)
   */
  void scanProgress(double progress);

private:
  ScanParameters createScanParameters(bool isCoarse) const;
  void setupCallbacks(const QString& deviceName);
  void cleanup();

  // Dependencies
  UnifiedMotionManager* m_motionManager;
  DataStore* m_dataStore;
  ILogger* m_logger;

  // Active scan controller (created on-demand)
  AdaptiveScanController* m_scanController;
};
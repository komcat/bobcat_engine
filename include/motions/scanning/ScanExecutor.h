// ScanExecutor.h
#pragma once

#include <QObject>
#include <QString>
#include <string>
#include <memory>

// Forward declarations
class AdaptiveScanController;
class RotationScanController;
class UnifiedMotionManager;
class DataStore;
class ILogger;
class SafetyWatchManager;
class ScanActivityTracker;
struct ScanParameters;
struct ScanResults;
struct ScanProgressEvent;
struct ScanError;
struct RotationScanParameters;
struct RotationScanResults;
enum class RotationAxis;

// Scan mode enumeration
enum class ScanMode { Coarse, Fine, UltraFine };

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
  void setSafetyWatchManager(SafetyWatchManager* safetyManager);
  /// Wire the scan-busy broadcaster (drives TopBar indicator).
  /// Null is OK — scan operations simply won't broadcast.
  void setScanActivityTracker(ScanActivityTracker* tracker);

  /**
   * @brief Execute a hexapod translation scan
   * @param deviceName Device to scan (e.g. "hex-left", "hex-right")
   * @param mode Scan mode: Coarse (5→2→1→0.5 µm), Fine (2→1→0.5 µm), UltraFine (0.5→0.2→0.1 µm)
   * @param channel Data channel to optimize
   * @return True if scan started successfully
   */
  bool executeScan(const QString& deviceName, ScanMode mode, const QString& channel);

  /**
   * @brief Execute a rotation optimization scan
   * @param deviceName Device to scan (e.g. "hex-right")
   * @param rotationAxis Rotation axis: "U" (yaw), "V" (pitch), or "W" (roll)
   * @param rotationStep Rotation step size in degrees (e.g., 0.2)
   * @param threshold Improvement threshold percentage (e.g., 0.01 = 1%)
   * @param maxNoImprovement Max consecutive rotations without improvement
   * @param channel Data channel to optimize
   * @return True if scan started successfully
   */
  bool executeRotationScan(const QString& deviceName,
                           const QString& rotationAxis,
                           double rotationStep,
                           double threshold,
                           int maxNoImprovement,
                           const QString& channel);

  /**
   * @brief Check if a scan is currently active
   */
  bool isScanActive() const;

  /**
   * @brief Check if a rotation scan is currently active
   */
  bool isRotationScanActive() const;

  /**
   * @brief Stop any active scan (translation or rotation)
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

  /**
   * @brief Emitted when rotation scan completes
   */
  void rotationScanCompleted(QString deviceName, double peakValue, double totalRotation, bool success);

private:
  ScanParameters createScanParameters(ScanMode mode) const;
  RotationScanParameters createRotationScanParameters(
    const QString& rotationAxis,
    double rotationStep,
    double threshold,
    int maxNoImprovement) const;
  void setupCallbacks(const QString& deviceName);
  void setupRotationCallbacks(const QString& deviceName);
  void cleanup();
  void cleanupRotation();

  // Dependencies
  UnifiedMotionManager* m_motionManager;
  DataStore* m_dataStore;
  ILogger* m_logger;
  SafetyWatchManager* m_safetyWatchManager;
  ScanActivityTracker* m_scanActivityTracker = nullptr;

  // Active scan controllers (created on-demand)
  AdaptiveScanController* m_scanController;
  RotationScanController* m_rotationScanController;

  // Scan-busy tracking: guards pair Begin/End around executeScan() and
  // executeRotationScan() so the TopBar indicator stays on from kickoff to
  // completion/error/cleanup. Tracked here (rather than via RAII scope) because
  // the scan is started non-blocking and completion arrives via a callback.
  bool m_scanBusyActive = false;
  bool m_rotationBusyActive = false;
  void beginScanBusy(const char* source);
  void endScanBusy(const char* source);
};
// ScanExecutor.cpp
#include "ScanExecutor.h"
#include "AdaptiveScanController.h"
#include "ScanTypes.h"
#include "unified_motion_manager.h"
#include "DataStore.h"
#include "ILogger.h"

#include <QMetaObject>
#include <iostream>

ScanExecutor::ScanExecutor(QObject* parent)
  : QObject(parent)
  , m_motionManager(nullptr)
  , m_dataStore(nullptr)
  , m_logger(nullptr)
  , m_scanController(nullptr)
{
}

ScanExecutor::~ScanExecutor()
{
  cleanup();
}

void ScanExecutor::setMotionManager(UnifiedMotionManager* manager)
{
  m_motionManager = manager;
}

void ScanExecutor::setDataStore(DataStore* dataStore)
{
  m_dataStore = dataStore;
}

void ScanExecutor::setLogger(ILogger* logger)
{
  m_logger = logger;
}

bool ScanExecutor::executeScan(const QString& deviceName, bool isCoarse, const QString& channel)
{
  // Validate dependencies
  if (!m_motionManager || !m_dataStore) {
    emit scanError("Motion manager or data store not available");
    return false;
  }

  if (channel.isEmpty()) {
    emit scanError("No data channel selected");
    return false;
  }

  // Clean up any existing scan
  if (m_scanController) {
    cleanup();
  }

  // Create scan controller
  m_scanController = new AdaptiveScanController(
    *m_motionManager,
    *m_dataStore,
    deviceName.toStdString(),
    channel.toStdString()
  );

  // Set logger if available
  if (m_logger) {
    m_scanController->SetLogger(m_logger);
  }

  // Create scan parameters
  ScanParameters params = createScanParameters(isCoarse);

  // Set up callbacks
  setupCallbacks(deviceName);

  // Log scan start
  emit logMessage(QString("Starting %1 scan on %2")
    .arg(isCoarse ? "COARSE" : "FINE")
    .arg(deviceName));
  emit logMessage(QString("  Scanning axes: Z, Y, X with steps: %1")
    .arg(isCoarse ? "10→5→2→1 µm" : "2→1→0.5 µm"));
  emit logMessage(QString("  Data channel: %1").arg(channel));

  // Start the scan
  bool success = m_scanController->StartScan(params);

  if (!success) {
    emit logMessage("Failed to start scan");
    emit scanError("Failed to start scan");
    cleanup();
    return false;
  }

  return true;
}

bool ScanExecutor::isScanActive() const
{
  return m_scanController != nullptr && m_scanController->IsScanActive();
}

void ScanExecutor::stopScan()
{
  if (m_scanController && m_scanController->IsScanActive()) {
    m_scanController->HaltScan();
    emit logMessage("Scan stopped by user");
  }
  cleanup();
}

ScanParameters ScanExecutor::createScanParameters(bool isCoarse) const
{
  ScanParameters params;

  // Hexapod scanning: Scan all 3 axes (Z, Y, X) consecutively
  params.axesToScan = { "Z", "Y", "X" };

  // Set step sizes (convert µm to mm)
  if (isCoarse) {
    // Coarse: 10, 5, 2, 1 µm = 0.010, 0.005, 0.002, 0.001 mm
    params.stepSizes = { 0.010, 0.005, 0.002, 0.001 };
  }
  else {
    // Fine: 2, 1, 0.5 µm = 0.002, 0.001, 0.0005 mm
    params.stepSizes = { 0.002, 0.001, 0.0005 };
  }

  // Motion parameters
  params.motionSettleTimeMs = 400;
  params.velocityPercent = 50.0;

  // Termination criteria (±100µm = 0.2mm total range)
  params.consecutiveDecreasesLimit = 3;
  params.significantDecreaseThreshold = 0.05;  // 5%
  params.maxTotalDistance = 0.2;  // 200µm = 0.2mm per direction

  // Position optimization
  params.improvementThreshold = 0.01;  // 1%

  // Measurement parameters
  params.averagingCount = 3;
  params.measurementTimeout = std::chrono::seconds(5);
  params.minValidValue = -1000.0;  // Adjust based on your sensor
  params.maxValidValue = 1000.0;

  // Safety limits
  params.maxStepSize = 0.5;  // 500µm max
  params.scanTimeout = std::chrono::minutes(10);

  return params;
}

void ScanExecutor::setupCallbacks(const QString& deviceName)
{
  // Progress callback
  m_scanController->SetProgressCallback(
    [this](const ScanProgressEvent& event) {
    QMetaObject::invokeMethod(this, [this, event]() {
      emit logMessage(QString("Scan: %1% - %2")
        .arg(event.progress * 100, 0, 'f', 1)
        .arg(QString::fromStdString(event.message)));
      emit scanProgress(event.progress);
    }, Qt::QueuedConnection);
  });

  // Completion callback
  m_scanController->SetCompletionCallback(
    [this, deviceName](const ScanResults& results) {
    QMetaObject::invokeMethod(this, [this, deviceName, results]() {
      if (results.globalPeak.valid) {
        emit logMessage(QString("✓ Scan complete on %1 - Peak value: %2")
          .arg(deviceName)
          .arg(results.globalPeak.value, 0, 'f', 6));
        emit scanCompleted(deviceName, results.globalPeak.value, true);
      }
      else {
        emit logMessage(QString("✗ Scan complete on %1 - No peak found")
          .arg(deviceName));
        emit scanCompleted(deviceName, 0.0, false);
      }

      cleanup();
    }, Qt::QueuedConnection);
  });

  // Error callback
  m_scanController->SetErrorCallback(
    [this](const ScanError& error) {
    QMetaObject::invokeMethod(this, [this, error]() {
      QString errorMsg = QString::fromStdString(error.message);
      emit logMessage(QString("ERROR: %1").arg(errorMsg));
      emit scanError(errorMsg);
      cleanup();
    }, Qt::QueuedConnection);
  });
}

void ScanExecutor::cleanup()
{
  if (m_scanController) {
    // Stop any active scan
    if (m_scanController->IsScanActive()) {
      m_scanController->HaltScan();
    }

    delete m_scanController;
    m_scanController = nullptr;

    std::cout << "[ScanExecutor] Scan controller cleaned up" << std::endl;
  }
}
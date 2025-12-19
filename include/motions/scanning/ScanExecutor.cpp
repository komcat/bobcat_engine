// ScanExecutor.cpp
#include "ScanExecutor.h"
#include "AdaptiveScanController.h"
#include "RotationScanController.h"
#include "RotationScanTypes.h"
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
  , m_rotationScanController(nullptr)
{
}

ScanExecutor::~ScanExecutor()
{
  cleanup();
  cleanupRotation();
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
    .arg(isCoarse ? "5→2→1→0.5 µm" : "2→1→0.5 µm"));
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

bool ScanExecutor::isRotationScanActive() const
{
  return m_rotationScanController != nullptr && m_rotationScanController->IsScanActive();
}

void ScanExecutor::stopScan()
{
  // Stop translation scan if active
  if (m_scanController && m_scanController->IsScanActive()) {
    emit logMessage("Stopping scan - returning to global peak position...");
    m_scanController->HaltScan();  // This blocks until return to peak is complete
    emit logMessage("Scan halted - device returned to best position found");
  }
  cleanup();

  // Stop rotation scan if active
  if (m_rotationScanController && m_rotationScanController->IsScanActive()) {
    emit logMessage("Stopping rotation scan - returning to global peak position...");
    m_rotationScanController->HaltScan();
    emit logMessage("Rotation scan halted - device returned to best position found");
  }
  cleanupRotation();
}

ScanParameters ScanExecutor::createScanParameters(bool isCoarse) const
{
  ScanParameters params;

  // Hexapod scanning: Scan all 3 axes (Z, Y, X) consecutively
  params.axesToScan = { "Z", "Y", "X" };

  // Set step sizes (convert µm to mm)
  if (isCoarse) {
    // Coarse: 5, 2, 1, 0.5 µm = 0.005, 0.002, 0.001, 0.0005 mm
    params.stepSizes = { 0.005, 0.002, 0.001, 0.0005 };
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

void ScanExecutor::cleanupRotation()
{
  if (m_rotationScanController) {
    if (m_rotationScanController->IsScanActive()) {
      m_rotationScanController->HaltScan();
    }

    delete m_rotationScanController;
    m_rotationScanController = nullptr;

    std::cout << "[ScanExecutor] Rotation scan controller cleaned up" << std::endl;
  }
}

bool ScanExecutor::executeRotationScan(const QString& deviceName,
                                        const QString& rotationAxis,
                                        double rotationStep,
                                        double threshold,
                                        int maxNoImprovement,
                                        const QString& channel)
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

  // Clean up any existing rotation scan
  if (m_rotationScanController) {
    cleanupRotation();
  }

  // Create rotation scan controller
  m_rotationScanController = new RotationScanController(
    *m_motionManager,
    *m_dataStore,
    deviceName.toStdString(),
    channel.toStdString()
  );

  // Set logger if available
  if (m_logger) {
    m_rotationScanController->SetLogger(m_logger);
  }

  // Create parameters
  RotationScanParameters params = createRotationScanParameters(
    rotationAxis, rotationStep, threshold, maxNoImprovement);

  // Set up callbacks
  setupRotationCallbacks(deviceName);

  // Log scan start
  emit logMessage(QString("Starting ROTATION scan on %1").arg(deviceName));
  emit logMessage(QString("  Rotation axis: %1, step: %2°")
    .arg(rotationAxis)
    .arg(rotationStep, 0, 'f', 2));
  emit logMessage(QString("  Threshold: %1%, max no-improvement: %2")
    .arg(threshold * 100, 0, 'f', 1)
    .arg(maxNoImprovement));
  emit logMessage(QString("  Translation axes: %1, steps: 2→1→0.5 µm")
    .arg(QString::fromStdString(
      params.GetTranslationAxes()[0] + ", " + params.GetTranslationAxes()[1])));
  emit logMessage(QString("  Data channel: %1").arg(channel));

  // Start the scan
  bool success = m_rotationScanController->StartScan(params);

  if (!success) {
    emit logMessage("Failed to start rotation scan");
    emit scanError("Failed to start rotation scan");
    cleanupRotation();
    return false;
  }

  return true;
}

RotationScanParameters ScanExecutor::createRotationScanParameters(
  const QString& rotationAxis,
  double rotationStep,
  double threshold,
  int maxNoImprovement) const
{
  RotationScanParameters params;

  // Set rotation axis
  if (rotationAxis == "U") {
    params.rotationAxis = RotationAxis::U;  // Yaw -> YZ scan
  } else if (rotationAxis == "V") {
    params.rotationAxis = RotationAxis::V;  // Pitch -> XZ scan
  } else {
    params.rotationAxis = RotationAxis::W;  // Roll -> XY scan (default)
  }

  // Rotation parameters
  params.rotationStep = rotationStep;
  params.improvementThreshold = threshold;
  params.maxNoImprovementCount = maxNoImprovement;
  params.maxRotationRange = 5.0;  // Safety limit: 5 degrees total

  // Translation scan parameters (FINE steps)
  params.translationStepSizes = { 0.002, 0.001, 0.0005 };  // 2, 1, 0.5 µm

  // Motion parameters
  params.motionSettleTimeMs = 400;
  params.velocityPercent = 50.0;

  // Translation termination criteria
  params.consecutiveDecreasesLimit = 3;
  params.significantDecreaseThreshold = 0.05;  // 5%
  params.maxTranslationDistance = 0.2;  // 200µm per direction

  // Measurement parameters
  params.averagingCount = 3;
  params.measurementTimeout = std::chrono::seconds(5);
  params.minValidValue = -1000.0;
  params.maxValidValue = 1000.0;

  // Safety
  params.scanTimeout = std::chrono::minutes(30);

  return params;
}

void ScanExecutor::setupRotationCallbacks(const QString& deviceName)
{
  // Progress callback
  m_rotationScanController->SetProgressCallback(
    [this](const RotationScanProgressEvent& event) {
    QMetaObject::invokeMethod(this, [this, event]() {
      emit logMessage(QString("Rotation: %1% - %2")
        .arg(event.overallProgress * 100, 0, 'f', 1)
        .arg(QString::fromStdString(event.message)));
      emit scanProgress(event.overallProgress);
    }, Qt::QueuedConnection);
  });

  // Completion callback
  m_rotationScanController->SetCompletionCallback(
    [this, deviceName](const RotationScanResults& results) {
    QMetaObject::invokeMethod(this, [this, deviceName, results]() {
      if (results.success && results.globalPeak.valid) {
        emit logMessage(QString("✓ Rotation scan complete on %1").arg(deviceName));
        emit logMessage(QString("  Peak value: %1").arg(results.globalPeak.value, 0, 'f', 6));
        emit logMessage(QString("  Total rotation: %1°").arg(results.totalRotation, 0, 'f', 2));

        if (results.positiveDirectionImproved) {
          emit logMessage("  Improvement found in POSITIVE direction");
        } else if (results.negativeDirectionImproved) {
          emit logMessage("  Improvement found in NEGATIVE direction");
        } else {
          emit logMessage("  No improvement found - returned to start");
        }

        emit rotationScanCompleted(deviceName, results.globalPeak.value,
                                    results.totalRotation, true);
        emit scanCompleted(deviceName, results.globalPeak.value, true);
      } else {
        emit logMessage(QString("✗ Rotation scan on %1 - %2")
          .arg(deviceName)
          .arg(results.success ? "No peak found" : "Stopped/Error"));
        emit rotationScanCompleted(deviceName, 0.0, 0.0, false);
        emit scanCompleted(deviceName, 0.0, false);
      }

      cleanupRotation();
    }, Qt::QueuedConnection);
  });

  // Error callback
  m_rotationScanController->SetErrorCallback(
    [this](const std::string& error) {
    QMetaObject::invokeMethod(this, [this, error]() {
      QString errorMsg = QString::fromStdString(error);
      emit logMessage(QString("ERROR: %1").arg(errorMsg));
      emit scanError(errorMsg);
      cleanupRotation();
    }, Qt::QueuedConnection);
  });
}
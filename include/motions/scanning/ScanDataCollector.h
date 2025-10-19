// include/scanning/ScanDataCollector.h
#pragma once

#include "ScanTypes.h"
#include "MotionTypes.h"
#include <vector>
#include <string>
#include <chrono>
#include <memory>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

/**
 * @brief Collects and manages scan data
 *
 * Responsibilities:
 * - Record baseline and measurements
 * - Track peak values
 * - Calculate statistics
 * - Save results to JSON
 */
class ScanDataCollector {
public:
  explicit ScanDataCollector(const std::string& deviceName);
  ~ScanDataCollector();

  // ============================================================================
  // RECORDING METHODS
  // ============================================================================

  /**
   * @brief Record baseline measurement
   */
  void RecordBaseline(double value, const PositionStruct& position);

  /**
   * @brief Record a measurement during scanning
   */
  void RecordMeasurement(
    double value,
    const PositionStruct& position,
    const std::string& axis,
    double stepSize,
    int direction);

  // ============================================================================
  // QUERY METHODS
  // ============================================================================

  BaselineInfo GetBaseline() const;
  PeakInfo GetPeakPosition() const;

  int GetMeasurementCount() const {
    return static_cast<int>(m_measurements.size());
  }

  const std::vector<ScanDataPoint>& GetMeasurements() const {
    return m_measurements;
  }

  /**
   * @brief Get complete scan results
   */
  ScanResults GetResults() const;

  // ============================================================================
  // SAVE RESULTS
  // ============================================================================

  /**
   * @brief Save results to JSON file
   * @return true if saved successfully
   */
  bool SaveResults() const;

  /**
   * @brief Save results to specific path
   */
  bool SaveResults(const std::filesystem::path& filepath) const;

private:
  std::string m_deviceName;
  std::string m_scanId;

  BaselineInfo m_baseline;
  PeakInfo m_currentPeak;
  std::vector<ScanDataPoint> m_measurements;

  std::chrono::system_clock::time_point m_startTime;

  // Helper methods
  void UpdatePeak(double value, const PositionStruct& position,
    const std::string& context);

  std::string GenerateScanId() const;
};



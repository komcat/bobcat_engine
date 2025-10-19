// src/scanning/ScanDataCollector.cpp
#include "ScanDataCollector.h"
#include <cmath>
#include <numeric>
#include <iomanip>
#include <sstream>
#include <algorithm>

ScanDataCollector::ScanDataCollector(const std::string& deviceName)
  : m_deviceName(deviceName)
  , m_scanId(GenerateScanId())
  , m_startTime(std::chrono::system_clock::now())
{
}

ScanDataCollector::~ScanDataCollector() {
  // Auto-save on destruction if not already saved
  SaveResults();
}

// ============================================================================
// RECORDING METHODS
// ============================================================================

void ScanDataCollector::RecordBaseline(double value, const PositionStruct& position) {
  m_baseline.value = value;
  m_baseline.position = position;
  m_baseline.timestamp = std::chrono::system_clock::now();
  m_baseline.sampleCount = 1;
  m_baseline.valid = true;

  // Initialize peak with baseline
  m_currentPeak.value = value;
  m_currentPeak.position = position;
  m_currentPeak.timestamp = m_baseline.timestamp;
  m_currentPeak.context = "Baseline Position";
  m_currentPeak.valid = true;
}

void ScanDataCollector::RecordMeasurement(
  double value,
  const PositionStruct& position,
  const std::string& axis,
  double stepSize,
  int direction) {

  ScanDataPoint dataPoint;
  dataPoint.value = value;
  dataPoint.position = position;
  dataPoint.timestamp = std::chrono::system_clock::now();
  dataPoint.axis = axis;
  dataPoint.stepSize = stepSize;
  dataPoint.direction = direction;
  dataPoint.isValid = true;

  // Calculate gradient and relative change if we have previous measurements
  if (!m_measurements.empty()) {
    const auto& prev = m_measurements.back();

    // Calculate distance moved
    double distance = 0.0;
    if (axis == "X") {
      distance = position.x - prev.position.x;
    }
    else if (axis == "Y") {
      distance = position.y - prev.position.y;
    }
    else if (axis == "Z") {
      distance = position.z - prev.position.z;
    }

    if (std::abs(distance) > 1e-10) {
      dataPoint.gradient = (value - prev.value) / distance;
    }

    if (prev.value > 0) {
      dataPoint.relativeChange = (value - prev.value) / prev.value;
    }
  }

  // Check if this is a new peak
  if (!m_currentPeak.valid || value > m_currentPeak.value) {
    dataPoint.isPeak = true;

    std::stringstream context;
    context << axis << " axis scan, "
      << (direction > 0 ? "positive" : "negative")
      << " direction, step " << std::fixed << std::setprecision(3)
      << (stepSize * 1000) << " microns";

    UpdatePeak(value, position, context.str());
  }

  m_measurements.push_back(dataPoint);
}

// ============================================================================
// QUERY METHODS
// ============================================================================

BaselineInfo ScanDataCollector::GetBaseline() const {
  return m_baseline;
}

PeakInfo ScanDataCollector::GetPeakPosition() const {
  return m_currentPeak;
}

ScanResults ScanDataCollector::GetResults() const {
  ScanResults results;

  results.deviceName = m_deviceName;
  results.scanId = m_scanId;
  results.startTime = m_startTime;
  results.endTime = std::chrono::system_clock::now();
  results.duration = std::chrono::duration_cast<std::chrono::seconds>(
    results.endTime - results.startTime);

  results.baseline = m_baseline;
  results.globalPeak = m_currentPeak;
  results.measurements = m_measurements;
  results.totalMeasurements = static_cast<int>(m_measurements.size());

  // Calculate statistics
  if (!m_measurements.empty()) {
    std::vector<double> values;
    values.reserve(m_measurements.size());

    for (const auto& m : m_measurements) {
      values.push_back(m.value);
    }

    auto [minIt, maxIt] = std::minmax_element(values.begin(), values.end());
    results.minValue = *minIt;
    results.maxValue = *maxIt;

    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    results.averageValue = sum / values.size();

    double squareSum = 0.0;
    for (double v : values) {
      squareSum += (v - results.averageValue) * (v - results.averageValue);
    }

    if (values.size() > 1) {
      results.stdDeviation = std::sqrt(squareSum / (values.size() - 1));
    }

    // Count per axis
    for (const auto& m : m_measurements) {
      results.measurementsPerAxis[m.axis]++;
    }
  }

  results.success = true;

  return results;
}

// ============================================================================
// SAVE RESULTS
// ============================================================================

bool ScanDataCollector::SaveResults() const {
  try {
    // Create logs directory
    std::filesystem::path logsPath =
      std::filesystem::current_path() / "logs" / "scanning";
    std::filesystem::create_directories(logsPath);

    // Create filename
    std::string filename = "ScanResults_" + m_deviceName + "_" + m_scanId + ".json";
    std::filesystem::path fullPath = logsPath / filename;

    return SaveResults(fullPath);
  }
  catch (const std::exception&) {
    return false;
  }
}

bool ScanDataCollector::SaveResults(const std::filesystem::path& filepath) const {
  try {
    auto results = GetResults();
    nlohmann::json j = results.ToJson();

    std::ofstream file(filepath);
    if (!file.is_open()) {
      return false;
    }

    file << std::setw(2) << j << std::endl;
    return true;

  }
  catch (const std::exception&) {
    return false;
  }
}

// ============================================================================
// HELPER METHODS
// ============================================================================

void ScanDataCollector::UpdatePeak(
  double value,
  const PositionStruct& position,
  const std::string& context) {

  m_currentPeak.value = value;
  m_currentPeak.position = position;
  m_currentPeak.timestamp = std::chrono::system_clock::now();
  m_currentPeak.context = context;
  m_currentPeak.valid = true;
}

std::string ScanDataCollector::GenerateScanId() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);

  std::stringstream ss;
  std::tm tm_buf;

#ifdef _WIN32
  localtime_s(&tm_buf, &time_t);
#else
  localtime_r(&time_t, &tm_buf);
#endif

  ss << "scan_" << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
  return ss.str();
}

// ============================================================================
// JSON SERIALIZATION
// ============================================================================

nlohmann::json ScanResults::ToJson() const {
  nlohmann::json j;

  j["deviceName"] = deviceName;
  j["scanId"] = scanId;

  // Format timestamps
  auto FormatTime = [](const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t);
#else
    localtime_r(&time_t, &tm_buf);
#endif
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
  };

  j["startTime"] = FormatTime(startTime);
  j["endTime"] = FormatTime(endTime);
  j["durationSeconds"] = duration.count();

  // Baseline
  if (baseline.valid) {
    j["baseline"]["value"] = baseline.value;
    j["baseline"]["position"] = {
      {"x", baseline.position.x},
      {"y", baseline.position.y},
      {"z", baseline.position.z}
    };
    j["baseline"]["timestamp"] = FormatTime(baseline.timestamp);
  }

  // Global peak
  if (globalPeak.valid) {
    j["globalPeak"]["value"] = globalPeak.value;
    j["globalPeak"]["position"] = {
      {"x", globalPeak.position.x},
      {"y", globalPeak.position.y},
      {"z", globalPeak.position.z}
    };
    j["globalPeak"]["timestamp"] = FormatTime(globalPeak.timestamp);
    j["globalPeak"]["context"] = globalPeak.context;
  }

  // Statistics
  j["statistics"]["totalMeasurements"] = totalMeasurements;
  j["statistics"]["minValue"] = minValue;
  j["statistics"]["maxValue"] = maxValue;
  j["statistics"]["averageValue"] = averageValue;
  j["statistics"]["standardDeviation"] = stdDeviation;
  j["statistics"]["measurementsPerAxis"] = measurementsPerAxis;

  // Measurements
  j["measurements"] = nlohmann::json::array();
  for (const auto& m : measurements) {
    nlohmann::json mj;
    mj["value"] = m.value;
    mj["position"] = {
      {"x", m.position.x},
      {"y", m.position.y},
      {"z", m.position.z}
    };
    mj["axis"] = m.axis;
    mj["stepSize"] = m.stepSize;
    mj["direction"] = m.direction;
    mj["gradient"] = m.gradient;
    mj["relativeChange"] = m.relativeChange;
    mj["isPeak"] = m.isPeak;
    j["measurements"].push_back(mj);
  }

  j["success"] = success;
  if (!errorMessage.empty()) {
    j["errorMessage"] = errorMessage;
  }

  return j;
}
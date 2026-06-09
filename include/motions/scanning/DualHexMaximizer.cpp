// DualHexMaximizer.cpp
#include "DualHexMaximizer.h"

#include "unified_motion_manager.h"
#include "DataStore.h"
#include "ILogger.h"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <cmath>

namespace {

// Snapshot of BOTH hexapods' positions at a recorded session peak. Kept
// internal to the implementation — callers only see the resulting signal.
struct DualHexSnapshot {
  PositionStruct leftPos;
  PositionStruct rightPos;
  double         signal = 0.0;
  bool           valid  = false;
};

// RAII guard: force both devices to the routine velocity on construction and
// restore their original system velocity on destruction (every exit path,
// no exceptions). SetSystemVelocity applies to all axes of a PI hexapod.
struct VelocityGuard {
  UnifiedMotionManager& motion;
  std::string devA;
  std::string devB;
  double origA = 0.0;
  double origB = 0.0;
  bool   haveA = false;
  bool   haveB = false;

  VelocityGuard(UnifiedMotionManager& m,
                const std::string& a,
                const std::string& b,
                double target)
    : motion(m), devA(a), devB(b) {
    haveA = motion.GetSystemVelocity(devA, origA);
    haveB = motion.GetSystemVelocity(devB, origB);
    motion.SetSystemVelocity(devA, target);
    motion.SetSystemVelocity(devB, target);
  }

  ~VelocityGuard() {
    if (haveA) { motion.SetSystemVelocity(devA, origA); }
    if (haveB) { motion.SetSystemVelocity(devB, origB); }
  }

  VelocityGuard(const VelocityGuard&) = delete;
  VelocityGuard& operator=(const VelocityGuard&) = delete;
};

std::string FormatMicrons(double mm) {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(3) << (mm * 1000.0);
  return ss.str();
}

std::string FormatValue(double v) {
  std::stringstream ss;
  ss << std::fixed << std::setprecision(6) << v;
  return ss.str();
}

}  // namespace

DualHexMaximizer::DualHexMaximizer(UnifiedMotionManager& motionManager,
                                   DataStore& dataStore)
  : m_motion(motionManager)
  , m_dataStore(dataStore) {
}

DualHexMaximizer::~DualHexMaximizer() {
  Halt();
}

bool DualHexMaximizer::Start(const DualHexMaximizeParams& params) {
  if (m_active.load()) {
    return false;
  }

  // Validate parameters.
  if (params.channel.empty()) {
    FireError("No data channel selected");
    return false;
  }
  if (params.zStepDevice.empty() || params.gridDevice.empty() ||
      params.zStepDevice == params.gridDevice) {
    FireError("Invalid device roles for dual-hex maximize");
    return false;
  }
  if (params.gridSize < 1 || (params.gridSize % 2) == 0) {
    FireError("Grid size must be an odd number (e.g. 3 or 5)");
    return false;
  }
  if (!m_motion.HasDevice(params.zStepDevice)) {
    FireError("Device not found: " + params.zStepDevice);
    return false;
  }
  if (!m_motion.HasDevice(params.gridDevice)) {
    FireError("Device not found: " + params.gridDevice);
    return false;
  }

  // Join any previously-finished worker before starting a new one.
  if (m_thread.joinable()) {
    m_thread.join();
  }

  m_params = params;
  m_halt.store(false);
  m_active.store(true);
  m_lastProgress = 0.0;

  m_thread = std::thread(&DualHexMaximizer::Run, this);
  return true;
}

void DualHexMaximizer::Halt() {
  m_halt.store(true);
  if (m_thread.joinable()) {
    m_thread.join();
  }
  m_halt.store(false);
}

void DualHexMaximizer::Run() {
  DualHexResult result;
  int iteration = 0;

  // Hold both hexapods at the routine velocity; restored on scope exit.
  VelocityGuard velGuard(m_motion, m_params.zStepDevice, m_params.gridDevice,
                         m_params.systemVelocity);

  Log("Dual-Hex Global Maximize starting");
  Log("  Z-stepper: " + m_params.zStepDevice + "  (-Z " +
      FormatMicrons(m_params.zStepMm) + " um/step)");
  Log("  Grid:      " + m_params.gridDevice + "  (" +
      std::to_string(m_params.gridSize) + "x" + std::to_string(m_params.gridSize) +
      " XY @ " + FormatMicrons(m_params.gridStepMm) + " um)");
  Log("  Channel:   " + m_params.channel);

  DualHexSnapshot sessionPeak;

  auto snapshot = [&](double signal) {
    DualHexSnapshot s;
    s.leftPos  = m_motion.GetCurrentPosition("hex-left");
    s.rightPos = m_motion.GetCurrentPosition("hex-right");
    s.signal   = signal;
    s.valid    = true;
    return s;
  };

  auto returnToSessionPeak = [&]() {
    if (!sessionPeak.valid) {
      return;
    }
    Log("Returning both devices to session peak (value: " +
        FormatValue(sessionPeak.signal) + ")");
    const bool okL = m_motion.MoveToAbs("hex-left",
        sessionPeak.leftPos.x, sessionPeak.leftPos.y, sessionPeak.leftPos.z,
        m_params.systemVelocity, true);
    const bool okR = m_motion.MoveToAbs("hex-right",
        sessionPeak.rightPos.x, sessionPeak.rightPos.y, sessionPeak.rightPos.z,
        m_params.systemVelocity, true);
    if (!okL || !okR) {
      Log("Warning: return-to-peak move reported failure");
    }
  };

  // --- BASELINE: grid the search device, record session peak for both. ---
  ReportProgress(0.0, "Baseline XY grid search");
  double baseline = GridSearchToPeak();
  if (m_halt.load()) {
    returnToSessionPeak();  // no-op (sessionPeak not yet valid) — stays put
    result.halted = true;
    m_active.store(false);
    if (m_completionCb) { m_completionCb(result); }
    return;
  }
  sessionPeak = snapshot(baseline);
  Log("Baseline signal: " + FormatValue(baseline));

  // --- ITERATE: -Z step on stepper, regrid search device, keep if >threshold. ---
  double cumulativeZ = 0.0;
  for (iteration = 0; iteration < m_params.maxIterations; ++iteration) {
    if (m_halt.load()) {
      break;
    }
    if (cumulativeZ + m_params.zStepMm > m_params.maxZTravelMm) {
      Log("Max Z travel reached (" + FormatMicrons(m_params.maxZTravelMm) +
          " um) - stopping");
      break;
    }

    const double prev = sessionPeak.signal;

    if (!m_motion.MoveRelative(m_params.zStepDevice, "Z", -m_params.zStepMm, true)) {
      FireError("Z step move failed on " + m_params.zStepDevice);
      break;
    }
    cumulativeZ += m_params.zStepMm;

    if (!Settle()) {
      break;  // halt requested during settle
    }

    const double newSignal = GridSearchToPeak();
    if (m_halt.load()) {
      break;
    }

    // Improvement is measured against the current session peak. When the
    // session peak is non-positive (e.g. no signal yet), any increase counts.
    bool improved;
    if (prev > 0.0) {
      improved = (newSignal > prev * (1.0 + m_params.improvementThreshold));
    } else {
      improved = (newSignal > prev);
    }

    if (improved) {
      sessionPeak = snapshot(newSignal);
      Log("Iteration " + std::to_string(iteration + 1) + ": improved -> " +
          FormatValue(newSignal));
      ReportProgress(std::min(0.95, 0.1 + 0.05 * (iteration + 1)),
                     "Iteration " + std::to_string(iteration + 1) + " improved");
    } else {
      Log("Iteration " + std::to_string(iteration + 1) + ": " +
          FormatValue(newSignal) + " <= threshold - converged");
      break;
    }
  }

  // --- FINISH: always settle both devices at the session peak. ---
  returnToSessionPeak();

  result.halted     = m_halt.load();
  result.success    = !result.halted && sessionPeak.valid;
  result.peakValue  = sessionPeak.signal;
  result.iterations = iteration;

  ReportProgress(1.0, result.halted ? "Stopped" : "Completed");

  // Velocity restored by velGuard destructor as this scope unwinds, before the
  // completion callback returns control to the caller.
  m_active.store(false);
  if (m_completionCb) {
    m_completionCb(result);
  }
}

double DualHexMaximizer::GridSearchToPeak() {
  const std::string& dev = m_params.gridDevice;
  const PositionStruct c = m_motion.GetCurrentPosition(dev);
  const int    n    = m_params.gridSize;
  const int    h    = (n - 1) / 2;
  const double step = m_params.gridStepMm;

  double bestValue = -std::numeric_limits<double>::infinity();
  double bestX = c.x;
  double bestY = c.y;

  for (int k = 0; k < n * n && !m_halt.load(); ++k) {
    const int i = (k / n) - h;
    const int j = (k % n) - h;
    const double tx = c.x + i * step;
    const double ty = c.y + j * step;

    if (!m_motion.MoveToAbs(dev, tx, ty, c.z, m_params.systemVelocity, true)) {
      Log("Grid move failed - skipping point");
      continue;
    }
    if (!Settle()) {
      break;  // halt requested
    }

    const double v = AveragedMeasurement();
    if (v > bestValue) {
      bestValue = v;
      bestX = tx;
      bestY = ty;
    }
  }

  // Move to the best XY found and re-measure there so the returned value
  // reflects the settled peak position.
  if (!m_motion.MoveToAbs(dev, bestX, bestY, c.z, m_params.systemVelocity, true)) {
    Log("Warning: move-to-peak reported failure");
  }
  SleepInterruptible(std::chrono::milliseconds(m_params.settleMs));
  return AveragedMeasurement();
}

double DualHexMaximizer::GetMeasurement() {
  const auto deadline = std::chrono::system_clock::now() + m_params.measurementTimeout;

  while (std::chrono::system_clock::now() < deadline && !m_halt.load()) {
    float value = 0.0f;
    if (m_dataStore.TryGetValue(m_params.channel, value)) {
      if (value >= m_params.minValidValue && value <= m_params.maxValidValue) {
        return static_cast<double>(value);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return 0.0;
}

double DualHexMaximizer::AveragedMeasurement() {
  const int samples = std::max(1, m_params.averagingCount);
  double sum = 0.0;
  for (int i = 0; i < samples && !m_halt.load(); ++i) {
    sum += GetMeasurement();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return sum / samples;
}

bool DualHexMaximizer::SleepInterruptible(std::chrono::milliseconds ms) {
  const auto end = std::chrono::steady_clock::now() + ms;
  while (std::chrono::steady_clock::now() < end) {
    if (m_halt.load()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return !m_halt.load();
}

bool DualHexMaximizer::Settle() {
  return SleepInterruptible(std::chrono::milliseconds(m_params.settleMs));
}

void DualHexMaximizer::Log(const std::string& msg) {
  if (m_logger) {
    m_logger->LogInfo(msg, "DualHexMaximizer");
  }
  if (m_progressCb) {
    DualHexProgressEvent ev;
    ev.progress = m_lastProgress;
    ev.message  = msg;
    m_progressCb(ev);
  }
}

void DualHexMaximizer::ReportProgress(double progress, const std::string& msg) {
  m_lastProgress = progress;
  Log(msg);
}

void DualHexMaximizer::FireError(const std::string& msg) {
  if (m_logger) {
    m_logger->LogError(msg, "DualHexMaximizer");
  }
  if (m_errorCb) {
    m_errorCb(msg);
  }
}

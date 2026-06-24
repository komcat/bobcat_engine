// RollCorrectionController.cpp
#include "RollCorrectionController.h"

#include "ApplicationContext.h"
#include "motions/unified_motion_manager.h"
#include "SequenceEngine.h"
#include "MotionCommandHandler.h"
#include "SequenceContext.h"
#include "DataStore.h"

#include <QTimer>

#include <chrono>
#include <cmath>
#include <future>
#include <map>
#include <string>
#include <thread>
#include <utility>

RollCorrectionController::RollCorrectionController(Deps deps, QObject* parent)
  : QObject(parent), m_deps(std::move(deps))
{
  if (!m_deps.widget) {
    return;
  }

  connect(m_deps.widget, &RollCorrectionWidget::registerCH1Requested,
    this, &RollCorrectionController::onRegisterCH1Requested);
  connect(m_deps.widget, &RollCorrectionWidget::registerCH2Requested,
    this, &RollCorrectionController::onRegisterCH2Requested);
  connect(m_deps.widget, &RollCorrectionWidget::moveToCH1Requested,
    this, &RollCorrectionController::onMoveToCH1Requested);
  connect(m_deps.widget, &RollCorrectionWidget::moveToCH2Requested,
    this, &RollCorrectionController::onMoveToCH2Requested);
  connect(m_deps.widget, &RollCorrectionWidget::correctWRequested,
    this, &RollCorrectionController::onCorrectWRequested);
  connect(m_deps.widget, &RollCorrectionWidget::fineGridScanRequested,
    this, &RollCorrectionController::onFineGridScanRequested);
  connect(m_deps.widget, &RollCorrectionWidget::stopScanRequested,
    this, &RollCorrectionController::onStopGridScanRequested);
  connect(m_deps.widget, &RollCorrectionWidget::clearRequested,
    this, &RollCorrectionController::onRollCorrectionClearRequested);
}

RollCorrectionController::~RollCorrectionController() = default;

void RollCorrectionController::log(const QString& msg)
{
  if (m_deps.appendLog) {
    m_deps.appendLog(msg);
  }
}

// === ROLL CORRECTION SLOTS ===

void RollCorrectionController::onRegisterCH1Requested(QString deviceName)
{
  if (!m_deps.context || !m_deps.context->motionManager) return;

  auto pos = m_deps.context->motionManager->GetCurrentPosition(deviceName.toStdString());
  m_deps.widget->setCH1Position(deviceName, pos.x, pos.y, pos.z, pos.u, pos.v, pos.w);

  log(QString("[RollCorrection] Registered CH1 peak for %1: X=%2 um")
    .arg(deviceName).arg(pos.x, 0, 'f', 4));
}

void RollCorrectionController::onRegisterCH2Requested(QString deviceName)
{
  if (!m_deps.context || !m_deps.context->motionManager) return;

  auto pos = m_deps.context->motionManager->GetCurrentPosition(deviceName.toStdString());
  m_deps.widget->setCH2Position(deviceName, pos.x, pos.y, pos.z, pos.u, pos.v, pos.w);

  log(QString("[RollCorrection] Registered CH2 peak for %1: X=%2 um")
    .arg(deviceName).arg(pos.x, 0, 'f', 4));
}

void RollCorrectionController::onMoveToCH1Requested(QString deviceName)
{
  if (!m_deps.context || !m_deps.context->motionManager) return;

  auto pos = m_deps.widget->getCH1Position(deviceName);
  if (!pos.valid) return;

  std::string dev = deviceName.toStdString();

  // Save current velocity, set to 1 mm/s for safe move
  double originalVelocity = 0.0;
  m_deps.context->motionManager->GetSystemVelocity(dev, originalVelocity);
  m_deps.context->motionManager->SetSystemVelocity(dev, 1.0);
  log(QString("[RollCorrection] %1 velocity: %2 -> 1.0 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  log(QString("[RollCorrection] Moving %1 to CH1 peak position...").arg(deviceName));

  PositionStruct target;
  target.x = pos.x; target.y = pos.y; target.z = pos.z;
  target.u = pos.u; target.v = pos.v; target.w = pos.w;
  bool ok = m_deps.context->motionManager->MoveToAbs6(dev, target, true);

  // Restore original velocity
  m_deps.context->motionManager->SetSystemVelocity(dev, originalVelocity);
  log(QString("[RollCorrection] %1 velocity restored to %2 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  log(ok ?
    QString("[RollCorrection] %1 moved to CH1 peak").arg(deviceName) :
    QString("[RollCorrection] FAILED to move %1 to CH1 peak").arg(deviceName));
}

void RollCorrectionController::onMoveToCH2Requested(QString deviceName)
{
  if (!m_deps.context || !m_deps.context->motionManager) return;

  auto pos = m_deps.widget->getCH2Position(deviceName);
  if (!pos.valid) return;

  std::string dev = deviceName.toStdString();

  // Save current velocity, set to 1 mm/s for safe move
  double originalVelocity = 0.0;
  m_deps.context->motionManager->GetSystemVelocity(dev, originalVelocity);
  m_deps.context->motionManager->SetSystemVelocity(dev, 1.0);
  log(QString("[RollCorrection] %1 velocity: %2 -> 1.0 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  log(QString("[RollCorrection] Moving %1 to CH2 peak position...").arg(deviceName));

  PositionStruct target;
  target.x = pos.x; target.y = pos.y; target.z = pos.z;
  target.u = pos.u; target.v = pos.v; target.w = pos.w;
  bool ok = m_deps.context->motionManager->MoveToAbs6(dev, target, true);

  // Restore original velocity
  m_deps.context->motionManager->SetSystemVelocity(dev, originalVelocity);
  log(QString("[RollCorrection] %1 velocity restored to %2 mm/s").arg(deviceName).arg(originalVelocity, 0, 'f', 2));

  log(ok ?
    QString("[RollCorrection] %1 moved to CH2 peak").arg(deviceName) :
    QString("[RollCorrection] FAILED to move %1 to CH2 peak").arg(deviceName));
}

void RollCorrectionController::onCorrectWRequested(QString deviceName, QString channel,
                                                   double angleDeg, bool positiveW, double halfwayXmm,
                                                   bool applyHalfwayX)
{
  // ----- Pre-flight validation (UI thread) -----
  if (!m_deps.context || !m_deps.context->motionManager ||
      !m_deps.context->sequenceEngine || !m_deps.context->dataStore) {
    log("[RollCorrection] ERROR: ApplicationContext incomplete");
    return;
  }
  if (m_correctionRunning) {
    log("[RollCorrection] ERROR: a correction is already running");
    return;
  }
  const bool bypassChannel = m_deps.widget->bypassChannelRequirement();
  if (channel.isEmpty() && !bypassChannel) {
    log("[RollCorrection] ERROR: tracking channel required - select one in the dropdown");
    return;
  }

  // Verify the selected channel still exists in DataStore (only when not bypassing)
  if (!bypassChannel) {
    const auto names = m_deps.context->dataStore->GetAllChannelNames();
    const std::string needle = channel.toStdString();
    bool found = false;
    for (const auto& n : names) {
      if (n == needle) { found = true; break; }
    }
    if (!found) {
      log(QString("[RollCorrection] ERROR: channel '%1' not present in DataStore").arg(channel));
      return;
    }
  }

  auto ch1Pos = m_deps.widget->getCH1Position(deviceName);
  if (!ch1Pos.valid) {
    log(QString("[RollCorrection] ERROR: CH1 peak not registered for %1").arg(deviceName));
    return;
  }

  auto* handler = m_deps.context->sequenceEngine->GetMotionCommandHandler();
  if (!handler) {
    log("[RollCorrection] ERROR: MotionCommandHandler unavailable");
    return;
  }

  // ----- Compute W micro-step splitting -----
  // Each W rotation is kept tiny (<= 0.02 deg) so optical power never collapses
  // into the background-noise floor between re-peaks. Power is sampled before
  // and after every micro-step; a >10% drop vs. the pre-step reading triggers a
  // 0.5 um XY regain scan (XCANExtended) to recover power before continuing.
  constexpr double kMicroStepUpper = 0.02;       // deg per micro-step (hard ceiling)
  constexpr double kPowerLossThreshold = 0.10;   // 10% drop triggers a regain scan
  const double totalAbs = std::abs(angleDeg);
  int microSteps = static_cast<int>(std::ceil(totalAbs / kMicroStepUpper));
  if (microSteps < 1) microSteps = 1;
  const double microStepAbs = totalAbs / static_cast<double>(microSteps);  // <= 0.02 deg
  const double signedMicroStep = (positiveW ? +1.0 : -1.0) * microStepAbs;

  log(QString(
    "[RollCorrection] %1: total=%2%3 deg, splitting into %4 micro-step(s) of %5 deg each (channel=%6)")
    .arg(deviceName)
    .arg(positiveW ? "+" : "-")
    .arg(angleDeg, 0, 'f', 4)
    .arg(microSteps)
    .arg(microStepAbs, 0, 'f', 4)
    .arg(bypassChannel ? QStringLiteral("<bypassed>") : channel));

  // ----- Set up running state -----
  m_correctionContext = std::make_unique<SequenceContext>();
  m_correctionRunning = true;
  m_deps.widget->setCorrectionRunning(true);

  // Snapshot values needed by the worker thread
  const std::string dev = deviceName.toStdString();
  const std::string channelStd = channel.toStdString();
  auto* mgr = m_deps.context->motionManager;
  auto* ds  = m_deps.context->dataStore;
  auto* ctx = m_correctionContext.get();
  RollCorrectionWidget::StoredPosition ch1Snapshot = ch1Pos;
  const double halfwayMm = halfwayXmm;
  const bool applyHalfwayFlag = applyHalfwayX;
  const int microStepCount = microSteps;
  const double signedStepDeg = signedMicroStep;
  const double powerLossThreshold = kPowerLossThreshold;
  const bool bypassChannelFlag = bypassChannel;

  // ----- Background worker -----
  auto future = std::async(std::launch::async,
    [handler, mgr, ds, ctx, dev, channelStd, ch1Snapshot, halfwayMm, applyHalfwayFlag,
     microStepCount, signedStepDeg, powerLossThreshold, bypassChannelFlag]()
      -> std::pair<bool, std::string>
    {
      double originalVelocity = 0.0;
      mgr->GetSystemVelocity(dev, originalVelocity);
      mgr->SetSystemVelocity(dev, 0.1);  // 0.1 mm/s safe velocity for all moves

      auto restoreAndReturn = [&](bool ok, const std::string& msg) -> std::pair<bool, std::string> {
        mgr->SetSystemVelocity(dev, originalVelocity);
        return { ok, msg };
      };

      // Step 1: Move W only to the CH1 peak's W angle (no XYZ/UV change). The
      // other axes are left wherever the operator already peaked them.
      if (!mgr->MoveToPosition(dev, "W", ch1Snapshot.w, true)) {
        return restoreAndReturn(false, "FAILED move W to CH1 peak");
      }

      if (ctx->IsStopRequested()) return restoreAndReturn(false, "stopped after CH1 W move");

      // Step 2 (optional): Halfway X shift so the W rotation pivots about the
      // CH1<->CH2 midpoint. Subtract because halfway is (ch2-ch1)/2, move opposite.
      auto curr = mgr->GetCurrentPosition(dev);
      if (applyHalfwayFlag) {
        const double newX = curr.x - halfwayMm;
        if (!mgr->MoveToPosition(dev, "X", newX, true)) {
          return restoreAndReturn(false, "FAILED halfway X move");
        }

        if (ctx->IsStopRequested()) return restoreAndReturn(false, "stopped after halfway X");
      }

      // Step 3..N: stepwise W loop. Each iteration rotates W by a tiny micro-step
      // (<= 0.02 deg) and guards optical power around the rotation:
      //   1. sample power BEFORE the rotation (rolling baseline)
      //   2. apply the W micro-step
      //   3. sample power AFTER the rotation (allowing it to settle)
      //   4. if power dropped > 10% vs. the baseline, run a 0.5 um XY regain scan
      // The fine micro-step keeps power from collapsing into the noise floor, so
      // the regain scan always has a usable gradient to climb.
      const std::map<std::string, std::string> scanParams = {
        {"axes", "XY"},
        {"step_sizes_um", "0.5"},
        {"channel", channelStd},
        {"settling_time_ms", "300"}
      };
      constexpr int kPowerSettleMs = 250;  // let the power reading settle after a W move

      // Power feedback is only possible with a real tracking channel + DataStore.
      const bool powerGuard = (!bypassChannelFlag && ds != nullptr);
      int regainScans = 0;   // number of times a regain scan was triggered
      int unrecovered = 0;   // regain scans that did not bring power back above threshold

      for (int i = 0; i < microStepCount; ++i) {
        if (ctx->IsStopRequested()) {
          return restoreAndReturn(false, "stopped at micro-step " + std::to_string(i + 1));
        }

        // (1) Capture power BEFORE the rotation — the rolling baseline for this step.
        float prePower = 0.0f;
        const bool havePre = powerGuard && ds->TryGetValue(channelStd, prePower);

        // (2) Apply one W micro-step.
        curr = mgr->GetCurrentPosition(dev);
        const double newW = curr.w + signedStepDeg;
        if (!mgr->MoveToPosition(dev, "W", newW, true)) {
          return restoreAndReturn(false, "FAILED W micro-step " + std::to_string(i + 1));
        }

        if (ctx->IsStopRequested()) {
          return restoreAndReturn(false, "stopped after W micro-step " + std::to_string(i + 1));
        }

        // Bypassed: W micro-steps still apply slowly, but no power feedback.
        if (!powerGuard) {
          continue;
        }

        // (3) Let the reading settle, then sample power AFTER the rotation.
        std::this_thread::sleep_for(std::chrono::milliseconds(kPowerSettleMs));
        float postPower = 0.0f;
        const bool havePost = ds->TryGetValue(channelStd, postPower);

        // (4) Trigger a regain scan when power dropped > 10% below the baseline.
        const bool lostPower = havePre && havePost && prePower > 0.0f &&
          postPower < static_cast<float>(1.0 - powerLossThreshold) * prePower;
        if (lostPower) {
          ++regainScans;
          std::string scanErr;
          const bool scanOk = handler->ExecuteCommand(
            mgr, dev, "XCANExtended", scanParams, scanErr, ctx);
          if (!scanOk) {
            // Hard command failure (bad params / device error) — cannot continue.
            return restoreAndReturn(false,
              "FAILED XCANExtended after W micro-step " + std::to_string(i + 1) + ": " + scanErr);
          }
          // Verify the scan actually regained power. If it did not, log a warning
          // and continue (per design decision) rather than aborting.
          float regained = 0.0f;
          if (ds->TryGetValue(channelStd, regained) &&
              regained < static_cast<float>(1.0 - powerLossThreshold) * prePower) {
            ++unrecovered;
          }
        }
      }

      std::string done = "complete (" + std::to_string(microStepCount) + " micro-steps, "
        + std::to_string(regainScans) + " regain scan(s)";
      if (unrecovered > 0) {
        done += ", WARNING: " + std::to_string(unrecovered)
          + " scan(s) did not fully regain power";
      }
      done += ")";
      return restoreAndReturn(true, done);
    });

  // ----- Poll for completion (UI thread, no blocking) -----
  auto* pollTimer = new QTimer(this);
  auto futurePtr = std::make_shared<std::future<std::pair<bool, std::string>>>(std::move(future));
  connect(pollTimer, &QTimer::timeout, this, [this, pollTimer, futurePtr, deviceName]() {
    if (futurePtr->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      pollTimer->stop();
      pollTimer->deleteLater();
      auto [ok, msg] = futurePtr->get();
      onRollCorrectionFinished(ok, deviceName, QString::fromStdString(msg));
    }
  });
  pollTimer->start(100);
}

void RollCorrectionController::onRollCorrectionFinished(bool success, QString deviceName, QString message)
{
  m_deps.widget->setCorrectionRunning(false);
  m_correctionContext.reset();
  m_correctionRunning = false;

  // Both branches log the same way today; kept distinct for future divergence.
  if (success) {
    log(QString("[RollCorrection] %1: %2").arg(deviceName, message));
  } else {
    log(QString("[RollCorrection] %1: %2").arg(deviceName, message));
  }
}

void RollCorrectionController::onFineGridScanRequested(QString deviceName)
{
  if (!m_deps.context || !m_deps.context->motionManager || !m_deps.context->sequenceEngine) return;

  auto* handler = m_deps.context->sequenceEngine->GetMotionCommandHandler();
  if (!handler) {
    log("[RollCorrection] ERROR: MotionCommandHandler not available");
    return;
  }

  // Create a fresh context for stop control
  m_gridScanContext = std::make_unique<SequenceContext>();

  // Update UI state
  m_deps.widget->setScanRunning(true);
  log(QString("[RollCorrection] Starting fine grid scan on %1...").arg(deviceName));

  // Grid Scan Dual Channel parameters matching screenshot config
  std::map<std::string, std::string> params;
  params["plane"] = "XY";
  params["range_axis1_um"] = "10";
  params["range_axis2_um"] = "10";
  params["direction_axis1"] = "centered";
  params["direction_axis2"] = "centered";
  params["step_size_um"] = "0.5";
  params["channel1"] = "SMU_SMU1S_Ch1";
  params["channel2"] = "SMU_SMU1S_Ch2";
  params["waveguide_spacing_um"] = std::to_string(static_cast<int>(m_deps.widget->spacing()));
  params["settling_time_ms"] = "300";
  params["snake_pattern"] = "true";
  params["return_to_peak"] = "channel1";

  // Run on background thread (GridScanDualChannel is blocking)
  auto* ctx = m_gridScanContext.get();
  auto* mgr = m_deps.context->motionManager;
  std::string dev = deviceName.toStdString();

  auto future = std::async(std::launch::async, [handler, mgr, dev, params, ctx]() {
    std::string errorMessage;
    bool ok = handler->ExecuteCommand(mgr, dev, "GridScanDualChannel", params, errorMessage, ctx);
    return std::make_pair(ok, errorMessage);
  });

  // Use a QTimer to poll for completion (avoid blocking UI)
  auto* pollTimer = new QTimer(this);
  auto futurePtr = std::make_shared<std::future<std::pair<bool, std::string>>>(std::move(future));
  connect(pollTimer, &QTimer::timeout, this, [this, pollTimer, futurePtr, deviceName]() {
    if (futurePtr->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      pollTimer->stop();
      pollTimer->deleteLater();
      auto [ok, errMsg] = futurePtr->get();
      onGridScanFinished(ok, deviceName, QString::fromStdString(errMsg));
    }
  });
  pollTimer->start(100);
}

void RollCorrectionController::onStopGridScanRequested()
{
  if (m_gridScanContext) {
    log("[RollCorrection] Stopping grid scan...");
    m_gridScanContext->RequestStop();
  }
  if (m_correctionContext) {
    log("[RollCorrection] Stop requested - will halt before next sub-step");
    m_correctionContext->RequestStop();
  }
}

void RollCorrectionController::onGridScanFinished(bool success, QString deviceName, QString errorMessage)
{
  m_deps.widget->setScanRunning(false);
  m_gridScanContext.reset();

  if (success) {
    log(QString("[RollCorrection] Fine grid scan completed on %1").arg(deviceName));
  } else {
    log(QString("[RollCorrection] Fine grid scan stopped/failed on %1: %2")
      .arg(deviceName, errorMessage));
  }
}

void RollCorrectionController::onRollCorrectionClearRequested(QString deviceName)
{
  m_deps.widget->clearPositions(deviceName);
  log(QString("[RollCorrection] Cleared positions for %1").arg(deviceName));
}

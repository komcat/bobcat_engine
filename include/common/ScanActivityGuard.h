// ScanActivityGuard.h
#pragma once

#include "ScanActivityTracker.h"
#include <QString>

/**
 * @brief RAII helper that calls ScanActivityTracker::Begin on construction
 *        and End on destruction.
 *
 * Use at the top of any function that represents a scan operation so the
 * tracker count is always decremented — even on early-return failure paths,
 * exception unwinds, or user-stop branches. Null tracker is OK (no-op) so
 * the guard can be used safely even before the tracker is wired into the
 * ApplicationContext.
 *
 * Example:
 *   bool MotionCommandHandler::ExecuteCommand(...) {
 *     else if (methodName == "GridScan") {
 *       ScanActivityGuard _scanBusy(m_scanActivityTracker, "GridScan");
 *       // ... many validation + early returns, all safe ...
 *     }
 *   }
 */
class ScanActivityGuard
{
public:
  ScanActivityGuard(ScanActivityTracker* tracker, const QString& source)
    : m_tracker(tracker), m_source(source)
  {
    if (m_tracker) {
      m_tracker->Begin(m_source);
    }
  }

  ~ScanActivityGuard()
  {
    if (m_tracker) {
      m_tracker->End(m_source);
    }
  }

  ScanActivityGuard(const ScanActivityGuard&) = delete;
  ScanActivityGuard& operator=(const ScanActivityGuard&) = delete;
  ScanActivityGuard(ScanActivityGuard&&) = delete;
  ScanActivityGuard& operator=(ScanActivityGuard&&) = delete;

private:
  ScanActivityTracker* m_tracker;
  QString m_source;
};

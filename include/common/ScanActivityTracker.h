// ScanActivityTracker.h
#pragma once

#include <QObject>
#include <QString>
#include <map>
#include <mutex>

/**
 * @brief Reference-counted broadcaster for "is any scan currently running?"
 *
 * Purpose: drives the TopBar scan-busy indicator so operators can see at a
 * glance that a scan (hexapod XCAN, GridScan, GridScanExtended, or
 * GridScanDualChannel) is in progress — whether initiated from the Scanning
 * tab buttons or from a recipe step.
 *
 * Multiple scan sources may legitimately overlap (e.g. two GridScan actions
 * on different devices in a parallel sequence block), so activity is tracked
 * as a reference count per-source-name. The aggregated `activeChanged(bool)`
 * signal fires only on 0↔N transitions — no signal spam while count changes
 * between 1 and 2.
 *
 * Thread safety: Begin/End may be called from any thread (scan operations
 * run on the SequenceEngine worker thread). The signal is emitted after the
 * internal mutex is released; receivers should use Qt::QueuedConnection if
 * they update UI.
 */
class ScanActivityTracker : public QObject
{
  Q_OBJECT
public:
  explicit ScanActivityTracker(QObject* parent = nullptr);
  ~ScanActivityTracker() override = default;

  /// Register a scan as starting. `source` is a short identifier such as
  /// "GridScan", "XCAN", "ScanExecutor". Safe to call multiple times with
  /// the same source (counted).
  void Begin(const QString& source);

  /// Register a scan as ending. Must be paired with a prior Begin for the
  /// same source. Defensive: clamps at zero on underflow rather than crashing,
  /// which would leave the indicator stuck on (visible) — easier to notice
  /// and fix than a crash.
  void End(const QString& source);

  /// True if any scan is currently active.
  bool IsActive() const;

  /// Total active scan count across all sources.
  int ActiveCount() const;

signals:
  /// Fired on 0↔N transitions only. `active` is true on 0→1, false on 1→0.
  void activeChanged(bool active);

private:
  mutable std::mutex m_mutex;
  std::map<QString, int> m_sourceCounts;
  int m_totalCount = 0;
};

// ScanActivityTracker.cpp
#include "ScanActivityTracker.h"

ScanActivityTracker::ScanActivityTracker(QObject* parent)
  : QObject(parent)
{
}

void ScanActivityTracker::Begin(const QString& source)
{
  bool wasInactive = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sourceCounts[source] += 1;
    wasInactive = (m_totalCount == 0);
    m_totalCount += 1;
  }
  // Emit outside the lock to prevent deadlocks if a slot re-enters.
  if (wasInactive) {
    emit activeChanged(true);
  }
}

void ScanActivityTracker::End(const QString& source)
{
  bool becameInactive = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sourceCounts.find(source);
    if (it != m_sourceCounts.end() && it->second > 0) {
      it->second -= 1;
      if (it->second == 0) {
        m_sourceCounts.erase(it);
      }
    }
    if (m_totalCount > 0) {
      m_totalCount -= 1;
      if (m_totalCount == 0) {
        becameInactive = true;
      }
    }
    // If underflow: total was 0 before decrement — leave at 0 (defensive,
    // indicates a Begin/End mismatch upstream).
  }
  if (becameInactive) {
    emit activeChanged(false);
  }
}

bool ScanActivityTracker::IsActive() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_totalCount > 0;
}

int ScanActivityTracker::ActiveCount() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_totalCount;
}

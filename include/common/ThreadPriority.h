// ThreadPriority.h
// Helper for setting thread priority on safety-critical threads.
#pragma once

#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ThreadUtil {

enum class Priority { Low, BelowNormal, Normal, AboveNormal, High, Critical };

inline bool SetCurrentThreadPriority(Priority p, const char* threadName = nullptr) {
#ifdef _WIN32
  int winPriority = THREAD_PRIORITY_NORMAL;
  const char* priorityName = "Normal";

  switch (p) {
    case Priority::Low:
      winPriority = THREAD_PRIORITY_BELOW_NORMAL;
      priorityName = "Low";
      break;
    case Priority::BelowNormal:
      winPriority = THREAD_PRIORITY_BELOW_NORMAL;
      priorityName = "BelowNormal";
      break;
    case Priority::Normal:
      winPriority = THREAD_PRIORITY_NORMAL;
      priorityName = "Normal";
      break;
    case Priority::AboveNormal:
      winPriority = THREAD_PRIORITY_ABOVE_NORMAL;
      priorityName = "AboveNormal";
      break;
    case Priority::High:
      winPriority = THREAD_PRIORITY_HIGHEST;
      priorityName = "High";
      break;
    case Priority::Critical:
      winPriority = THREAD_PRIORITY_TIME_CRITICAL;
      priorityName = "Critical";
      break;
  }

  bool ok = ::SetThreadPriority(::GetCurrentThread(), winPriority) != 0;
  if (threadName) {
    std::cout << "[ThreadPriority] " << threadName << " -> " << priorityName
              << (ok ? " (OK)" : " (FAILED)") << std::endl;
  }
  return ok;
#else
  (void)p;
  (void)threadName;
  return true;
#endif
}

} // namespace ThreadUtil

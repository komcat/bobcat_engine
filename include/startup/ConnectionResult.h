// ConnectionResult.h
#pragma once

#include <QString>
#include <chrono>

namespace Startup {

/**
 * @brief Status of a connection task
 */
enum class ConnectionStatus {
  Pending,     // Not yet started
  Connecting,  // Currently connecting
  Connected,   // Successfully connected
  Failed,      // Connection failed with error
  TimedOut,    // Connection timed out
  Cancelled    // Connection was cancelled
};

/**
 * @brief Result of a single connection task
 */
struct ConnectionResult {
  QString managerName;    // Internal manager name (e.g., "CameraManager")
  QString displayName;    // Display name for splash screen (e.g., "Cameras")
  ConnectionStatus status = ConnectionStatus::Pending;
  QString errorMessage;   // Error message if failed
  int devicesConnected = 0;  // Number of devices successfully connected
  std::chrono::milliseconds duration{0};  // Time taken to connect

  bool isSuccess() const {
    return status == ConnectionStatus::Connected;
  }

  bool isComplete() const {
    return status == ConnectionStatus::Connected ||
           status == ConnectionStatus::Failed ||
           status == ConnectionStatus::TimedOut ||
           status == ConnectionStatus::Cancelled;
  }

  QString statusString() const {
    switch (status) {
      case ConnectionStatus::Pending: return "Pending";
      case ConnectionStatus::Connecting: return "Connecting";
      case ConnectionStatus::Connected: return "Connected";
      case ConnectionStatus::Failed: return "Failed";
      case ConnectionStatus::TimedOut: return "Timed Out";
      case ConnectionStatus::Cancelled: return "Cancelled";
      default: return "Unknown";
    }
  }
};

/**
 * @brief Summary of all connection results
 */
struct ConnectionSummary {
  int totalTasks = 0;
  int completedTasks = 0;
  int successfulTasks = 0;
  int failedTasks = 0;
  int timedOutTasks = 0;
  std::chrono::milliseconds totalDuration{0};

  bool allSuccessful() const {
    return completedTasks == totalTasks && failedTasks == 0 && timedOutTasks == 0;
  }

  double successRate() const {
    return totalTasks > 0 ? static_cast<double>(successfulTasks) / totalTasks : 0.0;
  }
};

} // namespace Startup

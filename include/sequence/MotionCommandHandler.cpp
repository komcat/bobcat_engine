// MotionCommandHandler.cpp
#include "MotionCommandHandler.h"
#include "unified_motion_manager.h"
#include "LogManager.h"
#include "SequenceContext.h"
#include <chrono>
#include <thread>

// Helper to resolve context: references or parse direct values
double MotionCommandHandler::ResolveNumericValue(const std::string& valueStr, SequenceContext* context)
{
  // Check if value is a context reference (e.g., "context:needle_target_x")
  if (valueStr.find("context:") == 0) {
    std::string refKey = valueStr.substr(8);  // Remove "context:" prefix
    if (context) {
      double value = static_cast<double>(context->GetNumeric(refKey, 0.0f));
      if (m_logger) {
        m_logger->Info("Resolved " + valueStr + " = " + std::to_string(value), "MotionHandler");
      }
      return value;
    }
    if (m_logger) {
      m_logger->Warning("No context available to resolve " + valueStr + ", using 0.0", "MotionHandler");
    }
    return 0.0;  // Fallback if no context
  } else {
    // Parse as direct numeric value
    double value = std::stod(valueStr);
    if (m_logger) {
      m_logger->Info("Parsed direct value " + valueStr + " = " + std::to_string(value), "MotionHandler");
    }
    return value;
  }
}

bool MotionCommandHandler::ExecuteCommand(
  void* manager,
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);

  // Context can be used for capturing positions, velocities, etc.
  // Example: if (context && params.find("captureAs") != params.end()) {
  //   context->SetValue(params.at("captureAs"), value, deviceName, methodName);
  // }

  // ============================================================================
  // SINGLE AXIS ABSOLUTE MOVE
  // ============================================================================
  if (methodName == "MoveToPosition") {
    auto axisIt = params.find("axis");
    auto posIt = params.find("position");

    if (axisIt == params.end() || posIt == params.end()) {
      errorMessage = "Missing required parameters: axis, position";
      return false;
    }

    std::string axis = axisIt->second;
    double position = std::stod(posIt->second);

    // Check if non-blocking mode requested explicitly
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    if (m_logger) {
      m_logger->Info(
        "Moving " + deviceName + " axis " + axis + " to " + std::to_string(position),
        "MotionHandler"
      );
    }

    // ✅ SAFETY: Always use non-blocking mode + stop checking (unless explicitly requested non-blocking)
    if (!motionManager->MoveToPosition(deviceName, axis, position, false)) {
      errorMessage = "MoveToPosition failed to start";
      return false;
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // ✅ SAFETY: Wait for completion with stop flag monitoring and position verification
    TargetPosition target;
    if (axis == "X") {
      target.hasX = true;
      target.x = position;
    } else if (axis == "Y") {
      target.hasY = true;
      target.y = position;
    } else if (axis == "Z") {
      target.hasZ = true;
      target.z = position;
    }

    if (!WaitForMotionWithStopCheck(manager, deviceName, context, 30000, &target)) {
      errorMessage = "Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // MULTI-AXIS ABSOLUTE MOVE (X, Y, Z)
  // ============================================================================
  else if (methodName == "MoveToAbs") {
    auto xIt = params.find("x");
    auto yIt = params.find("y");
    auto zIt = params.find("z");

    if (xIt == params.end() || yIt == params.end() || zIt == params.end()) {
      errorMessage = "Missing required parameters: x, y, z";
      return false;
    }

    double x = ResolveNumericValue(xIt->second, context);
    double y = ResolveNumericValue(yIt->second, context);
    double z = ResolveNumericValue(zIt->second, context);
    double velocity = 100.0;  // Default velocity

    auto velIt = params.find("velocity");
    if (velIt != params.end()) {
      velocity = ResolveNumericValue(velIt->second, context);
    }

    // Check if non-blocking mode requested explicitly
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    if (m_logger) {
      m_logger->Info(
        "Moving " + deviceName + " to (" +
        std::to_string(x) + ", " +
        std::to_string(y) + ", " +
        std::to_string(z) + ")",
        "MotionHandler"
      );
    }

    // ✅ SAFETY: Always use non-blocking mode + stop checking
    if (!motionManager->MoveToAbs(deviceName, x, y, z, velocity, false)) {
      errorMessage = "MoveToAbs failed to start";
      return false;
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // ✅ SAFETY: Wait for completion with stop flag monitoring and position verification
    TargetPosition target;
    target.hasX = true;
    target.hasY = true;
    target.hasZ = true;
    target.x = x;
    target.y = y;
    target.z = z;

    if (!WaitForMotionWithStopCheck(manager, deviceName, context, 30000, &target)) {
      errorMessage = "Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // RELATIVE MOVE
  // ============================================================================
  else if (methodName == "MoveRelative") {
    auto axisIt = params.find("axis");
    auto distIt = params.find("distance");

    if (axisIt == params.end() || distIt == params.end()) {
      errorMessage = "Missing required parameters: axis, distance";
      return false;
    }

    std::string axis = axisIt->second;
    double distance = std::stod(distIt->second);

    // Check if non-blocking mode requested explicitly
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    // Get current position to calculate expected final position
    double currentPos = 0.0;
    if (!motionManager->GetPosition(deviceName, axis, currentPos)) {
      errorMessage = "Failed to get current position for relative move";
      return false;
    }

    double targetPos = currentPos + distance;

    if (m_logger) {
      m_logger->Info(
        "Moving " + deviceName + " axis " + axis + " by " + std::to_string(distance) +
        " (current: " + std::to_string(currentPos) + " -> target: " + std::to_string(targetPos) + ")",
        "MotionHandler"
      );
    }

    // ✅ SAFETY: Always use non-blocking mode + stop checking
    if (!motionManager->MoveRelative(deviceName, axis, distance, false)) {
      errorMessage = "MoveRelative failed to start";
      return false;
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // ✅ SAFETY: Wait for completion with stop flag monitoring and position verification
    TargetPosition target;
    if (axis == "X") {
      target.hasX = true;
      target.x = targetPos;
    } else if (axis == "Y") {
      target.hasY = true;
      target.y = targetPos;
    } else if (axis == "Z") {
      target.hasZ = true;
      target.z = targetPos;
    }

    if (!WaitForMotionWithStopCheck(manager, deviceName, context, 30000, &target)) {
      errorMessage = "Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // MOVE TO NAMED POSITION
  // ============================================================================
  else if (methodName == "MoveToNamedPosition") {
    auto posNameIt = params.find("positionName");

    if (posNameIt == params.end()) {
      errorMessage = "Missing required parameter: positionName";
      return false;
    }

    std::string positionName = posNameIt->second;

    if (m_logger) {
      m_logger->Info(
        "Moving " + deviceName + " to named position: " + positionName,
        "MotionHandler"
      );
    }

    // ✅ SAFETY: Start motion in non-blocking mode (blocking=false is default)
    // Recipe plugins typically follow this with WaitForMotionComplete for safety stop checking
    if (!motionManager->MoveToNamedPosition(deviceName, positionName, false)) {
      errorMessage = "MoveToNamedPosition failed to start";
      return false;
    }

    // Return immediately - let WaitForMotionComplete handle the waiting with stop checking
    return true;
  }

  // ============================================================================
  // STOP AXIS
  // ============================================================================
  else if (methodName == "StopAxis") {
    auto axisIt = params.find("axis");

    if (axisIt == params.end()) {
      errorMessage = "Missing required parameter: axis";
      return false;
    }

    std::string axis = axisIt->second;

    if (m_logger) {
      m_logger->Info("Stopping " + deviceName + " axis " + axis, "MotionHandler");
    }

    if (!motionManager->StopAxis(deviceName, axis)) {
      errorMessage = "StopAxis failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // EMERGENCY STOP
  // ============================================================================
  else if (methodName == "EmergencyStop") {
    if (m_logger) {
      m_logger->Warning("EMERGENCY STOP: " + deviceName, "MotionHandler");
    }

    if (!motionManager->EmergencyStop(deviceName)) {
      errorMessage = "EmergencyStop failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // HOME AXIS
  // ============================================================================
  else if (methodName == "HomeAxis") {
    auto axisIt = params.find("axis");

    if (axisIt == params.end()) {
      errorMessage = "Missing required parameter: axis";
      return false;
    }

    std::string axis = axisIt->second;

    if (m_logger) {
      m_logger->Info("Homing " + deviceName + " axis " + axis, "MotionHandler");
    }

    if (!motionManager->HomeAxis(deviceName, axis)) {
      errorMessage = "HomeAxis failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET VELOCITY
  // ============================================================================
  else if (methodName == "SetVelocity") {
    auto axisIt = params.find("axis");
    auto velIt = params.find("velocity");

    if (axisIt == params.end() || velIt == params.end()) {
      errorMessage = "Missing required parameters: axis, velocity";
      return false;
    }

    std::string axis = axisIt->second;
    double velocity = std::stod(velIt->second);

    if (m_logger) {
      m_logger->Info(
        "Setting velocity for " + deviceName + " axis " + axis +
        " to " + std::to_string(velocity),
        "MotionHandler"
      );
    }

    if (!motionManager->SetVelocity(deviceName, axis, velocity)) {
      errorMessage = "SetVelocity failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // SET SYSTEM VELOCITY (PI hexapods)
  // ============================================================================
  else if (methodName == "SetSystemVelocity") {
    auto velIt = params.find("velocity");

    if (velIt == params.end()) {
      errorMessage = "Missing required parameter: velocity";
      return false;
    }

    double velocity = std::stod(velIt->second);

    if (m_logger) {
      m_logger->Info(
        "Setting system velocity for " + deviceName +
        " to " + std::to_string(velocity) + " mm/s",
        "MotionHandler"
      );
    }

    if (!motionManager->SetSystemVelocity(deviceName, velocity)) {
      errorMessage = "SetSystemVelocity failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // ENABLE/DISABLE SERVO
  // ============================================================================
  else if (methodName == "EnableServo") {
    auto axisIt = params.find("axis");
    auto enableIt = params.find("enable");

    if (axisIt == params.end() || enableIt == params.end()) {
      errorMessage = "Missing required parameters: axis, enable";
      return false;
    }

    std::string axis = axisIt->second;
    bool enable = (enableIt->second == "true");

    if (m_logger) {
      m_logger->Info(
        (enable ? "Enabling" : "Disabling") + std::string(" servo for ") +
        deviceName + " axis " + axis,
        "MotionHandler"
      );
    }

    if (!motionManager->EnableServo(deviceName, axis, enable)) {
      errorMessage = "EnableServo failed";
      return false;
    }

    return true;
  }

  else if (methodName == "WaitForMotionComplete") {
    int timeoutMs = 30000;
    auto timeoutIt = params.find("timeout_ms");
    if (timeoutIt != params.end()) {
      timeoutMs = std::stoi(timeoutIt->second);
    }

    if (m_logger) {
      m_logger->Info("Waiting for motion complete on: " + deviceName, "MotionHandler");
    }

    // ✅ SAFETY: Use unified wait method with stop checking
    if (!WaitForMotionWithStopCheck(manager, deviceName, context, timeoutMs)) {
      errorMessage = "Motion stopped or timeout waiting for motion complete on: " + deviceName;
      return false;
    }

    if (m_logger) {
      m_logger->Info("Motion complete for: " + deviceName, "MotionHandler");
    }

    return true;
    }


  // ============================================================================
  // UNKNOWN METHOD
  // ============================================================================
  else {
    errorMessage = "Unknown motion method: " + methodName;
    return false;
  }
}

// =============================================================================
// HELPER: Wait for motion complete with safety stop checking and position verification
// =============================================================================
bool MotionCommandHandler::WaitForMotionWithStopCheck(
  void* manager,
  const std::string& deviceName,
  SequenceContext* context,
  int timeoutMs,
  const TargetPosition* targetPos)
{
  UnifiedMotionManager* motionManager = static_cast<UnifiedMotionManager*>(manager);
  auto startTime = std::chrono::steady_clock::now();

  // ✅ FIX: Small delay to ensure motion actually starts (avoid race condition)
  // When non-blocking motion is initiated, there's a brief window where the command
  // has been sent but the motor hasn't started moving yet
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  while (true) {
    // ✅ SAFETY-CRITICAL: Check stop flags FIRST (before motion status)
    if (context && context->IsStopRequestedAny()) {
      if (m_logger) {
        m_logger->Warning("SAFETY STOP: Stop requested during motion - triggering emergency stop", "MotionHandler");
      }

      // Emergency stop the device immediately
      if (!motionManager->EmergencyStop(deviceName)) {
        if (m_logger) {
          m_logger->Error("Emergency stop failed for device: " + deviceName, "MotionHandler");
        }
      }

      return false;  // Stop sequence execution
    }

    // Check motion status
    bool isMoving = motionManager->IsDeviceInMotion(deviceName);
    bool waypointActive = motionManager->IsWaypointSystemActive(deviceName);

    if (!isMoving && !waypointActive) {
      // Motion stopped - now verify position if target was provided
      if (targetPos) {
        const double TOLERANCE = 0.05;  // ±0.05mm tolerance

        bool positionOK = true;
        std::string errorDetail;

        // Verify X axis if specified
        if (targetPos->hasX) {
          double actualX = 0.0;
          if (motionManager->GetPosition(deviceName, "X", actualX)) {
            double errorX = std::abs(actualX - targetPos->x);
            if (errorX > TOLERANCE) {
              positionOK = false;
              errorDetail += "X: target=" + std::to_string(targetPos->x) +
                             ", actual=" + std::to_string(actualX) +
                             ", error=" + std::to_string(errorX) + "mm; ";
            }
          }
        }

        // Verify Y axis if specified
        if (targetPos->hasY) {
          double actualY = 0.0;
          if (motionManager->GetPosition(deviceName, "Y", actualY)) {
            double errorY = std::abs(actualY - targetPos->y);
            if (errorY > TOLERANCE) {
              positionOK = false;
              errorDetail += "Y: target=" + std::to_string(targetPos->y) +
                             ", actual=" + std::to_string(actualY) +
                             ", error=" + std::to_string(errorY) + "mm; ";
            }
          }
        }

        // Verify Z axis if specified
        if (targetPos->hasZ) {
          double actualZ = 0.0;
          if (motionManager->GetPosition(deviceName, "Z", actualZ)) {
            double errorZ = std::abs(actualZ - targetPos->z);
            if (errorZ > TOLERANCE) {
              positionOK = false;
              errorDetail += "Z: target=" + std::to_string(targetPos->z) +
                             ", actual=" + std::to_string(actualZ) +
                             ", error=" + std::to_string(errorZ) + "mm; ";
            }
          }
        }

        if (!positionOK) {
          if (m_logger) {
            m_logger->Error("Position verification FAILED for " + deviceName + ": " + errorDetail, "MotionHandler");
          }
          return false;  // Position error exceeds tolerance
        }

        if (m_logger) {
          m_logger->Info("Position verification OK for " + deviceName + " (tolerance ±" +
                         std::to_string(TOLERANCE) + "mm)", "MotionHandler");
        }
      }

      // Motion completed successfully (and position verified if required)
      return true;
    }

    // Check timeout
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime
    ).count();

    if (elapsed > timeoutMs) {
      if (m_logger) {
        m_logger->Error("Timeout waiting for motion complete on: " + deviceName, "MotionHandler");
      }
      return false;
    }

    // Sleep briefly to avoid busy-waiting (50ms polling rate)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}
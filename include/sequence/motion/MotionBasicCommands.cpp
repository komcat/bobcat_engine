// MotionBasicCommands.cpp
// Family A implementation. See MotionBasicCommands.h.
#include "MotionBasicCommands.h"

#include "MotionCommandHandler.h"        // for parent->Wait* / Resolve* / InvokeRetrace* / TargetPosition
#include "unified_motion_manager.h"
#include "TransformationMatrixLoader.h"
#include "CoordinateTransformer.h"
#include "LogManager.h"
#include "SequenceContext.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>

std::optional<bool> MotionBasicCommands::TryExecute(
  const std::string& deviceName,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const MotionCommandDeps& deps)
{
  UnifiedMotionManager* motionManager = deps.motion;

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

    if (deps.logger) {
      deps.logger->Info(
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
    MotionCommandHandler::TargetPosition target;
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

    if (!deps.parent->WaitForMotionWithStopCheck(motionManager, deviceName, context, 30000, &target)) {
      errorMessage = "Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // MULTI-AXIS ABSOLUTE MOVE (X, Y, Z)
  // ============================================================================
  if (methodName == "MoveToAbs") {
    auto xIt = params.find("x");
    auto yIt = params.find("y");
    auto zIt = params.find("z");

    if (xIt == params.end() || yIt == params.end() || zIt == params.end()) {
      errorMessage = "Missing required parameters: x, y, z";
      return false;
    }

    double x = deps.parent->ResolveNumericValue(xIt->second, context);
    double y = deps.parent->ResolveNumericValue(yIt->second, context);
    double z = deps.parent->ResolveNumericValue(zIt->second, context);

    // ✅ NEW: Support optional U, V, W parameters for hexapods
    double u = 0.0, v = 0.0, w = 0.0;
    bool hasUVW = false;

    auto uIt = params.find("u");
    auto vIt = params.find("v");
    auto wIt = params.find("w");

    if (uIt != params.end() && vIt != params.end() && wIt != params.end()) {
      u = deps.parent->ResolveNumericValue(uIt->second, context);
      v = deps.parent->ResolveNumericValue(vIt->second, context);
      w = deps.parent->ResolveNumericValue(wIt->second, context);
      hasUVW = true;
    }

    double velocity = 100.0;  // Default velocity

    auto velIt = params.find("velocity");
    if (velIt != params.end()) {
      velocity = deps.parent->ResolveNumericValue(velIt->second, context);
    }

    // Check if non-blocking mode requested explicitly
    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    if (deps.logger) {
      if (hasUVW) {
        deps.logger->Info(
          "Moving " + deviceName + " to (X=" +
          std::to_string(x) + ", Y=" +
          std::to_string(y) + ", Z=" +
          std::to_string(z) + ", U=" +
          std::to_string(u) + ", V=" +
          std::to_string(v) + ", W=" +
          std::to_string(w) + ")",
          "MotionHandler"
        );
      }
      else {
        deps.logger->Info(
          "Moving " + deviceName + " to (" +
          std::to_string(x) + ", " +
          std::to_string(y) + ", " +
          std::to_string(z) + ")",
          "MotionHandler"
        );
      }
    }

    // ✅ NEW: For 6-axis moves, move each axis individually
    bool moveSuccess = true;

    if (hasUVW) {
      // For hexapods with UVW, move all 6 axes
      moveSuccess &= motionManager->MoveToPosition(deviceName, "X", x, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "Y", y, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "Z", z, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "U", u, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "V", v, false);
      moveSuccess &= motionManager->MoveToPosition(deviceName, "W", w, false);
    }
    else {
      // Use the 3-axis move command
      moveSuccess = motionManager->MoveToAbs(deviceName, x, y, z, velocity, false);
    }

    if (!moveSuccess) {
      errorMessage = "MoveToAbs failed to start";
      return false;
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // ✅ SAFETY: Wait for completion with stop flag monitoring and position verification
    MotionCommandHandler::TargetPosition target;
    target.hasX = true;
    target.hasY = true;
    target.hasZ = true;
    target.hasU = hasUVW;
    target.hasV = hasUVW;
    target.hasW = hasUVW;
    target.x = x;
    target.y = y;
    target.z = z;
    target.u = u;
    target.v = v;
    target.w = w;

    if (!deps.parent->WaitForMotionWithStopCheck(motionManager, deviceName, context, 30000, &target)) {
      errorMessage = "Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // RELATIVE MOVE
  // ============================================================================
  if (methodName == "MoveRelative") {
    auto axisIt = params.find("axis");
    auto distIt = params.find("distance");

    if (axisIt == params.end() || distIt == params.end()) {
      errorMessage = "Missing required parameters: axis, distance";
      return false;
    }

    std::string axis = axisIt->second;
    // Normalize axis to uppercase so "x"/"X" both work (PI SDK, ACS SDK, and the
    // target-verification block below all expect uppercase).
    for (auto& c : axis) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    double distance = deps.parent->ResolveNumericValue(distIt->second, context);

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

    if (deps.logger) {
      deps.logger->Info(
        "Moving " + deviceName + " axis " + axis + " by " + std::to_string(distance) +
        " (current: " + std::to_string(currentPos) + " -> target: " + std::to_string(targetPos) + ")",
        "MotionHandler"
      );
    }

    // Check if axis is currently moving before starting new move (diagnostic)
    if (deps.logger) {
      bool isMoving = motionManager->IsMoving(deviceName, axis);
      bool isEnabled = motionManager->IsServoEnabled(deviceName, axis);
      deps.logger->Debug(
        "Pre-move state for " + deviceName + " axis " + axis +
        ": isMoving=" + std::to_string(isMoving) +
        ", servoEnabled=" + std::to_string(isEnabled),
        "MotionHandler"
      );
    }

    // ✅ SAFETY: Always use non-blocking mode + stop checking
    if (deps.logger) {
      deps.logger->Debug("Calling MoveRelative on " + deviceName + " axis " + axis, "MotionHandler");
    }
    if (!motionManager->MoveRelative(deviceName, axis, distance, false)) {
      // Get detailed error info from the controller
      int errorCode = motionManager->GetLastErrorCode(deviceName);
      std::string errorDetail = motionManager->GetLastErrorMessage(deviceName);
      if (deps.logger) {
        deps.logger->Error("MoveRelative FAILED for " + deviceName + " axis " + axis +
          " - errorCode=" + std::to_string(errorCode) + " detail=" + errorDetail, "MotionHandler");
      }
      errorMessage = "MoveRelative failed to start (code=" + std::to_string(errorCode) + "): " + errorDetail;
      return false;
    }
    if (deps.logger) {
      deps.logger->Debug("MoveRelative returned true for " + deviceName + " axis " + axis, "MotionHandler");
    }

    // If explicit non-blocking requested, return immediately
    if (explicitNonBlocking) {
      return true;
    }

    // ✅ SAFETY: Wait for completion with stop flag monitoring and position verification
    MotionCommandHandler::TargetPosition target;
    if (axis == "X") {
      target.hasX = true;
      target.x = targetPos;
    } else if (axis == "Y") {
      target.hasY = true;
      target.y = targetPos;
    } else if (axis == "Z") {
      target.hasZ = true;
      target.z = targetPos;
    } else if (axis == "U") {
      target.hasU = true;
      target.u = targetPos;
    } else if (axis == "V") {
      target.hasV = true;
      target.v = targetPos;
    } else if (axis == "W") {
      target.hasW = true;
      target.w = targetPos;
    }

    if (!deps.parent->WaitForMotionWithStopCheck(motionManager, deviceName, context, 30000, &target)) {
      errorMessage = "Motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // RELATIVE MOVE IN GLOBAL FRAME
  // Params (vector form): dx, dy, dz (mm). Any may be omitted -> 0.
  // Params (single-axis form): axis ("X"|"Y"|"Z"), distance (mm).
  // If 'axis' is present, single-axis form is used.
  // blocking=false skips the WaitForMotionWithStopCheck verification phase.
  // ============================================================================
  if (methodName == "MoveRelativeGlobal") {
    auto axisIt = params.find("axis");

    double dx = 0.0, dy = 0.0, dz = 0.0;

    if (axisIt != params.end()) {
      // Single-axis form: requires 'distance'
      auto distIt = params.find("distance");
      if (distIt == params.end()) {
        errorMessage = "MoveRelativeGlobal (axis form): missing required parameter 'distance'";
        return false;
      }
      std::string axis = axisIt->second;
      for (auto& c : axis) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      double distance = deps.parent->ResolveNumericValue(distIt->second, context);
      if (axis == "X")      { dx = distance; }
      else if (axis == "Y") { dy = distance; }
      else if (axis == "Z") { dz = distance; }
      else {
        errorMessage = "MoveRelativeGlobal: invalid global axis '" + axis + "' (expected X, Y, or Z)";
        return false;
      }
    } else {
      // Vector form: dx/dy/dz optional, default 0
      auto dxIt = params.find("dx");
      auto dyIt = params.find("dy");
      auto dzIt = params.find("dz");
      if (dxIt == params.end() && dyIt == params.end() && dzIt == params.end()) {
        errorMessage = "MoveRelativeGlobal: provide either {axis, distance} or at least one of {dx, dy, dz}";
        return false;
      }
      if (dxIt != params.end()) dx = deps.parent->ResolveNumericValue(dxIt->second, context);
      if (dyIt != params.end()) dy = deps.parent->ResolveNumericValue(dyIt->second, context);
      if (dzIt != params.end()) dz = deps.parent->ResolveNumericValue(dzIt->second, context);
    }

    bool explicitNonBlocking = false;
    auto blockingIt = params.find("blocking");
    if (blockingIt != params.end() && blockingIt->second == "false") {
      explicitNonBlocking = true;
    }

    if (deps.logger) {
      deps.logger->Info(
        "MoveRelativeGlobal " + deviceName +
        " global=(" + std::to_string(dx) + "," + std::to_string(dy) + "," + std::to_string(dz) + ")",
        "MotionHandler"
      );
    }

    // Pre-compute the expected local-axis targets so WaitForMotionWithStopCheck
    // can verify final position. This mirrors what MoveRelativeGlobal does
    // internally but is needed here because the matrix lookup result is not
    // returned to callers.
    MotionCommandHandler::TargetPosition target;
    auto* matrixLoader = motionManager->GetMatrixLoader();
    if (!matrixLoader) {
      errorMessage = "MoveRelativeGlobal: TransformationMatrixLoader not set on UnifiedMotionManager";
      return false;
    }
    const auto* matrix = matrixLoader->GetMatrix(deviceName);
    if (!matrix) {
      errorMessage = "MoveRelativeGlobal: device '" + deviceName + "' not registered in transformation_matrix.json";
      return false;
    }
    CoordinateTransformer::Vector6D globalDelta(dx, dy, dz, 0.0, 0.0, 0.0);
    CoordinateTransformer::Vector6D localDelta =
      CoordinateTransformer::TransformGlobalToLocal(globalDelta, *matrix);
    double curX = 0.0, curY = 0.0, curZ = 0.0;
    motionManager->GetPosition(deviceName, "X", curX);
    motionManager->GetPosition(deviceName, "Y", curY);
    motionManager->GetPosition(deviceName, "Z", curZ);
    constexpr double kAxisEps = 1e-5;
    if (std::fabs(localDelta.x) > kAxisEps) { target.hasX = true; target.x = curX + localDelta.x; }
    if (std::fabs(localDelta.y) > kAxisEps) { target.hasY = true; target.y = curY + localDelta.y; }
    if (std::fabs(localDelta.z) > kAxisEps) { target.hasZ = true; target.z = curZ + localDelta.z; }

    // Issue the move non-blocking; WaitForMotionWithStopCheck handles
    // stop-flag monitoring + final-position verification.
    if (!motionManager->MoveRelativeGlobal(deviceName, dx, dy, dz, false)) {
      int errorCode = motionManager->GetLastErrorCode(deviceName);
      std::string errorDetail = motionManager->GetLastErrorMessage(deviceName);
      if (deps.logger) {
        deps.logger->Error("MoveRelativeGlobal FAILED for " + deviceName +
          " - errorCode=" + std::to_string(errorCode) + " detail=" + errorDetail, "MotionHandler");
      }
      errorMessage = "MoveRelativeGlobal failed to start (code=" + std::to_string(errorCode) + "): " + errorDetail;
      return false;
    }

    if (explicitNonBlocking) {
      return true;
    }

    // If localDelta was below epsilon on all axes the move is a no-op; nothing to wait for.
    if (!target.hasX && !target.hasY && !target.hasZ) {
      return true;
    }

    if (!deps.parent->WaitForMotionWithStopCheck(motionManager, deviceName, context, 30000, &target)) {
      errorMessage = "MoveRelativeGlobal: motion stopped, timeout, or position verification failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // MOVE TO NAMED POSITION
  // ============================================================================
  if (methodName == "MoveToNamedPosition") {
    auto posNameIt = params.find("positionName");

    if (posNameIt == params.end()) {
      errorMessage = "Missing required parameter: positionName";
      return false;
    }

    std::string positionName = posNameIt->second;

    if (deps.logger) {
      deps.logger->Info(
        "Moving " + deviceName + " to named position: " + positionName,
        "MotionHandler"
      );
    }

    // ✅ Store retrace info in context for later retrieval by WaitForMotionComplete
    if (context && deps.parent->IsRetraceEditMode()) {
      // Get the target position coordinates before moving
      PositionStruct targetPos;
      if (motionManager->GetPositionWithCalculation(deviceName, positionName, targetPos)) {
        context->SetValue("_retrace_pending_device", deviceName);
        context->SetValue("_retrace_pending_position", positionName);
        context->SetValue("_retrace_original_x", static_cast<float>(targetPos.x));
        context->SetValue("_retrace_original_y", static_cast<float>(targetPos.y));
        context->SetValue("_retrace_original_z", static_cast<float>(targetPos.z));
        context->SetValue("_retrace_original_u", static_cast<float>(targetPos.u));
        context->SetValue("_retrace_original_v", static_cast<float>(targetPos.v));
        context->SetValue("_retrace_original_w", static_cast<float>(targetPos.w));
        if (deps.logger) {
          deps.logger->Info("Retrace: Stored pending move info for " + deviceName + "/" + positionName, "MotionHandler");
        }
      }
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
  if (methodName == "StopAxis") {
    auto axisIt = params.find("axis");

    if (axisIt == params.end()) {
      errorMessage = "Missing required parameter: axis";
      return false;
    }

    std::string axis = axisIt->second;

    if (deps.logger) {
      deps.logger->Info("Stopping " + deviceName + " axis " + axis, "MotionHandler");
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
  if (methodName == "EmergencyStop") {
    if (deps.logger) {
      deps.logger->Warning("EMERGENCY STOP: " + deviceName, "MotionHandler");
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
  if (methodName == "HomeAxis") {
    auto axisIt = params.find("axis");

    if (axisIt == params.end()) {
      errorMessage = "Missing required parameter: axis";
      return false;
    }

    std::string axis = axisIt->second;

    if (deps.logger) {
      deps.logger->Info("Homing " + deviceName + " axis " + axis, "MotionHandler");
    }

    if (!motionManager->HomeAxis(deviceName, axis)) {
      errorMessage = "HomeAxis failed";
      return false;
    }

    return true;
  }

  // ============================================================================
  // ENABLE/DISABLE SERVO
  // ============================================================================
  if (methodName == "EnableServo") {
    auto axisIt = params.find("axis");
    auto enableIt = params.find("enable");

    if (axisIt == params.end() || enableIt == params.end()) {
      errorMessage = "Missing required parameters: axis, enable";
      return false;
    }

    std::string axis = axisIt->second;
    bool enable = (enableIt->second == "true");

    if (deps.logger) {
      deps.logger->Info(
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

  if (methodName == "WaitForMotionComplete") {
    int timeoutMs = 30000;
    auto timeoutIt = params.find("timeout_ms");
    if (timeoutIt != params.end()) {
      timeoutMs = std::stoi(timeoutIt->second);
    }

    if (deps.logger) {
      deps.logger->Debug("Waiting for motion complete on: " + deviceName, "MotionHandler");
    }

    // ✅ SAFETY: Use unified wait method with stop checking
    if (!deps.parent->WaitForMotionWithStopCheck(motionManager, deviceName, context, timeoutMs)) {
      errorMessage = "Motion stopped or timeout waiting for motion complete on: " + deviceName;
      return false;
    }

    if (deps.logger) {
      deps.logger->Info("Motion complete for: " + deviceName, "MotionHandler");
    }

    // ✅ RETRACE EDIT: Check if we have pending retrace info for this device
    if (deps.parent->IsRetraceEditMode() && deps.parent->HasRetraceEditCallback() && context) {
      std::string pendingDevice = context->GetString("_retrace_pending_device", "");
      std::string pendingPosition = context->GetString("_retrace_pending_position", "");

      if (pendingDevice == deviceName && !pendingPosition.empty()) {
        // Retrieve original target coordinates
        double originalX = static_cast<double>(context->GetNumeric("_retrace_original_x", 0.0f));
        double originalY = static_cast<double>(context->GetNumeric("_retrace_original_y", 0.0f));
        double originalZ = static_cast<double>(context->GetNumeric("_retrace_original_z", 0.0f));
        double originalU = static_cast<double>(context->GetNumeric("_retrace_original_u", 0.0f));
        double originalV = static_cast<double>(context->GetNumeric("_retrace_original_v", 0.0f));
        double originalW = static_cast<double>(context->GetNumeric("_retrace_original_w", 0.0f));

        // Clear pending retrace info
        context->SetValue("_retrace_pending_device", std::string(""));
        context->SetValue("_retrace_pending_position", std::string(""));

        // Invoke retrace edit callback
        if (!deps.parent->InvokeRetraceEditCallback(motionManager, deviceName, pendingPosition, "MoveToNamedPosition", params,
          originalX, originalY, originalZ, originalU, originalV, originalW)) {
          errorMessage = "Retrace edit stopped by user";
          return false;
        }
      }
    }

    return true;
  }

  return std::nullopt;
}

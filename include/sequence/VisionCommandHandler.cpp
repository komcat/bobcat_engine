// VisionCommandHandler.cpp
//
// Orchestrator for vision commands. Per-family logic lives in
// include/sequence/vision/Vision*Commands.{h,cpp}; this file only holds the
// dispatch ladder. Wrapped in #ifdef HAS_OPENCV — the whole file compiles to
// an empty TU when OpenCV is absent (matches CMake's `if(OPENCV_FOUND)` gate).
#ifdef HAS_OPENCV

#include "VisionCommandHandler.h"

// Per-family helpers (dispatch order — each TryExecute returns std::nullopt
// when methodName isn't theirs, otherwise true/false handled).
#include "vision/VisionContinuousCommands.h"   // continuous-mode + cv-inject family
#include "vision/VisionTestCommands.h"         // test/scaffolding family
#include "vision/VisionAutoFocusCommands.h"    // Z-axis auto-focus family
#include "vision/VisionProfileCommands.h"      // profile execution pipeline

#include <iostream>                            // std::cout debug print

bool VisionCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    std::cout << "[VisionCommandHandler] 🔧 Executing: " << methodName
              << " on device: " << deviceName << std::endl;

    // Shared dependency bundle for split helper families.
    // Built once here and reused across every helper TryExecute call below.
    VisionCommandDeps deps{};
    deps.app    = m_context;
    deps.parent = this;

    // Continuous-mode + CV-inject family (start_continuous, stop_continuous,
    // enable_cv_inject, disable_cv_inject)
    if (auto r = VisionContinuousCommands::TryExecute(
          deviceName, methodName, params, errorMessage, context, deps)) {
        return *r;
    }
    // Test/scaffolding family (load_image_from_file)
    if (auto r = VisionTestCommands::TryExecute(
          deviceName, methodName, params, errorMessage, context, deps)) {
        return *r;
    }
    // Auto-focus family (auto_focus)
    if (auto r = VisionAutoFocusCommands::TryExecute(
          deviceName, methodName, params, errorMessage, context, deps)) {
        return *r;
    }
    // Profile execution family (load_profile, execute_profile, execute,
    // capture_and_execute, get_result, show_context_image)
    if (auto r = VisionProfileCommands::TryExecute(
          deviceName, methodName, params, errorMessage, context, deps)) {
        return *r;
    }

    errorMessage = "Unknown vision command: " + methodName;
    return false;
}

#endif // HAS_OPENCV

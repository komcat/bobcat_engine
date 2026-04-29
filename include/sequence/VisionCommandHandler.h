// VisionCommandHandler.h
//
// Thin orchestrator for vision commands. The actual command implementations
// live in include/sequence/vision/Vision*Commands.{h,cpp}; this class only
// owns the dispatch ladder + the ApplicationContext pointer that helpers
// reach via deps.app.
#pragma once

#include "ICommandHandler.h"

// Forward declaration
struct ApplicationContext;

/**
 * @brief Command handler for vision processing in sequences
 *
 * Supports commands (split across 4 helper families under vision/):
 *   Profile    — load_profile, execute_profile, execute, capture_and_execute,
 *                get_result, show_context_image
 *   AutoFocus  — auto_focus
 *   Continuous — start_continuous, stop_continuous, enable_cv_inject,
 *                disable_cv_inject
 *   Test       — load_image_from_file
 *
 * Results are stored in SequenceContext under "vision.*" keys. The whole
 * handler is `#ifdef HAS_OPENCV`-gated; with no OpenCV the .cpp compiles to
 * an empty TU and ExecuteCommand is a pure-virtual call (handled by CMake's
 * `if(OPENCV_FOUND)` source guard).
 */
class VisionCommandHandler : public ICommandHandler
{
public:
    VisionCommandHandler() : m_context(nullptr) {}
    VisionCommandHandler(ApplicationContext* context) : m_context(context) {}

    bool ExecuteCommand(
        void* manager,
        const std::string& deviceName,
        const std::string& methodName,
        const std::map<std::string, std::string>& params,
        std::string& errorMessage,
        SequenceContext* context
    ) override;

    std::string GetManagerType() const override { return "vision"; }

    void SetContext(ApplicationContext* context) { m_context = context; }

private:
    ApplicationContext* m_context;
};

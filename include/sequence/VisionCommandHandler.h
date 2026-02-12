// VisionCommandHandler.h
#pragma once

#include "ICommandHandler.h"

#ifdef HAS_OPENCV
#include <opencv2/core.hpp>
#endif

// Forward declaration
struct ApplicationContext;

/**
 * @brief Command handler for vision processing in sequences
 *
 * Supports commands:
 * - load_profile: Load a vision profile (plugin settings, ROI, etc.)
 * - execute: Execute the loaded vision plugin
 * - capture_and_execute: Capture image from camera and execute vision
 * - get_result: Get specific measurement from last execution
 * - show_context_image: Display image from context in popup window
 *
 * Results are stored in SequenceContext under "vision.*" keys
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

    // Command implementations
    bool LoadProfile(const std::string& profilePath, std::string& errorMessage, SequenceContext* context, bool skipCameraSettings = false);
    bool Execute(const std::string& pluginName, std::string& errorMessage, SequenceContext* context);
    bool CaptureAndExecute(const std::string& cameraName, const std::string& pluginName,
                           std::string& errorMessage, SequenceContext* context);
    bool GetResult(const std::string& measurementKey, std::string& errorMessage, SequenceContext* context);
    bool ShowContextImage(const std::string& contextKey, const std::string& title,
                          int autoCloseMs, std::string& errorMessage, SequenceContext* context);

    // Auto-focus command
    bool ExecuteAutoFocus(const std::map<std::string, std::string>& params,
                          std::string& errorMessage, SequenceContext* context);

#ifdef HAS_OPENCV
    // Focus measure calculation using Laplacian variance
    double CalculateFocusMeasure(const cv::Mat& image, int roiX, int roiY, int roiWidth, int roiHeight);
#endif

    // Helper to store VisionResult in context
    void StoreResultInContext(const struct VisionResult& result, SequenceContext* context);

#ifdef HAS_OPENCV
    // Helper to save vision result with history management
    // Returns the path to the saved file
    std::string SaveVisionResultWithHistory(const cv::Mat& image, SequenceContext* context);
#endif

    // Static members for vision history management
    static int s_historyIndex;           // Current index (0-9 for 10 results)
    static const int MAX_HISTORY = 10;   // Maximum number of history entries
    static const std::string HISTORY_DIR; // Directory for vision history
};

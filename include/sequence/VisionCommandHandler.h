// VisionCommandHandler.h
#pragma once

#include "ICommandHandler.h"

// Forward declaration
class ApplicationContext;

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
    bool LoadProfile(const std::string& profilePath, std::string& errorMessage, SequenceContext* context);
    bool Execute(const std::string& pluginName, std::string& errorMessage, SequenceContext* context);
    bool CaptureAndExecute(const std::string& cameraName, const std::string& pluginName,
                           std::string& errorMessage, SequenceContext* context);
    bool GetResult(const std::string& measurementKey, std::string& errorMessage, SequenceContext* context);
    bool ShowContextImage(const std::string& contextKey, const std::string& title,
                          int autoCloseMs, std::string& errorMessage, SequenceContext* context);

    // Helper to store VisionResult in context
    void StoreResultInContext(const struct VisionResult& result, SequenceContext* context);
};

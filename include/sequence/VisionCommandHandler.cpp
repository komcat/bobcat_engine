// VisionCommandHandler.cpp
#ifdef HAS_OPENCV

#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include "ContextImageViewer.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "../vision/VisionManager.h"
#include "../vision/VisionResult.h"
#include "../vision/VisionROI.h"
#include "../motions/unified_motion_manager.h"  // For GetCurrentPosition
#include "../motions/MotionTypes.h"              // For PositionStruct
#include "../lighting/LightingManager.h"         // For lighting control
#include "../lighting/LightingTypes.h"           // For Lighting::LightingResult
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>  // For cv::imwrite
#include <QMetaObject>
#include <QApplication>
#include <QDir>
#include "SequenceEngine.h"  // For VisionResultCallback
#include "../GdiWindow/GdiCameraWindowManager.h"

// Static member definitions for vision history
int VisionCommandHandler::s_historyIndex = 0;
const int VisionCommandHandler::MAX_HISTORY;  // Defined in header as 10
const std::string VisionCommandHandler::HISTORY_DIR = "logs/vision_history";

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

    if (methodName == "load_profile") {
        // Load vision profile from file
        // params: "path" = path to profile JSON
        auto it = params.find("path");
        if (it == params.end()) {
            errorMessage = "load_profile requires 'path' parameter";
            return false;
        }
        return LoadProfile(it->second, errorMessage, context);
    }
    else if (methodName == "execute_profile") {
        // Complete execution: load profile + capture + execute + return results
        // Always returns TRUE, stores success/error in context
        // params: "profile_path" = path to profile JSON, "camera" = camera name

        auto profileIt = params.find("profile_path");
        if (profileIt == params.end() || profileIt->second.empty()) {
            if (context) {
                context->SetValue("vision.result.success", false);
                context->SetValue("vision.result.error", std::string("Missing profile_path parameter"));
            }
            std::cout << "[VisionCommandHandler] ⚠️ execute_profile: Missing profile_path" << std::endl;
            return true;  // Always return TRUE
        }

        auto cameraIt = params.find("camera");
        if (cameraIt == params.end() || cameraIt->second.empty()) {
            if (context) {
                context->SetValue("vision.result.success", false);
                context->SetValue("vision.result.error", std::string("Missing camera parameter"));
            }
            std::cout << "[VisionCommandHandler] ⚠️ execute_profile: Missing camera" << std::endl;
            return true;  // Always return TRUE
        }

        std::string profilePath = profileIt->second;
        std::string cameraName = cameraIt->second;

        std::cout << "[VisionCommandHandler] 📄 Vision profile loaded: " << profilePath << std::endl;

        // Step 1: Load profile
        std::string loadError;
        if (!LoadProfile(profilePath, loadError, context)) {
            if (context) {
                context->SetValue("vision.result.success", false);
                context->SetValue("vision.result.error", std::string("Failed to load profile: ") + loadError);
            }
            std::cout << "[VisionCommandHandler] ❌ execute_profile: Load failed - " << loadError << std::endl;
            return true;  // Always return TRUE
        }

        // Step 2: Capture image and execute
        std::string execError;
        std::string pluginName = context ? context->GetString("vision.current_plugin", "") : "";
        if (!CaptureAndExecute(cameraName, pluginName, execError, context)) {
            // Error already stored in context by CaptureAndExecute
            if (context && !context->HasValue("vision.result.error")) {
                context->SetValue("vision.result.error", execError);
            }
            std::cout << "[VisionCommandHandler] ❌ execute_profile: Execute failed - " << execError << std::endl;
            return true;  // Always return TRUE
        }

        // Step 3: Copy results to custom prefix if capture_as is specified
        auto captureAsIt = params.find("capture_as");
        if (captureAsIt != params.end() && !captureAsIt->second.empty() && context) {
            std::string captureAs = captureAsIt->second;

            // Strip $ prefix if present for context keys
            std::string contextPrefix = captureAs;
            if (!contextPrefix.empty() && contextPrefix[0] == '$') {
                contextPrefix = contextPrefix.substr(1);
            }

            auto summary = context->GetSummary();

            std::cout << "[VisionCommandHandler] 📋 Copying results to prefix: " << contextPrefix << std::endl;

            // Save a unique copy of the result image for this capture_as
            cv::Mat outputImage = VisionManager::instance().getOutputImage();
            if (!outputImage.empty()) {
                std::string uniquePath = "logs/" + contextPrefix + "_result.png";
                cv::imwrite(uniquePath, outputImage);
                context->SetValue(contextPrefix + ".result.image_path", uniquePath);
                context->SetImageFrame(contextPrefix + ".result.image", outputImage);
                std::cout << "[VisionCommandHandler] 🖼️ Saved unique image for " << contextPrefix
                          << ": " << uniquePath << " (" << outputImage.cols << "x" << outputImage.rows << ")" << std::endl;
            }

            // Copy all vision.* values to <contextPrefix>.* (except image which we handled above)
            for (const auto& [key, value] : summary.capturedValues) {
                // Check if key starts with "vision."
                if (key.find("vision.") == 0) {
                    // Extract the part after "vision." and prepend with contextPrefix
                    std::string suffix = key.substr(7);  // Remove "vision."
                    std::string newKey = contextPrefix + "." + suffix;

                    // Skip image keys - we already handled them above
                    if (suffix.find("image") != std::string::npos) {
                        continue;
                    }

                    // Copy value to new key based on type
                    switch (value.type) {
                        case SequenceContext::ValueType::NUMERIC:
                            context->SetValue(newKey, value.numericValue);
                            break;
                        case SequenceContext::ValueType::STRING:
                            context->SetValue(newKey, value.stringValue);
                            break;
                        case SequenceContext::ValueType::BOOLEAN:
                            context->SetValue(newKey, value.booleanValue);
                            break;
                        default:
                            break;
                    }
                }
            }

            std::cout << "[VisionCommandHandler] ✅ Results copied to prefix: " << contextPrefix << std::endl;
        }

        // Step 4: Calculate world position if requested (GotoDetect feature)
        auto calcWorldIt = params.find("calculate_world_position");
        auto motionDeviceIt = params.find("motion_device");
        if (calcWorldIt != params.end() && calcWorldIt->second == "true" &&
            motionDeviceIt != params.end() && !motionDeviceIt->second.empty() && context) {

            std::string motionDevice = motionDeviceIt->second;
            std::cout << "[VisionCommandHandler] 🌍 Calculating world position for device: " << motionDevice << std::endl;

            // ✅ Find reference point from vision result (priority order)
            double referenceX_px = 0.0;
            double referenceY_px = 0.0;
            bool foundReference = false;
            bool usingAnchor = false;
            std::string referenceType = "";

            // Priority 1: Anchor point (pattern matching with custom anchor)
            if (context->HasValue("vision.result.match_0_anchor_x") &&
                context->HasValue("vision.result.match_0_anchor_y")) {
                referenceX_px = context->GetNumeric("vision.result.match_0_anchor_x", 0.0);
                referenceY_px = context->GetNumeric("vision.result.match_0_anchor_y", 0.0);
                foundReference = true;
                usingAnchor = true;
                referenceType = "anchor";
                std::cout << "[VisionCommandHandler] 📍 Using anchor point: ("
                          << referenceX_px << ", " << referenceY_px << ") px" << std::endl;
            }
            // Priority 2: Corner point (corner detector)
            else if (context->HasValue("vision.result.corner_x") && context->HasValue("vision.result.corner_y")) {
                referenceX_px = context->GetNumeric("vision.result.corner_x", 0.0);
                referenceY_px = context->GetNumeric("vision.result.corner_y", 0.0);
                foundReference = true;
                referenceType = "corner";
                std::cout << "[VisionCommandHandler] 📍 Using corner point: ("
                          << referenceX_px << ", " << referenceY_px << ") px" << std::endl;
            }
            // Priority 3: Center point (points/circles/line center)
            else if (context->HasValue("vision.result.center_x") && context->HasValue("vision.result.center_y")) {
                referenceX_px = context->GetNumeric("vision.result.center_x", 0.0);
                referenceY_px = context->GetNumeric("vision.result.center_y", 0.0);
                foundReference = true;
                referenceType = "center";
                std::cout << "[VisionCommandHandler] 📍 Using center point: ("
                          << referenceX_px << ", " << referenceY_px << ") px" << std::endl;
            }

            if (foundReference) {

                // Get image dimensions for center calculation
                double imageWidth = context->GetNumeric("vision.result.image_width", 1280.0);
                double imageHeight = context->GetNumeric("vision.result.image_height", 1024.0);
                double imageCenterX_px = imageWidth / 2.0;
                double imageCenterY_px = imageHeight / 2.0;

                // Calculate offset from image center (in pixels)
                double offsetX_px = referenceX_px - imageCenterX_px;
                double offsetY_px = referenceY_px - imageCenterY_px;

                // Convert to mm using calibration
                double pixelToMmX = VisionManager::instance().getPixelToMmX();
                double pixelToMmY = VisionManager::instance().getPixelToMmY();
                double offsetX_mm = offsetX_px * pixelToMmX;
                double offsetY_mm = offsetY_px * pixelToMmY;

                std::cout << "[VisionCommandHandler] 📐 Reference point: (" << referenceX_px << ", " << referenceY_px << ") px (detected feature in full image)" << std::endl;
                std::cout << "[VisionCommandHandler] 📐 Image center: (" << imageCenterX_px << ", " << imageCenterY_px << ") px (full image: " << imageWidth << "×" << imageHeight << ")" << std::endl;
                std::cout << "[VisionCommandHandler] 📐 Offset from center: (" << offsetX_px << ", " << offsetY_px << ") px" << std::endl;
                std::cout << "[VisionCommandHandler]    Calculation: offset_px = reference - center = (" << referenceX_px << " - " << imageCenterX_px << ", " << referenceY_px << " - " << imageCenterY_px << ")" << std::endl;
                std::cout << "[VisionCommandHandler] 📐 Offset in mm: (" << offsetX_mm << ", " << offsetY_mm << ") mm" << std::endl;
                std::cout << "[VisionCommandHandler]    Calculation: offset_mm = offset_px × calibration = (" << offsetX_px << " × " << pixelToMmX << ", " << offsetY_px << " × " << pixelToMmY << ")" << std::endl;

                // Get current motion device position
                double deviceX_mm = 0.0, deviceY_mm = 0.0, deviceZ_mm = 0.0;
                if (m_context && m_context->motionManager) {
                    PositionStruct currentPos = m_context->motionManager->GetCurrentPosition(motionDevice);
                    deviceX_mm = currentPos.x;
                    deviceY_mm = currentPos.y;
                    deviceZ_mm = currentPos.z;

                    std::cout << "[VisionCommandHandler] 📍 Device position: X=" << deviceX_mm
                              << ", Y=" << deviceY_mm << ", Z=" << deviceZ_mm << " mm" << std::endl;
                }

                // Calculate world position of detected feature
                // World position = device position + offset from camera center
                double worldX_mm = deviceX_mm + offsetX_mm;
                double worldY_mm = deviceY_mm + offsetY_mm;

                std::cout << "[VisionCommandHandler] 🌍 World position: X=" << worldX_mm
                          << ", Y=" << worldY_mm << " mm" << std::endl;
                std::cout << "[VisionCommandHandler]    Calculation: world = device + offset = (" << deviceX_mm << " + " << offsetX_mm << ", " << deviceY_mm << " + " << offsetY_mm << ")" << std::endl;

                // Store world position in context
                context->SetValue("vision.detected.world_x_mm", static_cast<float>(worldX_mm));
                context->SetValue("vision.detected.world_y_mm", static_cast<float>(worldY_mm));
                context->SetValue("vision.detected.offset_x_mm", static_cast<float>(offsetX_mm));
                context->SetValue("vision.detected.offset_y_mm", static_cast<float>(offsetY_mm));
                context->SetValue("vision.detected.device_x_mm", static_cast<float>(deviceX_mm));
                context->SetValue("vision.detected.device_y_mm", static_cast<float>(deviceY_mm));
                context->SetValue("vision.detected.device_z_mm", static_cast<float>(deviceZ_mm));  // Store Z for MoveToContextPosition
                context->SetValue("vision.detected.reference_x_px", static_cast<float>(referenceX_px));
                context->SetValue("vision.detected.reference_y_px", static_cast<float>(referenceY_px));
                context->SetValue("vision.detected.using_anchor", usingAnchor);

                // Also copy to capture_as prefix if specified
                if (captureAsIt != params.end() && !captureAsIt->second.empty()) {
                    std::string captureAs = captureAsIt->second;
                    context->SetValue(captureAs + ".detected.world_x_mm", static_cast<float>(worldX_mm));
                    context->SetValue(captureAs + ".detected.world_y_mm", static_cast<float>(worldY_mm));
                    context->SetValue(captureAs + ".detected.offset_x_mm", static_cast<float>(offsetX_mm));
                    context->SetValue(captureAs + ".detected.offset_y_mm", static_cast<float>(offsetY_mm));
                    context->SetValue(captureAs + ".detected.device_x_mm", static_cast<float>(deviceX_mm));
                    context->SetValue(captureAs + ".detected.device_y_mm", static_cast<float>(deviceY_mm));
                    context->SetValue(captureAs + ".detected.device_z_mm", static_cast<float>(deviceZ_mm));
                    context->SetValue(captureAs + ".detected.reference_x_px", static_cast<float>(referenceX_px));
                    context->SetValue(captureAs + ".detected.reference_y_px", static_cast<float>(referenceY_px));
                    context->SetValue(captureAs + ".detected.using_anchor", usingAnchor);
                }

                std::cout << "[VisionCommandHandler] ✅ World position calculated and stored (reference: " << referenceType << ")" << std::endl;
            } else {
                std::cout << "[VisionCommandHandler] ⚠️ No reference point found in vision result" << std::endl;
                std::cout << "[VisionCommandHandler]    Checked for: anchor, corner, center" << std::endl;
                std::cout << "[VisionCommandHandler]    Make sure vision plugin produces one of these coordinates" << std::endl;
            }
        }

        std::cout << "[VisionCommandHandler] ✅ Vision execution completed: success" << std::endl;
        return true;  // Always return TRUE
    }
    else if (methodName == "execute") {
        // Execute vision plugin
        // params: "plugin" = plugin name (optional, falls back to context vision.current_plugin)
        //         "capture_as" = optional prefix to copy vision.* results to (e.g., "splice1")
        std::string pluginName;
        auto it = params.find("plugin");
        if (it != params.end() && !it->second.empty()) {
            pluginName = it->second;
        }
        bool execResult = Execute(pluginName, errorMessage, context);

        // Copy results to custom prefix if capture_as is specified
        auto captureAsIt = params.find("capture_as");
        if (captureAsIt != params.end() && !captureAsIt->second.empty() && context) {
            std::string contextPrefix = captureAsIt->second;
            if (!contextPrefix.empty() && contextPrefix[0] == '$') {
                contextPrefix = contextPrefix.substr(1);
            }

            std::cout << "[VisionCommandHandler] Copying results to prefix: " << contextPrefix << std::endl;

            // Save a unique copy of the result image
            cv::Mat outputImage = VisionManager::instance().getOutputImage();
            if (!outputImage.empty()) {
                std::string uniquePath = "logs/" + contextPrefix + "_result.png";
                cv::imwrite(uniquePath, outputImage);
                context->SetValue(contextPrefix + ".result.image_path", uniquePath);
                context->SetImageFrame(contextPrefix + ".result.image", outputImage);
            }

            // Copy all vision.* values to <contextPrefix>.*
            auto summary = context->GetSummary();
            for (const auto& [key, value] : summary.capturedValues) {
                if (key.find("vision.") == 0) {
                    std::string suffix = key.substr(7);  // Remove "vision."
                    std::string newKey = contextPrefix + "." + suffix;
                    if (suffix.find("image") != std::string::npos) continue;
                    switch (value.type) {
                        case SequenceContext::ValueType::NUMERIC:
                            context->SetValue(newKey, value.numericValue);
                            break;
                        case SequenceContext::ValueType::STRING:
                            context->SetValue(newKey, value.stringValue);
                            break;
                        case SequenceContext::ValueType::BOOLEAN:
                            context->SetValue(newKey, value.booleanValue);
                            break;
                        default:
                            break;
                    }
                }
            }
            std::cout << "[VisionCommandHandler] Results copied to prefix: " << contextPrefix << std::endl;
        }

        return execResult;
    }
    else if (methodName == "capture_and_execute") {
        // Capture from camera and execute vision
        // params: "camera" = camera name, "plugin" = plugin name (optional if already loaded)
        auto cameraIt = params.find("camera");
        if (cameraIt == params.end()) {
            errorMessage = "capture_and_execute requires 'camera' parameter";
            return false;
        }

        std::string pluginName = deviceName;
        auto pluginIt = params.find("plugin");
        if (pluginIt != params.end()) {
            pluginName = pluginIt->second;
        }

        return CaptureAndExecute(cameraIt->second, pluginName, errorMessage, context);
    }
    else if (methodName == "get_result") {
        // Get specific measurement from last execution
        // params: "key" = measurement key, "variable" = context variable name to store
        auto keyIt = params.find("key");
        if (keyIt == params.end()) {
            errorMessage = "get_result requires 'key' parameter";
            return false;
        }
        return GetResult(keyIt->second, errorMessage, context);
    }
    else if (methodName == "show_context_image") {
        // Show image from context in non-modal popup window
        // params: "context_key" = key to the image in context (e.g., "$FLIPSN.result.image" or "vision.result.image")
        //         "title" = window title (optional)
        //         "auto_close_ms" = auto-close after ms (optional, 0 = no auto-close)
        auto keyIt = params.find("context_key");
        if (keyIt == params.end() || keyIt->second.empty()) {
            errorMessage = "show_context_image requires 'context_key' parameter";
            return false;
        }

        std::string contextKey = keyIt->second;
        std::string title = "Context Image";
        int autoCloseMs = 0;

        auto titleIt = params.find("title");
        if (titleIt != params.end() && !titleIt->second.empty()) {
            title = titleIt->second;
        }

        auto autoCloseIt = params.find("auto_close_ms");
        if (autoCloseIt != params.end() && !autoCloseIt->second.empty()) {
            try {
                autoCloseMs = std::stoi(autoCloseIt->second);
            } catch (...) {
                autoCloseMs = 0;
            }
        }

        return ShowContextImage(contextKey, title, autoCloseMs, errorMessage, context);
    }
    else if (methodName == "start_continuous") {
        // Start continuous vision processing on GdiCameraWindow
        // params: "profile_path" (optional) = vision profile to select before starting
        if (!m_context || !m_context->gdiCameraWindowManager) {
            errorMessage = "GdiCameraWindowManager not available";
            return false;
        }

        auto profileIt = params.find("profile_path");
        if (profileIt != params.end() && !profileIt->second.empty()) {
            m_context->gdiCameraWindowManager->selectVisionProfile(profileIt->second);
            std::cout << "[VisionCommandHandler] Selected continuous vision profile: " << profileIt->second << std::endl;
        }

        m_context->gdiCameraWindowManager->setVisionEnabled(true);
        std::cout << "[VisionCommandHandler] Continuous vision started" << std::endl;
        return true;
    }
    else if (methodName == "stop_continuous") {
        // Stop continuous vision processing on GdiCameraWindow
        if (!m_context || !m_context->gdiCameraWindowManager) {
            errorMessage = "GdiCameraWindowManager not available";
            return false;
        }

        m_context->gdiCameraWindowManager->setVisionEnabled(false);
        std::cout << "[VisionCommandHandler] Continuous vision stopped" << std::endl;
        return true;
    }
    else if (methodName == "enable_cv_inject") {
        // Enable cv.* context injection from continuous vision
        if (!m_context || !m_context->sequenceEngine) {
            errorMessage = "SequenceEngine not available";
            return false;
        }

        m_context->sequenceEngine->SetCvInjectEnabled(true);
        std::cout << "[VisionCommandHandler] CV inject enabled" << std::endl;
        return true;
    }
    else if (methodName == "disable_cv_inject") {
        // Disable cv.* context injection from continuous vision
        if (!m_context || !m_context->sequenceEngine) {
            errorMessage = "SequenceEngine not available";
            return false;
        }

        m_context->sequenceEngine->SetCvInjectEnabled(false);
        std::cout << "[VisionCommandHandler] CV inject disabled" << std::endl;
        return true;
    }
    else if (methodName == "load_image_from_file") {
        // Load image from file into vision pipeline (for offline testing)
        // params: "file_path" = path to image file
        auto fileIt = params.find("file_path");
        if (fileIt == params.end() || fileIt->second.empty()) {
            errorMessage = "load_image_from_file requires 'file_path' parameter";
            return false;
        }
        cv::Mat image = cv::imread(fileIt->second, cv::IMREAD_UNCHANGED);
        if (image.empty()) {
            errorMessage = "Failed to load image: " + fileIt->second;
            return false;
        }
        VisionManager::instance().setInputImage(image);
        if (context) {
            context->SetImageFrame("vision.loaded.image", image);
            context->SetValue("vision.loaded.file_path", fileIt->second);
            context->SetValue("vision.loaded.width", static_cast<float>(image.cols));
            context->SetValue("vision.loaded.height", static_cast<float>(image.rows));
        }
        std::cout << "[VisionCommandHandler] Loaded image from file: " << fileIt->second
                  << " (" << image.cols << "x" << image.rows << ")" << std::endl;
        return true;
    }
    else if (methodName == "auto_focus") {
        // Auto-focus in Z axis using Laplacian variance focus measure
        // params:
        //   "motion_device" = gantry device name (e.g., "gantry-main")
        //   "camera" = camera name for image capture
        //   "z_range_mm" = search range in mm (default: 0.2, searches +/- this value)
        //   "z_step_mm" = step size in mm (default: 0.05)
        //   "roi_x", "roi_y", "roi_width", "roi_height" = ROI for focus calculation (optional)
        //   "settle_time_ms" = time to wait after motion (default: 100)
        // Returns in context:
        //   autofocus.best_z_mm = best focus Z position
        //   autofocus.best_focus_value = focus measure at best position
        //   autofocus.start_z_mm = starting Z position
        //   autofocus.success = true/false

        return ExecuteAutoFocus(params, errorMessage, context);
    }
    else {
        errorMessage = "Unknown vision command: " + methodName;
        return false;
    }
}

bool VisionCommandHandler::LoadProfile(const std::string& profilePath,
                                        std::string& errorMessage,
                                        SequenceContext* context)
{
    std::cout << "[VisionCommandHandler] 📂 Loading profile: " << profilePath << std::endl;

    try {
        std::ifstream file(profilePath);
        if (!file.is_open()) {
            errorMessage = "Cannot open profile file: " + profilePath;
            return false;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        // Load ROI settings
        if (j.contains("roi")) {
            auto roiJson = j["roi"];
            int startX = roiJson.value("startX", 0);
            int startY = roiJson.value("startY", 0);
            int endX = roiJson.value("endX", 640);
            int endY = roiJson.value("endY", 480);
            bool enabled = roiJson.value("enabled", false);

            VisionROI roi(startX, startY, endX, endY);
            roi.setEnabled(enabled);
            VisionManager::instance().setROI(roi);

            std::cout << "[VisionCommandHandler] 🔲 ROI set: (" << startX << "," << startY
                      << ") to (" << endX << "," << endY << ") enabled=" << enabled << std::endl;
        }

        // Load calibration from SYSTEM config (not from profile)
        // This ensures consistent calibration across all profiles
        {
            // Try multiple possible paths for the calibration file
            std::vector<std::string> possiblePaths = {
                "config/SYSTEM/val_pixelmm.json",
                "./config/SYSTEM/val_pixelmm.json",
                "../config/SYSTEM/val_pixelmm.json"
            };

            // Also try to construct path relative to profile path
            size_t configPos = profilePath.find("/config/");
            if (configPos == std::string::npos) {
                configPos = profilePath.find("\\config\\");
            }
            if (configPos != std::string::npos) {
                std::string basePath = profilePath.substr(0, configPos);
                possiblePaths.insert(possiblePaths.begin(), basePath + "/config/SYSTEM/val_pixelmm.json");
            }

            double pixelToMmX = 0.00248;  // Default fallback
            double pixelToMmY = 0.00248;
            bool loaded = false;

            for (const auto& systemCalPath : possiblePaths) {
                std::ifstream calFile(systemCalPath);
                if (calFile.is_open()) {
                    try {
                        nlohmann::json calJson;
                        calFile >> calJson;
                        calFile.close();

                        if (calJson.contains("calibration")) {
                            pixelToMmX = calJson["calibration"].value("pixel_to_mm_x", 0.00248);
                            pixelToMmY = calJson["calibration"].value("pixel_to_mm_y", 0.00248);
                            std::cout << "[VisionCommandHandler] 📏 System calibration loaded from: " << systemCalPath << std::endl;
                            std::cout << "[VisionCommandHandler] 📏 Calibration values: "
                                      << pixelToMmX << " mm/px (X), " << pixelToMmY << " mm/px (Y)" << std::endl;
                            loaded = true;
                            break;
                        }
                    } catch (const std::exception& e) {
                        std::cout << "[VisionCommandHandler] ⚠️ Failed to parse " << systemCalPath
                                  << ": " << e.what() << std::endl;
                    }
                }
            }

            if (!loaded) {
                std::cout << "[VisionCommandHandler] ⚠️ System calibration file not found in any path:" << std::endl;
                for (const auto& p : possiblePaths) {
                    std::cout << "  - " << p << std::endl;
                }
                std::cout << "[VisionCommandHandler] Using default calibration: " << pixelToMmX << " mm/px" << std::endl;
            }

            // Set calibration without saving (to avoid overwriting system config)
            // Only update the internal values, don't save to file
            VisionManager::instance().setPixelToMmNoSave(pixelToMmX, pixelToMmY);
        }

        // Load camera settings (exposure/gain, cameraId, but NOT pixelToMm)
        if (j.contains("camera")) {
            auto camJson = j["camera"];

            // Store camera ID in context for later use
            std::string cameraId = "";
            if (camJson.contains("cameraId")) {
                cameraId = camJson.value("cameraId", "");
                if (context) {
                    context->SetValue("vision.camera_id", cameraId);
                }
                std::cout << "[VisionCommandHandler] 📷 Camera ID: " << cameraId << std::endl;
            }

            // ✅ Apply exposure/gain settings to camera if specified
            if (!cameraId.empty() && m_context && m_context->cameraManager) {
                // Support both camelCase (from UI) and snake_case field names
                bool hasExposureSettings = camJson.contains("exposureTime") || camJson.contains("exposure_time")
                                        || camJson.contains("gain");
                std::cout << "[VisionCommandHandler] 📷 Checking exposure settings: hasSettings=" << hasExposureSettings << std::endl;

                if (hasExposureSettings) {
                    ICameraHardware::ExposureSettings settings;
                    // Support both camelCase and snake_case
                    if (camJson.contains("exposureTime")) {
                        settings.exposure_time = camJson.value("exposureTime", 10000.0);
                    } else {
                        settings.exposure_time = camJson.value("exposure_time", 10000.0);
                    }
                    settings.gain = camJson.value("gain", 1.0);
                    if (camJson.contains("autoExposure")) {
                        settings.auto_exposure = camJson.value("autoExposure", false);
                    } else {
                        settings.auto_exposure = camJson.value("auto_exposure", false);
                    }
                    if (camJson.contains("autoGain")) {
                        settings.auto_gain = camJson.value("autoGain", false);
                    } else {
                        settings.auto_gain = camJson.value("auto_gain", false);
                    }

                    std::cout << "[VisionCommandHandler] 📷 Applying exposure: exp=" << settings.exposure_time
                              << "µs, gain=" << settings.gain
                              << ", autoExp=" << settings.auto_exposure
                              << ", autoGain=" << settings.auto_gain << std::endl;

                    if (m_context->cameraManager->ApplyExposureSettings(cameraId, settings)) {
                        std::cout << "[VisionCommandHandler] ✅ Exposure settings applied successfully" << std::endl;
                    } else {
                        std::cout << "[VisionCommandHandler] ⚠️ Failed to apply exposure settings to camera: "
                                  << cameraId << std::endl;
                    }
                } else {
                    std::cout << "[VisionCommandHandler] ℹ️ No exposure/gain settings in profile" << std::endl;
                }
            } else {
                if (cameraId.empty()) {
                    std::cout << "[VisionCommandHandler] ⚠️ No cameraId specified in profile" << std::endl;
                } else if (!m_context || !m_context->cameraManager) {
                    std::cout << "[VisionCommandHandler] ⚠️ CameraManager not available" << std::endl;
                }
            }
        }

        // Load and apply lighting settings
        if (j.contains("lighting")) {
            auto lightJson = j["lighting"];
            bool lightingEnabled = lightJson.value("enabled", false);

            if (lightingEnabled && m_context && m_context->lightingManager) {
                std::string controllerId = lightJson.value("controllerId", "");

                // If no controllerId in profile, use first available controller
                if (controllerId.empty()) {
                    auto controllerIds = m_context->lightingManager->GetControllerIds();
                    if (!controllerIds.empty()) {
                        controllerId = controllerIds[0];
                    }
                }

                if (!controllerId.empty() && lightJson.contains("channels") && lightJson["channels"].is_array()) {
                    std::cout << "[VisionCommandHandler] 💡 Applying lighting settings (controller: " << controllerId << ")" << std::endl;

                    // Wait for lighting controller to be ready
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));

                    for (const auto& chJson : lightJson["channels"]) {
                        int channel = chJson.value("channel", 0);
                        int intensity = chJson.value("intensity", 0);
                        bool on = chJson.value("on", false);

                        // Set intensity first
                        auto intensityResult = m_context->lightingManager->SetIntensity(controllerId, channel, intensity);
                        if (intensityResult != Lighting::LightingResult::Success) {
                            std::cout << "[VisionCommandHandler] ⚠️ Failed to set intensity for channel " << channel << std::endl;
                        }

                        // Set on/off state
                        auto onResult = m_context->lightingManager->SetOn(controllerId, channel, on);
                        if (onResult != Lighting::LightingResult::Success) {
                            std::cout << "[VisionCommandHandler] ⚠️ Failed to set on/off for channel " << channel << std::endl;
                        }

                        std::cout << "[VisionCommandHandler] 💡 Channel " << channel
                                  << ": intensity=" << intensity << ", on=" << (on ? "true" : "false") << std::endl;
                    }

                    // Wait for lighting to stabilize
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    std::cout << "[VisionCommandHandler] ✅ Lighting settings applied" << std::endl;
                } else {
                    std::cout << "[VisionCommandHandler] ⚠️ No lighting controller available or no channels specified" << std::endl;
                }
            } else if (lightingEnabled) {
                std::cout << "[VisionCommandHandler] ⚠️ Lighting enabled in profile but LightingManager not available" << std::endl;
            }
        }

        // Load plugin and parameters
        if (j.contains("plugin")) {
            auto pluginJson = j["plugin"];
            std::string pluginName = pluginJson.value("name", "");

            if (!pluginName.empty()) {
                auto plugin = VisionManager::instance().getPlugin(QString::fromStdString(pluginName));
                if (plugin && pluginJson.contains("parameters")) {
                    plugin->setParameters(pluginJson["parameters"]);
                    std::cout << "[VisionCommandHandler] 🔌 Plugin loaded: " << pluginName << std::endl;

                    // Store current plugin name in context
                    if (context) {
                        context->SetValue("vision.current_plugin", pluginName);
                    }
                } else if (!plugin) {
                    errorMessage = "Plugin not found: " + pluginName;
                    return false;
                }
            }
        }

        // Store profile path in context
        if (context) {
            context->SetValue("vision.profile_path", profilePath);
        }

        if (m_context && m_context->logManager) {
            m_context->logManager->Info("Vision profile loaded: " + profilePath, "VisionCommandHandler");
        }

        return true;

    } catch (const std::exception& e) {
        errorMessage = "Error loading profile: " + std::string(e.what());
        return false;
    }
}

bool VisionCommandHandler::Execute(const std::string& pluginName,
                                    std::string& errorMessage,
                                    SequenceContext* context)
{
    std::cout << "[VisionCommandHandler] 🔍 Executing plugin: " << pluginName << std::endl;

    // Check if we have an input image
    if (!VisionManager::instance().hasValidImage()) {
        errorMessage = "No input image available for vision processing";
        return false;
    }

    // Get plugin name (use provided or from context)
    QString qPluginName = QString::fromStdString(pluginName);
    if (pluginName.empty() && context) {
        std::string storedPlugin = context->GetString("vision.current_plugin", "");
        if (!storedPlugin.empty()) {
            qPluginName = QString::fromStdString(storedPlugin);
        }
    }

    if (qPluginName.isEmpty()) {
        errorMessage = "No plugin specified for execution";
        return false;
    }

    // Execute the plugin synchronously
    VisionResult result = VisionManager::instance().executePlugin(qPluginName);

    // Store results in context
    if (context) {
        StoreResultInContext(result, context);

        // ✅ Store the result image (original + overlay) in context
        cv::Mat outputImage = VisionManager::instance().getOutputImage();
        if (!outputImage.empty()) {
            context->SetImageFrame("vision.result.image", outputImage);
            std::cout << "[VisionCommandHandler] 🖼️ Result image stored: "
                      << outputImage.cols << "x" << outputImage.rows << std::endl;

            // ✅ Save to history and trigger callback for real-time UI update
            SaveVisionResultWithHistory(outputImage, context);
        } else {
            std::cout << "[VisionCommandHandler] ⚠️ No output image available from VisionManager" << std::endl;
        }
    }

    if (m_context && m_context->logManager) {
        m_context->logManager->Info("Vision execution completed: " +
            std::string(result.success ? "success" : "failed"), "VisionCommandHandler");
    }

    if (!result.success) {
        errorMessage = "Vision execution failed: " + result.errorMessage;
        return false;
    }

    return true;
}

bool VisionCommandHandler::CaptureAndExecute(const std::string& cameraName,
                                              const std::string& pluginName,
                                              std::string& errorMessage,
                                              SequenceContext* context)
{
    std::cout << "[VisionCommandHandler] 📷 Capture and execute: camera=" << cameraName
              << ", plugin=" << pluginName << std::endl;

    if (!m_context || !m_context->cameraManager) {
        errorMessage = "CameraManager not available";
        return false;
    }

    // Create temporary subscriber to capture frame
    auto subscriber = std::make_shared<LiveVideoSubscriber>(cameraName, "VisionCommandHandler_Capture");
    m_context->cameraManager->SubscribeToFrames(subscriber);
    subscriber->InitializeStatusFromManager(m_context->cameraManager);

    // Wait for frame
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    if (!subscriber->HasNewFrame()) {
        m_context->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());
        errorMessage = "No frame available from camera: " + cameraName;
        return false;
    }

    // Get frame and convert to cv::Mat
    CameraFrameData frame = subscriber->GetLatestFrame();
    m_context->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());

    if (!frame.IsValid()) {
        errorMessage = "Invalid frame data from camera";
        return false;
    }

    // Convert to cv::Mat
    cv::Mat cvImage;
    if (frame.channels == 3) {
        cvImage = cv::Mat(frame.height, frame.width, CV_8UC3, frame.imageData.data()).clone();
        cv::cvtColor(cvImage, cvImage, cv::COLOR_RGB2BGR);
    } else {
        cvImage = cv::Mat(frame.height, frame.width, CV_8UC1, frame.imageData.data()).clone();
    }

    // Set input image
    VisionManager::instance().setInputImage(cvImage);

    std::cout << "[VisionCommandHandler] 🖼️ Image captured: " << frame.width << "x" << frame.height << std::endl;

    // Now execute the plugin
    return Execute(pluginName, errorMessage, context);
}

bool VisionCommandHandler::GetResult(const std::string& measurementKey,
                                      std::string& errorMessage,
                                      SequenceContext* context)
{
    if (!context) {
        errorMessage = "No context available";
        return false;
    }

    // Results are stored as vision.result.<key>
    std::string fullKey = "vision.result." + measurementKey;

    if (!context->HasValue(fullKey)) {
        errorMessage = "Measurement not found: " + measurementKey;
        return false;
    }

    std::string value = context->GetString(fullKey, "");

    std::cout << "[VisionCommandHandler] Result retrieved: " << measurementKey << " = " << value << std::endl;
    return true;
}

void VisionCommandHandler::StoreResultInContext(const VisionResult& result, SequenceContext* context)
{
    if (!context) return;

    // Store basic info
    context->SetValue("vision.result.success", result.success);
    context->SetValue("vision.result.plugin", result.pluginName);
    context->SetValue("vision.result.processing_time_ms", static_cast<float>(result.processingTimeMs));

    if (!result.success) {
        context->SetValue("vision.result.error", result.errorMessage);
        return;
    }

    // Store image dimensions (needed for GotoDetect world position calculation)
    cv::Mat inputImage = VisionManager::instance().getInputImage();
    if (!inputImage.empty()) {
        context->SetValue("vision.result.image_width", static_cast<float>(inputImage.cols));
        context->SetValue("vision.result.image_height", static_cast<float>(inputImage.rows));
    }

    // Store all measurements (applying ROI offset for coordinate measurements)
    // Get ROI offset from metadata if available
    double roiOffsetX = 0.0, roiOffsetY = 0.0;
    if (result.metadata.count("roi_offset_x") > 0) {
        try {
            roiOffsetX = std::stod(result.metadata.at("roi_offset_x"));
        } catch (...) {}
    }
    if (result.metadata.count("roi_offset_y") > 0) {
        try {
            roiOffsetY = std::stod(result.metadata.at("roi_offset_y"));
        } catch (...) {}
    }

    for (const auto& kv : result.measurements) {
        std::string key = "vision.result." + kv.first;
        double value = kv.second;

        // ✅ Skip image_width and image_height from measurements when ROI is active
        // These are ROI dimensions, not full image dimensions
        // Full image dimensions were already stored above from inputImage
        bool isImageSize = (kv.first == "image_width" || kv.first == "image_height");
        if (isImageSize && (roiOffsetX != 0.0 || roiOffsetY != 0.0)) {
            // Skip ROI dimensions, keep full image dimensions
            std::cout << "[VisionCommandHandler] 📐 Skipping ROI dimension: " << kv.first << "=" << value
                      << " (keeping full image dimensions)" << std::endl;
            continue;
        }

        // ✅ Apply ROI offset to ALL coordinate measurements (_x, _y)
        // This converts from ROI-relative to whole-image coordinates
        bool isXCoord = (kv.first.length() >= 2 && kv.first.substr(kv.first.length() - 2) == "_x");
        bool isYCoord = (kv.first.length() >= 2 && kv.first.substr(kv.first.length() - 2) == "_y");

        // Apply offset to all X coordinates (corner_x, center_x, start_x, end_x, match_*_x, etc.)
        if (isXCoord) {
            value += roiOffsetX;
        }
        // Apply offset to all Y coordinates (corner_y, center_y, start_y, end_y, match_*_y, etc.)
        if (isYCoord) {
            value += roiOffsetY;
        }

        context->SetValue(key, static_cast<float>(value));
    }

    if (roiOffsetX != 0.0 || roiOffsetY != 0.0) {
        std::cout << "[VisionCommandHandler] 📐 Applied ROI offset to measurements: ("
                  << roiOffsetX << ", " << roiOffsetY << ") px" << std::endl;
    }

    // Store metadata
    for (const auto& kv : result.metadata) {
        std::string key = "vision.metadata." + kv.first;
        context->SetValue(key, kv.second);
    }

    // Store detected points (main results for PR)
    int pointIndex = 0;
    for (const auto& point : result.points) {
        std::string prefix = "vision.point." + std::to_string(pointIndex) + ".";
        context->SetValue(prefix + "x", static_cast<float>(point.pixelCoord.x));
        context->SetValue(prefix + "y", static_cast<float>(point.pixelCoord.y));
        context->SetValue(prefix + "mm_x", static_cast<float>(point.mmCoord.x));
        context->SetValue(prefix + "mm_y", static_cast<float>(point.mmCoord.y));
        context->SetValue(prefix + "confidence", static_cast<float>(point.confidence));
        if (!point.name.empty()) {
            context->SetValue(prefix + "name", point.name);
        }
        pointIndex++;
    }
    context->SetValue("vision.point.count", static_cast<float>(pointIndex));

    // Store detected circles
    int circleIndex = 0;
    for (const auto& circle : result.circles) {
        std::string prefix = "vision.circle." + std::to_string(circleIndex) + ".";
        context->SetValue(prefix + "x", static_cast<float>(circle.centerPx.x));
        context->SetValue(prefix + "y", static_cast<float>(circle.centerPx.y));
        context->SetValue(prefix + "radius", static_cast<float>(circle.radiusPx));
        context->SetValue(prefix + "mm_x", static_cast<float>(circle.centerMm.x));
        context->SetValue(prefix + "mm_y", static_cast<float>(circle.centerMm.y));
        context->SetValue(prefix + "radius_mm", static_cast<float>(circle.radiusMm));
        circleIndex++;
    }
    context->SetValue("vision.circle.count", static_cast<float>(circleIndex));

    // Store detected lines
    int lineIndex = 0;
    for (const auto& line : result.lines) {
        std::string prefix = "vision.line." + std::to_string(lineIndex) + ".";
        context->SetValue(prefix + "start_x", static_cast<float>(line.startPx.x));
        context->SetValue(prefix + "start_y", static_cast<float>(line.startPx.y));
        context->SetValue(prefix + "end_x", static_cast<float>(line.endPx.x));
        context->SetValue(prefix + "end_y", static_cast<float>(line.endPx.y));
        lineIndex++;
    }
    context->SetValue("vision.line.count", static_cast<float>(lineIndex));

    // Store detected rectangles
    int rectIndex = 0;
    for (const auto& rect : result.rectangles) {
        std::string prefix = "vision.rectangle." + std::to_string(rectIndex) + ".";
        context->SetValue(prefix + "center_x", static_cast<float>(rect.centerPx.x));
        context->SetValue(prefix + "center_y", static_cast<float>(rect.centerPx.y));
        context->SetValue(prefix + "width", static_cast<float>(rect.widthPx));
        context->SetValue(prefix + "height", static_cast<float>(rect.heightPx));
        context->SetValue(prefix + "angle", static_cast<float>(rect.angleDeg));
        context->SetValue(prefix + "mm_x", static_cast<float>(rect.centerMm.x));
        context->SetValue(prefix + "mm_y", static_cast<float>(rect.centerMm.y));
        rectIndex++;
    }
    context->SetValue("vision.rectangle.count", static_cast<float>(rectIndex));

    // Store detected beams
    int beamIndex = 0;
    for (const auto& beam : result.beams) {
        std::string prefix = "vision.beam." + std::to_string(beamIndex) + ".";
        context->SetValue(prefix + "center_x", static_cast<float>(beam.centerPx.x));
        context->SetValue(prefix + "center_y", static_cast<float>(beam.centerPx.y));
        context->SetValue(prefix + "center_mm_x", static_cast<float>(beam.centerMm.x));
        context->SetValue(prefix + "center_mm_y", static_cast<float>(beam.centerMm.y));
        context->SetValue(prefix + "fwhm_major_px", static_cast<float>(beam.fwhmMajorPx));
        context->SetValue(prefix + "fwhm_minor_px", static_cast<float>(beam.fwhmMinorPx));
        context->SetValue(prefix + "fwhm_major_mm", static_cast<float>(beam.fwhmMajorMm));
        context->SetValue(prefix + "fwhm_minor_mm", static_cast<float>(beam.fwhmMinorMm));
        context->SetValue(prefix + "mean_intensity", static_cast<float>(beam.meanIntensity));
        context->SetValue(prefix + "max_intensity", static_cast<float>(beam.maxIntensity));
        context->SetValue(prefix + "is_saturated", beam.isSaturated);
        context->SetValue(prefix + "angle_deg", static_cast<float>(beam.angleDeg));
        context->SetValue(prefix + "ellipticity", static_cast<float>(beam.ellipticity));
        context->SetValue(prefix + "area_px", static_cast<float>(beam.areaPx));
        if (!beam.name.empty()) {
            context->SetValue(prefix + "name", beam.name);
        }
        beamIndex++;
    }
    context->SetValue("vision.beam.count", static_cast<float>(beamIndex));

    // Store primary detected center (for GotoDetect world position calculation)
    // Priority: rectangle center > point > circle center
    bool hasPrimaryCenter = false;
    double primaryCenterX = 0.0, primaryCenterY = 0.0;

    if (!result.rectangles.empty()) {
        primaryCenterX = result.rectangles[0].centerPx.x;
        primaryCenterY = result.rectangles[0].centerPx.y;
        hasPrimaryCenter = true;
    } else if (!result.points.empty()) {
        primaryCenterX = result.points[0].pixelCoord.x;
        primaryCenterY = result.points[0].pixelCoord.y;
        hasPrimaryCenter = true;
    } else if (!result.circles.empty()) {
        primaryCenterX = result.circles[0].centerPx.x;
        primaryCenterY = result.circles[0].centerPx.y;
        hasPrimaryCenter = true;
    }

    if (hasPrimaryCenter) {
        context->SetValue("vision.result.center_x", static_cast<float>(primaryCenterX));
        context->SetValue("vision.result.center_y", static_cast<float>(primaryCenterY));
        std::cout << "[VisionCommandHandler] 📍 Primary center: (" << primaryCenterX << ", " << primaryCenterY << ") px" << std::endl;
    }

    std::cout << "[VisionCommandHandler] 📊 Results stored in context: "
              << result.measurements.size() << " measurements, "
              << pointIndex << " points, "
              << circleIndex << " circles, "
              << rectIndex << " rectangles, "
              << lineIndex << " lines, "
              << beamIndex << " beams" << std::endl;
}

bool VisionCommandHandler::ShowContextImage(const std::string& contextKey,
                                             const std::string& title,
                                             int autoCloseMs,
                                             std::string& errorMessage,
                                             SequenceContext* context)
{
    std::cout << "[VisionCommandHandler] 🖼️ ShowContextImage: key=" << contextKey
              << ", title=" << title << ", autoClose=" << autoCloseMs << "ms" << std::endl;

    if (!context) {
        errorMessage = "No context available";
        return false;
    }

    // Resolve the context key (handle $ prefix for capture_as variables)
    std::string baseKey = contextKey;
    if (!contextKey.empty() && contextKey[0] == '$') {
        baseKey = contextKey.substr(1);  // Remove $ prefix
    }

    // PRIORITY 1: Try file paths first (these contain the full processed image with overlays)
    std::vector<std::string> pathKeysToTry;
    pathKeysToTry.push_back(baseKey + ".result.image_path");
    pathKeysToTry.push_back(baseKey + ".image_path");
    pathKeysToTry.push_back("vision.result.image_path");  // Fallback to default

    for (const auto& pathKey : pathKeysToTry) {
        std::string imagePath = context->GetString(pathKey, "");
        if (!imagePath.empty()) {
            std::cout << "[VisionCommandHandler] 📂 Found image path at '" << pathKey << "': " << imagePath << std::endl;

            cv::Mat image = cv::imread(imagePath);
            if (!image.empty()) {
                std::cout << "[VisionCommandHandler] ✅ Loaded image from file: "
                          << image.cols << "x" << image.rows << std::endl;

                QString qTitle = QString::fromStdString(title.empty() ? contextKey : title);
                int closeMsCapture = autoCloseMs;

                QMetaObject::invokeMethod(qApp, [image, qTitle, closeMsCapture]() {
                    ContextImageViewer::ShowImage(image, qTitle, closeMsCapture, nullptr);
                }, Qt::QueuedConnection);

                return true;
            } else {
                std::cout << "[VisionCommandHandler] ⚠️ Failed to load image from: " << imagePath << std::endl;
            }
        }
    }

    // PRIORITY 2: Try cv::Mat stored in context
    std::vector<std::string> keysToTry;
    keysToTry.push_back(baseKey + ".result.image");
    keysToTry.push_back(baseKey + ".image");
    keysToTry.push_back(baseKey);
    keysToTry.push_back("vision.result.image");  // Fallback to default

    for (const auto& key : keysToTry) {
        std::cout << "[VisionCommandHandler] 🔍 Trying image key: " << key << std::endl;

        auto imageOpt = context->GetImageFrame(key);
        if (imageOpt.has_value() && !imageOpt.value().empty()) {
            cv::Mat image = imageOpt.value();
            std::cout << "[VisionCommandHandler] ✅ Found image at key '" << key << "': "
                      << image.cols << "x" << image.rows << std::endl;

            QString qTitle = QString::fromStdString(title.empty() ? contextKey : title);
            int closeMsCapture = autoCloseMs;

            QMetaObject::invokeMethod(qApp, [image, qTitle, closeMsCapture]() {
                ContextImageViewer::ShowImage(image, qTitle, closeMsCapture, nullptr);
            }, Qt::QueuedConnection);

            return true;
        }
    }

    errorMessage = "Image not found in context for key: " + contextKey;
    std::cout << "[VisionCommandHandler] ❌ " << errorMessage << std::endl;

    // List available image keys for debugging
    auto allKeys = context->GetAllKeys();
    std::cout << "[VisionCommandHandler] Available keys containing 'image':" << std::endl;
    for (const auto& key : allKeys) {
        if (key.find("image") != std::string::npos) {
            std::cout << "  - " << key << std::endl;
        }
    }

    return false;
}

double VisionCommandHandler::CalculateFocusMeasure(const cv::Mat& image,
                                                    int roiX, int roiY,
                                                    int roiWidth, int roiHeight)
{
    cv::Mat gray;

    // Convert to grayscale if needed
    if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = image;
    }

    // Apply ROI if specified
    cv::Mat roiImage;
    if (roiWidth > 0 && roiHeight > 0) {
        // Clamp ROI to image bounds
        int x = std::max(0, std::min(roiX, gray.cols - 1));
        int y = std::max(0, std::min(roiY, gray.rows - 1));
        int w = std::min(roiWidth, gray.cols - x);
        int h = std::min(roiHeight, gray.rows - y);

        if (w > 0 && h > 0) {
            roiImage = gray(cv::Rect(x, y, w, h));
        } else {
            roiImage = gray;
        }
    } else {
        roiImage = gray;
    }

    // Calculate Laplacian variance (focus measure)
    // Higher variance = sharper image = better focus
    cv::Mat laplacian;
    cv::Laplacian(roiImage, laplacian, CV_64F);

    cv::Scalar mean, stddev;
    cv::meanStdDev(laplacian, mean, stddev);

    // Return variance (stddev^2)
    double variance = stddev.val[0] * stddev.val[0];

    return variance;
}

bool VisionCommandHandler::ExecuteAutoFocus(const std::map<std::string, std::string>& params,
                                             std::string& errorMessage,
                                             SequenceContext* context)
{
    std::cout << "[VisionCommandHandler] 🔍 Starting auto-focus..." << std::endl;

    // Get required parameters
    auto motionDeviceIt = params.find("motion_device");
    if (motionDeviceIt == params.end() || motionDeviceIt->second.empty()) {
        errorMessage = "auto_focus requires 'motion_device' parameter";
        if (context) context->SetValue("autofocus.success", false);
        return false;
    }
    std::string motionDevice = motionDeviceIt->second;

    auto cameraIt = params.find("camera");
    if (cameraIt == params.end() || cameraIt->second.empty()) {
        errorMessage = "auto_focus requires 'camera' parameter";
        if (context) context->SetValue("autofocus.success", false);
        return false;
    }
    std::string cameraName = cameraIt->second;

    // Get optional parameters with defaults
    double zRangeMm = 0.2;
    double zStepMm = 0.05;
    int settleTimeMs = 100;
    int roiX = 0, roiY = 0, roiWidth = 0, roiHeight = 0;

    auto rangeIt = params.find("z_range_mm");
    if (rangeIt != params.end() && !rangeIt->second.empty()) {
        try { zRangeMm = std::stod(rangeIt->second); } catch (...) {}
    }

    auto stepIt = params.find("z_step_mm");
    if (stepIt != params.end() && !stepIt->second.empty()) {
        try { zStepMm = std::stod(stepIt->second); } catch (...) {}
    }

    auto settleIt = params.find("settle_time_ms");
    if (settleIt != params.end() && !settleIt->second.empty()) {
        try { settleTimeMs = std::stoi(settleIt->second); } catch (...) {}
    }

    // ROI parameters - check for centered ROI mode
    bool useCenterRoi = false;
    auto centerRoiIt = params.find("use_center_roi");
    if (centerRoiIt != params.end() && centerRoiIt->second == "true") {
        useCenterRoi = true;
    }

    auto roiWIt = params.find("roi_width");
    if (roiWIt != params.end()) { try { roiWidth = std::stoi(roiWIt->second); } catch (...) {} }

    auto roiHIt = params.find("roi_height");
    if (roiHIt != params.end()) { try { roiHeight = std::stoi(roiHIt->second); } catch (...) {} }

    // Legacy support for explicit roi_x, roi_y (if not using center mode)
    if (!useCenterRoi) {
        auto roiXIt = params.find("roi_x");
        if (roiXIt != params.end()) { try { roiX = std::stoi(roiXIt->second); } catch (...) {} }

        auto roiYIt = params.find("roi_y");
        if (roiYIt != params.end()) { try { roiY = std::stoi(roiYIt->second); } catch (...) {} }
    }

    // Context prefix for result storage (default: "autofocus")
    std::string contextPrefix = "autofocus";
    auto prefixIt = params.find("context_prefix");
    if (prefixIt != params.end() && !prefixIt->second.empty()) {
        contextPrefix = prefixIt->second;
        // Strip $ prefix if present (so $$autofocus becomes $autofocus)
        // But keep single $ for context variable references
    }

    // Validate we have required managers
    if (!m_context || !m_context->motionManager || !m_context->cameraManager) {
        errorMessage = "MotionManager or CameraManager not available";
        if (context) context->SetValue(contextPrefix + ".success", false);
        return false;
    }

    // Get current Z position
    PositionStruct startPos = m_context->motionManager->GetCurrentPosition(motionDevice);
    double startZ = startPos.z;

    std::cout << "[VisionCommandHandler] 📍 Start Z: " << startZ << " mm" << std::endl;
    std::cout << "[VisionCommandHandler] 📐 Search range: +/- " << zRangeMm << " mm, step: " << zStepMm << " mm" << std::endl;

    // Store start Z in context
    if (context) {
        context->SetValue(contextPrefix + ".start_z_mm", static_cast<float>(startZ));
    }

    // Create camera subscriber for frame capture
    auto subscriber = std::make_shared<LiveVideoSubscriber>(cameraName, "AutoFocus_Capture");
    m_context->cameraManager->SubscribeToFrames(subscriber);
    subscriber->InitializeStatusFromManager(m_context->cameraManager);

    // Calculate Z positions to scan
    double zMin = startZ - zRangeMm;
    double zMax = startZ + zRangeMm;
    int numSteps = static_cast<int>((zMax - zMin) / zStepMm) + 1;

    std::cout << "[VisionCommandHandler] 🔢 Scanning " << numSteps << " Z positions from "
              << zMin << " to " << zMax << " mm" << std::endl;

    // Scan through Z positions
    double bestZ = startZ;
    double bestFocusValue = -1.0;
    std::vector<std::pair<double, double>> focusData;  // (z, focus_value) pairs

    for (int i = 0; i < numSteps; i++) {
        double currentZ = zMin + (i * zStepMm);

        // Move to Z position (blocking = true, so it waits for completion)
        bool moveResult = m_context->motionManager->MoveToAbs(
            motionDevice,
            startPos.x, startPos.y, currentZ,
            100.0,  // velocity
            true    // blocking - wait for completion
        );

        if (!moveResult) {
            std::cout << "[VisionCommandHandler] ⚠️ Move failed at Z=" << currentZ << std::endl;
            continue;
        }

        // Wait for settle time
        std::this_thread::sleep_for(std::chrono::milliseconds(settleTimeMs));

        // Capture image
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Extra frame capture time

        if (!subscriber->HasNewFrame()) {
            std::cout << "[VisionCommandHandler] ⚠️ No frame at Z=" << currentZ << std::endl;
            continue;
        }

        CameraFrameData frame = subscriber->GetLatestFrame();
        if (!frame.IsValid()) {
            std::cout << "[VisionCommandHandler] ⚠️ Invalid frame at Z=" << currentZ << std::endl;
            continue;
        }

        // Convert to cv::Mat
        cv::Mat cvImage;
        if (frame.channels == 3) {
            cvImage = cv::Mat(frame.height, frame.width, CV_8UC3, frame.imageData.data()).clone();
        } else {
            cvImage = cv::Mat(frame.height, frame.width, CV_8UC1, frame.imageData.data()).clone();
        }

        // Calculate centered ROI position if using center mode
        int actualRoiX = roiX;
        int actualRoiY = roiY;
        if (useCenterRoi && roiWidth > 0 && roiHeight > 0) {
            actualRoiX = (cvImage.cols - roiWidth) / 2;
            actualRoiY = (cvImage.rows - roiHeight) / 2;
            // Clamp to valid range
            actualRoiX = std::max(0, actualRoiX);
            actualRoiY = std::max(0, actualRoiY);

            // Log ROI info on first frame
            if (i == 0) {
                std::cout << "[VisionCommandHandler] 📐 Image: " << cvImage.cols << "x" << cvImage.rows
                          << ", Center ROI: (" << actualRoiX << "," << actualRoiY << ") "
                          << roiWidth << "x" << roiHeight << " px" << std::endl;
            }
        }

        // Calculate focus measure
        double focusValue = CalculateFocusMeasure(cvImage, actualRoiX, actualRoiY, roiWidth, roiHeight);
        focusData.push_back({currentZ, focusValue});

        std::cout << "[VisionCommandHandler] 📊 Z=" << std::fixed << std::setprecision(3)
                  << currentZ << " mm, Focus=" << std::setprecision(2) << focusValue << std::endl;

        // Track best focus
        if (focusValue > bestFocusValue) {
            bestFocusValue = focusValue;
            bestZ = currentZ;
        }
    }

    // Unsubscribe from camera
    m_context->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());

    // Check if we found a valid focus position
    if (bestFocusValue < 0) {
        errorMessage = "Auto-focus failed: no valid focus measurements";
        if (context) context->SetValue(contextPrefix + ".success", false);
        // Return to start position (blocking)
        (void)m_context->motionManager->MoveToAbs(motionDevice, startPos.x, startPos.y, startZ, 100.0, true);
        return false;
    }

    std::cout << "[VisionCommandHandler] ✅ Best focus at Z=" << bestZ
              << " mm (value=" << bestFocusValue << ")" << std::endl;

    // Move to best focus position (blocking)
    bool moveResult = m_context->motionManager->MoveToAbs(
        motionDevice, startPos.x, startPos.y, bestZ, 100.0, true
    );

    if (!moveResult) {
        std::cout << "[VisionCommandHandler] ⚠️ Failed to move to best focus position" << std::endl;
    } else {
        std::cout << "[VisionCommandHandler] 📍 Moved to best focus Z=" << bestZ << " mm" << std::endl;
    }

    // Store results in context using the configured prefix
    if (context) {
        context->SetValue(contextPrefix + ".success", true, "vision", "auto_focus");
        context->SetValue(contextPrefix + ".best_z_mm", static_cast<float>(bestZ), "vision", "auto_focus");
        context->SetValue(contextPrefix + ".best_focus_value", static_cast<float>(bestFocusValue), "vision", "auto_focus");
        context->SetValue(contextPrefix + ".start_z_mm", static_cast<float>(startZ), "vision", "auto_focus");
        context->SetValue(contextPrefix + ".z_range_mm", static_cast<float>(zRangeMm), "vision", "auto_focus");
        context->SetValue(contextPrefix + ".z_step_mm", static_cast<float>(zStepMm), "vision", "auto_focus");
        context->SetValue(contextPrefix + ".num_measurements", static_cast<float>(focusData.size()), "vision", "auto_focus");

        std::cout << "[VisionCommandHandler] 📝 Stored 7 context values (" << contextPrefix << ".*)" << std::endl;

        // Verify storage
        auto summary = context->GetSummary();
        std::cout << "[VisionCommandHandler] 📊 Context now has " << summary.capturedValues.size() << " total values" << std::endl;
    } else {
        std::cout << "[VisionCommandHandler] ⚠️ No context available to store autofocus results!" << std::endl;
    }

    std::cout << "[VisionCommandHandler] ✅ Auto-focus complete: best Z=" << bestZ
              << " mm (delta from start: " << (bestZ - startZ) << " mm)" << std::endl;

    return true;
}

// =============================================================================
// VISION HISTORY MANAGEMENT
// =============================================================================

std::string VisionCommandHandler::SaveVisionResultWithHistory(const cv::Mat& image, SequenceContext* context)
{
    if (image.empty()) {
        std::cout << "[VisionCommandHandler] ⚠️ Cannot save empty image to history" << std::endl;
        return "";
    }

    // Ensure history directory exists
    QDir dir(QString::fromStdString(HISTORY_DIR));
    if (!dir.exists()) {
        dir.mkpath(".");
        std::cout << "[VisionCommandHandler] 📁 Created vision history directory: " << HISTORY_DIR << std::endl;
    }

    // Generate filename with current index (1-based for display: vision_001.png to vision_010.png)
    std::ostringstream filename;
    filename << HISTORY_DIR << "/vision_" << std::setfill('0') << std::setw(3) << (s_historyIndex + 1) << ".png";
    std::string filePath = filename.str();

    // Save the image
    bool saved = cv::imwrite(filePath, image);
    if (!saved) {
        std::cout << "[VisionCommandHandler] ❌ Failed to save vision result to: " << filePath << std::endl;
        return "";
    }

    std::cout << "[VisionCommandHandler] 🖼️ Vision result saved to history: " << filePath
              << " (index " << (s_historyIndex + 1) << "/" << MAX_HISTORY << ")" << std::endl;

    // Store the path and current index in context
    if (context) {
        context->SetValue("vision.result.image_path", filePath);
        context->SetValue("vision.history.current_index", static_cast<float>(s_historyIndex));
        context->SetValue("vision.history.total_count", static_cast<float>(std::min(s_historyIndex + 1, MAX_HISTORY)));
    }

    // Also save to the legacy temp file for backward compatibility
    std::string tempPath = "logs/vision_result_temp.png";
    cv::imwrite(tempPath, image);

    // Advance the history index (circular buffer)
    s_historyIndex = (s_historyIndex + 1) % MAX_HISTORY;

    // Call the VisionResultCallback if registered (for real-time UI updates)
    if (m_context && m_context->sequenceEngine) {
        auto callback = m_context->sequenceEngine->GetVisionResultCallback();
        if (callback) {
            std::cout << "[VisionCommandHandler] 📤 Calling VisionResultCallback for: " << filePath << std::endl;
            callback(filePath);
        }
    }

    return filePath;
}

#endif // HAS_OPENCV

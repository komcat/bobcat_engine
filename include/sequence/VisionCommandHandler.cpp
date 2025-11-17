// VisionCommandHandler.cpp
#ifdef HAS_OPENCV

#include "VisionCommandHandler.h"
#include "SequenceContext.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "../vision/VisionManager.h"
#include "../vision/VisionResult.h"
#include "../vision/VisionROI.h"
#include "../motions/unified_motion_manager.h"  // For GetCurrentPosition
#include "../motions/MotionTypes.h"              // For PositionStruct
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <chrono>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>  // For cv::imwrite

bool VisionCommandHandler::ExecuteCommand(
    void* manager,
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
            auto summary = context->GetSummary();

            std::cout << "[VisionCommandHandler] 📋 Copying results to prefix: " << captureAs << std::endl;

            // Copy all vision.* values to <capture_as>.*
            for (const auto& [key, value] : summary.capturedValues) {
                // Check if key starts with "vision."
                if (key.find("vision.") == 0) {
                    // Extract the part after "vision." and prepend with capture_as
                    std::string suffix = key.substr(7);  // Remove "vision."
                    std::string newKey = captureAs + "." + suffix;

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

            std::cout << "[VisionCommandHandler] ✅ Results copied to prefix: " << captureAs << std::endl;
        }

        // Step 4: Calculate world position if requested (GotoDetect feature)
        auto calcWorldIt = params.find("calculate_world_position");
        auto motionDeviceIt = params.find("motion_device");
        if (calcWorldIt != params.end() && calcWorldIt->second == "true" &&
            motionDeviceIt != params.end() && !motionDeviceIt->second.empty() && context) {

            std::string motionDevice = motionDeviceIt->second;
            std::cout << "[VisionCommandHandler] 🌍 Calculating world position for device: " << motionDevice << std::endl;

            // Get detected feature center (from vision result)
            if (context->HasValue("vision.result.center_x") && context->HasValue("vision.result.center_y")) {
                double detectedCenterX_px = context->GetNumeric("vision.result.center_x", 0.0);
                double detectedCenterY_px = context->GetNumeric("vision.result.center_y", 0.0);

                // Check if anchor point is available (for pattern matching with custom anchor)
                // If anchor exists, use it instead of center for more precise positioning
                double referenceX_px = detectedCenterX_px;
                double referenceY_px = detectedCenterY_px;
                bool usingAnchor = false;

                if (context->HasValue("vision.result.match_0_anchor_x") &&
                    context->HasValue("vision.result.match_0_anchor_y")) {
                    referenceX_px = context->GetNumeric("vision.result.match_0_anchor_x", detectedCenterX_px);
                    referenceY_px = context->GetNumeric("vision.result.match_0_anchor_y", detectedCenterY_px);
                    usingAnchor = true;
                    std::cout << "[VisionCommandHandler] 📍 Using custom anchor point: ("
                              << referenceX_px << ", " << referenceY_px << ") px" << std::endl;
                } else {
                    std::cout << "[VisionCommandHandler] 📍 Using pattern center: ("
                              << referenceX_px << ", " << referenceY_px << ") px" << std::endl;
                }

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

                std::cout << "[VisionCommandHandler] 📐 Detected center: (" << detectedCenterX_px << ", " << detectedCenterY_px << ") px" << std::endl;
                std::cout << "[VisionCommandHandler] 📐 Image center: (" << imageCenterX_px << ", " << imageCenterY_px << ") px" << std::endl;
                std::cout << "[VisionCommandHandler] 📐 Offset from center: (" << offsetX_px << ", " << offsetY_px << ") px" << std::endl;
                std::cout << "[VisionCommandHandler] 📐 Offset in mm: (" << offsetX_mm << ", " << offsetY_mm << ") mm" << std::endl;

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
                    context->SetValue(captureAs + ".detected.device_z_mm", static_cast<float>(deviceZ_mm));
                    context->SetValue(captureAs + ".detected.reference_x_px", static_cast<float>(referenceX_px));
                    context->SetValue(captureAs + ".detected.reference_y_px", static_cast<float>(referenceY_px));
                    context->SetValue(captureAs + ".detected.using_anchor", usingAnchor);
                }

                std::cout << "[VisionCommandHandler] ✅ World position calculated and stored" << std::endl;
            } else {
                std::cout << "[VisionCommandHandler] ⚠️ No detected center point found in vision result" << std::endl;
            }
        }

        std::cout << "[VisionCommandHandler] ✅ Vision execution completed: success" << std::endl;
        return true;  // Always return TRUE
    }
    else if (methodName == "execute") {
        // Execute vision plugin
        // deviceName = plugin name (or empty to use currently loaded plugin)
        std::string pluginName = deviceName;
        if (pluginName.empty()) {
            auto it = params.find("plugin");
            if (it != params.end()) {
                pluginName = it->second;
            }
        }
        return Execute(pluginName, errorMessage, context);
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

            // ✅ Also save to temporary file for cross-thread access
            std::string tempPath = "logs/vision_result_temp.png";
            cv::imwrite(tempPath, outputImage);
            context->SetValue("vision.result.image_path", tempPath);
            std::cout << "[VisionCommandHandler] 🖼️ Result image saved to: " << tempPath << std::endl;
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

        // Apply ROI offset to coordinate measurements (match_*_x, match_*_y, match_*_anchor_x, match_*_anchor_y)
        // This converts from ROI-relative to whole-image coordinates
        bool isXCoord = (kv.first.length() >= 2 && kv.first.substr(kv.first.length() - 2) == "_x");
        bool isYCoord = (kv.first.length() >= 2 && kv.first.substr(kv.first.length() - 2) == "_y");
        bool isMatchCoord = (kv.first.find("match_") == 0);  // Starts with "match_"

        if (isMatchCoord && isXCoord) {
            value += roiOffsetX;
        }
        if (isMatchCoord && isYCoord) {
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
              << lineIndex << " lines" << std::endl;
}

#endif // HAS_OPENCV

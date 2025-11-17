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

        // Load calibration
        if (j.contains("camera")) {
            auto camJson = j["camera"];
            double pixelToMmX = camJson.value("pixelToMmX", 0.00248);
            double pixelToMmY = camJson.value("pixelToMmY", 0.00248);
            VisionManager::instance().setPixelToMm(pixelToMmX, pixelToMmY);

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

    // Store all measurements
    for (const auto& kv : result.measurements) {
        std::string key = "vision.result." + kv.first;
        context->SetValue(key, static_cast<float>(kv.second));
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

    std::cout << "[VisionCommandHandler] 📊 Results stored in context: "
              << result.measurements.size() << " measurements, "
              << pointIndex << " points, "
              << circleIndex << " circles, "
              << lineIndex << " lines" << std::endl;
}

#endif // HAS_OPENCV

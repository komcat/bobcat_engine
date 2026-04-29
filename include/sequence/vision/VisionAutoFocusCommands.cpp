// VisionAutoFocusCommands.cpp
// Auto-focus family. See VisionAutoFocusCommands.h.
#include "VisionAutoFocusCommands.h"

#ifdef HAS_OPENCV

#include "ApplicationContext.h"
#include "SequenceContext.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "../../motions/unified_motion_manager.h"
#include "../../motions/MotionTypes.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

// Focus measure (Laplacian variance) — pure compute, no orchestrator state.
double CalculateFocusMeasure(const cv::Mat& image,
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

} // anonymous namespace

bool VisionAutoFocusCommands::ExecuteAutoFocus(const std::map<std::string, std::string>& params,
                                             std::string& errorMessage,
                                             SequenceContext* context,
                                                 const VisionCommandDeps& deps)
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
    if (!deps.app || !deps.app->motionManager || !deps.app->cameraManager) {
        errorMessage = "MotionManager or CameraManager not available";
        if (context) context->SetValue(contextPrefix + ".success", false);
        return false;
    }

    // Get current Z position
    PositionStruct startPos = deps.app->motionManager->GetCurrentPosition(motionDevice);
    double startZ = startPos.z;

    std::cout << "[VisionCommandHandler] 📍 Start Z: " << startZ << " mm" << std::endl;
    std::cout << "[VisionCommandHandler] 📐 Search range: +/- " << zRangeMm << " mm, step: " << zStepMm << " mm" << std::endl;

    // Store start Z in context
    if (context) {
        context->SetValue(contextPrefix + ".start_z_mm", static_cast<float>(startZ));
    }

    // Create camera subscriber for frame capture
    auto subscriber = std::make_shared<LiveVideoSubscriber>(cameraName, "AutoFocus_Capture");
    deps.app->cameraManager->SubscribeToFrames(subscriber);
    subscriber->InitializeStatusFromManager(deps.app->cameraManager);

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
        bool moveResult = deps.app->motionManager->MoveToAbs(
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
    deps.app->cameraManager->UnsubscribeFromFrames(subscriber->GetSubscriberId());

    // Check if we found a valid focus position
    if (bestFocusValue < 0) {
        errorMessage = "Auto-focus failed: no valid focus measurements";
        if (context) context->SetValue(contextPrefix + ".success", false);
        // Return to start position (blocking)
        (void)deps.app->motionManager->MoveToAbs(motionDevice, startPos.x, startPos.y, startZ, 100.0, true);
        return false;
    }

    std::cout << "[VisionCommandHandler] ✅ Best focus at Z=" << bestZ
              << " mm (value=" << bestFocusValue << ")" << std::endl;

    // Move to best focus position (blocking)
    bool moveResult = deps.app->motionManager->MoveToAbs(
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

std::optional<bool> VisionAutoFocusCommands::TryExecute(
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const VisionCommandDeps& deps)
{
    if (methodName == "auto_focus") {
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

        return ExecuteAutoFocus(params, errorMessage, context, deps);
    }

  return std::nullopt;
}

#endif // HAS_OPENCV

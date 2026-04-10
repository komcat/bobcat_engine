// VisionWizardState.h
// Shared state struct for the wizard-style VisionManagerPage
#pragma once

#include <QString>
#include <functional>
#include "VisionRightPanelWidget.h"  // VisionCameraSettings, VisionLightingSettings

#ifdef HAS_OPENCV
#include "../VisionROI.h"
#endif

struct VisionWizardState {
    // Step 1: Camera & Lighting
    QString selectedCameraId;
    VisionCameraSettings cameraSettings;
    VisionLightingSettings lightingSettings;
    bool cameraConnected = false;
    bool cameraGrabbing = false;

    // Step 2: Capture & ROI
    bool hasValidImage = false;
#ifdef HAS_OPENCV
    VisionROI roi;
#endif
    bool roiEnabled = false;
    int searchBoxStartX = 0;
    int searchBoxStartY = 0;
    int searchBoxEndX = 800;
    int searchBoxEndY = 600;
    bool searchBoxEnabled = false;

    // Step 3: Plugin
    QString selectedPluginName;
    bool pluginHasValidParams = false;

    // Profile management
    QString currentProfilePath;
    QString currentRecipePath;
    bool profileModified = false;

    // Template selection mode
    bool templateSelectionMode = false;
    std::function<void(int, int, int, int)> templateSelectionOnConfirm;
    std::function<void()> templateSelectionOnCancel;

    // ========== Caution Logic ==========

    [[nodiscard]] bool isCameraCautionActive() const {
        return selectedCameraId.isEmpty() || !cameraConnected;
    }

    [[nodiscard]] bool isCaptureCautionActive() const {
        return !hasValidImage;
    }

    [[nodiscard]] bool isPluginCautionActive() const {
        return selectedPluginName.isEmpty();
    }

    [[nodiscard]] bool isExecuteCautionActive() const {
        return isCameraCautionActive() || isCaptureCautionActive() || isPluginCautionActive();
    }
};

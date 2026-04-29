// VisionContinuousCommands.cpp
// Continuous-mode + CV-inject family. See VisionContinuousCommands.h.
#include "VisionContinuousCommands.h"

#ifdef HAS_OPENCV

#include "VisionCommandHandler.h"
#include "ApplicationContext.h"
#include "../../GdiWindow/GdiCameraWindowManager.h"
#include "SequenceEngine.h"

#include <iostream>
#include <string>

std::optional<bool> VisionContinuousCommands::TryExecute(
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* /*context*/,
  const VisionCommandDeps& deps)
{
    if (methodName == "start_continuous") {
        // Start continuous vision processing on GdiCameraWindow
        // params: "profile_path" (optional) = vision profile to select before starting
        if (!deps.app || !deps.app->gdiCameraWindowManager) {
            errorMessage = "GdiCameraWindowManager not available";
            return false;
        }

        auto profileIt = params.find("profile_path");
        if (profileIt != params.end() && !profileIt->second.empty()) {
            deps.app->gdiCameraWindowManager->selectVisionProfile(profileIt->second);
            std::cout << "[VisionCommandHandler] Selected continuous vision profile: " << profileIt->second << std::endl;
        }

        deps.app->gdiCameraWindowManager->setVisionEnabled(true);
        std::cout << "[VisionCommandHandler] Continuous vision started" << std::endl;
        return true;
    }
    if (methodName == "stop_continuous") {
        // Stop continuous vision processing on GdiCameraWindow
        if (!deps.app || !deps.app->gdiCameraWindowManager) {
            errorMessage = "GdiCameraWindowManager not available";
            return false;
        }

        deps.app->gdiCameraWindowManager->setVisionEnabled(false);
        std::cout << "[VisionCommandHandler] Continuous vision stopped" << std::endl;
        return true;
    }
    if (methodName == "enable_cv_inject") {
        // Enable cv.* context injection from continuous vision
        if (!deps.app || !deps.app->sequenceEngine) {
            errorMessage = "SequenceEngine not available";
            return false;
        }

        deps.app->sequenceEngine->SetCvInjectEnabled(true);
        std::cout << "[VisionCommandHandler] CV inject enabled" << std::endl;
        return true;
    }
    if (methodName == "disable_cv_inject") {
        // Disable cv.* context injection from continuous vision
        if (!deps.app || !deps.app->sequenceEngine) {
            errorMessage = "SequenceEngine not available";
            return false;
        }

        deps.app->sequenceEngine->SetCvInjectEnabled(false);
        std::cout << "[VisionCommandHandler] CV inject disabled" << std::endl;
        return true;
    }

  return std::nullopt;
}

#endif // HAS_OPENCV

// VisionTestCommands.cpp
// Test/scaffolding family. See VisionTestCommands.h.
#include "VisionTestCommands.h"

#ifdef HAS_OPENCV

#include "SequenceContext.h"
#include "../../vision/VisionManager.h"

#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <string>

std::optional<bool> VisionTestCommands::TryExecute(
  const std::string& /*deviceName*/,
  const std::string& methodName,
  const std::map<std::string, std::string>& params,
  std::string& errorMessage,
  SequenceContext* context,
  const VisionCommandDeps& /*deps*/)
{
    if (methodName == "load_image_from_file") {
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

  return std::nullopt;
}

#endif // HAS_OPENCV

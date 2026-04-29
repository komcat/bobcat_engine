// VisionProfileCommands.h
// Family: vision profile execution pipeline (the bulk of VisionCommandHandler).
//   - load_profile        : load a profile JSON
//   - execute_profile     : full integrated pipeline (load + capture + run + extract)
//   - execute             : run a previously-loaded plugin
//   - capture_and_execute : capture a fresh frame and run a plugin
//   - get_result          : read a measurement from the cached last result
//   - show_context_image  : display an image stored in SequenceContext
//
// Owns 7 implementation methods + static history-rotation state +
// ApplyNoMatchGuardToContext (in .cpp anon namespace).
#pragma once

#include "VisionCommandDeps.h"
#include <map>
#include <optional>
#include <string>

#ifdef HAS_OPENCV
namespace cv { class Mat; }
#endif

class SequenceContext;
struct VisionResult;

class VisionProfileCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const VisionCommandDeps& deps);

private:
  static bool LoadProfile(const std::string& profilePath,
                          std::string& errorMessage,
                          SequenceContext* context,
                          bool skipCameraSettings,
                          const VisionCommandDeps& deps);
  static bool Execute(const std::string& pluginName,
                      std::string& errorMessage,
                      SequenceContext* context,
                      const VisionCommandDeps& deps);
  static bool CaptureAndExecute(const std::string& cameraName,
                                const std::string& pluginName,
                                std::string& errorMessage,
                                SequenceContext* context,
                                const VisionCommandDeps& deps);
  static bool GetResult(const std::string& measurementKey,
                        std::string& errorMessage,
                        SequenceContext* context);
  static void StoreResultInContext(const VisionResult& result, SequenceContext* context);
  static bool ShowContextImage(const std::string& contextKey,
                               const std::string& title,
                               int autoCloseMs,
                               std::string& errorMessage,
                               SequenceContext* context);
#ifdef HAS_OPENCV
  static std::string SaveVisionResultWithHistory(const cv::Mat& image,
                                                 SequenceContext* context,
                                                 const VisionCommandDeps& deps);

  // History-rotation state (moved from VisionCommandHandler)
  static int s_historyIndex;
  static const int MAX_HISTORY = 10;
  static const std::string HISTORY_DIR;
#endif
};

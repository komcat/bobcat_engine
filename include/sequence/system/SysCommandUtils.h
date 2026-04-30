// SysCommandUtils.h
// Free functions shared by multiple system command families:
//   - ExpandContextReferences : substitutes "context:varName" tokens with
//     formatted values from the SequenceContext.
//   - SaveFrameDataToFile     : writes a CameraFrameData buffer to a PNG file.
// Lifted verbatim from SystemCommandHandler.cpp during the per-family split.
#pragma once

#include <string>

class SequenceContext;
struct CameraFrameData;

namespace SysCommandUtils {

// Replace all "context:variable_name" occurrences in `input` with the
// formatted value of that variable from `context`. Numeric values are
// formatted with 4-decimal precision and trailing zeros trimmed; missing
// variables become "[varName: N/A]". Returns `input` unchanged when no
// references are present (or `context` is null).
std::string ExpandContextReferences(const std::string& input,
                                    SequenceContext* context);

// Save `frameData` to `filepath` as a PNG (RGB888 / Grayscale8 / RGBA8888
// depending on `frameData.channels`). Returns false on bad channel count,
// invalid frame data, or QImage::save failure, with `errorMessage` populated.
bool SaveFrameDataToFile(const CameraFrameData& frameData,
                         const std::string& filepath,
                         std::string& errorMessage);

}  // namespace SysCommandUtils

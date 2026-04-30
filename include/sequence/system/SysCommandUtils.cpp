// SysCommandUtils.cpp — see header for description.
#include "SysCommandUtils.h"

#include "SequenceContext.h"
#include "CameraFrameData.h"

#include <iomanip>
#include <regex>
#include <sstream>
#include <utility>
#include <vector>

#include <QImage>

namespace SysCommandUtils {

std::string ExpandContextReferences(const std::string& input, SequenceContext* context)
{
  if (!context || input.find("context:") == std::string::npos) {
    return input;  // No context or no references to expand
  }

  std::string result = input;
  std::regex contextPattern("context:([a-zA-Z_][a-zA-Z0-9_]*)");
  std::smatch match;
  std::string::const_iterator searchStart(result.cbegin());

  // Find all context:variable_name patterns and replace them
  std::string expanded = result;
  std::sregex_iterator iter(result.begin(), result.end(), contextPattern);
  std::sregex_iterator end;

  // Collect all matches first to avoid iterator invalidation
  std::vector<std::pair<std::string, std::string>> replacements;
  for (; iter != end; ++iter) {
    std::string fullMatch = (*iter)[0].str();  // e.g., "context:offset_x"
    std::string varName = (*iter)[1].str();    // e.g., "offset_x"

    // Try to get the value from context
    std::string valueStr;
    if (context->HasValue(varName)) {
      // Check if it's numeric or string
      float numValue = context->GetNumeric(varName, 0.0f);
      std::string strValue = context->GetString(varName, "");

      if (!strValue.empty()) {
        valueStr = strValue;
      } else {
        // Format numeric value nicely
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4) << numValue;
        valueStr = oss.str();
        // Remove trailing zeros
        valueStr.erase(valueStr.find_last_not_of('0') + 1, std::string::npos);
        if (valueStr.back() == '.') valueStr.pop_back();
      }
    } else {
      valueStr = "[" + varName + ": N/A]";  // Variable not found
    }

    replacements.push_back({fullMatch, valueStr});
  }

  // Apply replacements in reverse order to maintain string positions
  for (const auto& [from, to] : replacements) {
    size_t pos = expanded.find(from);
    if (pos != std::string::npos) {
      expanded.replace(pos, from.length(), to);
    }
  }

  return expanded;
}

bool SaveFrameDataToFile(const CameraFrameData& frameData,
                         const std::string& filepath,
                         std::string& errorMessage)
{
  if (!frameData.IsValid()) {
    errorMessage = "Invalid frame data";
    return false;
  }

  // Determine QImage format based on channels
  QImage::Format format;
  if (frameData.channels == 3) {
    format = QImage::Format_RGB888;
  } else if (frameData.channels == 1) {
    format = QImage::Format_Grayscale8;
  } else if (frameData.channels == 4) {
    format = QImage::Format_RGBA8888;
  } else {
    errorMessage = "Unsupported channel count: " + std::to_string(frameData.channels);
    return false;
  }

  // Create QImage from raw data
  QImage image(
    frameData.imageData.data(),
    frameData.width,
    frameData.height,
    frameData.width * frameData.channels,
    format
  );

  // Save as PNG
  if (!image.save(QString::fromStdString(filepath), "PNG")) {
    errorMessage = "Failed to save image to: " + filepath;
    return false;
  }

  return true;
}

}  // namespace SysCommandUtils

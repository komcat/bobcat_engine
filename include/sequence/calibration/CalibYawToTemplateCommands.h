// CalibYawToTemplateCommands.h
// Family F: template-relative yaw correction commands.
//   - LoadYawToTemplateData             : load template + target corner pairs
//   - CalculateYawToTemplateCorrection  : compute correction relative to template
//   - ApplyYawToTemplateCorrection      : apply the computed correction
//   - ShowYawToTemplateResult           : surface result back to context
// Owns the `config/SYSTEM/yaw_to_template_<uid>.json` file + load/save impls.
// Sibling of CalibYawCorrectionCommands but kept separate because the data
// shape and workflow differ (template-relative vs absolute corner pair).
#pragma once

#include "CalibCommandDeps.h"
#include <map>
#include <optional>
#include <string>

class SequenceContext;

class CalibYawToTemplateCommands {
public:
  static std::optional<bool> TryExecute(
    const std::string& deviceName,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const CalibCommandDeps& deps);
};

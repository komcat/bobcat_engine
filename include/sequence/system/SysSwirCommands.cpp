// SysSwirCommands.cpp — see header for description.
#include "SysSwirCommands.h"

#include "../SystemCommandHandler.h"
#include "SequenceContext.h"

#include <string>

std::optional<bool> SysSwirCommands::TryExecute(
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context,
    const SysCommandDeps& deps)
{
  // ============================================================================
  // SAVE SWIR PACKAGE — saves raw + overlay + per-beam crops + collage
  // ============================================================================
  if (methodName == "SaveSwirPackage") {
    auto saveSwirPackageCallback = deps.parent ? deps.parent->GetSaveSwirPackageCallback() : nullptr;
    if (!saveSwirPackageCallback) {
      errorMessage = "SaveSwirPackage: handler not wired (SwirBeamWindowManager unavailable)";
      return false;
    }

    auto dirIt = params.find("dir_path");
    auto prefixIt = params.find("prefix");
    if (prefixIt == params.end() || prefixIt->second.empty()) {
      errorMessage = "SaveSwirPackage: missing required 'prefix' parameter";
      return false;
    }
    // Default directory mirrors the SAVE PKG button: logs/swir_captures/<prefix>/
    std::string dir = (dirIt != params.end() && !dirIt->second.empty())
                      ? dirIt->second
                      : ("logs/swir_captures/" + prefixIt->second);

    bool ok = saveSwirPackageCallback(dir, prefixIt->second);
    if (!ok) {
      errorMessage = "SaveSwirPackage: save failed (no frame? window not visible?)";
      return false;
    }

    if (context) {
      auto pkgCaptureAsIt = params.find("captureAs");
      if (pkgCaptureAsIt != params.end()) {
        context->SetValue(pkgCaptureAsIt->second, dir, "system", "SaveSwirPackage");
      }
    }
    return true;
  }

  // ============================================================================
  // GET SWIR BEAM STATE — snapshot live SWIR window beam results into context
  //
  // Writes:
  //   swir.num_beams                                                  (int)
  //   swir.{i}.label, .ga_label                                       (string)
  //   swir.{i}.cx_px, .cy_px, .cx_um, .cy_um                          (double)
  //   swir.{i}.d4_x_px, .d4_y_px, .d4_x_um, .d4_y_um                  (double)
  //   swir.{i}.area_um2 (= pi * (dx/2)*(dy/2) in um, ellipse area)    (double)
  //   swir.{i}.peak_intensity, .ee_pct                                (double)
  //   swir.{i}.saturated (0/1), .sat_pixel_count                      (int)
  //   swir.{i}.total_energy, .circle_energy                           (double)
  // ============================================================================
  if (methodName == "GetSwirBeamState") {
    auto getSwirBeamStateCallback = deps.parent ? deps.parent->GetGetSwirBeamStateCallback() : nullptr;
    if (!getSwirBeamStateCallback) {
      errorMessage = "GetSwirBeamState: handler not wired";
      return false;
    }
    if (!context) {
      errorMessage = "GetSwirBeamState: SequenceContext is null";
      return false;
    }
    auto snaps = getSwirBeamStateCallback();
    context->SetValue("swir.num_beams",
                       std::to_string(snaps.size()),
                       "system", methodName);
    for (size_t i = 0; i < snaps.size(); ++i) {
      const auto& s = snaps[i];
      const std::string p = "swir." + std::to_string(i) + ".";
      const double upp = s.micronsPerPixel;
      const double dxUm = s.dx_px * upp;
      const double dyUm = s.dy_px * upp;
      const double areaUm2 = 3.14159265358979323846 * (dxUm * 0.5) * (dyUm * 0.5);
      // Store all numerics as strings (ctx.getNumeric in Lua parses them).
      // This sidesteps SetValue's ambiguous double/int overloads.
      auto setD = [&](const char* k, double v) {
        context->SetValue(p + k, std::to_string(v), "system", methodName);
      };
      auto setS = [&](const char* k, const std::string& v) {
        context->SetValue(p + k, v, "system", methodName);
      };
      setS("label", s.label);
      setS("ga_label", s.gaLabel);
      setD("cx_px", s.cx_px);
      setD("cy_px", s.cy_px);
      setD("cx_um", s.cx_px * upp);
      setD("cy_um", s.cy_px * upp);
      setD("d4_x_px", s.dx_px);
      setD("d4_y_px", s.dy_px);
      setD("d4_x_um", dxUm);
      setD("d4_y_um", dyUm);
      setD("area_um2", areaUm2);
      setD("peak_intensity", static_cast<double>(s.peakIntensity));
      setD("ee_pct", s.encircledEnergy * 100.0);
      setD("saturated", s.saturated ? 1.0 : 0.0);
      setD("sat_pixel_count", static_cast<double>(s.saturatedPixelCount));
      setD("total_energy", s.totalEnergy);
      setD("circle_energy", s.circleEnergy);
    }
    return true;
  }

  // ============================================================================
  // SET BEAM DETECT / SET AUTO SAT — programmatic toggles for the SWIR window
  // ============================================================================
  if (methodName == "SetBeamDetect" || methodName == "SetAutoSat") {
    auto enabledIt = params.find("enabled");
    if (enabledIt == params.end()) {
      errorMessage = methodName + ": missing required 'enabled' parameter (true/false)";
      return false;
    }
    const std::string& v = enabledIt->second;
    bool enable = (v == "true" || v == "1" || v == "True" || v == "TRUE");

    if (methodName == "SetBeamDetect") {
      auto beamDetectToggleCallback = deps.parent ? deps.parent->GetBeamDetectToggleCallback() : nullptr;
      if (!beamDetectToggleCallback) {
        errorMessage = "SetBeamDetect: handler not wired";
        return false;
      }
      beamDetectToggleCallback(enable);
    } else {
      auto autoSatToggleCallback = deps.parent ? deps.parent->GetAutoSatToggleCallback() : nullptr;
      if (!autoSatToggleCallback) {
        errorMessage = "SetAutoSat: handler not wired";
        return false;
      }
      autoSatToggleCallback(enable);
    }
    return true;
  }

  return std::nullopt;  // not our family
}

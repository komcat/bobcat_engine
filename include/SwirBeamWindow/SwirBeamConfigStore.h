// SwirBeamConfigStore.h
// JSON I/O for the SWIR window's beam-detection config triplet
// (d4sigma + blob + halcon) plus the cross-cutting tracking flags.
//
// This was extracted from SwirBeamWindow::LoadBeamConfig / SaveBeamConfig
// so the window class can stay focused on rendering and message dispatch.
// Behaviour is byte-identical to the previous in-line code; only the home
// has changed.
#pragma once

#include <string>
#include "SwirBeamDetector.h"

namespace SwirBeamConfigStore {

// Load reads `config/beam_detection.local.json` first, falling back to
// `config/beam_detection.json`. Missing keys leave the corresponding output
// field at its caller-supplied default. Returns the path that was actually
// read, or "" if neither file existed / parse failed.
std::string Load(BeamDetectionConfig& d4,
                 BlobDetectionConfig& blob,
                 HalconBeamDetectionConfig& halcon,
                 bool& useBlob,
                 bool& useHalcon,
                 bool& trackingEnabled,
                 double& trackingMaxJumpPx,
                 int& trackingStaleMax);

// Save writes the merged config to `config/beam_detection.local.json` in
// pretty-printed form (4-space indent + trailing newline). Returns true on
// success.
bool Save(const BeamDetectionConfig& d4,
          const BlobDetectionConfig& blob,
          const HalconBeamDetectionConfig& halcon,
          bool useBlob,
          bool useHalcon,
          bool trackingEnabled,
          double trackingMaxJumpPx,
          int trackingStaleMax);

}  // namespace SwirBeamConfigStore

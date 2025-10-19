// GripperSequenceHelpers.h
#pragma once

#include "SequenceEngine.h"
#include <string>

namespace GripperSequenceHelpers {

  // Configuration struct for gripper operations
  struct GripperConfig {
    std::string gripperDevice;      // e.g., "hex-left"
    std::string cameraDevice;       // e.g., "gantry-main"
    std::string ioDevice;           // e.g., "IOBottom"
    int gripperPin;                 // e.g., 0 for left, 2 for right
    int releaseWaitMs;              // e.g., 1500
    int regripWaitMs;               // e.g., 500
  };

  // Add pick sequence to builder
  void AddPickSequence(
    SequenceBuilder* builder,
    const GripperConfig& config,
    const std::string& pickNode,
    const std::string& viewNode,
    const std::string& lensName = "lens"
  );

  // Add place sequence to builder
  void AddPlaceSequence(
    SequenceBuilder* builder,
    const GripperConfig& config,
    const std::string& placeNode,
    const std::string& viewNode,
    const std::string& lensName = "lens"
  );

  // Add complete pick-and-place sequence
  void AddPickAndPlaceSequence(
    SequenceBuilder* builder,
    const GripperConfig& config,
    const std::string& pickNode,
    const std::string& pickViewNode,
    const std::string& placeNode,
    const std::string& placeViewNode,
    const std::string& lensName = "lens"
  );

  // Predefined configs
  GripperConfig GetLeftGripperConfig();
  GripperConfig GetRightGripperConfig();

} // namespace GripperSequenceHelpers
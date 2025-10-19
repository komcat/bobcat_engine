// GripperSequenceHelpers.cpp
#include "GripperSequenceHelpers.h"

namespace GripperSequenceHelpers {

  void AddPickSequence(
    SequenceBuilder* builder,
    const GripperConfig& config,
    const std::string& pickNode,
    const std::string& viewNode,
    const std::string& lensName)
  {
    // Move gripper to pick position
    builder->AddAction(config.gripperDevice, "MoveToNamedPosition", {
      {"positionName", pickNode}
    });
    // ✅ Wait for motion to complete
    builder->AddAction(config.gripperDevice, "WaitForMotionComplete", {});

    // Move camera to view position
    builder->AddAction(config.cameraDevice, "MoveToNamedPosition", {
      {"positionName", viewNode}
    });
    // ✅ Wait for motion to complete
    builder->AddAction(config.cameraDevice, "WaitForMotionComplete", {});

    // User confirmation before gripping
    builder->AddAction("system", "UserPrompt", {
      {"title", "Lens Position Check"},
      {"message", "Check the " + lensName + " position before gripping & click confirm"}
    });

    // Grip the lens
    builder->AddAction(config.ioDevice, "SetOutputPin", {
      {"pin", std::to_string(config.gripperPin)},
      {"state", "true"}
    });

    // Release temporarily
    builder->AddAction(config.ioDevice, "SetOutputPin", {
      {"pin", std::to_string(config.gripperPin)},
      {"state", "false"}
    });

    // Wait
    builder->AddAction("system", "Wait", {
      {"milliseconds", std::to_string(config.releaseWaitMs)}
    });

    // Re-grip
    builder->AddAction(config.ioDevice, "SetOutputPin", {
      {"pin", std::to_string(config.gripperPin)},
      {"state", "true"}
    });

    // Wait for re-grip to settle
    builder->AddAction("system", "Wait", {
      {"milliseconds", std::to_string(config.regripWaitMs)}
    });

    // User confirmation that grip is successful
    builder->AddAction("system", "UserPrompt", {
      {"title", "Grip Confirmation"},
      {"message", "Confirm " + lensName + " is successfully gripped"}
    });
  }

  void AddPlaceSequence(
    SequenceBuilder* builder,
    const GripperConfig& config,
    const std::string& placeNode,
    const std::string& viewNode,
    const std::string& lensName)
  {
    // Move camera to place view position
    builder->AddAction(config.cameraDevice, "MoveToNamedPosition", {
      {"positionName", viewNode}
    });
    // ✅ Wait for motion to complete
    builder->AddAction(config.cameraDevice, "WaitForMotionComplete", {});

    // Move gripper to placement position
    builder->AddAction(config.gripperDevice, "MoveToNamedPosition", {
      {"positionName", placeNode}
    });
    // ✅ Wait for motion to complete
    builder->AddAction(config.gripperDevice, "WaitForMotionComplete", {});

    // Optional: User confirmation before releasing (commented out by default)
    // builder->AddAction("system", "UserPrompt", {
    //   {"title", "Placement Check"},
    //   {"message", "Confirm " + lensName + " is in correct position before releasing"}
    // });

    // Release the lens
    builder->AddAction(config.ioDevice, "SetOutputPin", {
      {"pin", std::to_string(config.gripperPin)},
      {"state", "false"}
    });
  }

  void AddPickAndPlaceSequence(
    SequenceBuilder* builder,
    const GripperConfig& config,
    const std::string& pickNode,
    const std::string& pickViewNode,
    const std::string& placeNode,
    const std::string& placeViewNode,
    const std::string& lensName)
  {
    AddPickSequence(builder, config, pickNode, pickViewNode, lensName);
    AddPlaceSequence(builder, config, placeNode, placeViewNode, lensName);
  }

  GripperConfig GetLeftGripperConfig() {
    return GripperConfig{
      "hex-left",      // gripperDevice
      "gantry-main",   // cameraDevice
      "IOBottom",      // ioDevice
      0,               // gripperPin
      1500,            // releaseWaitMs
      500              // regripWaitMs
    };
  }

  GripperConfig GetRightGripperConfig() {
    return GripperConfig{
      "hex-right",     // gripperDevice
      "gantry-main",   // cameraDevice
      "IOBottom",      // ioDevice
      2,               // gripperPin
      1500,            // releaseWaitMs
      500              // regripWaitMs
    };
  }

} // namespace GripperSequenceHelpers
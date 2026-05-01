// SmuChannelSelect.h
// Shared inline helper for SMU command families: select a channel on the
// active device when the device exposes per-channel addressing.
// Currently only Keithley6482Adapter overrides SelectChannel; for adapters
// without per-channel selection (KeithleySMUAdapter etc.) this is a no-op.
#pragma once

#include "Keithley6482Adapter.h"

#include <memory>

class ISMUDevice;

namespace SmuHelpers {

inline void SelectChannelIfSupported(const std::shared_ptr<ISMUDevice>& device, int channel)
{
  auto keithley6482 = std::dynamic_pointer_cast<Keithley6482Adapter>(device);
  if (keithley6482) {
    keithley6482->SelectChannel(channel);
  }
}

}  // namespace SmuHelpers

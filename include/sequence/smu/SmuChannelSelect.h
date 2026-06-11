// SmuChannelSelect.h
// Shared inline helper for SMU command families: select a channel on the
// active device when the device exposes per-channel addressing.
// Both Keithley6482Adapter (dual-channel picoammeter) and KeithleySMUAdapter
// (2400/2600 series, multi-channel TSP) expose SelectChannel — adapters
// without per-channel selection are silently no-op'd.
//
// NOTE: KeithleySMUAdapter::SetVoltage / SetOutputOn / SetOutputOff all
// dispatch to the *currently selected* channel (smua/smub on 2600 TSP). Skip
// this helper and the operation hits whatever channel was last left active —
// which on a fresh process defaults to 1 but persists across recipe runs.
#pragma once

#include "Keithley6482Adapter.h"
#include "KeithleySMUAdapter.h"

#include <memory>

class ISMUDevice;

namespace SmuHelpers {

inline void SelectChannelIfSupported(const std::shared_ptr<ISMUDevice>& device, int channel)
{
  if (auto keithley6482 = std::dynamic_pointer_cast<Keithley6482Adapter>(device)) {
    keithley6482->SelectChannel(channel);
    return;
  }
  if (auto keithleySmu = std::dynamic_pointer_cast<KeithleySMUAdapter>(device)) {
    keithleySmu->SelectChannel(channel);
    return;
  }
}

}  // namespace SmuHelpers

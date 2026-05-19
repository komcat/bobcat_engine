# UDP3305S Integration Handoff

**Status:** Connection verified ✅, SCPI command set verified ✅ on 2026-05-19.
**Next:** Write a `bobcat_engine` adapter so it plugs into `PowerSupplyManager`.
**Audience:** the agent that will implement the C++ adapter.

---

## 1. Device identity

```
Manufacturer : Uni-Trend
Model        : UDP3305S-E   (3-channel programmable DC PSU)
Serial       : ADP3424500154
Firmware     : 1.16
Transport    : USB → USBTMC → NI-VISA → SCPI
VISA resource: USB0::0x0483::0x5740::ADP3424500154::INSTR
```

The serial number is part of the resource string, so the JSON config must let the user paste the full VISA resource (not an IP). Vendor ID `0x0483` is STMicro — UNI-T enumerates with the ST USB chip's VID, which is why it does not look like a UNI-T device at the bus level.

Channel ranges (datasheet, NOT verified on this unit — confirm before exposing limits in UI):
- **CH1, CH2** — 0–32 V / 0–5 A, independent
- **CH3** — 0–6 V / 0–3 A

## 2. Verified SCPI command set

These are the exact commands `docs/UDP3305S/test_channels.py` exercised and got correct responses for on all three channels (1.0 V programmed, 0.1 A limit, ON → OFF cycle, output reads ON/OFF and measured V collapses after OFF).

| Purpose | Command | Notes |
|---|---|---|
| Identity | `*IDN?` | returns `Uni-Trend,UDP3305S-E,<serial>,<fw>` |
| Set V+I in one shot | `APPLy CH<n>,<volt>,<curr>` | e.g. `APPLy CH1,1.0,0.1` |
| Read programmed V | `SOURce<n>:VOLTage?` | returns numeric V |
| Read programmed I | `SOURce<n>:CURRent?` | returns numeric A |
| Output on/off | `OUTPut CH<n>,ON` / `OUTPut CH<n>,OFF` | channel-suffixed form |
| Read output state | `OUTPut? CH<n>` | returns `ON` or `OFF` (not `1`/`0`) |
| Measure V | `MEASure:VOLTage? CH<n>` | numeric, e.g. `1.0` |
| Measure I | `MEASure:CURRent? CH<n>` | numeric, e.g. `0.0` |

Likely-but-untested (assume safe based on SCPI conventions, **verify before relying on them**):
- `SOURce<n>:VOLTage <v>` / `SOURce<n>:CURRent <i>` — set V or I individually
- `SOURce<n>:VOLTage:PROTection <v>` (OVP), `SOURce<n>:CURRent:PROTection <i>` (OCP)
- `*RST`, `*CLS`, `SYSTem:ERRor?`

Quirk to handle: `OUTPut? CH<n>` returns the string `ON`/`OFF`, **not** `1`/`0`. The adapter must parse both forms — Keysight returns `1`/`0`, this one returns `ON`/`OFF`. Both branches already exist in `test_channels.py`'s assertion `state_on in ("ON", "1")`.

## 3. Reference reproducers (already in repo)

- `docs/UDP3305S/check_connection.py` — enumerates VISA resources, finds the UDP3305S by VID, prints `*IDN?` and current state. Use this when wiring a new unit.
- `docs/UDP3305S/test_channels.py` — programmable + on/off cycle for all 3 channels. Safe 1 V / 0.1 A test points, force-off on exit.

These are the canonical truth for "what works"; the C++ adapter must produce the same byte-on-the-wire commands.

## 4. Where it slots into the codebase

`PowerSupplyManager` already supports multi-channel adapters via `IPowerSupply`. Follow the **`KeysightE36441A`** pattern (4-channel PSU) for adapter shape, but the **transport** is different — use NI-VISA, not raw TCP sockets. The VISA pattern is already in this repo:

- `include/TEC/TED4015DirectAdapter.{h,cpp}` — clean `#include <visa.h>` direct-adapter example
- `include/TEC/CLD101xDirectAdapter.{h,cpp}` — another direct-VISA TEC adapter
- `include/SMU/Keithley6482Adapter.{h,cpp}` — direct-VISA SMU adapter

Read TED4015 first; it's the closest in shape (USB VISA resource string, SCPI query/write pair, mutex-guarded).

### Files to add

```
include/PSU/UnitTUDP3305S.h
include/PSU/UnitTUDP3305S.cpp
```

Class skeleton (mirror `KeysightE36441A.h`, swap socket for VISA session):

```cpp
class UnitTUDP3305S : public IPowerSupply {
public:
  explicit UnitTUDP3305S(const std::string& visaResource,
                         const std::string& deviceName = "UDP3305S");
  ~UnitTUDP3305S() override;

  bool connect() override;                    // viOpenDefaultRM + viOpen + *IDN?
  void disconnect() override;                 // viClose
  bool isConnected() const override;

  std::string getDeviceName() const override { return m_deviceName; }
  std::string getDeviceID() const override   { return m_deviceID; }

  bool setOutputOn(int channel = 1) override;    // OUTPut CH<n>,ON
  bool setOutputOff(int channel = 1) override;   // OUTPut CH<n>,OFF

  bool setVoltage(double v, int channel = 1) override;  // SOURce<n>:VOLTage <v>
  bool setCurrent(double i, int channel = 1) override;  // SOURce<n>:CURRent <i>

  bool setOVP(double v, int channel = 1) override;      // SOURce<n>:VOLTage:PROTection <v>
  bool setOCP(double i, int channel = 1) override;      // SOURce<n>:CURRent:PROTection <i>

  double readVoltage(int channel = 1) override;     // MEASure:VOLTage? CH<n>
  double readCurrent(int channel = 1) override;     // MEASure:CURRent? CH<n>
  double readSetVoltage(int channel = 1) override;  // SOURce<n>:VOLTage?
  double readSetCurrent(int channel = 1) override;  // SOURce<n>:CURRent?
  bool readOutputState(int channel = 1) override;   // OUTPut? CH<n>, parse ON/OFF

  std::string getLastError() const override { return m_lastError; }

private:
  std::string m_visaResource;
  std::string m_deviceName;
  std::string m_deviceID;
  ViSession   m_rm   = VI_NULL;
  ViSession   m_inst = VI_NULL;
  bool m_connected = false;
  mutable std::mutex m_mutex;
  mutable std::string m_lastError;

  static constexpr int NUM_CHANNELS = 3;
  static constexpr int TIMEOUT_MS   = 5000;
};
```

Validate `1 <= channel <= 3` in every method that takes a channel.

### Files to modify

1. **`include/PSU/PowerSupplyJsonLoader.cpp`** — add a branch in `createPowerSupply()`:
   ```cpp
   if (type == "unit_t_udp3305s") {
       std::string resource = deviceConfig.value("visa_resource", "");
       return std::make_shared<UnitTUDP3305S>(resource, name);
   }
   ```
   The current loader passes `deviceIP`/`devicePort` (TCP-shaped). You need a parallel path that reads `visa_resource` from the JSON instead. Touch the loader signature carefully — `createPowerSupply()` is `static`; either widen it to accept the raw `json deviceConfig`, or read `visa_resource` from the calling site in `loadConfiguration()` and pass it through. Widening to take `const nlohmann::json&` is the cleaner fix and matches what other direct adapters would want anyway.

2. **`config/power_supply_config.json`** — extend with a USB/VISA entry. Don't break the existing IP-based Keysight entries. Example shape:
   ```json
   {
     "name": "PSU3",
     "type": "unit_t_udp3305s",
     "visa_resource": "USB0::0x0483::0x5740::ADP3424500154::INSTR",
     "channels": 3
   }
   ```
   Use `config/power_supply_config.local.json` on this machine — it's gitignored and that resource string is per-unit.

3. **`CMakeLists.txt`** — add to the PSU `SOURCES` block at line ~820 and `HEADERS` block at line ~1157:
   ```cmake
   include/PSU/UnitTUDP3305S.cpp
   ...
   include/PSU/UnitTUDP3305S.h
   ```
   NI-VISA is already linked for the TEC/SMU adapters, so no new SDK detection is needed — just include `<visa.h>` and reuse the existing VISA target.

4. **`include/AppUI/PowerSupplyControlPage/PowerSupplyControlPage.cpp`** and **`PowerSupplyFormWidget`** — already channel-count-driven (see `setDeviceChannelCount` call in the loader). Verify the 3-channel layout renders correctly; no code change expected.

## 5. Acceptance criteria

The adapter is done when:

1. With the JSON entry above and `cmake --preset x64-debug-hardware` build, `PowerSupplyManager` reports the UDP3305S as connected on startup.
2. The PSU control page shows CH1/CH2/CH3 with independent V/I set + readback + output toggle, behaving identically to what `test_channels.py` does.
3. A recipe step using the existing PSU process plugins (set voltage / set current / output on / output off / read measured V) works on this device by name. No new plugin code should be needed — `PSUCommandHandler` routes by device name through `IPowerSupply`.
4. `OUTPut? CH<n>` parsing handles both `ON`/`OFF` and `1`/`0` (defensive — UDP3305S returns the string form).
5. Force-off on `disconnect()` and on adapter destruction. Match the safety habit already in `KeysightE36441A`.
6. Per project policy: no `throw`, no `while(true)`, RAII for the VISA session (close in destructor), `[[nodiscard]]` on the boolean setters, return `ErrorCode` via `m_lastError` for failures. See `CLAUDE.md` → *Coding Conventions (MISRA-Aligned)*.

## 6. Things explicitly NOT decided yet

- **Channel-specific max limits.** CH3 is 6 V / 3 A on the datasheet but the adapter currently has no enforcement layer. Either clamp in `setVoltage`/`setCurrent` per channel or surface a per-channel limit struct via the config — defer this decision to the user; ask before adding silent clamping.
- **OVP / OCP commands.** Not exercised by `test_channels.py`. The SCPI guesses in §2 should be confirmed against the UDP3305S programming manual (PDF: `docs/UDP3305S/1677834612592819.pdf`) before being relied on.
- **`*RST` on connect.** Other adapters in this repo do not auto-reset; don't reset on connect — it would turn outputs off mid-experiment if the app reconnects.

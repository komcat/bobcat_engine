"""Basic functional test for the UNI-T UDP3305S 3-channel programmable DC power supply.

Tests, per channel (CH1/CH2/CH3):
  1. Program a low voltage with a current limit
  2. Turn the output ON, read back measured V/I
  3. Turn the output OFF, confirm it reads ~0

Outputs are kept at LOW, SAFE values (1 V, 0.1 A limit). Run with nothing
hazardous wired to the terminals.

Usage:  python test_channels.py
"""
import sys
import time
import pyvisa

RESOURCE = "USB0::0x0483::0x5740::ADP3424500154::INSTR"

# Conservative per-channel test points: (set_voltage_V, current_limit_A)
TEST_POINTS = {
    1: (1.0, 0.1),
    2: (1.0, 0.1),
    3: (1.0, 0.1),
}

SETTLE_S = 0.5  # let the output settle before measuring


def check(condition: bool, msg: str) -> bool:
    mark = "PASS" if condition else "FAIL"
    print(f"    [{mark}] {msg}")
    return condition


def test_channel(inst, ch: int, set_v: float, lim_i: float) -> bool:
    print(f"\n--- CH{ch} ---")
    ok = True

    # 1. Program voltage + current limit (APPLy CHn,<volt>,<curr>)
    inst.write(f"APPLy CH{ch},{set_v},{lim_i}")
    time.sleep(SETTLE_S)
    prog_v = float(inst.query(f"SOURce{ch}:VOLTage?").strip())
    prog_i = float(inst.query(f"SOURce{ch}:CURRent?").strip())
    ok &= check(abs(prog_v - set_v) < 0.05, f"programmed voltage {prog_v} V (want {set_v})")
    ok &= check(abs(prog_i - lim_i) < 0.05, f"programmed current limit {prog_i} A (want {lim_i})")

    # 2. Output ON
    inst.write(f"OUTPut CH{ch},ON")
    time.sleep(SETTLE_S)
    state_on = inst.query(f"OUTPut? CH{ch}").strip()
    meas_v = float(inst.query(f"MEASure:VOLTage? CH{ch}").strip())
    meas_i = float(inst.query(f"MEASure:CURRent? CH{ch}").strip())
    print(f"    output state = {state_on}, measured {meas_v} V / {meas_i} A")
    ok &= check(state_on in ("ON", "1"), "output reports ON")
    ok &= check(abs(meas_v - set_v) < 0.2, f"measured voltage near {set_v} V (open-load)")

    # 3. Output OFF
    inst.write(f"OUTPut CH{ch},OFF")
    time.sleep(SETTLE_S)
    state_off = inst.query(f"OUTPut? CH{ch}").strip()
    meas_v_off = float(inst.query(f"MEASure:VOLTage? CH{ch}").strip())
    print(f"    output state = {state_off}, measured {meas_v_off} V")
    ok &= check(state_off in ("OFF", "0"), "output reports OFF")
    ok &= check(meas_v_off < 0.5, "measured voltage collapsed after OFF")

    return ok


def main() -> int:
    rm = pyvisa.ResourceManager()
    try:
        inst = rm.open_resource(RESOURCE)
    except Exception as exc:  # noqa: BLE001
        print(f"Cannot open {RESOURCE}: {exc}")
        print("Run check_connection.py first to confirm the device address.")
        return 1

    inst.timeout = 5000  # ms
    idn = inst.query("*IDN?").strip()
    print(f"Device: {idn}")
    if "UDP3305" not in idn.upper():
        print("WARNING: resource does not identify as a UDP3305S")

    all_ok = True
    try:
        for ch, (set_v, lim_i) in TEST_POINTS.items():
            all_ok &= test_channel(inst, ch, set_v, lim_i)
    finally:
        # Safety: make sure every channel is off on exit
        for ch in TEST_POINTS:
            inst.write(f"OUTPut CH{ch},OFF")
        inst.close()

    print("\n" + ("ALL CHANNELS PASSED" if all_ok else "SOME CHECKS FAILED"))
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())

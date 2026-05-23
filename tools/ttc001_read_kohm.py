"""
Read thermistor resistance (kOhm) and inferred temperature from a Thorlabs
TTC-001 T-Cube TEC controller configured in 20 kOhm thermistor mode.

Sensor on device: 10 kOhm NTC @ 25 C, beta = 3977 (but TTC-001 hardware sensor
mode is fixed to 20 kOhm in the Kinesis GUI). The device returns raw units
0..2000 = 0..20 kOhm (1 unit = 10 Ohm), so kOhm = raw / 100 regardless of which
thermistor is actually plugged in. We convert ohms -> Celsius using the beta
equation against the actual sensor's R0 / T0 / beta.

Requires: Thorlabs Kinesis installed (or the DLLs colocated with this script).
Python 3.8+ (uses os.add_dll_directory).
"""

import ctypes
import math
import os
import sys
import time
from ctypes import c_char_p, c_int, c_short, c_bool

SERIAL_NUMBER       = b"87814387"
THERMISTOR_R0_OHMS  = 10000.0
THERMISTOR_T0_C     = 25.0
THERMISTOR_BETA     = 3977.0
POLL_INTERVAL_MS    = 250
SAMPLE_INTERVAL_S   = 0.5

KINESIS_DIRS = [
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "..", "external", "kinesis", "bin_x64"),
    r"C:\Program Files\Thorlabs\Kinesis",
]


def load_kinesis():
    dll_dir = None
    for d in KINESIS_DIRS:
        d = os.path.abspath(d)
        if os.path.isfile(os.path.join(d, "Thorlabs.MotionControl.TCube.TEC.dll")):
            dll_dir = d
            break
    if dll_dir is None:
        sys.exit("Could not locate Thorlabs.MotionControl.TCube.TEC.dll in any "
                 "of: " + ", ".join(KINESIS_DIRS))

    os.add_dll_directory(dll_dir)
    dll = ctypes.CDLL(os.path.join(dll_dir, "Thorlabs.MotionControl.TCube.TEC.dll"))

    dll.TLI_BuildDeviceList.restype       = c_short
    dll.TC_Open.argtypes                  = [c_char_p]; dll.TC_Open.restype = c_short
    dll.TC_Close.argtypes                 = [c_char_p]; dll.TC_Close.restype = None
    dll.TC_StartPolling.argtypes          = [c_char_p, c_int]; dll.TC_StartPolling.restype = c_bool
    dll.TC_StopPolling.argtypes           = [c_char_p]; dll.TC_StopPolling.restype = None
    dll.TC_GetTemperatureReading.argtypes = [c_char_p]; dll.TC_GetTemperatureReading.restype = c_short
    dll.TC_GetSensorType.argtypes         = [c_char_p]; dll.TC_GetSensorType.restype = c_int
    dll.TC_SetSensorType.argtypes         = [c_char_p, c_int]; dll.TC_SetSensorType.restype = c_short
    dll.TC_RequestSettings.argtypes       = [c_char_p]; dll.TC_RequestSettings.restype = c_short
    dll.TC_Enable.argtypes                = [c_char_p]; dll.TC_Enable.restype = c_short
    dll.TC_Disable.argtypes               = [c_char_p]; dll.TC_Disable.restype = c_short
    return dll


def ohms_to_celsius(ohms: float) -> float:
    if ohms <= 0.0:
        return float("nan")
    t0_k = THERMISTOR_T0_C + 273.15
    inv_t = 1.0 / t0_k + math.log(ohms / THERMISTOR_R0_OHMS) / THERMISTOR_BETA
    if inv_t <= 0.0:
        return float("nan")
    return 1.0 / inv_t - 273.15


SENSOR_NAMES = {0: "Transducer (AD590/AD592)",
                1: "Thermistor 20 kOhm",
                2: "Thermistor 200 kOhm"}


def main():
    dll = load_kinesis()

    rc = dll.TLI_BuildDeviceList()
    if rc != 0:
        sys.exit(f"TLI_BuildDeviceList failed: {rc}")

    rc = dll.TC_Open(SERIAL_NUMBER)
    if rc != 0:
        sys.exit(f"TC_Open({SERIAL_NUMBER.decode()}) failed: {rc}")

    try:
        dll.TC_RequestSettings(SERIAL_NUMBER)
        time.sleep(0.2)

        sensor = dll.TC_GetSensorType(SERIAL_NUMBER)
        print(f"Connected to TTC-001 SN={SERIAL_NUMBER.decode()}  initial sensor mode: "
              f"{SENSOR_NAMES.get(sensor, f'unknown({sensor})')}")

        # The Kinesis GUI pushes the sensor mode on connect (since 'Persist Settings
        # to the Device' is unchecked). Force 20 kOhm thermistor for this session.
        if sensor != 1:
            rc = dll.TC_SetSensorType(SERIAL_NUMBER, 1)
            if rc != 0:
                sys.exit(f"TC_SetSensorType(20kOhm) failed: {rc}")
            time.sleep(0.3)
            sensor = dll.TC_GetSensorType(SERIAL_NUMBER)
            print(f"After TC_SetSensorType: {SENSOR_NAMES.get(sensor, f'unknown({sensor})')}")

        if sensor == 1:
            ohms_per_unit = 10.0
        elif sensor == 2:
            ohms_per_unit = 100.0
        else:
            print("WARNING: device is in Transducer mode, not thermistor — kOhm "
                  "conversion will not apply.")
            ohms_per_unit = None

        if not dll.TC_StartPolling(SERIAL_NUMBER, POLL_INTERVAL_MS):
            sys.exit("TC_StartPolling failed")
        time.sleep(POLL_INTERVAL_MS / 1000.0 * 2)

        ans = input("Enable TEC output now? [y/N]: ").strip().lower()
        tec_enabled = ans in ("y", "yes")
        if tec_enabled:
            rc = dll.TC_Enable(SERIAL_NUMBER)
            if rc != 0:
                sys.exit(f"TC_Enable failed: {rc}")
            print("TEC ENABLED. Press any key to stop.")
        else:
            print("TEC stays OFF. Press any key to stop reading.")

        try:
            import msvcrt
            def stop_requested():
                return msvcrt.kbhit()
        except ImportError:
            # Non-Windows fallback: stop on Ctrl-C only
            def stop_requested():
                return False

        print(f"{'t(s)':>6}  {'raw':>6}  {'kOhm':>8}  {'inferred C':>11}")
        t_start = time.monotonic()
        try:
            while not stop_requested():
                raw = dll.TC_GetTemperatureReading(SERIAL_NUMBER)
                t_elapsed = time.monotonic() - t_start
                if ohms_per_unit is None:
                    celsius_native = raw / 100.0
                    print(f"{t_elapsed:>6.1f}  {raw:>6}  {'-':>8}  {celsius_native:>11.3f}  (transducer)")
                else:
                    ohms = raw * ohms_per_unit
                    kohm = ohms / 1000.0
                    celsius = ohms_to_celsius(ohms)
                    print(f"{t_elapsed:>6.1f}  {raw:>6}  {kohm:>8.3f}  {celsius:>11.3f}")
                time.sleep(SAMPLE_INTERVAL_S)
        except KeyboardInterrupt:
            pass
        finally:
            # Drain the key that was pressed so it doesn't leak into the shell
            try:
                import msvcrt
                while msvcrt.kbhit():
                    msvcrt.getch()
            except ImportError:
                pass

            if tec_enabled:
                print("Disabling TEC output...")
                dll.TC_Disable(SERIAL_NUMBER)
            dll.TC_StopPolling(SERIAL_NUMBER)
    finally:
        dll.TC_Close(SERIAL_NUMBER)


if __name__ == "__main__":
    main()

"""
Read and tune the TTC-001 temperature loop PID gains via Kinesis.

Kinesis stores P/I/D as unsigned short (0..32767). The Kinesis GUI presents a
1..100 scale ("equivalent to 1 to 100" per the SDK header). In practice the
on-device value and the GUI display are the same small integers (e.g. the
default 'TEC' settings file shows P=3, I=1, D=0), so this script also works in
the small-integer domain — type the same numbers you see in the GUI.

Ranges (per SDK header):
  P: 1..32767  (must be >= 1)
  I: 0..32767
  D: 0..32767

Changes apply immediately to the running loop. They are NOT persisted to flash
unless you tick 'Persist Settings to the Device' in the Kinesis GUI.
"""

import ctypes
import os
import sys
import time
from ctypes import (Structure, byref, c_char_p, c_int, c_short, c_ushort)

SERIAL_NUMBER = b"87814387"

KINESIS_DIRS = [
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "..", "external", "kinesis", "bin_x64"),
    r"C:\Program Files\Thorlabs\Kinesis",
]


class TC_LoopParameters(Structure):
    _fields_ = [
        ("proportionalGain", c_ushort),
        ("integralGain",     c_ushort),
        ("differentialGain", c_ushort),
    ]


def load_kinesis():
    dll_dir = None
    for d in KINESIS_DIRS:
        d = os.path.abspath(d)
        if os.path.isfile(os.path.join(d, "Thorlabs.MotionControl.TCube.TEC.dll")):
            dll_dir = d
            break
    if dll_dir is None:
        sys.exit("Could not locate Thorlabs.MotionControl.TCube.TEC.dll")

    os.add_dll_directory(dll_dir)
    dll = ctypes.CDLL(os.path.join(dll_dir, "Thorlabs.MotionControl.TCube.TEC.dll"))

    dll.TLI_BuildDeviceList.restype          = c_short
    dll.TC_Open.argtypes                     = [c_char_p]; dll.TC_Open.restype = c_short
    dll.TC_Close.argtypes                    = [c_char_p]; dll.TC_Close.restype = None
    dll.TC_RequestTempLoopParams.argtypes    = [c_char_p]; dll.TC_RequestTempLoopParams.restype = c_short
    dll.TC_GetTempLoopParams.argtypes        = [c_char_p, ctypes.POINTER(TC_LoopParameters)]
    dll.TC_GetTempLoopParams.restype         = c_short
    dll.TC_SetTempLoopParams.argtypes        = [c_char_p, ctypes.POINTER(TC_LoopParameters)]
    dll.TC_SetTempLoopParams.restype         = c_short
    return dll


def read_pid(dll) -> TC_LoopParameters:
    # Ask the device to publish its current loop params, then fetch them.
    rc = dll.TC_RequestTempLoopParams(SERIAL_NUMBER)
    if rc != 0:
        sys.exit(f"TC_RequestTempLoopParams failed: {rc}")
    time.sleep(0.2)
    params = TC_LoopParameters()
    rc = dll.TC_GetTempLoopParams(SERIAL_NUMBER, byref(params))
    if rc != 0:
        sys.exit(f"TC_GetTempLoopParams failed: {rc}")
    return params


def write_pid(dll, p: int, i: int, d: int):
    if not (1 <= p <= 32767 and 0 <= i <= 32767 and 0 <= d <= 32767):
        sys.exit(f"Out of range: P={p} (1..32767), I={i} (0..32767), D={d} (0..32767)")
    params = TC_LoopParameters(p, i, d)
    rc = dll.TC_SetTempLoopParams(SERIAL_NUMBER, byref(params))
    if rc != 0:
        sys.exit(f"TC_SetTempLoopParams failed: {rc}")


def prompt_int(label: str, default: int, lo: int, hi: int) -> int:
    while True:
        raw = input(f"  {label} [{default}]: ").strip()
        if raw == "":
            return default
        try:
            v = int(raw)
        except ValueError:
            print(f"    not an integer")
            continue
        if not (lo <= v <= hi):
            print(f"    out of range ({lo}..{hi})")
            continue
        return v


def main():
    dll = load_kinesis()
    if dll.TLI_BuildDeviceList() != 0:
        sys.exit("TLI_BuildDeviceList failed")
    rc = dll.TC_Open(SERIAL_NUMBER)
    if rc != 0:
        sys.exit(f"TC_Open failed: {rc}")

    try:
        cur = read_pid(dll)
        print(f"Current PID:  P={cur.proportionalGain}  "
              f"I={cur.integralGain}  D={cur.differentialGain}")

        ans = input("\nChange PID? [y/N]: ").strip().lower()
        if ans not in ("y", "yes"):
            return

        print("Enter new values (blank = keep current):")
        new_p = prompt_int("P (1..32767)", cur.proportionalGain, 1, 32767)
        new_i = prompt_int("I (0..32767)", cur.integralGain,     0, 32767)
        new_d = prompt_int("D (0..32767)", cur.differentialGain, 0, 32767)

        if (new_p, new_i, new_d) == (cur.proportionalGain,
                                     cur.integralGain,
                                     cur.differentialGain):
            print("No change.")
            return

        write_pid(dll, new_p, new_i, new_d)
        time.sleep(0.3)
        after = read_pid(dll)
        print(f"New PID:      P={after.proportionalGain}  "
              f"I={after.integralGain}  D={after.differentialGain}")
        if (after.proportionalGain, after.integralGain, after.differentialGain) \
                != (new_p, new_i, new_d):
            print("WARNING: device reported back different values than requested.")

        print("\nNote: values are live but NOT persisted to flash. Use the "
              "Kinesis GUI's 'Persist Settings to the Device' to save.")
    finally:
        dll.TC_Close(SERIAL_NUMBER)


if __name__ == "__main__":
    main()

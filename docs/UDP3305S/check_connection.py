"""Quick connection check for the UNI-T UDP3305S DC power supply (USB / USBTMC + SCPI).

Usage:  python check_connection.py
"""
import sys
import pyvisa


def main() -> int:
    rm = pyvisa.ResourceManager()
    resources = rm.list_resources()
    print(f"VISA backend : {rm}")
    print(f"Resources    : {resources}\n")

    # UNI-T instruments enumerate with the STMicroelectronics USB VID 0x0483.
    candidates = [r for r in resources if "0x0483" in r] or list(resources)

    udp = None
    for res in candidates:
        try:
            inst = rm.open_resource(res)
            inst.timeout = 3000  # ms
            idn = inst.query("*IDN?").strip()
            print(f"  {res}")
            print(f"    *IDN? -> {idn}")
            if "UDP3305" in idn.upper() or "UNI-T" in idn.upper():
                udp = inst
                break
            inst.close()
        except Exception as exc:  # noqa: BLE001 - diagnostic script
            print(f"  {res}\n    FAILED: {exc}")

    if udp is None:
        print("\nUDP3305S not identified. Is it powered on and in USB (not RS232) mode?")
        return 1

    print("\nUDP3305S connected. Reading state:")
    try:
        for ch in (1, 2, 3):
            v = udp.query(f"MEASure:VOLTage? CH{ch}").strip()
            i = udp.query(f"MEASure:CURRent? CH{ch}").strip()
            print(f"  CH{ch}: {v} V   {i} A")
    except Exception as exc:  # noqa: BLE001
        print(f"  channel read failed: {exc}")
    finally:
        udp.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

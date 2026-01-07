"""
Simple script to find Thorlabs USB VISA devices
Thorlabs Vendor ID: 0x1313
"""

import pyvisa

def find_thorlabs_devices():
    """Find all Thorlabs USB VISA devices"""
    print("Searching for Thorlabs USB VISA devices...")
    print("-" * 50)

    try:
        rm = pyvisa.ResourceManager()
        print(f"VISA Backend: {rm.visalib}")
        print()

        # List all resources
        resources = rm.list_resources()

        if not resources:
            print("No VISA resources found.")
            return None

        print(f"Found {len(resources)} VISA resource(s):")
        for r in resources:
            print(f"  {r}")
        print()

        # Filter for Thorlabs devices (vendor ID 0x1313)
        thorlabs_devices = [r for r in resources if "0x1313" in r.upper() or "1313" in r]

        if not thorlabs_devices:
            print("No Thorlabs USB devices found.")
            print("Tip: Check USB cable and connection.")
            return None

        print(f"Found {len(thorlabs_devices)} Thorlabs device(s):")
        for device in thorlabs_devices:
            print(f"  {device}")

            # Try to open and identify the device
            try:
                instr = rm.open_resource(device)
                instr.timeout = 5000  # 5 second timeout
                idn = instr.query("*IDN?").strip()
                print(f"    IDN: {idn}")
                instr.close()
            except Exception as e:
                print(f"    Could not query device: {e}")

        return thorlabs_devices

    except Exception as e:
        print(f"Error: {e}")
        return None

if __name__ == "__main__":
    devices = find_thorlabs_devices()
    print()
    if devices:
        print(f"Use this resource string in your config: {devices[0]}")

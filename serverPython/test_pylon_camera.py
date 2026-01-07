"""
Test script to check if Basler USB camera is visible via Pylon SDK
"""

from pypylon import pylon

def main():
    print("=" * 60)
    print("Pylon Camera Detection Test")
    print("=" * 60)

    # Get the transport layer factory
    tl_factory = pylon.TlFactory.GetInstance()

    # List available transport layers
    print("\n[1] Available Transport Layers:")
    print("-" * 40)
    tl_info_list = tl_factory.EnumerateTls()
    if len(tl_info_list) == 0:
        print("  No transport layers found!")
    else:
        for i, tl_info in enumerate(tl_info_list):
            print(f"  [{i}] {tl_info.GetFullName()}")
            print(f"      Device Class: {tl_info.GetDeviceClass()}")

    # Enumerate all devices
    print("\n[2] Available Cameras:")
    print("-" * 40)
    devices = tl_factory.EnumerateDevices()

    if len(devices) == 0:
        print("  No cameras found!")
        print("\n  Possible reasons:")
        print("  - Pylon Viewer or another app has exclusive access")
        print("  - USB camera not properly connected")
        print("  - USB transport layer not installed")
    else:
        for i, device in enumerate(devices):
            print(f"\n  Camera [{i}]:")
            print(f"    Model:        {device.GetModelName()}")
            print(f"    Serial:       {device.GetSerialNumber()}")
            print(f"    Device Class: {device.GetDeviceClass()}")
            print(f"    Vendor:       {device.GetVendorName()}")

            # Try to get more info for USB cameras
            if "Usb" in device.GetDeviceClass():
                print(f"    [USB Camera Detected]")
            elif "GigE" in device.GetDeviceClass():
                try:
                    print(f"    IP Address:   {device.GetIpAddress()}")
                except:
                    pass

    # Try to connect to a specific serial number
    print("\n[3] Testing connection to serial 40724599:")
    print("-" * 40)

    target_serial = "40724599"
    found = False

    for device in devices:
        if device.GetSerialNumber() == target_serial:
            found = True
            print(f"  Found camera with serial {target_serial}")

            # Try to open the camera
            try:
                camera = pylon.InstantCamera(tl_factory.CreateDevice(device))
                camera.Open()
                print(f"  Successfully opened camera!")
                print(f"  Model: {camera.GetDeviceInfo().GetModelName()}")
                camera.Close()
                print(f"  Camera closed successfully")
            except Exception as e:
                print(f"  Failed to open camera: {e}")
            break

    if not found:
        print(f"  Camera with serial {target_serial} not found in device list")

    print("\n" + "=" * 60)
    print("Test Complete")
    print("=" * 60)

if __name__ == "__main__":
    main()

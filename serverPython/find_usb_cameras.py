"""
USB Camera Detection Script
Detects connected USB cameras using OpenCV and Windows device enumeration.
"""

import subprocess
import re


def find_cameras_opencv():
    """Find cameras using OpenCV by testing video capture indices."""
    print("\n=== OpenCV Camera Detection ===")

    try:
        import cv2

        cameras = []
        # Test indices 0-9 for connected cameras
        for i in range(10):
            cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)  # DirectShow backend on Windows
            if cap.isOpened():
                # Try to get camera properties
                width = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
                height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
                fps = cap.get(cv2.CAP_PROP_FPS)
                backend = cap.getBackendName()

                cameras.append({
                    'index': i,
                    'width': int(width),
                    'height': int(height),
                    'fps': fps,
                    'backend': backend
                })
                print(f"  Camera {i}: {int(width)}x{int(height)} @ {fps:.1f}fps ({backend})")
                cap.release()
            else:
                cap.release()

        if not cameras:
            print("  No cameras found via OpenCV")
        else:
            print(f"  Total: {len(cameras)} camera(s) found")

        return cameras

    except ImportError:
        print("  OpenCV (cv2) not installed - skipping")
        return []


def find_cameras_wmi():
    """Find cameras using Windows WMI queries."""
    print("\n=== Windows Device Enumeration ===")

    try:
        # Query for imaging devices using PowerShell/WMI
        cmd = '''powershell -Command "Get-WmiObject Win32_PnPEntity | Where-Object { $_.Caption -match 'camera|webcam|imaging|video|basler|ids|flir|ximea|allied' } | Select-Object Caption, DeviceID, Manufacturer, Status | ConvertTo-Json"'''

        result = subprocess.run(cmd, capture_output=True, text=True, shell=True)

        if result.returncode == 0 and result.stdout.strip():
            import json
            devices = json.loads(result.stdout)

            # Handle single device (not a list)
            if isinstance(devices, dict):
                devices = [devices]

            for dev in devices:
                print(f"  Device: {dev.get('Caption', 'Unknown')}")
                print(f"    Manufacturer: {dev.get('Manufacturer', 'N/A')}")
                print(f"    DeviceID: {dev.get('DeviceID', 'N/A')}")
                print(f"    Status: {dev.get('Status', 'N/A')}")
                print()

            print(f"  Total: {len(devices)} device(s) found")
            return devices
        else:
            print("  No imaging devices found via WMI")
            return []

    except Exception as e:
        print(f"  WMI query failed: {e}")
        return []


def find_cameras_usb():
    """Find USB devices that might be cameras."""
    print("\n=== USB Device Enumeration ===")

    try:
        # Query USB devices
        cmd = '''powershell -Command "Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB*' -and ($_.FriendlyName -match 'camera|webcam|video|imaging|basler|ids|flir' -or $_.Class -eq 'Image' -or $_.Class -eq 'Camera') } | Select-Object FriendlyName, InstanceId, Class, Status | ConvertTo-Json"'''

        result = subprocess.run(cmd, capture_output=True, text=True, shell=True)

        if result.returncode == 0 and result.stdout.strip():
            import json
            devices = json.loads(result.stdout)

            if isinstance(devices, dict):
                devices = [devices]

            for dev in devices:
                print(f"  Device: {dev.get('FriendlyName', 'Unknown')}")
                print(f"    Class: {dev.get('Class', 'N/A')}")
                print(f"    InstanceId: {dev.get('InstanceId', 'N/A')}")
                print(f"    Status: {dev.get('Status', 'N/A')}")
                print()

            print(f"  Total: {len(devices)} USB camera device(s) found")
            return devices
        else:
            print("  No USB camera devices found")
            return []

    except Exception as e:
        print(f"  USB enumeration failed: {e}")
        return []


def find_pylon_cameras():
    """Find Basler cameras using Pylon SDK (if available)."""
    print("\n=== Pylon/Basler Camera Detection ===")

    try:
        from pypylon import pylon

        # Get the transport layer factory
        tlf = pylon.TlFactory.GetInstance()

        # Enumerate all devices
        devices = tlf.EnumerateDevices()

        if len(devices) == 0:
            print("  No Basler cameras found")
            return []

        cameras = []
        for i, dev in enumerate(devices):
            info = {
                'index': i,
                'friendly_name': dev.GetFriendlyName(),
                'full_name': dev.GetFullName(),
                'serial': dev.GetSerialNumber(),
                'model': dev.GetModelName(),
                'vendor': dev.GetVendorName(),
                'device_class': dev.GetDeviceClass()
            }
            cameras.append(info)

            print(f"  Camera {i}: {info['model']}")
            print(f"    Serial: {info['serial']}")
            print(f"    Vendor: {info['vendor']}")
            print(f"    Class: {info['device_class']}")
            print(f"    Full Name: {info['full_name']}")
            print()

        print(f"  Total: {len(cameras)} Basler camera(s) found")
        return cameras

    except ImportError:
        print("  pypylon not installed - skipping Basler detection")
        return []
    except Exception as e:
        print(f"  Pylon detection failed: {e}")
        return []


def main():
    print("=" * 50)
    print("USB Camera Detection Tool")
    print("=" * 50)

    all_cameras = {}

    # Try different detection methods
    all_cameras['opencv'] = find_cameras_opencv()
    all_cameras['wmi'] = find_cameras_wmi()
    all_cameras['usb'] = find_cameras_usb()
    all_cameras['pylon'] = find_pylon_cameras()

    print("\n" + "=" * 50)
    print("Summary")
    print("=" * 50)
    print(f"  OpenCV cameras: {len(all_cameras['opencv'])}")
    print(f"  WMI devices: {len(all_cameras['wmi'])}")
    print(f"  USB devices: {len(all_cameras['usb'])}")
    print(f"  Pylon/Basler: {len(all_cameras['pylon'])}")

    return all_cameras


if __name__ == "__main__":
    main()

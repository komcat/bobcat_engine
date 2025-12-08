"""
Simple Keithley IDN Query Script
Queries *IDN? from two Keithley devices at 192.168.0.80 and 192.168.0.81
"""

import socket

# Target devices
DEVICES = [
    ("192.168.0.80", 5025),
    ("192.168.0.81", 5025),
]


def get_idn(ip: str, port: int, timeout: float = 2.0) -> str:
    """Query *IDN? from a device and return the response."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(timeout)
            sock.connect((ip, port))
            sock.sendall(b"*IDN?\n")
            response = sock.recv(1024).decode('utf-8', errors='ignore').strip()
            return response
    except socket.timeout:
        return f"Timeout connecting to {ip}:{port}"
    except ConnectionRefusedError:
        return f"Connection refused by {ip}:{port}"
    except Exception as e:
        return f"Error: {e}"


def main():
    print("Keithley IDN Query")
    print("=" * 50)

    for ip, port in DEVICES:
        print(f"\n{ip}:{port}")
        idn = get_idn(ip, port)
        print(f"  IDN: {idn}")


if __name__ == "__main__":
    main()

"""
WAGO Input Monitor Script (48 Inputs)
Continuously reads and displays all digital inputs until Ctrl+C.
"""

from pymodbus.client import ModbusTcpClient
from pymodbus.exceptions import ModbusException
import time
import os

# WAGO Configuration
WAGO_IP = "192.168.0.13"
WAGO_PORT = 502

# Input Configuration
NUM_INPUTS = 48
INPUT_READ_ADDR = 0  # Discrete inputs start at address 0
POLL_INTERVAL = 0.1  # 100ms refresh rate


def read_inputs(client: ModbusTcpClient) -> list:
    """Read all digital inputs."""
    try:
        result = client.read_discrete_inputs(address=INPUT_READ_ADDR, count=NUM_INPUTS)
        if result.isError():
            return None
        return result.bits[:NUM_INPUTS]
    except ModbusException:
        return None


def format_inputs_grid(bits: list) -> str:
    """Format inputs as a grid display."""
    if bits is None:
        return "[READ ERROR]"

    lines = []
    # Show in rows of 16
    for row in range(3):  # 3 rows of 16 = 48
        start = row * 16
        end = min(start + 16, NUM_INPUTS)

        # Header row
        header = "     " + " ".join(f"{i:2d}" for i in range(start, end))
        # Value row
        values = "     " + "  ".join("ON" if bits[i] else "--" for i in range(start, end))

        lines.append(f"[{start:2d}-{end-1:2d}] " + "  ".join("ON" if bits[i] else "--" for i in range(start, end)))

    return "\n".join(lines)


def format_active_inputs(bits: list) -> str:
    """Show which inputs are currently active."""
    if bits is None:
        return ""
    active = [i for i, bit in enumerate(bits) if bit]
    if not active:
        return "Active: None"
    return f"Active: {active}"


def clear_screen():
    """Clear terminal screen."""
    os.system('cls' if os.name == 'nt' else 'clear')


def main():
    print("=" * 60)
    print("WAGO Input Monitor (48 Inputs)")
    print("=" * 60)
    print(f"IP: {WAGO_IP}:{WAGO_PORT}")
    print(f"Refresh: {POLL_INTERVAL * 1000:.0f}ms")
    print("Press Ctrl+C to stop")
    print("=" * 60)

    client = ModbusTcpClient(
        host=WAGO_IP,
        port=WAGO_PORT,
        timeout=3
    )

    try:
        if not client.connect():
            print("[FAILED] Could not connect to WAGO device")
            return

        print("[OK] Connected to WAGO device")
        print("\nStarting input monitor...\n")
        time.sleep(1)

        last_bits = None
        read_count = 0

        while True:
            bits = read_inputs(client)
            read_count += 1

            # Clear and redraw
            clear_screen()
            print("=" * 60)
            print("WAGO Input Monitor (48 Inputs) - Press Ctrl+C to stop")
            print("=" * 60)
            print(f"Reads: {read_count} | Refresh: {POLL_INTERVAL * 1000:.0f}ms")
            print("-" * 60)
            print()
            print(format_inputs_grid(bits))
            print()
            print("-" * 60)
            print(format_active_inputs(bits))
            print("-" * 60)

            # Show changes
            if last_bits is not None and bits is not None:
                changed = []
                for i in range(NUM_INPUTS):
                    if bits[i] != last_bits[i]:
                        state = "ON" if bits[i] else "OFF"
                        changed.append(f"Input {i}: {state}")
                if changed:
                    print("Changes: " + ", ".join(changed))

            last_bits = bits
            time.sleep(POLL_INTERVAL)

    except KeyboardInterrupt:
        print("\n\nMonitor stopped by user")
    except Exception as e:
        print(f"\n[ERROR] {e}")
    finally:
        client.close()
        print("Connection closed.")


if __name__ == "__main__":
    main()

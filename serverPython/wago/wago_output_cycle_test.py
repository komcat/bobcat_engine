"""
WAGO Output Cycle Test Script (48 Outputs)
Cycles through each output pin: ON briefly, then OFF, then next pin.
"""

from pymodbus.client import ModbusTcpClient
from pymodbus.exceptions import ModbusException
import time

# WAGO 750-342 Configuration
WAGO_IP = "192.168.0.13"
WAGO_PORT = 502
UNIT_ID = 1

# Test Configuration
NUM_OUTPUTS = 48      # Number of output pins to cycle through (48 outputs)
ON_DURATION = 0.5     # Seconds to keep each output ON (reduced for 48 outputs)
CYCLES = 1            # Number of times to repeat the full cycle
OUTPUT_READ_ADDR = 512  # Address 0x0200 for reading output status


def set_output(client: ModbusTcpClient, address: int, value: bool) -> bool:
    """Set a single output coil."""
    try:
        result = client.write_coil(address=address, value=value)
        if result.isError():
            print(f"[ERROR] Failed to write coil {address}: {result}")
            return False
        return True
    except ModbusException as e:
        print(f"[ERROR] Modbus exception: {e}")
        return False


def read_outputs_status(client: ModbusTcpClient, count: int = NUM_OUTPUTS) -> list:
    """Read current status of all outputs from address 0x0200 (512)."""
    try:
        result = client.read_coils(address=OUTPUT_READ_ADDR, count=count)
        if result.isError():
            return None
        return result.bits[:count]
    except ModbusException:
        return None


def format_outputs_status(bits: list) -> str:
    """Format output status as a compact string showing active outputs."""
    if bits is None:
        return "[READ ERROR]"
    # Show which outputs are ON
    active = [i for i, bit in enumerate(bits) if bit]
    if not active:
        return "All OFF"
    return f"ON: {active}"


def wait_with_polling(client: ModbusTcpClient, duration: float, poll_interval: float = 0.25):
    """Wait for duration while polling and displaying output status."""
    elapsed = 0.0
    while elapsed < duration:
        bits = read_outputs_status(client)
        status = format_outputs_status(bits)
        print(f"\r  Status: {status}", end="", flush=True)
        time.sleep(poll_interval)
        elapsed += poll_interval
    print()  # New line after polling


def clear_all_outputs(client: ModbusTcpClient, count: int = NUM_OUTPUTS):
    """Turn off all outputs."""
    print("Clearing all outputs...")
    values = [False] * count
    try:
        result = client.write_coils(address=0, values=values)
        if result.isError():
            print(f"[ERROR] Failed to clear outputs: {result}")
            return False
        print("[OK] All outputs cleared")
        return True
    except ModbusException as e:
        print(f"[ERROR] Modbus exception: {e}")
        return False


def run_output_cycle(client: ModbusTcpClient):
    """Cycle through each output pin."""
    print(f"\nStarting output cycle test")
    print(f"Outputs: 0-{NUM_OUTPUTS-1}")
    print(f"ON duration: {ON_DURATION} seconds per output")
    print(f"Total time: ~{NUM_OUTPUTS * ON_DURATION:.0f} seconds")
    print(f"Cycles: {CYCLES}")
    print("-" * 50)

    for cycle in range(CYCLES):
        if CYCLES > 1:
            print(f"\n=== Cycle {cycle + 1}/{CYCLES} ===")

        for output_num in range(NUM_OUTPUTS):
            # Turn ON
            print(f"Output {output_num}: ON")
            if not set_output(client, output_num, True):
                continue

            # Wait with polling
            wait_with_polling(client, ON_DURATION, poll_interval=0.1)

            # Turn OFF
            if set_output(client, output_num, False):
                print(f"Output {output_num}: OFF")
            else:
                print(f"Output {output_num}: [ERROR turning OFF]")
            print()

    print("-" * 50)
    print("Output cycle test completed!")


def main():
    print("=" * 50)
    print("WAGO Output Cycle Test (48 Outputs)")
    print("=" * 50)
    print(f"IP: {WAGO_IP}:{WAGO_PORT}")

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

        # Clear all outputs first
        clear_all_outputs(client)

        # Run the cycle test
        run_output_cycle(client)

        # Clear all outputs at the end
        clear_all_outputs(client)

    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        # Try to clear outputs on interrupt
        try:
            clear_all_outputs(client)
        except:
            pass
    except Exception as e:
        print(f"\n[FATAL ERROR] {e}")
    finally:
        client.close()
        print("\nConnection closed.")


if __name__ == "__main__":
    main()

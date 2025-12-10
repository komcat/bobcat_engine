"""
WAGO Debug Script - Find correct output addresses
"""

from pymodbus.client import ModbusTcpClient
import time

WAGO_IP = "192.168.0.13"
WAGO_PORT = 502

client = ModbusTcpClient(host=WAGO_IP, port=WAGO_PORT, timeout=3)

if not client.connect():
    print("Failed to connect")
    exit()

print("Connected to WAGO")
print("=" * 60)

# Try to write coil 0 ON
print("\nWriting coil 0 = ON...")
result = client.write_coil(address=0, value=True)
print(f"Write result: {result}")

time.sleep(0.5)

# Read from various address ranges to find where outputs appear
print("\n" + "=" * 60)
print("Reading from different address ranges:")
print("=" * 60)

# Read coils (function 0x01) from different offsets
for offset in [0, 512, 0x200, 0x100]:
    try:
        result = client.read_coils(address=offset, count=8)
        if not result.isError():
            bits = result.bits[:8]
            print(f"Coils @ {offset} (0x{offset:04X}): {bits}")
    except Exception as e:
        print(f"Coils @ {offset}: Error - {e}")

# Read discrete inputs (function 0x02)
for offset in [0, 512, 0x200, 0x100]:
    try:
        result = client.read_discrete_inputs(address=offset, count=8)
        if not result.isError():
            bits = result.bits[:8]
            print(f"Discrete Inputs @ {offset} (0x{offset:04X}): {bits}")
    except Exception as e:
        print(f"Discrete Inputs @ {offset}: Error - {e}")

# Read holding registers that might contain output status
print("\n" + "=" * 60)
print("Reading holding registers (might contain packed output bits):")
print("=" * 60)
for offset in [0, 0x200, 0x100, 512]:
    try:
        result = client.read_holding_registers(address=offset, count=4)
        if not result.isError():
            print(f"Holding Reg @ {offset} (0x{offset:04X}): {result.registers}")
            # Show as binary
            for i, reg in enumerate(result.registers):
                print(f"  Reg {offset+i}: {reg:016b} (binary)")
    except Exception as e:
        print(f"Holding Reg @ {offset}: Error - {e}")

print("\n" + "=" * 60)
print("Turning coil 0 OFF...")
client.write_coil(address=0, value=False)

client.close()
print("\nDone")

"""
WAGO 750-342 Modbus TCP Test Script
Device: WAGO 750-342 FBC Eth (Fieldbus Coupler Ethernet)
IP Address: 192.168.0.13
Protocol: Modbus TCP, Port 502
"""

from pymodbus.client import ModbusTcpClient
from pymodbus.exceptions import ModbusException
import time

# WAGO 750-342 Configuration
WAGO_IP = "192.168.0.13"
WAGO_PORT = 502
UNIT_ID = 1  # Modbus unit ID (typically 1 for WAGO)

# WAGO 750 Series Modbus Address Mapping:
# Digital Outputs (Coils):        Address 0x0000 - 0x01FF (0-511)
# Digital Inputs (Discrete):      Address 0x0000 - 0x01FF (0-511)
# Analog Outputs (Holding Reg):   Address 0x0000 - 0x00FF (0-255)
# Analog Inputs (Input Reg):      Address 0x0000 - 0x00FF (0-255)


def test_connection(client: ModbusTcpClient) -> bool:
    """Test basic connection to WAGO device."""
    print(f"\n{'='*50}")
    print(f"Testing connection to WAGO 750-342")
    print(f"IP: {WAGO_IP}:{WAGO_PORT}")
    print(f"{'='*50}")

    if client.connect():
        print("[OK] Connected to WAGO device")
        return True
    else:
        print("[FAILED] Could not connect to WAGO device")
        return False


def read_digital_inputs(client: ModbusTcpClient, start_addr: int = 0, count: int = 16):
    """
    Read digital inputs (discrete inputs).
    Function code: 0x02 (Read Discrete Inputs)
    """
    print(f"\n--- Reading Digital Inputs (Address {start_addr}-{start_addr+count-1}) ---")
    try:
        result = client.read_discrete_inputs(address=start_addr, count=count)
        if result.isError():
            print(f"[ERROR] Failed to read digital inputs: {result}")
            return None

        print(f"[OK] Digital Inputs: {result.bits[:count]}")
        for i, bit in enumerate(result.bits[:count]):
            status = "ON" if bit else "OFF"
            print(f"  DI[{start_addr + i:02d}]: {status}")
        return result.bits[:count]
    except ModbusException as e:
        print(f"[ERROR] Modbus exception: {e}")
        return None


def read_digital_outputs(client: ModbusTcpClient, start_addr: int = 0, count: int = 16):
    """
    Read digital outputs (coils).
    Function code: 0x01 (Read Coils)
    """
    print(f"\n--- Reading Digital Outputs/Coils (Address {start_addr}-{start_addr+count-1}) ---")
    try:
        result = client.read_coils(address=start_addr, count=count)
        if result.isError():
            print(f"[ERROR] Failed to read coils: {result}")
            return None

        print(f"[OK] Digital Outputs: {result.bits[:count]}")
        for i, bit in enumerate(result.bits[:count]):
            status = "ON" if bit else "OFF"
            print(f"  DO[{start_addr + i:02d}]: {status}")
        return result.bits[:count]
    except ModbusException as e:
        print(f"[ERROR] Modbus exception: {e}")
        return None


def write_single_coil(client: ModbusTcpClient, address: int, value: bool):
    """
    Write single digital output (coil).
    Function code: 0x05 (Write Single Coil)
    """
    print(f"\n--- Writing Single Coil ---")
    print(f"Address: {address}, Value: {'ON' if value else 'OFF'}")
    try:
        result = client.write_coil(address=address, value=value)
        if result.isError():
            print(f"[ERROR] Failed to write coil: {result}")
            return False
        print(f"[OK] Coil {address} set to {'ON' if value else 'OFF'}")
        return True
    except ModbusException as e:
        print(f"[ERROR] Modbus exception: {e}")
        return False


def write_multiple_coils(client: ModbusTcpClient, start_addr: int, values: list):
    """
    Write multiple digital outputs (coils).
    Function code: 0x0F (Write Multiple Coils)
    """
    print(f"\n--- Writing Multiple Coils ---")
    print(f"Start Address: {start_addr}, Values: {values}")
    try:
        result = client.write_coils(address=start_addr, values=values)
        if result.isError():
            print(f"[ERROR] Failed to write coils: {result}")
            return False
        print(f"[OK] Coils written successfully")
        return True
    except ModbusException as e:
        print(f"[ERROR] Modbus exception: {e}")
        return False


def read_analog_inputs(client: ModbusTcpClient, start_addr: int = 0, count: int = 4):
    """
    Read analog inputs (input registers).
    Function code: 0x04 (Read Input Registers)
    """
    print(f"\n--- Reading Analog Inputs (Address {start_addr}-{start_addr+count-1}) ---")
    try:
        result = client.read_input_registers(address=start_addr, count=count)
        if result.isError():
            print(f"[ERROR] Failed to read analog inputs: {result}")
            return None

        print(f"[OK] Analog Input Registers: {result.registers}")
        for i, reg in enumerate(result.registers):
            print(f"  AI[{start_addr + i:02d}]: {reg} (raw)")
        return result.registers
    except ModbusException as e:
        print(f"[ERROR] Modbus exception: {e}")
        return None


def read_holding_registers(client: ModbusTcpClient, start_addr: int = 0, count: int = 4):
    """
    Read holding registers (analog outputs / configuration).
    Function code: 0x03 (Read Holding Registers)
    """
    print(f"\n--- Reading Holding Registers (Address {start_addr}-{start_addr+count-1}) ---")
    try:
        result = client.read_holding_registers(address=start_addr, count=count)
        if result.isError():
            print(f"[ERROR] Failed to read holding registers: {result}")
            return None

        print(f"[OK] Holding Registers: {result.registers}")
        for i, reg in enumerate(result.registers):
            print(f"  HR[{start_addr + i:02d}]: {reg}")
        return result.registers
    except ModbusException as e:
        print(f"[ERROR] Modbus exception: {e}")
        return None


def write_holding_register(client: ModbusTcpClient, address: int, value: int):
    """
    Write single holding register.
    Function code: 0x06 (Write Single Register)
    """
    print(f"\n--- Writing Single Holding Register ---")
    print(f"Address: {address}, Value: {value}")
    try:
        result = client.write_register(address=address, value=value)
        if result.isError():
            print(f"[ERROR] Failed to write register: {result}")
            return False
        print(f"[OK] Register {address} set to {value}")
        return True
    except ModbusException as e:
        print(f"[ERROR] Modbus exception: {e}")
        return False


def main():
    """Main test routine."""
    print("=" * 60)
    print("WAGO 750-342 Modbus TCP Test")
    print("=" * 60)

    # Create Modbus TCP client
    client = ModbusTcpClient(
        host=WAGO_IP,
        port=WAGO_PORT,
        timeout=3
    )

    try:
        # Test connection
        if not test_connection(client):
            return

        # Read operations (safe, won't affect outputs)
        print("\n" + "=" * 60)
        print("READING I/O STATUS")
        print("=" * 60)

        # Read digital inputs
        read_digital_inputs(client, start_addr=0, count=8)

        # Read digital outputs (coils)
        read_digital_outputs(client, start_addr=0, count=8)

        # Read analog inputs (if analog modules are present)
        read_analog_inputs(client, start_addr=0, count=4)

        # Read holding registers
        read_holding_registers(client, start_addr=0, count=4)

        # Uncomment below to test writing (BE CAREFUL with real hardware!)
        # print("\n" + "=" * 60)
        # print("WRITE TEST (COMMENTED OUT FOR SAFETY)")
        # print("=" * 60)
        #
        # # Write single coil test
        # write_single_coil(client, address=0, value=True)
        # time.sleep(1)
        # write_single_coil(client, address=0, value=False)
        #
        # # Write multiple coils test
        # write_multiple_coils(client, start_addr=0, values=[True, False, True, False])

        print("\n" + "=" * 60)
        print("TEST COMPLETED")
        print("=" * 60)

    except Exception as e:
        print(f"\n[FATAL ERROR] {e}")
    finally:
        client.close()
        print("\nConnection closed.")


if __name__ == "__main__":
    main()

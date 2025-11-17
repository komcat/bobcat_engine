#!/usr/bin/env python3
"""
Thorlabs TL4000 Series TEC Controller Test Script

Supports: TED4000, LDC4000, ITC4000, CLD1000 series instruments
Communication: USB via NI-VISA

Requirements:
- pyvisa
- NI-VISA runtime (http://www.ni.com/visa)
- Thorlabs TL4000 VXIpnp driver installed
"""

import pyvisa
import time
import sys


def find_thorlabs_devices():
    """Find all connected Thorlabs TL4000 series devices"""
    rm = pyvisa.ResourceManager()
    resources = rm.list_resources()

    thorlabs_devices = []

    print("Scanning for Thorlabs TL4000 series devices...")
    print("-" * 50)

    for resource in resources:
        # Look for USB devices (Thorlabs uses USB TMC)
        if 'USB' in resource.upper():
            try:
                instr = rm.open_resource(resource)
                instr.timeout = 3000

                # Query device ID
                idn = instr.query('*IDN?').strip()

                # Check if it's a Thorlabs device
                if 'THORLABS' in idn.upper() or 'TED4' in idn or 'LDC4' in idn or 'ITC4' in idn or 'CLD1' in idn:
                    print(f"Found: {resource}")
                    print(f"  ID: {idn}")
                    thorlabs_devices.append((resource, idn))

                instr.close()
            except Exception as e:
                pass  # Not a compatible device

    return thorlabs_devices, rm


def test_tec_controller(resource_name, rm):
    """Test TEC controller functionality"""
    print(f"\nConnecting to {resource_name}...")
    print("=" * 50)

    try:
        instr = rm.open_resource(resource_name)
        instr.timeout = 5000
        instr.write_termination = '\n'
        instr.read_termination = '\n'

        # Basic identification
        print("\n--- Device Information ---")
        idn = instr.query('*IDN?').strip()
        print(f"Identification: {idn}")

        # Parse IDN string (typically: Manufacturer, Model, Serial, Firmware)
        parts = idn.split(',')
        if len(parts) >= 4:
            print(f"  Manufacturer: {parts[0].strip()}")
            print(f"  Model: {parts[1].strip()}")
            print(f"  Serial: {parts[2].strip()}")
            print(f"  Firmware: {parts[3].strip()}")

        # Self-test
        print("\n--- Self Test ---")
        try:
            self_test = instr.query('*TST?').strip()
            if self_test == '0':
                print("Self-test: PASSED")
            else:
                print(f"Self-test result: {self_test}")
        except Exception as e:
            print(f"Self-test not supported or error: {e}")

        # TEC Status and Measurements
        print("\n--- TEC Status ---")

        # Check if TEC output is enabled
        try:
            tec_output = instr.query('OUTP:STAT?').strip()
            print(f"TEC Output: {'ON' if tec_output == '1' else 'OFF'}")
        except:
            print("TEC Output status: N/A")

        # Read actual temperature
        try:
            temp_act = instr.query('MEAS:TEMP?').strip()
            print(f"Actual Temperature: {temp_act} °C")
        except:
            print("Actual Temperature: N/A")

        # Read setpoint temperature
        try:
            temp_set = instr.query('SOUR:TEMP?').strip()
            print(f"Temperature Setpoint: {temp_set} °C")
        except:
            print("Temperature Setpoint: N/A")

        # Read TEC current
        try:
            tec_curr = instr.query('MEAS:CURR?').strip()
            print(f"TEC Current: {tec_curr} A")
        except:
            print("TEC Current: N/A")

        # Read TEC voltage
        try:
            tec_volt = instr.query('MEAS:VOLT?').strip()
            print(f"TEC Voltage: {tec_volt} V")
        except:
            print("TEC Voltage: N/A")

        # Read sensor resistance (if thermistor)
        try:
            sensor_res = instr.query('MEAS:RES?').strip()
            print(f"Sensor Resistance: {sensor_res} Ω")
        except:
            pass  # May not be available

        # TEC Limits
        print("\n--- TEC Limits ---")
        try:
            curr_lim = instr.query('SOUR:CURR:LIM?').strip()
            print(f"Current Limit: {curr_lim} A")
        except:
            pass

        try:
            volt_lim = instr.query('SOUR:VOLT:LIM?').strip()
            print(f"Voltage Limit: {volt_lim} V")
        except:
            pass

        try:
            temp_win_high = instr.query('SOUR:TEMP:LIM:HIGH?').strip()
            temp_win_low = instr.query('SOUR:TEMP:LIM:LOW?').strip()
            print(f"Temperature Window: {temp_win_low} to {temp_win_high} °C")
        except:
            pass

        # Check for LD (Laser Diode) features - for ITC/LDC/CLD models
        print("\n--- Laser Diode Status (if applicable) ---")
        try:
            ld_output = instr.query('OUTP2:STAT?').strip()
            print(f"LD Output: {'ON' if ld_output == '1' else 'OFF'}")

            ld_curr_set = instr.query('SOUR2:CURR?').strip()
            print(f"LD Current Setpoint: {ld_curr_set} A")

            ld_curr_act = instr.query('MEAS2:CURR?').strip()
            print(f"LD Actual Current: {ld_curr_act} A")
        except:
            print("LD features not available on this model")

        # PID Parameters
        print("\n--- PID Parameters ---")
        try:
            p_gain = instr.query('SOUR:TEMP:PGAIN?').strip()
            i_gain = instr.query('SOUR:TEMP:IGAIN?').strip()
            d_gain = instr.query('SOUR:TEMP:DGAIN?').strip()
            print(f"P Gain: {p_gain}")
            print(f"I Gain: {i_gain}")
            print(f"D Gain: {d_gain}")
        except:
            print("PID parameters not accessible")

        # Error queue
        print("\n--- Error Status ---")
        try:
            errors = instr.query('SYST:ERR?').strip()
            print(f"Error Queue: {errors}")
        except:
            pass

        instr.close()
        print("\n✓ Connection test completed successfully")

    except Exception as e:
        print(f"\n✗ Error during test: {e}")
        import traceback
        traceback.print_exc()


def demo_temperature_control(resource_name, rm, target_temp=25.0):
    """
    Demo: Set temperature and monitor stabilization
    WARNING: This will change the device setpoint!
    """
    print(f"\n{'='*50}")
    print("DEMO: Temperature Control")
    print(f"{'='*50}")
    print(f"Target Temperature: {target_temp} °C")

    confirm = input("This will change the TEC setpoint. Continue? (yes/no): ").strip().lower()
    if confirm != 'yes':
        print("Demo cancelled.")
        return

    try:
        instr = rm.open_resource(resource_name)
        instr.timeout = 5000

        # Read current setpoint
        current_setpoint = float(instr.query('SOUR:TEMP?').strip())
        print(f"Current Setpoint: {current_setpoint} °C")

        # Set new temperature
        print(f"Setting temperature to {target_temp} °C...")
        instr.write(f'SOUR:TEMP {target_temp}')
        time.sleep(0.1)

        # Verify setpoint was changed
        new_setpoint = float(instr.query('SOUR:TEMP?').strip())
        print(f"New Setpoint: {new_setpoint} °C")

        # Enable TEC output if not already enabled
        tec_state = instr.query('OUTP:STAT?').strip()
        if tec_state != '1':
            print("Enabling TEC output...")
            instr.write('OUTP:STAT 1')
            time.sleep(0.5)

        # Monitor temperature for 30 seconds
        print("\nMonitoring temperature stabilization (30 seconds)...")
        print("Time(s)  Actual(°C)  Setpoint(°C)  Current(A)  Status")
        print("-" * 60)

        for i in range(30):
            temp_act = float(instr.query('MEAS:TEMP?').strip())
            temp_set = float(instr.query('SOUR:TEMP?').strip())
            curr = float(instr.query('MEAS:CURR?').strip())

            # Check if temperature is within window
            diff = abs(temp_act - temp_set)
            if diff < 0.1:
                status = "STABLE"
            elif diff < 0.5:
                status = "SETTLING"
            else:
                status = "CHANGING"

            print(f"{i:6d}   {temp_act:8.3f}    {temp_set:8.3f}     {curr:8.4f}   {status}")
            time.sleep(1)

        print("\nDemo completed.")

        # Optionally restore original setpoint
        restore = input(f"Restore original setpoint ({current_setpoint} °C)? (yes/no): ").strip().lower()
        if restore == 'yes':
            instr.write(f'SOUR:TEMP {current_setpoint}')
            print(f"Setpoint restored to {current_setpoint} °C")

        instr.close()

    except Exception as e:
        print(f"Demo error: {e}")
        import traceback
        traceback.print_exc()


def manual_command_mode(resource_name, rm):
    """Interactive SCPI command mode"""
    print(f"\n{'='*50}")
    print("Interactive SCPI Command Mode")
    print(f"{'='*50}")
    print("Enter SCPI commands directly. Type 'exit' to quit.")
    print("Common commands:")
    print("  *IDN?              - Device identification")
    print("  MEAS:TEMP?         - Read temperature")
    print("  SOUR:TEMP 25.0     - Set temperature to 25°C")
    print("  OUTP:STAT?         - Check TEC output state")
    print("  OUTP:STAT 1        - Enable TEC output")
    print("  OUTP:STAT 0        - Disable TEC output")
    print("  SYST:ERR?          - Check error queue")
    print()

    try:
        instr = rm.open_resource(resource_name)
        instr.timeout = 5000

        while True:
            cmd = input("SCPI> ").strip()

            if cmd.lower() == 'exit':
                break

            if not cmd:
                continue

            try:
                if '?' in cmd:
                    # Query command
                    response = instr.query(cmd).strip()
                    print(f"Response: {response}")
                else:
                    # Write command
                    instr.write(cmd)
                    print("OK")
            except Exception as e:
                print(f"Error: {e}")

        instr.close()

    except Exception as e:
        print(f"Connection error: {e}")


def main():
    print("Thorlabs TL4000 Series TEC Controller Test")
    print("=" * 50)
    print("Supports: TED4000, LDC4000, ITC4000, CLD1000 series")
    print()

    # Find devices
    devices, rm = find_thorlabs_devices()

    if not devices:
        print("\nNo Thorlabs TL4000 series devices found.")
        print("\nTroubleshooting:")
        print("1. Ensure device is connected via USB")
        print("2. Ensure NI-VISA runtime is installed")
        print("3. Ensure Thorlabs drivers are installed")
        print("4. Check Device Manager for the device")

        # List all VISA resources for debugging
        print("\nAll available VISA resources:")
        resources = rm.list_resources()
        if resources:
            for res in resources:
                print(f"  {res}")
        else:
            print("  No VISA resources found")

        return

    # Select device if multiple found
    if len(devices) == 1:
        selected_resource = devices[0][0]
    else:
        print(f"\nFound {len(devices)} devices. Select one:")
        for i, (res, idn) in enumerate(devices):
            print(f"  {i+1}. {res}")
            print(f"     {idn}")

        while True:
            try:
                choice = int(input("Enter device number: "))
                if 1 <= choice <= len(devices):
                    selected_resource = devices[choice-1][0]
                    break
            except:
                pass
            print("Invalid selection.")

    # Test the selected device
    test_tec_controller(selected_resource, rm)

    # Additional options
    print("\n" + "=" * 50)
    print("Additional Options:")
    print("1. Demo temperature control")
    print("2. Interactive SCPI command mode")
    print("3. Exit")

    choice = input("Select option (1-3): ").strip()

    if choice == '1':
        temp = input("Enter target temperature (°C) [default: 25.0]: ").strip()
        target = float(temp) if temp else 25.0
        demo_temperature_control(selected_resource, rm, target)
    elif choice == '2':
        manual_command_mode(selected_resource, rm)

    print("\nTest complete.")


if __name__ == "__main__":
    main()

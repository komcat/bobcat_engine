import serial
import serial.tools.list_ports
import time

def list_available_ports():
    """List all available COM ports on the system"""
    ports = serial.tools.list_ports.comports()
    available_ports = []
    
    print("Available COM ports:")
    print("-" * 50)
    for port in ports:
        print(f"Port: {port.device}")
        print(f"Description: {port.description}")
        print(f"Hardware ID: {port.hwid}")
        print("-" * 50)
        available_ports.append(port.device)
    
    return available_ports

def test_keithley_connection(port, baudrate=9600, timeout=2):
    """
    Test connection to Keithley 6482 on specified port
    
    Parameters:
    - port: COM port string (e.g., 'COM3')
    - baudrate: Communication speed (default 9600 for Keithley 6482)
    - timeout: Read timeout in seconds
    """
    try:
        # Configure serial connection
        # Keithley 6482 default settings:
        # - 9600 baud
        # - 8 data bits
        # - 1 stop bit
        # - No parity
        # - No flow control
        ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=timeout,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False
        )
        
        print(f"\nTesting connection on {port}...")
        
        # Clear any existing data in buffers
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        # Send identification query
        ser.write(b'*IDN?\r\n')
        time.sleep(0.1)
        
        # Read response
        response = ser.readline().decode('utf-8').strip()
        
        if response and 'KEITHLEY' in response.upper():
            print(f"✓ Keithley device found on {port}!")
            print(f"Device identification: {response}")
            return ser, response
        else:
            print(f"✗ No Keithley response on {port}")
            ser.close()
            return None, None
            
    except serial.SerialException as e:
        print(f"✗ Could not open {port}: {e}")
        return None, None
    except Exception as e:
        print(f"✗ Error on {port}: {e}")
        if 'ser' in locals():
            ser.close()
        return None, None

def auto_discover_keithley():
    """Automatically discover Keithley 6482 on all available COM ports"""
    print("Starting automatic discovery of Keithley 6482...")
    print("=" * 50)
    
    # List all available ports
    available_ports = list_available_ports()
    
    if not available_ports:
        print("No COM ports found. Please check your USB-RS232 cable connection.")
        return None, None
    
    print(f"\nFound {len(available_ports)} COM port(s). Testing each port...")
    print("=" * 50)
    
    # Test each port
    for port in available_ports:
        ser, device_id = test_keithley_connection(port)
        if ser:
            return ser, port
    
    print("\nNo Keithley 6482 found on any COM port.")
    print("\nTroubleshooting tips:")
    print("1. Check that the USB-RS232 cable is properly connected")
    print("2. Verify the Keithley 6482 is powered on")
    print("3. Check RS-232 settings on the Keithley (Menu → Communication)")
    print("4. Ensure drivers for USB-RS232 adapter are installed")
    print("5. Try different baud rates (4800, 9600, 19200, 38400)")
    
    return None, None

def keithley_basic_commands(ser):
    """Demonstrate basic commands with the Keithley 6482"""
    print("\n" + "=" * 50)
    print("Testing basic Keithley 6482 commands...")
    print("=" * 50)
    
    try:
        # Reset the instrument
        print("\nResetting instrument...")
        ser.write(b'*RST\r\n')
        time.sleep(1)
        
        # Query error status
        ser.write(b'SYST:ERR?\r\n')
        time.sleep(0.1)
        error = ser.readline().decode('utf-8').strip()
        print(f"System error status: {error}")
        
        # Configure for current measurement
        print("\nConfiguring for current measurement...")
        ser.write(b':CONF:CURR\r\n')
        time.sleep(0.5)
        
        # Read a measurement
        print("Taking a measurement...")
        ser.write(b':READ?\r\n')
        time.sleep(0.5)
        measurement = ser.readline().decode('utf-8').strip()
        print(f"Current measurement: {measurement} A")
        
    except Exception as e:
        print(f"Error during command execution: {e}")

def main():
    """Main function to discover and connect to Keithley 6482"""
    print("Keithley 6482 Picoammeter Discovery Tool")
    print("=" * 50)
    
    # Try automatic discovery
    ser, port = auto_discover_keithley()
    
    if ser:
        print(f"\n✓ Successfully connected to Keithley 6482 on {port}")
        
        # Demonstrate basic commands
        response = input("\nWould you like to test basic commands? (y/n): ")
        if response.lower() == 'y':
            keithley_basic_commands(ser)
        
        # Keep connection open for manual testing
        print("\nConnection established. You can now send commands manually.")
        print("Example commands:")
        print("  *IDN?     - Identify instrument")
        print("  :READ?    - Take a measurement")
        print("  :CONF:CURR - Configure for current measurement")
        print("  *RST      - Reset instrument")
        print("\nType 'exit' to quit")
        
        while True:
            command = input("\nEnter command (or 'exit'): ")
            if command.lower() == 'exit':
                break
            
            try:
                ser.write(f"{command}\r\n".encode())
                time.sleep(0.1)
                
                if '?' in command:  # Query command
                    response = ser.readline().decode('utf-8').strip()
                    print(f"Response: {response}")
            except Exception as e:
                print(f"Error: {e}")
        
        ser.close()
        print("Connection closed.")
    else:
        # Manual port entry option
        print("\nWould you like to manually specify a COM port?")
        manual_port = input("Enter COM port (e.g., COM3) or press Enter to exit: ")
        
        if manual_port:
            baudrates = [9600, 4800, 19200, 38400]
            for baud in baudrates:
                print(f"\nTrying {manual_port} at {baud} baud...")
                ser, device_id = test_keithley_connection(manual_port, baudrate=baud)
                if ser:
                    print(f"✓ Connected at {baud} baud!")
                    ser.close()
                    break

if __name__ == "__main__":
    # Check if pyserial is installed
    try:
        import serial
        import serial.tools.list_ports
    except ImportError:
        print("Error: pyserial library not installed.")
        print("Please install it using: pip install pyserial")
        exit(1)
    
    main()
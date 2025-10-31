import pyvisa
import time
from typing import Optional, Tuple

class PowerSupplyController:
    """
    A class to control a programmable power supply via VISA.
    Tested with Siglent SPD series power supplies.
    """
    
    def __init__(self, resource_string: str):
        """
        Initialize the power supply controller.
        
        Args:
            resource_string: VISA resource string (e.g., "USB0::0xF4EC::0x1410::SPD13DCQ7R0719::INSTR")
        """
        self.resource_string = resource_string
        self.instrument = None
        self.rm = None
        self.is_connected = False
        
    def connect(self) -> bool:
        """
        Connect to the power supply.
        
        Returns:
            bool: True if connection successful, False otherwise
        """
        try:
            # Create resource manager
            self.rm = pyvisa.ResourceManager()
            
            # Connect to instrument
            self.instrument = self.rm.open_resource(self.resource_string)
            
            # Set timeout (10 seconds)
            self.instrument.timeout = 10000
            
            # Test connection by getting instrument ID
            idn = self.instrument.query('*IDN?')
            print(f"Connected to: {idn.strip()}")
            
            self.is_connected = True
            return True
            
        except Exception as e:
            print(f"Connection failed: {e}")
            self.is_connected = False
            return False
    
    def disconnect(self) -> None:
        """Disconnect from the power supply."""
        try:
            if self.instrument:
                self.instrument.close()
            if self.rm:
                self.rm.close()
            self.is_connected = False
            print("Disconnected from power supply")
        except Exception as e:
            print(f"Error during disconnection: {e}")
    
    def set_voltage(self, channel: int, voltage: float) -> bool:
        """
        Set output voltage for specified channel.
        
        Args:
            channel: Channel number (typically 1 or 2)
            voltage: Voltage to set in volts
            
        Returns:
            bool: True if successful, False otherwise
        """
        if not self.is_connected:
            print("Not connected to power supply")
            return False
            
        try:
            command = f"CH{channel}:VOLT {voltage}"
            self.instrument.write(command)
            print(f"Set CH{channel} voltage to {voltage}V")
            return True
        except Exception as e:
            print(f"Error setting voltage: {e}")
            return False
    
    def set_current(self, channel: int, current: float) -> bool:
        """
        Set current limit for specified channel.
        
        Args:
            channel: Channel number (typically 1 or 2)
            current: Current limit in amperes
            
        Returns:
            bool: True if successful, False otherwise
        """
        if not self.is_connected:
            print("Not connected to power supply")
            return False
            
        try:
            command = f"CH{channel}:CURR {current}"
            self.instrument.write(command)
            print(f"Set CH{channel} current limit to {current}A")
            return True
        except Exception as e:
            print(f"Error setting current: {e}")
            return False
    
    def set_output(self, channel: int, state: bool) -> bool:
        """
        Turn channel output on or off.
        
        Args:
            channel: Channel number
            state: True for ON, False for OFF
            
        Returns:
            bool: True if successful, False otherwise
        """
        if not self.is_connected:
            print("Not connected to power supply")
            return False
            
        try:
            state_str = "ON" if state else "OFF"
            command = f"OUTP CH{channel},{state_str}"
            self.instrument.write(command)
            print(f"CH{channel} output: {state_str}")
            return True
        except Exception as e:
            print(f"Error setting output state: {e}")
            return False
    
    def get_voltage(self, channel: int) -> Optional[float]:
        """
        Get actual output voltage.
        
        Args:
            channel: Channel number
            
        Returns:
            float: Actual voltage, or None if error
        """
        if not self.is_connected:
            print("Not connected to power supply")
            return None
            
        try:
            command = f"MEAS:VOLT? CH{channel}"
            response = self.instrument.query(command)
            voltage = float(response.strip())
            return voltage
        except Exception as e:
            print(f"Error reading voltage: {e}")
            return None
    
    def get_current(self, channel: int) -> Optional[float]:
        """
        Get actual output current.
        
        Args:
            channel: Channel number
            
        Returns:
            float: Actual current, or None if error
        """
        if not self.is_connected:
            print("Not connected to power supply")
            return None
            
        try:
            command = f"MEAS:CURR? CH{channel}"
            response = self.instrument.query(command)
            current = float(response.strip())
            return current
        except Exception as e:
            print(f"Error reading current: {e}")
            return None
    
    def get_output_state(self, channel: int) -> Optional[bool]:
        """
        Get output state (on/off).
        
        Args:
            channel: Channel number
            
        Returns:
            bool: True if on, False if off, None if error
        """
        if not self.is_connected:
            print("Not connected to power supply")
            return None
            
        try:
            command = f"OUTP? CH{channel}"
            response = self.instrument.query(command)
            state = response.strip() == "ON"
            return state
        except Exception as e:
            print(f"Error reading output state: {e}")
            return None
    
    def setup_channel(self, channel: int, voltage: float, current: float, enable: bool = False) -> bool:
        """
        Convenience method to set up a channel with voltage, current, and output state.
        
        Args:
            channel: Channel number
            voltage: Voltage setting in volts
            current: Current limit in amperes
            enable: Whether to enable output immediately
            
        Returns:
            bool: True if all operations successful
        """
        success = True
        success &= self.set_voltage(channel, voltage)
        success &= self.set_current(channel, current)
        if enable:
            success &= self.set_output(channel, True)
        return success
    
    def get_status(self, channel: int) -> dict:
        """
        Get comprehensive status of a channel.
        
        Args:
            channel: Channel number
            
        Returns:
            dict: Dictionary with voltage, current, and output state
        """
        status = {
            'voltage': self.get_voltage(channel),
            'current': self.get_current(channel),
            'output_enabled': self.get_output_state(channel)
        }
        return status


# Example usage and test script
if __name__ == "__main__":
    # Your device resource string
    RESOURCE_STRING = "USB0::0xF4EC::0x1410::SPD13DCQ7R0719::INSTR"
    
    # Create power supply controller
    ps = PowerSupplyController(RESOURCE_STRING)
    
    try:
        # Connect to the power supply
        if ps.connect():
            print("\n=== Power Supply Connected ===")
            
            # Test basic functionality
            channel = 1  # Use channel 1
            
            # Set up channel: 5V, 1A current limit, but don't turn on yet
            print(f"\nSetting up CH{channel}: 5V, 1A limit")
            ps.setup_channel(channel, 5.0, 1.0, enable=False)
            
            # Check status
            status = ps.get_status(channel)
            print(f"CH{channel} Status: {status}")
            
            # Turn on output
            print(f"\nTurning ON CH{channel}")
            ps.set_output(channel, True)
            
            # Wait a moment and check again
            time.sleep(1)
            status = ps.get_status(channel)
            print(f"CH{channel} Status after turning on: {status}")
            
            # Turn off output
            print(f"\nTurning OFF CH{channel}")
            ps.set_output(channel, False)
            
        else:
            print("Failed to connect to power supply")
            
    except KeyboardInterrupt:
        print("\nOperation cancelled by user")
        
    finally:
        # Always disconnect
        ps.disconnect()
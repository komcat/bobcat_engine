"""Keithley multimeter interface module with dual channel support."""

import pyvisa
from typing import Optional, Tuple, Dict, List


class KeithleyMultimeter:
    """Interface for Keithley 6482 dual-channel multimeter operations."""
    
    def __init__(self, address: str):
        """
        Initialize the multimeter interface.
        
        Args:
            address: VISA resource address of the multimeter
        """
        self.address = address
        self.multimeter = None
        self.resource_manager = None
        self.enabled_channels = [1, 2]
        
    def connect(self) -> Tuple[bool, str]:
        """
        Connect to the multimeter.
        
        Returns:
            Tuple of (success, message)
        """
        try:
            self.resource_manager = pyvisa.ResourceManager()
            self.multimeter = self.resource_manager.open_resource(self.address)
            
            # Verify it's a Keithley 6482
            idn = self.multimeter.query("*IDN?")
            if "6482" in idn:
                return True, f"Connected to Keithley 6482: {idn.strip()}"
            else:
                return True, f"Connected to multimeter: {idn.strip()}"
                
        except Exception as e:
            return False, f"Failed to connect to multimeter: {e}"
    
    def configure(self, function: str = 'CURR', channels: List[int] = None) -> bool:
        """
        Configure the multimeter measurement function.
        
        Args:
            function: Measurement function (e.g., 'CURR' for current)
            channels: List of channels to enable [1, 2]
            
        Returns:
            Success status
        """
        if not self.multimeter:
            return False
        
        if channels:
            self.enabled_channels = channels
            
        try:
            # Configure for current measurement on both channels
            self.multimeter.write(f":SENS:FUNC '{function}'")
            
            # Enable both channels for Keithley 6482
            if len(self.enabled_channels) == 2:
                self.multimeter.write(":ROUT:SCAN:INT (@1:2)")
                self.multimeter.write(":ROUT:SCAN:LSEL INT")
            elif 1 in self.enabled_channels:
                self.multimeter.write(":ROUT:SCAN:INT (@1)")
            elif 2 in self.enabled_channels:
                self.multimeter.write(":ROUT:SCAN:INT (@2)")
                
            return True
        except Exception as e:
            print(f"Failed to configure multimeter: {e}")
            return False
    
    def read_measurement(self) -> Dict[int, Optional[float]]:
        """
        Read measurements from enabled channels.
        
        Returns:
            Dictionary with channel numbers as keys and measurements as values
        """
        if not self.multimeter:
            return {ch: None for ch in self.enabled_channels}
            
        try:
            response = self.multimeter.query(":READ?")
            data_str = response.strip()
            
            # Parse response for both channels
            values = {}
            if ',' in data_str:
                # Multi-channel response
                parts = data_str.split(',')
                for i, ch in enumerate(self.enabled_channels):
                    if i < len(parts):
                        try:
                            values[ch] = float(parts[i])
                        except ValueError:
                            values[ch] = None
                    else:
                        values[ch] = None
            else:
                # Single channel response
                try:
                    values[self.enabled_channels[0]] = float(data_str)
                except ValueError:
                    values[self.enabled_channels[0]] = None
                    
            # Fill in None for any missing channels
            for ch in self.enabled_channels:
                if ch not in values:
                    values[ch] = None
                    
            return values
                
        except (pyvisa.errors.VisaIOError, ValueError) as e:
            print(f"Error reading measurement: {e}")
            return {ch: None for ch in self.enabled_channels}
        except Exception as e:
            print(f"Unexpected error: {e}")
            return {ch: None for ch in self.enabled_channels}
    
    def read_channel(self, channel: int) -> Optional[float]:
        """
        Read measurement from a specific channel.
        
        Args:
            channel: Channel number (1 or 2)
            
        Returns:
            Measurement value or None if error
        """
        values = self.read_measurement()
        return values.get(channel, None)
    
    def close(self):
        """Close the multimeter connection."""
        if self.multimeter:
            try:
                self.multimeter.close()
            except:
                pass
        
        if self.resource_manager:
            try:
                self.resource_manager.close()
            except:
                pass
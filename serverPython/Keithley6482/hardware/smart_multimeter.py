"""Smart Keithley multimeter interface with voltage source control - SYNTAX CORRECTED."""

import pyvisa
import time
from typing import Optional, Tuple, Dict, List
from enum import Enum


class CurrentRange(Enum):
    """Keithley 6482 current ranges."""
    RANGE_2nA = 2e-9
    RANGE_20nA = 2e-8
    RANGE_200nA = 2e-7
    RANGE_2uA = 2e-6
    RANGE_20uA = 2e-5
    RANGE_200uA = 2e-4
    RANGE_2mA = 2e-3
    RANGE_20mA = 2e-2


class SmartKeithleyMultimeter:
    """Smart interface for Keithley 6482 with voltage source and current measurement."""
    
    def __init__(self, address: str):
        """Initialize the smart multimeter interface."""
        self.address = address
        self.multimeter = None
        self.resource_manager = None
        self.enabled_channels = [1, 2]
        
        # Range management
        self.current_ranges = {1: CurrentRange.RANGE_20mA.value, 
                               2: CurrentRange.RANGE_20mA.value}
        self.use_auto_range = True
        self.range_check_interval = 100
        self.reading_count = {1: 0, 2: 0}
        
        # Performance tracking
        self.overflow_count = {1: 0, 2: 0}
        self.underflow_count = {1: 0, 2: 0}
        
        # Voltage source settings
        self.voltage_enabled = {1: False, 2: False}
        self.voltage_levels = {1: 0.0, 2: 0.0}
        self.voltage_compliance = 20e-3  # 20mA current compliance (default)
        
    def connect(self) -> Tuple[bool, str]:
        """Connect to the multimeter with optimized settings."""
        try:
            self.resource_manager = pyvisa.ResourceManager()
            self.multimeter = self.resource_manager.open_resource(self.address)
            
            # Set timeout
            self.multimeter.timeout = 1000
            
            # Clear errors
            self.multimeter.write("*CLS")
            
            # Get ID
            idn = self.multimeter.query("*IDN?")
            
            # Optimize VISA
            if hasattr(self.multimeter, 'read_termination'):
                self.multimeter.read_termination = '\n'
            if hasattr(self.multimeter, 'write_termination'):
                self.multimeter.write_termination = '\n'
            if hasattr(self.multimeter, 'chunk_size'):
                self.multimeter.chunk_size = 102400
                
            return True, f"Connected: {idn.strip()}"
                
        except Exception as e:
            return False, f"Failed to connect: {e}"
    
    # ============= VOLTAGE SOURCE FUNCTIONS =============
    
    def set_voltage_source(self, channel: int, voltage: float, 
                          enable: bool = True, 
                          compliance_current: Optional[float] = None) -> Tuple[bool, str]:
        """
        Set voltage source on a channel.
        
        Args:
            channel: Channel number (1 or 2)
            voltage: Voltage to source in Volts (e.g., 1.0 for 1V)
            enable: Whether to enable the output
            compliance_current: Current compliance limit in Amps (default: 20mA)
            
        Returns:
            Tuple of (success, message)
        """
        if not self.multimeter:
            return False, "Multimeter not connected"
        
        if channel not in [1, 2]:
            return False, f"Invalid channel: {channel}. Must be 1 or 2"
        
        # Keithley 6482 voltage range is typically ±30V
        if abs(voltage) > 30:
            return False, f"Voltage {voltage}V exceeds maximum range (±30V)"
        
        try:
            # CORRECTED SYNTAX: Use proper channel numbering for 6482
            ch_str = "" if channel == 1 else "2"
            
            # Note: 6482 has fixed 20mA compliance, cannot be changed
            if compliance_current is not None:
                self.voltage_compliance = abs(compliance_current)
                # Store for reference but cannot actually set on 6482
            
            # CORRECTED: Set voltage level (6482 doesn't need FUNC command)
            self.multimeter.write(f":SOUR{ch_str}:VOLT {voltage}")
            self.voltage_levels[channel] = voltage
            
            # CORRECTED: Enable/disable output using OUTPut command
            if enable:
                self.multimeter.write(f":OUTP{ch_str} ON")
                self.voltage_enabled[channel] = True
                message = f"Channel {channel}: Voltage source set to {voltage}V (enabled)"
            else:
                self.multimeter.write(f":OUTP{ch_str} OFF")
                self.voltage_enabled[channel] = False
                message = f"Channel {channel}: Voltage source set to {voltage}V (disabled)"
            
            print(message)
            return True, message
            
        except Exception as e:
            return False, f"Failed to set voltage source: {e}"
    
    def enable_voltage_output(self, channel: int, enable: bool = True) -> Tuple[bool, str]:
        """
        Enable or disable voltage output on a channel.
        
        Args:
            channel: Channel number (1 or 2)
            enable: True to enable, False to disable
            
        Returns:
            Tuple of (success, message)
        """
        if not self.multimeter:
            return False, "Multimeter not connected"
        
        if channel not in [1, 2]:
            return False, f"Invalid channel: {channel}"
        
        try:
            # CORRECTED SYNTAX
            ch_str = "" if channel == 1 else "2"
            
            if enable:
                self.multimeter.write(f":OUTP{ch_str} ON")
                self.voltage_enabled[channel] = True
                message = f"Channel {channel} voltage output enabled"
            else:
                self.multimeter.write(f":OUTP{ch_str} OFF")
                self.voltage_enabled[channel] = False
                message = f"Channel {channel} voltage output disabled"
            
            print(message)
            return True, message
            
        except Exception as e:
            return False, f"Failed to control voltage output: {e}"
    
    def set_both_channels_voltage(self, voltage1: float, voltage2: float, 
                                 enable: bool = True) -> Tuple[bool, str]:
        """
        Set voltage on both channels simultaneously.
        
        Args:
            voltage1: Voltage for channel 1 in Volts
            voltage2: Voltage for channel 2 in Volts
            enable: Whether to enable outputs
            
        Returns:
            Tuple of (success, message)
        """
        success1, msg1 = self.set_voltage_source(1, voltage1, enable)
        success2, msg2 = self.set_voltage_source(2, voltage2, enable)
        
        if success1 and success2:
            return True, f"Both channels set: Ch1={voltage1}V, Ch2={voltage2}V"
        elif success1:
            return False, f"Only Ch1 set. Ch2 error: {msg2}"
        elif success2:
            return False, f"Only Ch2 set. Ch1 error: {msg1}"
        else:
            return False, f"Both channels failed. Ch1: {msg1}, Ch2: {msg2}"
    
    def disable_all_voltage_outputs(self) -> Tuple[bool, str]:
        """
        Disable voltage output on all channels (safety function).
        
        Returns:
            Tuple of (success, message)
        """
        try:
            # CORRECTED SYNTAX
            self.multimeter.write(":OUTP1 OFF")
            self.multimeter.write(":OUTP2 OFF")
            self.voltage_enabled[1] = False
            self.voltage_enabled[2] = False
            return True, "All voltage outputs disabled"
        except Exception as e:
            return False, f"Failed to disable voltage outputs: {e}"
    
    def get_voltage_status(self) -> Dict:
        """
        Get current voltage source status for all channels.
        
        Returns:
            Dictionary with voltage status for each channel
        """
        status = {}
        for ch in [1, 2]:
            status[ch] = {
                'enabled': self.voltage_enabled[ch],
                'voltage': self.voltage_levels[ch],
                'compliance': self.voltage_compliance
            }
        return status
    
    def measure_with_voltage_source(self, channel: int) -> Tuple[Optional[float], Optional[float]]:
        """
        Measure current while sourcing voltage on a channel.
        
        Args:
            channel: Channel number (1 or 2)
            
        Returns:
            Tuple of (current in Amps, voltage in Volts)
        """
        if not self.multimeter:
            return None, None
        
        try:
            # CORRECTED SYNTAX: Set format element to read correct channel
            if channel == 1:
                self.multimeter.write(":FORM:ELEM CURR1")
            else:
                self.multimeter.write(":FORM:ELEM CURR2")
            
            # Read current measurement
            response = self.multimeter.query(":READ?")
            
            try:
                current = float(response)
                return current, self.voltage_levels[channel]
            except ValueError:
                return None, None
                
        except Exception as e:
            print(f"Measurement error: {e}")
            return None, None
    
    # ============= ALIGNMENT MODE WITH VOLTAGE SOURCE =============
    
    def configure_for_alignment_with_bias(self, bias_voltage: float = 1.0, 
                                         initial_range: str = "AUTO") -> bool:
        """
        Configure for alignment with bias voltage applied.
        
        Args:
            bias_voltage: Voltage to apply during alignment (V)
            initial_range: Current measurement range setting
            
        Returns:
            Success status
        """
        if not self.multimeter:
            return False
            
        try:
            # Configure current measurement first
            success = self.configure_for_alignment(initial_range)
            
            if success:
                # Apply bias voltage to both channels
                self.set_both_channels_voltage(bias_voltage, bias_voltage, enable=True)
                print(f"Alignment mode with {bias_voltage}V bias configured")
                return True
            
            return False
            
        except Exception as e:
            print(f"Configuration failed: {e}")
            return False
    
    # ============= ORIGINAL FUNCTIONS (with corrected syntax) =============
    
    def configure_for_alignment(self, initial_range: str = "AUTO") -> bool:
        """Configure for optical/electrical alignment with wide dynamic range."""
        if not self.multimeter:
            return False
            
        try:
            # Reset
            #self.multimeter.write("*RST")
            #time.sleep(0.5)
            
            # CORRECTED: 6482 doesn't have SENS:FUNC command (measures current by default)
            # Removed: self.multimeter.write(":SENS:FUNC 'CURR'")
            
            # CORRECTED: Configure channels individually
            if initial_range == "LOW":
                self._set_range(CurrentRange.RANGE_2uA.value)
                self.multimeter.write(":SENS1:CURR:NPLC 1")
                self.multimeter.write(":SENS2:CURR:NPLC 1")
            elif initial_range == "MID":
                self._set_range(CurrentRange.RANGE_200uA.value)
                self.multimeter.write(":SENS1:CURR:NPLC 0.1")
                self.multimeter.write(":SENS2:CURR:NPLC 0.1")
            elif initial_range == "HIGH":
                self._set_range(CurrentRange.RANGE_20mA.value)
                self.multimeter.write(":SENS1:CURR:NPLC 0.01")
                self.multimeter.write(":SENS2:CURR:NPLC 0.01")
            else:
                self.multimeter.write(":SENS1:CURR:RANG:AUTO ON")
                self.multimeter.write(":SENS2:CURR:RANG:AUTO ON")
                self.multimeter.write(":SENS1:CURR:NPLC 0.1")
                self.multimeter.write(":SENS2:CURR:NPLC 0.1")
                self.use_auto_range = True
                
            # CORRECTED: Optimization settings with proper syntax
            self.multimeter.write(":FORM:ELEM CURR1,CURR2")
            self.multimeter.write(":SENS1:CURR:AVER:STAT ON")
            self.multimeter.write(":SENS1:CURR:AVER:COUN 3")
            self.multimeter.write(":SENS1:CURR:AVER:TCON REP")
            self.multimeter.write(":SENS2:CURR:AVER:STAT ON")
            self.multimeter.write(":SENS2:CURR:AVER:COUN 3")
            self.multimeter.write(":SENS2:CURR:AVER:TCON REP")
            
            # CORRECTED: Auto-zero syntax
            self.multimeter.write(":SYST:AZER ON")
            
            try:
                self.multimeter.write(":DISP:ENAB ON")
            except:
                pass
                
            return True
            
        except Exception as e:
            print(f"Configuration failed: {e}")
            return False
    
    def configure_fast_mode(self) -> bool:
        """Switch to fast mode after initial alignment."""
        if not self.multimeter:
            return False
            
        try:
            # CORRECTED SYNTAX for both channels
            self.multimeter.write(":SENS1:CURR:NPLC 0.01")
            self.multimeter.write(":SENS2:CURR:NPLC 0.01")
            self.multimeter.write(":SYST:AZER OFF")
            self.multimeter.write(":SENS1:CURR:AVER:STAT OFF")
            self.multimeter.write(":SENS2:CURR:AVER:STAT OFF")
            self._set_range(CurrentRange.RANGE_20mA.value)
            
            print("Switched to fast mode (20mA range, NPLC=0.01)")
            return True
            
        except Exception as e:
            print(f"Fast mode configuration failed: {e}")
            return False
    
    def configure_ultra_fast_mode(self) -> bool:
        """Ultra-fast mode for maximum speed."""
        if not self.multimeter:
            return False
            
        try:
            print("Configuring ULTRA-FAST mode...")
            
            # CORRECTED: Minimum NPLC for 6482 is 0.01
            self.multimeter.write(":SENS1:CURR:NPLC 0.01")
            self.multimeter.write(":SENS2:CURR:NPLC 0.01")
            self.multimeter.write(":SYST:AZER OFF")
            self.multimeter.write(":SENS1:CURR:AVER:STAT OFF")
            self.multimeter.write(":SENS2:CURR:AVER:STAT OFF")
            
            try:
                self.multimeter.write(":SENS1:CURR:MED:STAT OFF")
                self.multimeter.write(":SENS2:CURR:MED:STAT OFF")
            except:
                pass
            
            self.multimeter.write(":SENS1:CURR:RANG:AUTO OFF")
            self.multimeter.write(":SENS1:CURR:RANG 0.02")
            self.multimeter.write(":SENS2:CURR:RANG:AUTO OFF")
            self.multimeter.write(":SENS2:CURR:RANG 0.02")
            
            try:
                self.multimeter.write(":DISP:ENAB OFF")
            except:
                pass
            
            self.multimeter.write(":INIT:CONT ON")
            self.multimeter.write(":FORM:ELEM CURR1,CURR2")
            self.multimeter.write(":FORM:DATA ASC")
            
            try:
                # CORRECTED: 6482 doesn't have CALC:STAT command
                # Removed: self.multimeter.write(":CALC:STAT OFF")
                # CORRECTED: 6482 doesn't have SYST:REM command
                # Removed: self.multimeter.write(":SYST:REM")
                pass
            except:
                pass
            
            self.multimeter.write("*CLS")
            
            print("ULTRA-FAST mode configured - Target: 100+ Hz")
            return True
            
        except Exception as e:
            print(f"Ultra-fast mode configuration failed: {e}")
            return False
    
    def _set_range(self, range_value: float):
        """Set measurement range."""
        # CORRECTED SYNTAX for both channels
        self.multimeter.write(":SENS1:CURR:RANG:AUTO OFF")
        self.multimeter.write(f":SENS1:CURR:RANG {range_value}")
        self.multimeter.write(":SENS2:CURR:RANG:AUTO OFF")
        self.multimeter.write(f":SENS2:CURR:RANG {range_value}")
        for ch in self.enabled_channels:
            self.current_ranges[ch] = range_value
    
    def read_with_smart_ranging(self) -> Dict[int, Tuple[Optional[float], str]]:
        """Read measurements with intelligent range management."""
        if not self.multimeter:
            return {ch: (None, "Disconnected") for ch in self.enabled_channels}
            
        try:
            response = self.multimeter.query(":READ?")
            
            if 'OVFL' in response or '+9.9E37' in response:
                self._handle_overflow()
                return self.read_with_smart_ranging()
                
            values = {}
            range_info = {}
            
            if ',' in response:
                parts = response.split(',')
                for i, ch in enumerate(self.enabled_channels):
                    if i < len(parts):
                        try:
                            value = float(parts[i])
                            values[ch] = value
                            range_info[ch] = self._check_optimal_range(ch, value)
                        except ValueError:
                            values[ch] = None
                            range_info[ch] = "Error"
            else:
                try:
                    value = float(response)
                    ch = self.enabled_channels[0]
                    values[ch] = value
                    range_info[ch] = self._check_optimal_range(ch, value)
                except ValueError:
                    values[self.enabled_channels[0]] = None
                    range_info[self.enabled_channels[0]] = "Error"
            
            for ch in self.enabled_channels:
                self.reading_count[ch] += 1
                
                if self.reading_count[ch] % self.range_check_interval == 0:
                    if ch in values and values[ch] is not None:
                        self._optimize_range(ch, values[ch])
            
            result = {}
            for ch in self.enabled_channels:
                if ch in values:
                    result[ch] = (values[ch], range_info.get(ch, "Unknown"))
                else:
                    result[ch] = (None, "No data")
                    
            return result
            
        except Exception as e:
            print(f"Read error: {e}")
            return {ch: (None, "Error") for ch in self.enabled_channels}
    
    def _check_optimal_range(self, channel: int, value: float) -> str:
        """Check if current value is in optimal range."""
        if value is None:
            return "Invalid"
            
        current_range = self.current_ranges[channel]
        abs_value = abs(value)
        
        if abs_value < current_range * 0.1:
            if current_range > CurrentRange.RANGE_2nA.value:
                return "Underrange"
        elif abs_value > current_range * 0.9:
            if current_range < CurrentRange.RANGE_20mA.value:
                return "Near overflow"
        
        # Include voltage info if sourcing
        voltage_info = ""
        if self.voltage_enabled[channel]:
            voltage_info = f" @{self.voltage_levels[channel]}V"
        
        if current_range >= 1e-3:
            return f"{current_range*1000:.0f}mA{voltage_info}"
        elif current_range >= 1e-6:
            return f"{current_range*1e6:.0f}μA{voltage_info}"
        else:
            return f"{current_range*1e9:.0f}nA{voltage_info}"
    
    def _optimize_range(self, channel: int, value: float):
        """Optimize range based on measured value."""
        if self.use_auto_range:
            return
            
        abs_value = abs(value)
        current_range = self.current_ranges[channel]
        
        optimal_range = None
        for range_enum in CurrentRange:
            range_val = range_enum.value
            if abs_value <= range_val * 0.9:
                optimal_range = range_val
                break
        
        if optimal_range and optimal_range != current_range:
            # CORRECTED SYNTAX
            ch_str = "" if channel == 1 else "2"
            print(f"Channel {channel}: Switching range from {current_range} to {optimal_range}")
            self.multimeter.write(f":SENS{ch_str}:CURR:RANG {optimal_range}")
            self.current_ranges[channel] = optimal_range
    
    def _handle_overflow(self):
        """Handle overflow by increasing range."""
        # CORRECTED SYNTAX
        self._set_range(CurrentRange.RANGE_20mA.value)
        print("Overflow detected - switched to 20mA range")
    
    def get_fast_measurement(self) -> Dict[int, Optional[float]]:
        """Fast measurement without range checking."""
        if not self.multimeter:
            return {ch: None for ch in self.enabled_channels}
            
        try:
            response = self.multimeter.query(":READ?")
            values = {}
            
            if ',' in response:
                parts = response.split(',')
                for i, ch in enumerate(self.enabled_channels):
                    if i < len(parts):
                        try:
                            values[ch] = float(parts[i])
                        except:
                            values[ch] = None
            else:
                try:
                    values[self.enabled_channels[0]] = float(response)
                except:
                    values[self.enabled_channels[0]] = None
                    
            for ch in self.enabled_channels:
                if ch not in values:
                    values[ch] = None
                    
            return values
            
        except Exception as e:
            return {ch: None for ch in self.enabled_channels}
    
    def read_ultra_fast(self) -> Dict[int, Optional[float]]:
        """Ultra-fast read without any checking."""
        if not self.multimeter:
            return {ch: None for ch in self.enabled_channels}
        
        try:
            if hasattr(self.multimeter, 'query_ascii_values'):
                response = self.multimeter.query_ascii_values(":READ?")
                
                values = {}
                if len(response) >= 2:
                    values[1] = response[0]
                    values[2] = response[1]
                elif len(response) == 1:
                    values[self.enabled_channels[0]] = response[0]
                    
                for ch in self.enabled_channels:
                    if ch not in values:
                        values[ch] = None
                        
                return values
            else:
                return self.get_fast_measurement()
                
        except:
            return {ch: None for ch in self.enabled_channels}
    
    def close(self):
        """Close connection and disable voltage outputs safely."""
        if self.multimeter:
            try:
                # Safety: Disable all voltage outputs before closing
                self.disable_all_voltage_outputs()
                
                # Reset to safe defaults - CORRECTED SYNTAX
                self.multimeter.write(":SENS1:CURR:RANG:AUTO ON")
                self.multimeter.write(":SENS2:CURR:RANG:AUTO ON")
                self.multimeter.write(":SYST:AZER ON")
                self.multimeter.write(":DISP:ENAB ON")
                self.multimeter.close()
            except:
                pass
        
        if self.resource_manager:
            try:
                self.resource_manager.close()
            except:
                pass
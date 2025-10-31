import pyvisa
import time

def connect_keithley_2400():
    """Connect to Keithley 2400 SourceMeter"""
    rm = pyvisa.ResourceManager()
    instrument = rm.open_resource('GPIB1::24::INSTR')
    instrument.timeout = 5000  # 5 second timeout
    return instrument

def basic_setup_and_test():
    """Basic setup and measurement example"""
    keithley = connect_keithley_2400()
    
    try:
        # Reset and setup
        keithley.write('*RST')                    # Reset to defaults
        keithley.write('*CLS')                    # Clear status
        
        # Basic configuration
        keithley.write(':SOUR:FUNC VOLT')        # Source voltage
        keithley.write(':SOUR:VOLT:MODE FIXED')  # Fixed voltage mode
        keithley.write(':SOUR:VOLT 0')           # Set voltage to 0V
        keithley.write(':SOUR:VOLT:RANG 20')     # 20V range
        
        # Measurement setup
        keithley.write(':SENS:FUNC "CURR"')      # Measure current
        keithley.write(':SENS:CURR:RANG:AUTO ON') # Auto-range current
        keithley.write(':SENS:CURR:PROT 0.1')    # 100mA compliance
        
        # Output control
        keithley.write(':OUTP ON')               # Turn output on
        
        # Take a measurement
        measurement = keithley.query(':READ?')
        print(f"Measurement: {measurement.strip()}")
        
        # Turn output off
        keithley.write(':OUTP OFF')
        
    finally:
        keithley.close()

# Essential Commands for Keithley 2400
"""
=== BASIC CONTROL ===
*RST                           # Reset instrument
*CLS                           # Clear status
*IDN?                          # Get identification
:SYST:ERR?                     # Check for errors

=== SOURCE CONFIGURATION ===
:SOUR:FUNC {VOLT|CURR}         # Set source function
:SOUR:VOLT <value>             # Set voltage level
:SOUR:CURR <value>             # Set current level
:SOUR:VOLT:RANG <range>        # Set voltage range
:SOUR:CURR:RANG <range>        # Set current range
:SOUR:VOLT:MODE {FIXED|LIST|SWE} # Voltage mode
:SOUR:CURR:MODE {FIXED|LIST|SWE} # Current mode

=== MEASUREMENT CONFIGURATION ===
:SENS:FUNC "{VOLT|CURR|RES|POW}" # Set measurement function
:SENS:VOLT:RANG <range>          # Voltage measurement range  
:SENS:CURR:RANG <range>          # Current measurement range
:SENS:VOLT:RANG:AUTO {ON|OFF}    # Auto-range voltage
:SENS:CURR:RANG:AUTO {ON|OFF}    # Auto-range current
:SENS:VOLT:PROT <value>          # Voltage compliance
:SENS:CURR:PROT <value>          # Current compliance

=== OUTPUT CONTROL ===
:OUTP {ON|OFF}                 # Turn output on/off
:OUTP?                         # Query output state

=== MEASUREMENTS ===
:READ?                         # Take reading (all enabled functions)
:MEAS:VOLT?                    # Measure voltage only
:MEAS:CURR?                    # Measure current only
:FETC?                         # Fetch last reading

=== DISPLAY CONTROL ===
:DISP:WIND1:TEXT:DATA "text"   # Display custom text
:DISP:WIND1:TEXT:STAT {ON|OFF} # Enable/disable text display
:DISP:ENAB {ON|OFF}            # Enable/disable display

=== USEFUL QUERIES ===
:SOUR:VOLT?                    # Query source voltage setting
:SOUR:CURR?                    # Query source current setting
:SENS:VOLT:PROT?               # Query voltage compliance
:SENS:CURR:PROT?               # Query current compliance
"""

def voltage_sweep_example():
    """Example: Perform a voltage sweep and measure current"""
    keithley = connect_keithley_2400()
    
    try:
        # Setup
        keithley.write('*RST')
        keithley.write(':SOUR:FUNC VOLT')
        keithley.write(':SOUR:VOLT:MODE FIXED')
        keithley.write(':SENS:FUNC "CURR"')
        keithley.write(':SENS:CURR:PROT 0.01')  # 10mA compliance
        keithley.write(':OUTP ON')
        
        # Sweep from 0V to 5V
        voltages = [0, 1, 2, 3, 4, 5]
        results = []
        
        for voltage in voltages:
            keithley.write(f':SOUR:VOLT {voltage}')
            time.sleep(0.1)  # Small delay for settling
            
            # Read voltage and current
            reading = keithley.query(':READ?')
            values = reading.strip().split(',')
            measured_voltage = float(values[0])
            measured_current = float(values[1])
            
            results.append((voltage, measured_voltage, measured_current))
            print(f"Set: {voltage}V, Measured: {measured_voltage:.6f}V, {measured_current:.9f}A")
        
        keithley.write(':OUTP OFF')
        return results
        
    finally:
        keithley.close()

def current_source_example():
    """Example: Use as current source and measure voltage"""
    keithley = connect_keithley_2400()
    
    try:
        # Setup as current source
        keithley.write('*RST')
        keithley.write(':SOUR:FUNC CURR')        # Source current
        keithley.write(':SOUR:CURR 0.001')       # 1mA
        keithley.write(':SENS:FUNC "VOLT"')      # Measure voltage
        keithley.write(':SENS:VOLT:PROT 10')     # 10V compliance
        keithley.write(':OUTP ON')
        
        # Take measurement
        reading = keithley.query(':READ?')
        values = reading.strip().split(',')
        voltage = float(values[0])
        current = float(values[1])
        
        print(f"Sourcing {current:.6f}A, Measuring {voltage:.6f}V")
        print(f"Calculated resistance: {voltage/current:.2f} Ohms")
        
        keithley.write(':OUTP OFF')
        
    finally:
        keithley.close()

if __name__ == "__main__":
    print("Keithley 2400 Examples")
    print("=" * 30)
    
    # Run basic test
    print("1. Basic Setup and Test:")
    basic_setup_and_test()
    
    print("\n2. Voltage Sweep Example:")
    voltage_sweep_example()
    
    print("\n3. Current Source Example:")
    current_source_example()
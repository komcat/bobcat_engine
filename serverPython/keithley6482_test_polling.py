import serial
import serial.tools.list_ports
import time
import datetime
import threading
import queue
import numpy as np

class Keithley6482Fast:
    """Optimized Keithley 6482 class for fastest possible RS-232 communication"""
    
    def __init__(self):
        self.ser = None
        self.port = None
        self.connected = False
        
    def auto_connect(self):
        """Auto-connect to Keithley 6482"""
        print("Searching for Keithley 6482...")
        
        # Find all COM ports
        ports = serial.tools.list_ports.comports()
        
        for port in ports:
            # Try different baud rates - start with common defaults
            baud_rates = [9600, 57600, 38400, 19200, 4800]
            
            for baud in baud_rates:
                print(f"Testing {port.device} at {baud} baud...", end=" ")
                
                try:
                    test_ser = serial.Serial(
                        port=port.device,
                        baudrate=baud,
                        bytesize=serial.EIGHTBITS,
                        parity=serial.PARITY_NONE,
                        stopbits=serial.STOPBITS_ONE,
                        timeout=0.5,
                        write_timeout=0.5,
                        xonxoff=False,  # No software flow control
                        rtscts=False,    # No hardware flow control
                        dsrdtr=False
                    )
                    
                    # Clear buffers
                    test_ser.reset_input_buffer()
                    test_ser.reset_output_buffer()
                    
                    # Send ID query
                    test_ser.write(b'*IDN?\r\n')
                    time.sleep(0.2)
                    
                    response = test_ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    if 'KEITHLEY' in response.upper() and '6482' in response:
                        self.ser = test_ser
                        self.port = port.device
                        self.connected = True
                        print(f"✓ Found!")
                        print(f"Device: {response}")
                        
                        # If we found it at 9600, try to increase to 57600
                        if baud == 9600:
                            print("\nAttempting to increase baud rate to 57600 for maximum speed...")
                            if self.set_baud_rate(57600):
                                print("Successfully increased to 57600 baud! (6x faster)")
                            else:
                                print("Staying at 9600 baud")
                        elif baud == 57600:
                            print("Already at maximum speed (57600 baud)!")
                        
                        return True
                    else:
                        test_ser.close()
                        print("✗")
                        
                except Exception as e:
                    print(f"✗")
                    if 'test_ser' in locals() and test_ser.is_open:
                        test_ser.close()
        
        print("\nKeithley 6482 not found!")
        print("Please check:")
        print("1. USB-RS232 cable is connected")
        print("2. Keithley 6482 is powered on")
        print("3. RS-232 cable is connected to Keithley")
        return False
    
    def set_baud_rate(self, new_baud):
        """Try to change the instrument's baud rate for faster communication"""
        try:
            print(f"  Sending command to change to {new_baud} baud...")
            
            # Send command to change baud rate on instrument
            # Using SCPI command format from manual
            self.ser.write(f':SYST:COMM:SER:BAUD {new_baud}\r\n'.encode())
            time.sleep(0.5)
            
            # The instrument will immediately switch to the new baud rate
            # So we need to close and reopen the connection
            old_port = self.port
            self.ser.close()
            
            print(f"  Reconnecting at {new_baud} baud...")
            
            # Reopen with new baud rate
            self.ser = serial.Serial(
                port=old_port,
                baudrate=new_baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.5,
                write_timeout=0.5,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False
            )
            
            # Clear buffers
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            
            # Test connection at new baud rate
            self.ser.write(b'*IDN?\r\n')
            time.sleep(0.2)
            response = self.ser.readline().decode('utf-8', errors='ignore').strip()
            
            if response and 'KEITHLEY' in response.upper():
                print(f"  ✓ Successfully changed to {new_baud} baud!")
                return True
            else:
                print(f"  ✗ Failed to verify at {new_baud} baud")
                # Try to reconnect at original baud rate
                self.ser.close()
                self.ser = serial.Serial(
                    port=old_port,
                    baudrate=9600,
                    bytesize=serial.EIGHTBITS,
                    parity=serial.PARITY_NONE,
                    stopbits=serial.STOPBITS_ONE,
                    timeout=0.5,
                    write_timeout=0.5
                )
                return False
                
        except Exception as e:
            print(f"  ✗ Error changing baud rate: {e}")
            return False
    
    def optimize_settings(self):
        """Configure Keithley for absolute fastest measurements"""
        if not self.connected:
            return False
        
        print("\nOptimizing for maximum speed...")
        
        commands = [
            ('*RST', 1.0, 'Reset'),
            ('*CLS', 0.1, 'Clear'),
            (':SENS:FUNC "CURR"', 0.1, 'Set to current'),
            (':FORM:ELEM READ', 0.1, 'Return only reading'),
            (':FORM:DATA ASC', 0.1, 'ASCII format'),  # Ensure ASCII format
            (':SENS:CURR:NPLC 0.01', 0.1, 'Fastest integration (0.01 NPLC)'),
            (':SENS:CURR:RANG:AUTO OFF', 0.1, 'Disable auto-range'),
            (':SENS:CURR:RANG 2e-9', 0.1, 'Set fixed 2nA range'),
            (':SYST:AZERO OFF', 0.1, 'Disable auto-zero'),
            # (':DISP:ENAB OFF', 0.1, 'Disable display'),  # Comment out - may cause issues
            (':TRIG:SOUR IMM', 0.1, 'Immediate trigger'),
            (':TRIG:DEL 0', 0.1, 'No trigger delay'),
        ]
        
        for cmd, delay, description in commands:
            try:
                print(f"  {description}...", end="")
                self.ser.write(f'{cmd}\r\n'.encode())
                time.sleep(delay)
                
                # Clear any response from configuration commands
                self.ser.reset_input_buffer()
                
                print(" ✓")
            except Exception as e:
                print(f" ✗ ({e})")
                return False
        
        # Test that readings work
        print("  Testing readings...", end="")
        try:
            self.ser.reset_input_buffer()
            self.ser.write(b':READ?\r\n')
            time.sleep(0.2)
            test_response = self.ser.readline().decode('utf-8', errors='ignore').strip()
            
            if test_response and not test_response.startswith('`'):
                test_value = float(test_response.split(',')[0])
                print(f" ✓ (got {test_value:.3e} A)")
            else:
                print(f" ✗ (got unexpected: {repr(test_response)})")
                print("\n  Trying alternative configuration...")
                
                # Try without some optimizations
                self.ser.write(b':DISP:ENAB ON\r\n')
                time.sleep(0.1)
                self.ser.write(b':FORM:DATA ASC\r\n')
                time.sleep(0.1)
                self.ser.reset_input_buffer()
                
                # Test again
                self.ser.write(b':READ?\r\n')
                time.sleep(0.2)
                test_response = self.ser.readline().decode('utf-8', errors='ignore').strip()
                
                if test_response:
                    test_value = float(test_response.split(',')[0])
                    print(f"  Alternative config works: {test_value:.3e} A")
                
        except Exception as e:
            print(f" ✗ ({e})")
            return False
        
        print("Optimization complete!")
        return True
    
    def query(self, command):
        """Emulate PyVISA query method for compatibility"""
        if not self.connected:
            raise Exception("Not connected")
        
        max_retries = 3
        
        for retry in range(max_retries):
            try:
                # Clear input buffer for clean read
                self.ser.reset_input_buffer()
                
                # Send command
                self.ser.write(f'{command}\r\n'.encode())
                
                # Read response with appropriate timeout
                if command == ':READ?':
                    self.ser.timeout = 0.5  # Longer timeout for measurements
                else:
                    self.ser.timeout = 0.2
                
                response = self.ser.readline().decode('utf-8', errors='ignore').strip()
                
                # Check if response is valid
                if response and not response.startswith('`') and not response.startswith('\x06'):
                    return response
                elif retry < max_retries - 1:
                    # Bad response, clear and retry
                    time.sleep(0.05)
                    self.ser.reset_input_buffer()
                    continue
                else:
                    # Return empty on final retry
                    return ""
                    
            except Exception as e:
                if retry < max_retries - 1:
                    time.sleep(0.05)
                    continue
                else:
                    return ""
        
        return ""
    
    def close(self):
        """Close connection and restore settings"""
        if self.connected and self.ser:
            try:
                # Restore settings
                self.ser.write(b':DISP:ENAB ON\r\n')
                time.sleep(0.05)
                self.ser.write(b':SYST:AZERO ON\r\n')
                time.sleep(0.05)
                self.ser.close()
                print("Connection closed")
            except:
                pass
            finally:
                self.connected = False

class DataCollector:
    """Data collector matching your original code's structure"""
    
    def __init__(self, multimeter):
        self.multimeter = multimeter
        self.running = False
        self.elapsed_times = []
        self.measurements = []
        self.data_count = 0
        self.lock = threading.Lock()
        
    def collect_data(self, duration=5.0):
        """Collect data matching your original code's pattern"""
        print(f"\nCollecting data for {duration} seconds...")
        print("Press Ctrl+C to stop early\n")
        
        self.running = True
        start_time = time.time()
        
        # Display header
        print(f"{'Time (s)':>8} | {'Current (A)':>15} | {'Read Time (ms)':>12} | {'Rate (Hz)':>10}")
        print("-" * 60)
        
        while self.running and (time.time() - start_time) < duration:
            try:
                loop_start = time.time()
                
                # Get measurement - exactly like your code
                response = self.multimeter.query(":READ?")
                data_str = response.strip()
                
                # Parse response - handle both single and dual channel
                if response:
                    try:
                        if ',' in data_str:
                            channel1_value = float(data_str.split(',')[0])
                        else:
                            channel1_value = float(data_str)
                        
                        # Calculate timing
                        elapsed = time.time() - loop_start
                        self.elapsed_times.append(elapsed * 1000)  # Convert to ms
                        
                        # Store measurement
                        timestamp = time.time() - start_time
                        self.measurements.append((timestamp, channel1_value))
                        self.data_count += 1
                        
                        # Display like your histogram shows
                        avg_time = np.mean(self.elapsed_times[-10:]) if len(self.elapsed_times) > 0 else elapsed * 1000
                        rate = 1000.0 / avg_time if avg_time > 0 else 0
                        
                        print(f"{timestamp:8.3f} | {channel1_value:+15.6e} | {elapsed*1000:12.1f} | {rate:10.1f}")
                    except ValueError as e:
                        # Skip bad readings silently
                        pass
                
                # Sleep time from your code
                time.sleep(0.05)  # 50ms as in your original
                
            except ValueError as e:
                print(f"Parse error: {e}")
            except Exception as e:
                print(f"Error: {e}")
        
        self.running = False
        return self.measurements, self.elapsed_times
    
    def get_statistics(self):
        """Calculate statistics like your histogram"""
        if not self.elapsed_times:
            return None
        
        times_ms = self.elapsed_times
        
        return {
            'count': len(times_ms),
            'mean_ms': np.mean(times_ms),
            'min_ms': np.min(times_ms),
            'max_ms': np.max(times_ms),
            'std_ms': np.std(times_ms),
            'rate_hz': 1000.0 / np.mean(times_ms) if np.mean(times_ms) > 0 else 0
        }

def main():
    """Main function matching your original code's behavior"""
    
    print("=" * 60)
    print("Keithley 6482 Fast RS-232 Data Collection")
    print("Optimized for 57600 baud (6x faster than default)")
    print("=" * 60)
    
    print("\nBaud Rate Configuration Options:")
    print("1. Auto-detect and auto-upgrade to 57600")
    print("2. Manual setup (if you already set 57600 on Keithley)")
    
    choice = input("\nSelect option (1 or 2): ")
    
    # Create and connect
    keithley = Keithley6482Fast()
    
    if choice == '2':
        # Manual setup - assume Keithley is already at 57600
        print("\nManual Setup Instructions:")
        print("1. On Keithley: Press MENU")
        print("2. Navigate to COMMUNICATION → RS-232 → BAUD RATE")
        print("3. Set to 57600")
        print("4. Exit menu and save settings")
        input("\nPress Enter when Keithley is set to 57600 baud...")
        
        # Try to connect directly at 57600
        keithley.ser = serial.Serial(
            port='COM13',  # Use known port
            baudrate=57600,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.5,
            write_timeout=0.5
        )
        keithley.port = 'COM13'
        keithley.connected = True
        
        # Verify connection
        keithley.ser.write(b'*IDN?\r\n')
        time.sleep(0.2)
        response = keithley.ser.readline().decode('utf-8', errors='ignore').strip()
        
        if 'KEITHLEY' in response.upper():
            print(f"✓ Connected at 57600 baud!")
            print(f"Device: {response}")
        else:
            print("Failed to connect at 57600 baud")
            return
    else:
        # Auto-detect and upgrade
        if not keithley.auto_connect():
            print("Failed to connect. Exiting.")
            return
    
    # Optimize settings
    if not keithley.optimize_settings():
        print("Warning: Some optimizations failed")
    
    # Create data collector
    collector = DataCollector(keithley)
    
    # Collect data
    try:
        measurements, elapsed_times = collector.collect_data(duration=5.0)
        
        # Show statistics
        stats = collector.get_statistics()
        
        print("\n" + "=" * 60)
        print("Collection Statistics:")
        print("=" * 60)
        
        print(f"Total measurements: {stats['count']}")
        print(f"Average read time: {stats['mean_ms']:.1f} ms")
        print(f"Min read time: {stats['min_ms']:.1f} ms")
        print(f"Max read time: {stats['max_ms']:.1f} ms")
        print(f"Std deviation: {stats['std_ms']:.1f} ms")
        print(f"Effective rate: {stats['rate_hz']:.1f} Hz")
        
        # Compare to GPIB performance
        print("\nPerformance Comparison:")
        print(f"Your GPIB setup: 50-110 ms (9-20 Hz)")
        print(f"This RS-232 @ 57600: {stats['mean_ms']:.1f} ms ({stats['rate_hz']:.1f} Hz)")
        
        if stats['mean_ms'] < 100:
            print("✓ Achieved GPIB-like performance!")
        
        # Create histogram
        print("\nRead Time Distribution:")
        hist, bins = np.histogram(elapsed_times, bins=10)
        for i in range(len(hist)):
            bar_width = int(hist[i] * 40 / max(hist))
            print(f"{bins[i]:6.1f}-{bins[i+1]:6.1f} ms: {'█' * bar_width} ({hist[i]})")
        
        # Save option
        save = input("\nSave to CSV? (y/n): ")
        if save.lower() == 'y':
            filename = f"keithley_57600baud_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
            with open(filename, 'w') as f:
                f.write("Time_s,Current_A,ReadTime_ms\n")
                for i, (t, current) in enumerate(measurements):
                    read_time = elapsed_times[i] if i < len(elapsed_times) else 0
                    f.write(f"{t:.3f},{current:.15e},{read_time:.1f}\n")
            print(f"Saved to {filename}")
            
    except KeyboardInterrupt:
        print("\n\nStopped by user")
    finally:
        keithley.close()

if __name__ == "__main__":
    main()
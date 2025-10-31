#!/usr/bin/env python3
"""
Keithley 2400 SourceMeter Server - GUI Version with System Tray
Provides TCP server interface for remote control of Keithley 2400
"""

import socket
import threading
import json
import time
import pyvisa
import logging
from datetime import datetime
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import queue
import argparse
import sys

# Try to import pystray for system tray functionality
try:
    import pystray
    from pystray import MenuItem as item
    from PIL import Image, ImageDraw
    TRAY_AVAILABLE = True
except ImportError:
    TRAY_AVAILABLE = False
    print("Warning: pystray and/or PIL not available. System tray functionality disabled.")
    print("Install with: pip install pystray pillow")

# Setup logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

class Keithley2400Server:
    def __init__(self, host='localhost', port=8888, gpib_address='GPIB1::24::INSTR', fast_mode=True):
        self.host = host
        self.port = port
        self.gpib_address = gpib_address
        self.fast_mode = fast_mode
        self.instrument = None
        self.server_socket = None
        self.running = False
        self.clients = []
        
        # Performance tracking
        self.read_count = 0
        self.error_count = 0
        self.last_stats_time = time.time()
        
        # Enhanced polling optimization
        self.polling_optimized = fast_mode
        self.read_cache_timeout = 0.05  # 50ms cache for very fast polling
        self.cached_reading = None
        self.cache_timestamp = 0
        self.cache_hits = 0
        
        # GUI callback for logging
        self.log_callback = None
        
        # Connect to instrument
        self.connect_instrument()
        
    def set_log_callback(self, callback):
        """Set callback function for GUI logging"""
        self.log_callback = callback
        
    def log_message(self, message, level="INFO"):
        """Log message and send to GUI if callback is set"""
        if level == "INFO":
            logger.info(message)
        elif level == "ERROR":
            logger.error(message)
        elif level == "WARNING":
            logger.warning(message)
            
        if self.log_callback:
            self.log_callback(f"[{datetime.now().strftime('%H:%M:%S')}] {level}: {message}")
        
    def connect_instrument(self):
        """Connect to Keithley 2400"""
        try:
            rm = pyvisa.ResourceManager()
            self.instrument = rm.open_resource(self.gpib_address)
            
            # Optimize timeouts for fast polling
            if self.fast_mode:
                self.instrument.timeout = 1000
                self.instrument.write(':SYST:AZER OFF')
                self.instrument.write(':DISP:ENAB OFF')
            else:
                self.instrument.timeout = 5000
            
            # Test connection
            idn = self.instrument.query('*IDN?').strip()
            self.log_message(f"Connected to: {idn}")
            
            # Initialize instrument
            self.instrument.write('*RST')
            self.instrument.write('*CLS')
            
            if self.fast_mode:
                self.log_message("Fast mode enabled - optimized for high-rate polling")
                self.instrument.write(':SENS:FUNC:CONC ON')
                self.instrument.write(':FORM:ELEM VOLT,CURR')
            
        except Exception as e:
            self.log_message(f"Failed to connect to instrument: {e}", "ERROR")
            raise
            
    def start_server(self):
        """Start the TCP server"""
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(5)
            
            self.running = True
            self.log_message(f"Keithley 2400 Server started on {self.host}:{self.port}")
            if self.fast_mode:
                self.log_message("Server optimized for high-frequency polling (50ms-5s rates)")
            
            # Start statistics reporting thread
            stats_thread = threading.Thread(target=self.stats_reporter, daemon=True)
            stats_thread.start()
            
            # Start server thread
            server_thread = threading.Thread(target=self.server_loop, daemon=True)
            server_thread.start()
            
        except Exception as e:
            self.log_message(f"Server error: {e}", "ERROR")
            raise
    
    def server_loop(self):
        """Main server loop"""
        while self.running:
            try:
                self.server_socket.settimeout(1.0)
                client_socket, address = self.server_socket.accept()
                self.log_message(f"Client connected from {address}")
                
                client_thread = threading.Thread(
                    target=self.handle_client,
                    args=(client_socket, address),
                    daemon=True
                )
                client_thread.start()
                
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    self.log_message(f"Error accepting connection: {e}", "ERROR")
                break
    
    def stats_reporter(self):
        """Enhanced stats reporting for polling monitoring"""
        while self.running:
            time.sleep(30)
            if self.read_count > 0:
                elapsed = time.time() - self.last_stats_time
                rate = self.read_count / elapsed
                error_rate = (self.error_count / self.read_count) * 100 if self.read_count > 0 else 0
                cache_rate = (self.cache_hits / self.read_count) * 100 if self.read_count > 0 else 0
                
                self.log_message(f"Performance: {rate:.1f} reads/sec, {error_rate:.1f}% errors, {cache_rate:.1f}% cache hits")
                
                # Detailed stats for fast polling
                if rate > 5.0:  # High frequency polling
                    self.log_message(f"High-frequency polling active - Rate optimization enabled")
                
                # Reset counters
                self.read_count = 0
                self.error_count = 0
                self.cache_hits = 0
                self.last_stats_time = time.time()
            
    def handle_client(self, client_socket, address):
        """Enhanced client handler with polling detection"""
        self.clients.append(client_socket)
        
        # Track polling statistics per client
        client_read_count = 0
        last_command_time = time.time()
        
        try:
            while self.running:
                data = client_socket.recv(1024).decode('utf-8')
                if not data:
                    break
                    
                try:
                    command = json.loads(data)
                    current_time = time.time()
                    
                    # Detect high-frequency polling
                    if command.get('type') == 'read':
                        client_read_count += 1
                        time_since_last = current_time - last_command_time
                        
                        # Log high-frequency polling detection
                        if time_since_last < 0.2 and client_read_count > 5:  # Less than 200ms between reads
                            if client_read_count == 6:  # Log once when detected
                                self.log_message(f"High-frequency polling detected from {address} (interval: {time_since_last*1000:.0f}ms)")
                    else:
                        # Log non-read commands
                        self.log_message(f"Command from {address}: {command}")
                    
                    last_command_time = current_time
                    
                    response = self.process_command(command)
                    response_json = json.dumps(response)
                    client_socket.send(response_json.encode('utf-8'))
                    
                except json.JSONDecodeError:
                    error_response = {"status": "error", "message": "Invalid JSON format"}
                    client_socket.send(json.dumps(error_response).encode('utf-8'))
                    
                except Exception as e:
                    error_response = {"status": "error", "message": str(e)}
                    client_socket.send(json.dumps(error_response).encode('utf-8'))
                    self.log_message(f"Error processing command: {e}", "ERROR")
                    
        except Exception as e:
            self.log_message(f"Client {address} error: {e}", "ERROR")
        finally:
            client_socket.close()
            if client_socket in self.clients:
                self.clients.remove(client_socket)
            self.log_message(f"Client {address} disconnected (processed {client_read_count} reads)")
            
    def handle_optimized_read(self):
        """Optimized read handler for high-frequency polling"""
        current_time = time.time()
        self.read_count += 1
        
        # Check if we can use cached reading for very fast polling
        if (self.polling_optimized and 
            self.cached_reading is not None and 
            (current_time - self.cache_timestamp) < self.read_cache_timeout):
            
            self.cache_hits += 1
            # Return cached reading with updated timestamp
            cached_response = self.cached_reading.copy()
            cached_response["data"]["timestamp"] = datetime.now().isoformat()
            cached_response["data"]["cached"] = True
            return cached_response
        
        # Perform actual instrument read
        start_time = current_time
        
        try:
            if self.fast_mode:
                # Optimized fast read
                result = self.instrument.query(':READ?').strip()
                values = result.split(',')
                
                if len(values) >= 2:
                    voltage = float(values[0])
                    current = float(values[1])
                    
                    # Calculate derived values
                    if abs(current) > 1e-12:
                        resistance = voltage / current
                    else:
                        resistance = 1e9  # Very high resistance for open circuit
                        
                    power = voltage * current
                    
                    measurement = {
                        "voltage": voltage,
                        "current": current,
                        "resistance": resistance,
                        "power": power,
                        "timestamp": datetime.now().isoformat(),
                        "read_time_ms": (time.time() - start_time) * 1000,
                        "cached": False
                    }
                else:
                    raise ValueError(f"Invalid measurement format: {result}")
            else:
                # Standard read mode
                result = self.instrument.query(':READ?').strip()
                values = result.split(',')
                measurement = {
                    "voltage": float(values[0]),
                    "current": float(values[1]),
                    "resistance": float(values[2]) if len(values) > 2 else None,
                    "power": float(values[3]) if len(values) > 3 else None,
                    "timestamp": datetime.now().isoformat(),
                    "read_time_ms": (time.time() - start_time) * 1000,
                    "cached": False
                }
            
            response = {"status": "success", "data": measurement}
            
            # Cache the successful reading
            if self.polling_optimized:
                self.cached_reading = response
                self.cache_timestamp = current_time
            
            return response
            
        except Exception as e:
            self.error_count += 1
            error_msg = str(e)
            
            # Enhanced error handling for polling
            if "timeout" in error_msg.lower() or "VI_ERROR_TMO" in error_msg:
                # Don't spam logs for timeout errors during fast polling
                if self.error_count % 50 == 1:
                    self.log_message(f"Read timeout (x{self.error_count}): {error_msg}", "WARNING")
                    
                # Return a safe "no reading" response instead of error for timeouts
                safe_response = {
                    "status": "success", 
                    "data": {
                        "voltage": 0.0,
                        "current": 0.0, 
                        "resistance": 1e9,
                        "power": 0.0,
                        "timestamp": datetime.now().isoformat(),
                        "read_time_ms": (time.time() - start_time) * 1000,
                        "timeout": True,
                        "cached": False
                    }
                }
                return safe_response
            else:
                self.log_message(f"Read measurement failed: {error_msg}", "ERROR")
                return {"status": "error", "message": error_msg}
            
    def process_command(self, command):
        """Process instrument command with optimized read handling"""
        cmd_type = command.get('type', '')
        cmd_data = command.get('data', {})
        
        try:
            if cmd_type == 'write':
                scpi_cmd = cmd_data.get('command', '')
                self.instrument.write(scpi_cmd)
                return {"status": "success", "message": f"Command '{scpi_cmd}' executed"}
                
            elif cmd_type == 'query':
                scpi_cmd = cmd_data.get('command', '')
                result = self.instrument.query(scpi_cmd).strip()
                return {"status": "success", "data": result}
                
            elif cmd_type == 'read':
                return self.handle_optimized_read()
                
            elif cmd_type == 'get_status':
                idn = self.instrument.query('*IDN?').strip()
                output_state = self.instrument.query(':OUTP?').strip()
                source_func = self.instrument.query(':SOUR:FUNC?').strip()
                
                status = {
                    "instrument": idn,
                    "output": "ON" if output_state == "1" else "OFF",
                    "source_function": source_func,
                    "timestamp": datetime.now().isoformat(),
                    "fast_mode": self.fast_mode,
                    "read_count": self.read_count,
                    "error_count": self.error_count,
                    "cache_hits": self.cache_hits,
                    "client_count": len(self.clients)
                }
                return {"status": "success", "data": status}

            elif cmd_type == 'output':
                state = cmd_data.get('state', 'OFF').upper()
                if state in ['ON', 'OFF']:
                    if state == 'ON':
                        # Enhanced output ON with safety checks
                        self.log_message("Output ON requested - checking instrument configuration")
                        
                        # Check if source is configured
                        try:
                            source_func = self.instrument.query(':SOUR:FUNC?').strip()
                            if source_func in ['VOLT', 'CURR']:
                                self.instrument.write(f':OUTP {state}')
                                self.log_message(f"Output set to {state} ({source_func} mode)")
                            else:
                                self.log_message("Warning: Source function not properly configured", "WARNING")
                                self.instrument.write(f':OUTP {state}')
                        except:
                            # Fallback - just set output
                            self.instrument.write(f':OUTP {state}')
                            self.log_message(f"Output set to {state} (no verification)")
                    else:
                        self.instrument.write(f':OUTP {state}')
                        self.log_message(f"Output set to {state}")
                    
                    return {"status": "success", "message": f"Output set to {state}"}
                else:
                    return {"status": "error", "message": f"Invalid output state: {state}. Use 'ON' or 'OFF'"}

            elif cmd_type == 'reset':
                # Enhanced reset with fast mode restoration
                self.log_message("Resetting instrument...")
                self.instrument.write('*RST')
                self.instrument.write('*CLS')
                
                # Restore fast mode settings if enabled
                if self.fast_mode:
                    time.sleep(0.5)  # Wait for reset to complete
                    self.instrument.write(':SYST:AZER OFF')
                    self.instrument.write(':DISP:ENAB OFF')
                    self.instrument.write(':SENS:FUNC:CONC ON')
                    self.instrument.write(':FORM:ELEM VOLT,CURR')
                    self.log_message("Fast mode settings restored after reset")
                
                # Clear cache after reset
                self.cached_reading = None
                self.cache_timestamp = 0
                
                self.log_message("Instrument reset completed")
                return {"status": "success", "message": "Instrument reset completed"}

            elif cmd_type == 'setup_voltage_source':
                voltage = cmd_data.get('voltage', 0)
                compliance = cmd_data.get('compliance', 0.1)
                range_val = cmd_data.get('range', 'AUTO')
                
                try:
                    self.log_message(f"Configuring voltage source: {voltage}V, compliance {compliance}A")
                    
                    # Follow the exact sequence from working Python code
                    self.instrument.write(':SOUR:FUNC VOLT')        # Source voltage
                    self.instrument.write(':SOUR:VOLT:MODE FIXED')  # Fixed voltage mode
                    self.instrument.write(f':SOUR:VOLT {voltage}')  # Set voltage
                    
                    if range_val != 'AUTO':
                        self.instrument.write(f':SOUR:VOLT:RANG {range_val}')
                    else:
                        self.instrument.write(':SOUR:VOLT:RANG 20')  # 20V range like working code
                    
                    # Measurement setup (critical!)
                    self.instrument.write(':SENS:FUNC "CURR"')       # Measure current
                    self.instrument.write(':SENS:CURR:RANG:AUTO ON') # Auto-range current
                    self.instrument.write(f':SENS:CURR:PROT {compliance}')  # Compliance
                    
                    # Restore fast mode settings if needed
                    if self.fast_mode:
                        self.instrument.write(':SENS:FUNC:CONC ON')
                        self.instrument.write(':FORM:ELEM VOLT,CURR')
                    
                    self.log_message(f"Voltage source configured: {voltage}V, compliance {compliance}A")
                    return {"status": "success", "message": f"Voltage source configured: {voltage}V, compliance {compliance}A"}
                    
                except Exception as e:
                    error_msg = f"Failed to setup voltage source: {str(e)}"
                    self.log_message(error_msg, "ERROR")
                    return {"status": "error", "message": error_msg}
                
            elif cmd_type == 'setup_current_source':
                current = cmd_data.get('current', 0)
                compliance = cmd_data.get('compliance', 10.0)
                range_val = cmd_data.get('range', 'AUTO')
                
                try:
                    self.log_message(f"Configuring current source: {current}A, compliance {compliance}V")
                    
                    # Configure current source similar to working voltage source code
                    self.instrument.write(':SOUR:FUNC CURR')         # Source current
                    self.instrument.write(':SOUR:CURR:MODE FIXED')   # Fixed current mode
                    self.instrument.write(f':SOUR:CURR {current}')   # Set current
                    
                    if range_val != 'AUTO':
                        self.instrument.write(f':SOUR:CURR:RANG {range_val}')
                    else:
                        self.instrument.write(':SOUR:CURR:RANG:AUTO ON')
                    
                    # Measurement setup for voltage measurement
                    self.instrument.write(':SENS:FUNC "VOLT"')       # Measure voltage
                    self.instrument.write(':SENS:VOLT:RANG:AUTO ON') # Auto-range voltage
                    self.instrument.write(f':SENS:VOLT:PROT {compliance}')  # Voltage compliance
                    
                    # Restore fast mode settings if needed
                    if self.fast_mode:
                        self.instrument.write(':SENS:FUNC:CONC ON')
                        self.instrument.write(':FORM:ELEM VOLT,CURR')
                    
                    self.log_message(f"Current source configured: {current}A, compliance {compliance}V")
                    return {"status": "success", "message": f"Current source configured: {current}A, compliance {compliance}V"}
                    
                except Exception as e:
                    error_msg = f"Failed to setup current source: {str(e)}"
                    self.log_message(error_msg, "ERROR")
                    return {"status": "error", "message": error_msg}

            elif cmd_type == 'voltage_sweep':
                start = cmd_data.get('start', 0)
                stop = cmd_data.get('stop', 5)
                steps = cmd_data.get('steps', 11)
                compliance = cmd_data.get('compliance', 0.1)
                delay = cmd_data.get('delay', 0.1)
                
                self.log_message(f"Starting voltage sweep: {start}V to {stop}V, {steps} steps, {compliance}A compliance")
                
                try:
                    # Perform voltage sweep - based on working Python code
                    results = []
                    
                    # Calculate voltage points
                    if steps <= 1:
                        voltages = [start]
                    else:
                        voltages = [start + (stop - start) * i / (steps - 1) for i in range(steps)]
                    
                    # Setup instrument for sweep (like working Python code)
                    self.instrument.write('*RST')                    # Reset to defaults
                    self.instrument.write('*CLS')                    # Clear status
                    self.instrument.write(':SOUR:FUNC VOLT')        # Source voltage
                    self.instrument.write(':SOUR:VOLT:MODE FIXED')  # Fixed voltage mode
                    self.instrument.write(':SOUR:VOLT:RANG 20')     # 20V range
                    self.instrument.write(':SENS:FUNC "CURR"')      # Measure current
                    self.instrument.write(':SENS:CURR:RANG:AUTO ON') # Auto-range current
                    self.instrument.write(f':SENS:CURR:PROT {compliance}')  # Compliance
                    
                    # Restore fast mode for sweep if enabled
                    if self.fast_mode:
                        self.instrument.write(':SENS:FUNC:CONC ON')
                        self.instrument.write(':FORM:ELEM VOLT,CURR')
                    
                    # Turn output ON
                    self.instrument.write(':OUTP ON')
                    
                    try:
                        for i, voltage in enumerate(voltages):
                            # Set voltage
                            self.instrument.write(f':SOUR:VOLT {voltage}')
                            
                            # Wait for settling
                            time.sleep(delay)
                            
                            # Take measurement
                            measurement = self.instrument.query(':READ?').strip()
                            values = measurement.split(',')
                            
                            if len(values) >= 2:
                                measured_voltage = float(values[0])
                                measured_current = float(values[1])
                                
                                result = {
                                    "set_voltage": voltage,
                                    "measured_voltage": measured_voltage,
                                    "measured_current": measured_current,
                                    "timestamp": datetime.now().isoformat(),
                                    "step": i + 1
                                }
                                results.append(result)
                                
                                # Log progress every few steps
                                if (i + 1) % 5 == 0 or i == 0 or i == len(voltages) - 1:
                                    self.log_message(f"Step {i+1}: {voltage}V -> {measured_voltage:.6f}V, {measured_current:.9f}A")
                            else:
                                self.log_message(f"Invalid measurement format at step {i+1}: {measurement}", "WARNING")
                                
                    finally:
                        # Always turn output OFF after sweep
                        self.instrument.write(':OUTP OFF')
                        self.log_message("Voltage sweep completed - output OFF")
                        
                        # Restore fast mode settings after sweep
                        if self.fast_mode:
                            self.instrument.write(':SYST:AZER OFF')
                            self.instrument.write(':DISP:ENAB OFF')
                        
                    self.log_message(f"Voltage sweep completed successfully with {len(results)} points")
                    return {"status": "success", "data": results, "message": f"Sweep completed with {len(results)} points"}
                    
                except Exception as e:
                    # Ensure output is OFF on error
                    try:
                        self.instrument.write(':OUTP OFF')
                    except:
                        pass
                    
                    error_msg = f"Voltage sweep failed: {str(e)}"
                    self.log_message(error_msg, "ERROR")
                    return {"status": "error", "message": error_msg}

            elif cmd_type == 'current_sweep':
                start = cmd_data.get('start', 0)
                stop = cmd_data.get('stop', 0.001)
                steps = cmd_data.get('steps', 11)
                compliance = cmd_data.get('compliance', 10.0)
                delay = cmd_data.get('delay', 0.1)
                
                self.log_message(f"Starting current sweep: {start}A to {stop}A, {steps} steps, {compliance}V compliance")
                
                try:
                    # Perform current sweep - based on working voltage sweep code
                    results = []
                    
                    # Calculate current points
                    if steps <= 1:
                        currents = [start]
                    else:
                        currents = [start + (stop - start) * i / (steps - 1) for i in range(steps)]
                    
                    # Setup instrument for current sweep
                    self.instrument.write('*RST')                    # Reset to defaults
                    self.instrument.write('*CLS')                    # Clear status
                    self.instrument.write(':SOUR:FUNC CURR')        # Source current
                    self.instrument.write(':SOUR:CURR:MODE FIXED')  # Fixed current mode
                    self.instrument.write(':SOUR:CURR:RANG:AUTO ON') # Auto-range current
                    self.instrument.write(':SENS:FUNC "VOLT"')      # Measure voltage
                    self.instrument.write(':SENS:VOLT:RANG:AUTO ON') # Auto-range voltage
                    self.instrument.write(f':SENS:VOLT:PROT {compliance}')  # Voltage compliance
                    
                    # Restore fast mode for sweep if enabled
                    if self.fast_mode:
                        self.instrument.write(':SENS:FUNC:CONC ON')
                        self.instrument.write(':FORM:ELEM VOLT,CURR')
                    
                    # Turn output ON
                    self.instrument.write(':OUTP ON')
                    
                    try:
                        for i, current in enumerate(currents):
                            # Set current
                            self.instrument.write(f':SOUR:CURR {current}')
                            
                            # Wait for settling
                            time.sleep(delay)
                            
                            # Take measurement
                            measurement = self.instrument.query(':READ?').strip()
                            values = measurement.split(',')
                            
                            if len(values) >= 2:
                                measured_voltage = float(values[0])
                                measured_current = float(values[1])
                                
                                result = {
                                    "set_current": current,
                                    "measured_voltage": measured_voltage,
                                    "measured_current": measured_current,
                                    "resistance": measured_voltage / measured_current if abs(measured_current) > 1e-12 else 1e9,
                                    "power": measured_voltage * measured_current,
                                    "timestamp": datetime.now().isoformat(),
                                    "step": i + 1
                                }
                                results.append(result)
                                
                                # Log progress every few steps
                                if (i + 1) % 5 == 0 or i == 0 or i == len(currents) - 1:
                                    self.log_message(f"Step {i+1}: {current}A -> {measured_voltage:.6f}V, {measured_current:.9f}A")
                            else:
                                self.log_message(f"Invalid measurement format at step {i+1}: {measurement}", "WARNING")
                                
                    finally:
                        # Always turn output OFF after sweep
                        self.instrument.write(':OUTP OFF')
                        self.log_message("Current sweep completed - output OFF")
                        
                        # Restore fast mode settings after sweep
                        if self.fast_mode:
                            self.instrument.write(':SYST:AZER OFF')
                            self.instrument.write(':DISP:ENAB OFF')
                    
                    self.log_message(f"Current sweep completed successfully with {len(results)} points")
                    return {"status": "success", "data": results, "message": f"Current sweep completed with {len(results)} points"}
                    
                except Exception as e:
                    # Ensure output is OFF on error
                    try:
                        self.instrument.write(':OUTP OFF')
                    except:
                        pass
                    
                    error_msg = f"Current sweep failed: {str(e)}"
                    self.log_message(error_msg, "ERROR")
                    return {"status": "error", "message": error_msg}





            else:
                return {"status": "error", "message": f"Unknown command type: {cmd_type}"}
                
        except Exception as e:
            self.log_message(f"Command processing error: {str(e)}", "ERROR")
            return {"status": "error", "message": str(e)}
            
    def stop_server(self):
        """Stop the server"""
        self.running = False
        if self.server_socket:
            try:
                self.server_socket.close()
            except:
                pass
        self.log_message("Server stopped")
        
    def cleanup(self):
        """Cleanup resources"""
        self.running = False
        
        for client in self.clients:
            try:
                client.close()
            except:
                pass
                
        if self.server_socket:
            try:
                self.server_socket.close()
            except:
                pass
                
        if self.instrument:
            try:
                self.instrument.write(':OUTP OFF')
                if self.fast_mode:
                    self.instrument.write(':SYST:AZER ON')
                    self.instrument.write(':DISP:ENAB ON')
                self.instrument.close()
            except:
                pass
                
        self.log_message("Server cleanup completed")




class Keithley2400GUI:
    def __init__(self, auto_start=False, host='127.0.0.101', port=8888, gpib='GPIB1::24::INSTR', fast_mode=True):
        self.root = tk.Tk()
        self.root.title("Keithley 2400 Server Control Panel : Update 2025.08.21")
        self.root.geometry("800x500")
        
        # Server instance
        self.server = None
        self.auto_start = auto_start
        
        # Server settings
        self.host = tk.StringVar(value=host)
        self.port = tk.IntVar(value=port)
        self.gpib_address = tk.StringVar(value=gpib)
        self.fast_mode = tk.BooleanVar(value=fast_mode)
        
        # Status variables
        self.server_status = tk.StringVar(value="Stopped")
        self.client_count = tk.StringVar(value="0")
        self.read_count = tk.StringVar(value="0")
        
        # Communication queue for thread-safe GUI updates
        self.log_queue = queue.Queue()
        
        # System tray
        self.tray_icon = None
        self.is_minimized_to_tray = False
        
        self.setup_ui()
        self.setup_system_tray()
        self.start_log_updater()
        
        # Configure window close behavior
        self.root.protocol("WM_DELETE_WINDOW", self.on_window_close)
        
        # Auto-start functionality
        if self.auto_start:
            self.root.after(1000, self.auto_start_sequence)
    
    def create_tray_icon(self):
        """Create a simple icon for the system tray"""
        width = 64
        height = 64
        
        image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        
        if self.server and self.server.running:
            draw.ellipse([8, 8, 56, 56], fill=(0, 255, 0, 255), outline=(0, 200, 0, 255), width=2)
            draw.text((22, 20), "K", fill=(255, 255, 255, 255))
        else:
            draw.ellipse([8, 8, 56, 56], fill=(255, 0, 0, 255), outline=(200, 0, 0, 255), width=2)
            draw.text((22, 20), "K", fill=(255, 255, 255, 255))
            
        return image
    
    def setup_system_tray(self):
        """Setup system tray icon and menu"""
        if not TRAY_AVAILABLE:
            return
            
        try:
            menu = pystray.Menu(
                item('Show Window', self.show_window),
                item('Hide Window', self.hide_window),
                pystray.Menu.SEPARATOR,
                item('Server Status', self.show_server_status),
                item('Start Server', self.start_server_from_tray, enabled=lambda item: not (self.server and self.server.running)),
                item('Stop Server', self.stop_server_from_tray, enabled=lambda item: self.server and self.server.running),
                pystray.Menu.SEPARATOR,
                item('Exit', self.quit_application)
            )
            
            icon_image = self.create_tray_icon()
            self.tray_icon = pystray.Icon("Keithley2400 Server", icon_image, menu=menu)
            
        except Exception as e:
            print(f"Failed to setup system tray: {e}")
            self.tray_icon = None
    
    def start_tray_icon(self):
        """Start the system tray icon in a separate thread"""
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                tray_thread = threading.Thread(target=self.tray_icon.run, daemon=True)
                tray_thread.start()
            except Exception as e:
                print(f"Failed to start tray icon: {e}")
    
    def update_tray_icon(self):
        """Update the tray icon to reflect server status"""
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                new_icon = self.create_tray_icon()
                self.tray_icon.icon = new_icon
                
                status = "Running" if self.server and self.server.running else "Stopped"
                self.tray_icon.title = f"Keithley2400 Server - {status}"
            except Exception as e:
                print(f"Failed to update tray icon: {e}")
    
    def show_window(self, icon=None, item=None):
        """Show the main window"""
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()
        self.is_minimized_to_tray = False
    
    def hide_window(self, icon=None, item=None):
        """Hide the main window to system tray"""
        if TRAY_AVAILABLE and self.tray_icon:
            self.root.withdraw()
            self.is_minimized_to_tray = True
            
            if not hasattr(self, '_first_minimize_done'):
                self._first_minimize_done = True
                self.show_tray_notification("Keithley2400 Server minimized to system tray")
        else:
            messagebox.showwarning("System Tray Not Available", 
                                 "System tray functionality is not available.\n"
                                 "Install pystray and pillow: pip install pystray pillow")
    
    def show_tray_notification(self, message):
        """Show a system tray notification"""
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                self.tray_icon.notify(message, "Keithley2400 Server")
            except Exception as e:
                print(f"Failed to show notification: {e}")
    
    def show_server_status(self, icon=None, item=None):
        """Show server status in tray notification"""
        status_msg = f"Server: {self.server_status.get()}\n"
        status_msg += f"Clients: {self.client_count.get()}\n"
        status_msg += f"Reads: {self.read_count.get()}"
        
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                self.tray_icon.notify(status_msg, "Keithley2400 Server Status")
            except Exception as e:
                print(f"Failed to show status notification: {e}")
    
    def start_server_from_tray(self, icon=None, item=None):
        """Start server from tray menu"""
        self.start_server()
        self.show_tray_notification("Server started")
    
    def stop_server_from_tray(self, icon=None, item=None):
        """Stop server from tray menu"""
        self.stop_server()
        self.show_tray_notification("Server stopped")
    
    def quit_application(self, icon=None, item=None):
        """Quit the entire application"""
        self.is_minimized_to_tray = False
        self.on_closing()
    
    def on_window_close(self):
        """Handle window close button - minimize to tray instead of closing"""
        if TRAY_AVAILABLE and self.tray_icon:
            self.hide_window()
        else:
            result = messagebox.askyesnocancel(
                "Close Application",
                "System tray is not available.\n\n"
                "Yes: Exit application\n"
                "No: Minimize to taskbar\n"
                "Cancel: Keep window open"
            )
            
            if result is True:
                self.on_closing()
            elif result is False:
                self.root.iconify()
    
    def auto_start_sequence(self):
        """Automatically start the server after GUI initialization"""
        self.log_message("Auto-start sequence initiated...")
        self.start_server()
    
    def setup_ui(self):
        # System tray info
        if TRAY_AVAILABLE:
            tray_frame = ttk.Frame(self.root)
            tray_frame.pack(fill='x', padx=5, pady=5)
            ttk.Label(tray_frame, text="💾 System Tray: Available - Close window to minimize to tray", 
                     foreground="blue", font=('Arial', 9)).pack()
        else:
            tray_frame = ttk.Frame(self.root)
            tray_frame.pack(fill='x', padx=5, pady=5)
            ttk.Label(tray_frame, text="❌ System Tray: Not Available (pip install pystray pillow)", 
                     foreground="red", font=('Arial', 9)).pack()
        
        # Auto-start indicator
        if self.auto_start:
            auto_frame = ttk.Frame(self.root)
            auto_frame.pack(fill='x', padx=5, pady=5)
            ttk.Label(auto_frame, text="⚡ AUTO-START MODE ENABLED", 
                     foreground="green", font=('Arial', 10, 'bold')).pack()
        
        # Server Settings
        settings_frame = ttk.LabelFrame(self.root, text="Server Settings")
        settings_frame.pack(fill='x', padx=5, pady=5)
        
        ttk.Label(settings_frame, text="Host:").grid(row=0, column=0, sticky='w', padx=5, pady=2)
        ttk.Entry(settings_frame, textvariable=self.host, width=15).grid(row=0, column=1, padx=5, pady=2)
        
        ttk.Label(settings_frame, text="Port:").grid(row=0, column=2, sticky='w', padx=5, pady=2)
        ttk.Entry(settings_frame, textvariable=self.port, width=8).grid(row=0, column=3, padx=5, pady=2)
        
        ttk.Label(settings_frame, text="GPIB Address:").grid(row=1, column=0, sticky='w', padx=5, pady=2)
        ttk.Entry(settings_frame, textvariable=self.gpib_address, width=25).grid(row=1, column=1, columnspan=2, sticky='ew', padx=5, pady=2)
        
        ttk.Checkbutton(settings_frame, text="Fast Mode", variable=self.fast_mode).grid(row=1, column=3, padx=5, pady=2)
        
        # Server Control
        control_frame = ttk.LabelFrame(self.root, text="Server Control")
        control_frame.pack(fill='x', padx=5, pady=5)
        
        self.start_btn = ttk.Button(control_frame, text="Start Server", command=self.start_server)
        self.start_btn.pack(side='left', padx=5, pady=5)
        
        self.stop_btn = ttk.Button(control_frame, text="Stop Server", command=self.stop_server, state='disabled')
        self.stop_btn.pack(side='left', padx=5, pady=5)
        
        # Tray control buttons
        if TRAY_AVAILABLE:
            ttk.Button(control_frame, text="Hide to Tray", command=self.hide_window).pack(side='right', padx=5, pady=5)
        
        # Server Status
        status_frame = ttk.LabelFrame(self.root, text="Server Status")
        status_frame.pack(fill='x', padx=5, pady=5)
        
        ttk.Label(status_frame, text="Status:").grid(row=0, column=0, sticky='w', padx=5, pady=2)
        ttk.Label(status_frame, textvariable=self.server_status, font=('Arial', 10, 'bold')).grid(row=0, column=1, sticky='w', padx=5, pady=2)
        
        ttk.Label(status_frame, text="Clients:").grid(row=0, column=2, sticky='w', padx=5, pady=2)
        ttk.Label(status_frame, textvariable=self.client_count).grid(row=0, column=3, sticky='w', padx=5, pady=2)
        
        ttk.Label(status_frame, text="Reads:").grid(row=0, column=4, sticky='w', padx=5, pady=2)
        ttk.Label(status_frame, textvariable=self.read_count).grid(row=0, column=5, sticky='w', padx=5, pady=2)
        
        # Activity Logs
        log_frame = ttk.LabelFrame(self.root, text="Activity Logs")
        log_frame.pack(fill='both', expand=True, padx=5, pady=5)
        
        self.log_text = scrolledtext.ScrolledText(log_frame, height=15, width=80)
        self.log_text.pack(fill='both', expand=True, padx=5, pady=5)
        
        log_control_frame = ttk.Frame(log_frame)
        log_control_frame.pack(fill='x', padx=5, pady=5)
        
        ttk.Button(log_control_frame, text="Clear Logs", command=self.clear_logs).pack(side='left', padx=5)
        
        self.auto_scroll = tk.BooleanVar(value=True)
        ttk.Checkbutton(log_control_frame, text="Auto-scroll", variable=self.auto_scroll).pack(side='left', padx=5)
    
    def log_message(self, message):
        """Add message to log queue"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        log_entry = f"[{timestamp}] {message}\n"
        self.log_queue.put(log_entry)
    
    def start_log_updater(self):
        """Update logs from queue in main thread"""
        try:
            while True:
                log_entry = self.log_queue.get_nowait()
                self.log_text.insert(tk.END, log_entry)
                if self.auto_scroll.get():
                    self.log_text.see(tk.END)
        except queue.Empty:
            pass
        
        # Update status displays
        if self.server:
            self.client_count.set(str(len(self.server.clients)))
            self.read_count.set(str(self.server.read_count))
        
        self.root.after(100, self.start_log_updater)
    
    def start_server(self):
        """Start the Keithley server"""
        if self.server and self.server.running:
            return
        
        try:
            self.server = Keithley2400Server(
                host=self.host.get(),
                port=self.port.get(),
                gpib_address=self.gpib_address.get(),
                fast_mode=self.fast_mode.get()
            )
            
            # Set log callback
            self.server.set_log_callback(self.log_message)
            
            self.server.start_server()
            
            self.start_btn.config(state='disabled')
            self.stop_btn.config(state='normal')
            self.server_status.set("Running")
            
            # Update tray icon
            self.update_tray_icon()
            
        except Exception as e:
            self.log_message(f"Failed to start server: {e}")
            messagebox.showerror("Server Error", f"Failed to start server:\n{e}")
    
    def stop_server(self):
        """Stop the Keithley server"""
        if self.server:
            self.server.stop_server()
            self.server.cleanup()
            self.server = None
        
        self.start_btn.config(state='normal')
        self.stop_btn.config(state='disabled')
        self.server_status.set("Stopped")
        self.client_count.set("0")
        
        # Update tray icon
        self.update_tray_icon()
        
        self.log_message("Server stopped")
    
    def clear_logs(self):
        """Clear log display"""
        self.log_text.delete(1.0, tk.END)
    
    def run(self):
        """Start the GUI application"""
        self.start_tray_icon()
        self.root.mainloop()
    
    def on_closing(self):
        """Clean shutdown"""
        if self.server:
            self.server.cleanup()
        
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                self.tray_icon.stop()
            except:
                pass
        
        self.root.destroy()

def main():
    parser = argparse.ArgumentParser(description='Keithley 2400 Server GUI - Optimized for High-Rate Polling')
    parser.add_argument('--host', default='127.0.0.101', help='Server host')
    parser.add_argument('--port', type=int, default=8888, help='Server port')
    parser.add_argument('--gpib', default='GPIB1::24::INSTR', help='GPIB address')
    parser.add_argument('--fast', action='store_true', default=True, help='Enable fast mode (default: True)')
    parser.add_argument('--no-fast', action='store_true', help='Disable fast mode')
    parser.add_argument('--auto-start', '-a', action='store_true', help='Automatically start server')
    parser.add_argument('--headless', action='store_true', help='Run without GUI (original mode)')
    
    args = parser.parse_args()
    
    if args.headless:
        # Run original headless version
        fast_mode = args.fast and not args.no_fast
        server = Keithley2400Server(args.host, args.port, args.gpib, fast_mode)
        
        try:
            server.start_server()
        except KeyboardInterrupt:
            logger.info("Server interrupted by user")
        finally:
            server.cleanup()
    else:
        # Run GUI version
        fast_mode = args.fast and not args.no_fast
        
        app = Keithley2400GUI(
            auto_start=args.auto_start,
            host=args.host,
            port=args.port,
            gpib=args.gpib,
            fast_mode=fast_mode
        )
        
        app.run()

if __name__ == "__main__":
    main()
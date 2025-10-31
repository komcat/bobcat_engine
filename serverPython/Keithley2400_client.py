#!/usr/bin/env python3
"""
Keithley 2400 Client
Test client for communicating with Keithley 2400 Server
"""

import socket
import json
import time
import matplotlib.pyplot as plt
from datetime import datetime

class Keithley2400Client:
    def __init__(self, host='localhost', port=8888):
        self.host = host
        self.port = port
        self.socket = None
        
    def connect(self):
        """Connect to server"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            print(f"Connected to Keithley server at {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            return False
            
    def disconnect(self):
        """Disconnect from server"""
        if self.socket:
            self.socket.close()
            print("Disconnected from server")
            
    def send_command(self, command):
        """Send command to server and get response"""
        try:
            # Send command
            command_json = json.dumps(command)
            self.socket.send(command_json.encode('utf-8'))
            
            # Receive response
            response_data = self.socket.recv(4096).decode('utf-8')
            response = json.loads(response_data)
            
            return response
            
        except Exception as e:
            return {"status": "error", "message": f"Communication error: {e}"}
            
    def write_command(self, scpi_command):
        """Send SCPI write command"""
        command = {
            "type": "write",
            "data": {"command": scpi_command}
        }
        return self.send_command(command)
        
    def query_command(self, scpi_command):
        """Send SCPI query command"""
        command = {
            "type": "query",
            "data": {"command": scpi_command}
        }
        return self.send_command(command)
        
    def read_measurement(self):
        """Take a measurement"""
        command = {"type": "read"}
        return self.send_command(command)
        
    def setup_voltage_source(self, voltage=0, compliance=0.1, range_val="AUTO"):
        """Setup as voltage source"""
        command = {
            "type": "setup_voltage_source",
            "data": {
                "voltage": voltage,
                "compliance": compliance,
                "range": range_val
            }
        }
        return self.send_command(command)
        
    def setup_current_source(self, current=0, compliance=10, range_val="AUTO"):
        """Setup as current source"""
        command = {
            "type": "setup_current_source",
            "data": {
                "current": current,
                "compliance": compliance,
                "range": range_val
            }
        }
        return self.send_command(command)
        
    def set_output(self, state="OFF"):
        """Control output on/off"""
        command = {
            "type": "output",
            "data": {"state": state}
        }
        return self.send_command(command)
        
    def voltage_sweep(self, start=0, stop=5, steps=11, compliance=0.1, delay=0.1):
        """Perform voltage sweep"""
        command = {
            "type": "voltage_sweep",
            "data": {
                "start": start,
                "stop": stop,
                "steps": steps,
                "compliance": compliance,
                "delay": delay
            }
        }
        return self.send_command(command)
        
    def get_status(self):
        """Get instrument status"""
        command = {"type": "get_status"}
        return self.send_command(command)
        
    def reset_instrument(self):
        """Reset instrument"""
        command = {"type": "reset"}
        return self.send_command(command)

def demo_basic_operations():
    """Demonstrate basic operations"""
    client = Keithley2400Client()
    
    if not client.connect():
        return
        
    try:
        print("\n=== BASIC OPERATIONS DEMO ===")
        
        # Get instrument status
        print("\n1. Getting instrument status...")
        response = client.get_status()
        if response["status"] == "success":
            print(f"Instrument: {response['data']['instrument']}")
            print(f"Output: {response['data']['output']}")
            print(f"Source Function: {response['data']['source_function']}")
        
        # Reset instrument
        print("\n2. Resetting instrument...")
        response = client.reset_instrument()
        print(f"Reset: {response['message']}")
        
        # Setup voltage source
        print("\n3. Setting up voltage source (1V, 10mA compliance)...")
        response = client.setup_voltage_source(voltage=1.0, compliance=0.01)
        print(f"Setup: {response['message']}")
        
        # Turn output on
        print("\n4. Turning output ON...")
        response = client.set_output("ON")
        print(f"Output: {response['message']}")
        
        # Take measurements
        print("\n5. Taking measurements...")
        for i in range(3):
            response = client.read_measurement()
            if response["status"] == "success":
                data = response["data"]
                print(f"  Measurement {i+1}: {data['voltage']:.6f}V, {data['current']:.9f}A")
            time.sleep(0.5)
        
        # Turn output off
        print("\n6. Turning output OFF...")
        response = client.set_output("OFF")
        print(f"Output: {response['message']}")
        
    finally:
        client.disconnect()

def demo_voltage_sweep():
    """Demonstrate voltage sweep with plotting"""
    client = Keithley2400Client()
    
    if not client.connect():
        return
        
    try:
        print("\n=== VOLTAGE SWEEP DEMO ===")
        
        # Perform voltage sweep
        print("Performing voltage sweep from -2V to +2V...")
        response = client.voltage_sweep(
            start=-2.0,
            stop=2.0,
            steps=21,
            compliance=0.01,  # 10mA compliance
            delay=0.1
        )
        
        if response["status"] == "success":
            data = response["data"]
            print(f"Sweep completed: {len(data)} points")
            
            # Extract data for plotting
            voltages = [point["measured_voltage"] for point in data]
            currents = [point["measured_current"] for point in data]
            
            # Plot I-V curve
            plt.figure(figsize=(10, 6))
            plt.plot(voltages, currents, 'b.-', linewidth=2, markersize=6)
            plt.xlabel('Voltage (V)')
            plt.ylabel('Current (A)')
            plt.title('I-V Characteristic')
            plt.grid(True, alpha=0.3)
            plt.axhline(y=0, color='k', linestyle='-', alpha=0.3)
            plt.axvline(x=0, color='k', linestyle='-', alpha=0.3)
            
            # Show some statistics
            max_current = max(abs(c) for c in currents)
            print(f"Maximum current: {max_current:.9f} A")
            
            plt.tight_layout()
            plt.show()
            
        else:
            print(f"Sweep failed: {response['message']}")
            
    finally:
        client.disconnect()

def demo_current_source():
    """Demonstrate current source mode"""
    client = Keithley2400Client()
    
    if not client.connect():
        return
        
    try:
        print("\n=== CURRENT SOURCE DEMO ===")
        
        # Setup current source
        print("Setting up current source (1mA, 10V compliance)...")
        response = client.setup_current_source(current=0.001, compliance=10)
        print(f"Setup: {response['message']}")
        
        # Turn output on
        response = client.set_output("ON")
        print(f"Output: {response['message']}")
        
        # Take measurement
        response = client.read_measurement()
        if response["status"] == "success":
            data = response["data"]
            voltage = data["voltage"]
            current = data["current"]
            if abs(current) > 1e-9:  # Avoid division by zero
                resistance = voltage / current
                print(f"Sourcing: {current:.6f}A")
                print(f"Measuring: {voltage:.6f}V")
                print(f"Calculated resistance: {resistance:.2f} Ohms")
            else:
                print("No current flow detected (open circuit)")
        
        # Turn output off
        response = client.set_output("OFF")
        print(f"Output: {response['message']}")
        
    finally:
        client.disconnect()

def interactive_mode():
    """Interactive command mode"""
    client = Keithley2400Client()
    
    if not client.connect():
        return
        
    try:
        print("\n=== INTERACTIVE MODE ===")
        print("Available commands:")
        print("  status - Get instrument status")
        print("  reset - Reset instrument")
        print("  output on/off - Control output")
        print("  read - Take measurement")
        print("  write <command> - Send SCPI write command")
        print("  query <command> - Send SCPI query command")
        print("  vsource <voltage> - Setup voltage source")
        print("  isource <current> - Setup current source")
        print("  sweep - Perform voltage sweep")
        print("  quit - Exit")
        
        while True:
            try:
                cmd = input("\nKeithley> ").strip()
                
                if cmd == "quit":
                    break
                elif cmd == "status":
                    response = client.get_status()
                    if response["status"] == "success":
                        for key, value in response["data"].items():
                            print(f"  {key}: {value}")
                elif cmd == "reset":
                    response = client.reset_instrument()
                    print(f"  {response['message']}")
                elif cmd == "read":
                    response = client.read_measurement()
                    if response["status"] == "success":
                        data = response["data"]
                        print(f"  V: {data['voltage']:.6f}V, I: {data['current']:.9f}A")
                elif cmd.startswith("output "):
                    state = cmd.split()[1].upper()
                    response = client.set_output(state)
                    print(f"  {response['message']}")
                elif cmd.startswith("write "):
                    scpi_cmd = cmd[6:]  # Remove "write "
                    response = client.write_command(scpi_cmd)
                    print(f"  {response['message']}")
                elif cmd.startswith("query "):
                    scpi_cmd = cmd[6:]  # Remove "query "
                    response = client.query_command(scpi_cmd)
                    if response["status"] == "success":
                        print(f"  {response['data']}")
                    else:
                        print(f"  Error: {response['message']}")
                elif cmd.startswith("vsource "):
                    voltage = float(cmd.split()[1])
                    response = client.setup_voltage_source(voltage=voltage)
                    print(f"  {response['message']}")
                elif cmd.startswith("isource "):
                    current = float(cmd.split()[1])
                    response = client.setup_current_source(current=current)
                    print(f"  {response['message']}")
                elif cmd == "sweep":
                    response = client.voltage_sweep(start=0, stop=5, steps=11)
                    if response["status"] == "success":
                        print(f"  Sweep completed: {len(response['data'])} points")
                    else:
                        print(f"  Error: {response['message']}")
                else:
                    print("  Unknown command")
                    
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"  Error: {e}")
                
    finally:
        client.disconnect()

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Keithley 2400 Client')
    parser.add_argument('--host', default='localhost', help='Server host')
    parser.add_argument('--port', type=int, default=8888, help='Server port')
    parser.add_argument('--demo', choices=['basic', 'sweep', 'current', 'interactive'], 
                       help='Run demo mode')
    
    args = parser.parse_args()
    
    if args.demo == 'basic':
        demo_basic_operations()
    elif args.demo == 'sweep':
        demo_voltage_sweep()
    elif args.demo == 'current':
        demo_current_source()
    elif args.demo == 'interactive':
        interactive_mode()
    else:
        print("Keithley 2400 Client")
        print("Use --demo [basic|sweep|current|interactive] to run demos")
        print("Or create your own client using the Keithley2400Client class")

if __name__ == "__main__":
    main()
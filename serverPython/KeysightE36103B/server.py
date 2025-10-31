import socket
import json
import threading
from typing import Dict, Optional

class KeysightE36103B:
    def __init__(self, ip: str, port: int = 5025):
        self.ip = ip
        self.port = port
        self.sock = None
        self.lock = threading.Lock()
        
    def connect(self):
        """Connect to the instrument"""
        with self.lock:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(5)
            self.sock.connect((self.ip, self.port))
            print(f"✓ Connected to PSU at {self.ip}:{self.port}")
        
    def disconnect(self):
        """Disconnect from the instrument"""
        with self.lock:
            if self.sock:
                self.sock.close()
                print(f"✓ Disconnected from {self.ip}:{self.port}")
            
    def query(self, command: str) -> str:
        """Send a query and read response"""
        with self.lock:
            if not command.endswith('\n'):
                command += '\n'
            self.sock.sendall(command.encode('utf-8'))
            response = self.sock.recv(4096).decode('utf-8').strip()
            return response
    
    def write(self, command: str):
        """Send a command (no response expected)"""
        with self.lock:
            if not command.endswith('\n'):
                command += '\n'
            self.sock.sendall(command.encode('utf-8'))
    
    def get_id(self) -> str:
        return self.query('*IDN?')
    
    def get_voltage(self) -> float:
        return float(self.query('MEAS:VOLT?'))
    
    def get_current(self) -> float:
        return float(self.query('MEAS:CURR?'))
    
    def set_voltage(self, voltage: float):
        self.write(f'VOLT {voltage}')
        
    def set_current(self, current: float):
        self.write(f'CURR {current}')
    
    def set_ovp(self, voltage: float):
        """Set Over-Voltage Protection level"""
        self.write(f'VOLT:PROT {voltage}')
        self.write('VOLT:PROT:STAT ON')
    
    def set_ocp(self, current: float):
        """Set Over-Current Protection level"""
        self.write(f'CURR:PROT {current}')
        self.write('CURR:PROT:STAT ON')
        
    def output_on(self):
        self.write('OUTP ON')
        
    def output_off(self):
        self.write('OUTP OFF')


class PSUServer:
    def __init__(self, host: str = '127.0.0.81', port: int = 5000):
        self.host = host
        self.port = port
        self.devices: Dict[str, KeysightE36103B] = {}
        self.server_socket = None
        self.running = False
        
    def add_device(self, name: str, ip: str, port: int = 5025):
        """Add a PSU device with a nickname"""
        try:
            psu = KeysightE36103B(ip, port)
            psu.connect()
            idn = psu.get_id()
            self.devices[name] = psu
            print(f"✓ Device '{name}' added: {idn}")
            return True, f"Device added: {idn}"
        except Exception as e:
            return False, f"Failed to add device: {str(e)}"
    
    def handle_command(self, data: dict) -> dict:
        """Process incoming commands"""
        try:
            command = data.get('command')
            device_name = data.get('device')
            
            # Handle device registration
            if command == 'set_device_name':
                ip = data.get('ip')
                port = data.get('port', 5025)
                success, message = self.add_device(device_name, ip, port)
                return {
                    'status': 'success' if success else 'error',
                    'message': message
                }
            
            # Check if device exists
            if device_name not in self.devices:
                return {
                    'status': 'error',
                    'message': f"Device '{device_name}' not found"
                }
            
            psu = self.devices[device_name]
            
            # Execute commands
            if command == 'output_on':
                psu.output_on()
                return {'status': 'success', 'message': 'Output turned ON'}
                
            elif command == 'output_off':
                psu.output_off()
                return {'status': 'success', 'message': 'Output turned OFF'}
                
            elif command == 'set_voltage':
                voltage = float(data.get('value'))
                psu.set_voltage(voltage)
                return {'status': 'success', 'message': f'Voltage set to {voltage}V'}
                
            elif command == 'set_current':
                current = float(data.get('value'))
                psu.set_current(current)
                return {'status': 'success', 'message': f'Current limit set to {current}A'}
                
            elif command == 'set_ovp':
                ovp = float(data.get('value'))
                psu.set_ovp(ovp)
                return {'status': 'success', 'message': f'OVP set to {ovp}V'}
                
            elif command == 'set_ocp':
                ocp = float(data.get('value'))
                psu.set_ocp(ocp)
                return {'status': 'success', 'message': f'OCP set to {ocp}A'}
                
            elif command == 'read_voltage':
                voltage = psu.get_voltage()
                return {'status': 'success', 'data': {'voltage': voltage}}
                
            elif command == 'read_current':
                current = psu.get_current()
                return {'status': 'success', 'data': {'current': current}}
                
            elif command == 'list_devices':
                device_list = list(self.devices.keys())
                return {'status': 'success', 'data': {'devices': device_list}}
                
            else:
                return {'status': 'error', 'message': f"Unknown command: {command}"}
                
        except Exception as e:
            return {'status': 'error', 'message': str(e)}
    
    def handle_client(self, client_socket: socket.socket, address):
        """Handle individual client connection"""
        print(f"✓ Client connected from {address}")
        
        try:
            while self.running:
                # Receive data
                data = client_socket.recv(4096)
                if not data:
                    break
                
                # Parse JSON command
                try:
                    command_data = json.loads(data.decode('utf-8'))
                    print(f"Received command: {command_data.get('command')} for device: {command_data.get('device')}")
                    
                    # Process command
                    response = self.handle_command(command_data)
                    
                    # Send response
                    response_json = json.dumps(response)
                    client_socket.sendall(response_json.encode('utf-8'))
                    
                except json.JSONDecodeError:
                    error_response = json.dumps({
                        'status': 'error',
                        'message': 'Invalid JSON format'
                    })
                    client_socket.sendall(error_response.encode('utf-8'))
                    
        except Exception as e:
            print(f"Error handling client {address}: {e}")
        finally:
            client_socket.close()
            print(f"✗ Client disconnected: {address}")
    
    def start(self):
        """Start the server"""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(5)
            self.running = True
            
            print(f"✓ PSU Server started on {self.host}:{self.port}")
            print("Waiting for client connections...")
            
            while self.running:
                try:
                    client_socket, address = self.server_socket.accept()
                    # Handle each client in a separate thread
                    client_thread = threading.Thread(
                        target=self.handle_client,
                        args=(client_socket, address)
                    )
                    client_thread.daemon = True
                    client_thread.start()
                    
                except KeyboardInterrupt:
                    print("\n✗ Server shutting down...")
                    break
                    
        finally:
            self.stop()
    
    def stop(self):
        """Stop the server and disconnect all devices"""
        self.running = False
        
        # Disconnect all devices
        for name, psu in self.devices.items():
            try:
                psu.disconnect()
            except:
                pass
        
        if self.server_socket:
            self.server_socket.close()
        
        print("✓ Server stopped")


if __name__ == "__main__":
    # Create and start server
    server = PSUServer(host='127.0.0.60', port=5000)  # Using localhost
    
    try:
        server.start()
    except KeyboardInterrupt:
        print("\n✗ Server interrupted")
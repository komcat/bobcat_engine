import socket
import json
import time

class PSUClient:
    def __init__(self, server_host: str = '127.0.0.60', server_port: int = 5000):
        self.server_host = server_host
        self.server_port = server_port
        self.sock = None
        
    def connect(self):
        """Connect to the PSU server"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.server_host, self.server_port))
        print(f"✓ Connected to PSU server at {self.server_host}:{self.server_port}")
    
    def disconnect(self):
        """Disconnect from the server"""
        if self.sock:
            self.sock.close()
            print("✓ Disconnected from server")
    
    def send_command(self, command_data: dict) -> dict:
        """Send command and receive response"""
        # Send command
        command_json = json.dumps(command_data)
        self.sock.sendall(command_json.encode('utf-8'))
        
        # Receive response
        response = self.sock.recv(4096)
        response_data = json.loads(response.decode('utf-8'))
        
        return response_data
    
    def register_device(self, device_name: str, ip: str, port: int = 5025):
        """Register a new PSU device"""
        command = {
            'command': 'set_device_name',
            'device': device_name,
            'ip': ip,
            'port': port
        }
        return self.send_command(command)
    
    def output_on(self, device: str):
        """Turn output ON"""
        return self.send_command({'command': 'output_on', 'device': device})
    
    def output_off(self, device: str):
        """Turn output OFF"""
        return self.send_command({'command': 'output_off', 'device': device})
    
    def set_voltage(self, device: str, voltage: float):
        """Set voltage"""
        return self.send_command({
            'command': 'set_voltage',
            'device': device,
            'value': voltage
        })
    
    def set_current(self, device: str, current: float):
        """Set current limit"""
        return self.send_command({
            'command': 'set_current',
            'device': device,
            'value': current
        })
    
    def set_ovp(self, device: str, voltage: float):
        """Set Over-Voltage Protection"""
        return self.send_command({
            'command': 'set_ovp',
            'device': device,
            'value': voltage
        })
    
    def set_ocp(self, device: str, current: float):
        """Set Over-Current Protection"""
        return self.send_command({
            'command': 'set_ocp',
            'device': device,
            'value': current
        })
    
    def read_voltage(self, device: str):
        """Read voltage"""
        return self.send_command({'command': 'read_voltage', 'device': device})
    
    def read_current(self, device: str):
        """Read current"""
        return self.send_command({'command': 'read_current', 'device': device})


# Test Client
def test_psu_client():
    """Test all PSU functionality"""
    client = PSUClient(server_host='127.0.0.60', server_port=5000)
    
    try:
        # Connect to server
        client.connect()
        
        print("\n" + "="*60)
        print("TEST 1: Register PSU Device")
        print("="*60)
        response = client.register_device('PSU1', '192.168.0.60', 5025)
        print(f"Response: {response}")
        time.sleep(0.5)
        
        print("\n" + "="*60)
        print("TEST 2: Set Voltage to 5V")
        print("="*60)
        response = client.set_voltage('PSU1', 5.0)
        print(f"Response: {response}")
        time.sleep(0.5)
        
        print("\n" + "="*60)
        print("TEST 3: Set Current Limit to 1A")
        print("="*60)
        response = client.set_current('PSU1', 1.0)
        print(f"Response: {response}")
        time.sleep(0.5)
        
        print("\n" + "="*60)
        print("TEST 4: Set OVP to 6V")
        print("="*60)
        response = client.set_ovp('PSU1', 6.0)
        print(f"Response: {response}")
        time.sleep(0.5)
        
        print("\n" + "="*60)
        print("TEST 5: Set OCP to 1.2A")
        print("="*60)
        response = client.set_ocp('PSU1', 1.2)
        print(f"Response: {response}")
        time.sleep(0.5)
        
        print("\n" + "="*60)
        print("TEST 6: Turn Output ON")
        print("="*60)
        response = client.output_on('PSU1')
        print(f"Response: {response}")
        time.sleep(1)
        
        print("\n" + "="*60)
        print("TEST 7: Read Voltage")
        print("="*60)
        response = client.read_voltage('PSU1')
        print(f"Response: {response}")
        if response['status'] == 'success':
            print(f"Voltage: {response['data']['voltage']} V")
        time.sleep(0.5)
        
        print("\n" + "="*60)
        print("TEST 8: Read Current")
        print("="*60)
        response = client.read_current('PSU1')
        print(f"Response: {response}")
        if response['status'] == 'success':
            print(f"Current: {response['data']['current']} A")
        time.sleep(0.5)
        
        print("\n" + "="*60)
        print("TEST 9: Turn Output OFF")
        print("="*60)
        response = client.output_off('PSU1')
        print(f"Response: {response}")
        time.sleep(0.5)
        
        print("\n" + "="*60)
        print("✓ All tests completed!")
        print("="*60)
        
    except Exception as e:
        print(f"✗ Error: {e}")
        import traceback
        traceback.print_exc()
        
    finally:
        client.disconnect()


if __name__ == "__main__":
    print("PSU Client Test Program")
    print("="*60)
    input("Make sure the server is running, then press Enter to start tests...")
    test_psu_client()
import socket

class KeysightE36103B:
    def __init__(self, ip, port=5025):
        self.ip = ip
        self.port = port
        self.sock = None
        
    def connect(self):
        """Connect to the instrument"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(5)
        self.sock.connect((self.ip, self.port))
        print(f"✓ Connected to {self.ip}:{self.port}")
        
    def disconnect(self):
        """Disconnect from the instrument"""
        if self.sock:
            self.sock.close()
            print("✓ Disconnected")
            
    def query(self, command):
        """Send a query and read response"""
        if not command.endswith('\n'):
            command += '\n'
        self.sock.sendall(command.encode('utf-8'))
        response = self.sock.recv(4096).decode('utf-8').strip()
        return response
    
    def write(self, command):
        """Send a command (no response expected)"""
        if not command.endswith('\n'):
            command += '\n'
        self.sock.sendall(command.encode('utf-8'))
        
    def get_id(self):
        """Get instrument ID"""
        return self.query('*IDN?')
    
    def get_voltage(self):
        """Measure output voltage"""
        return float(self.query('MEAS:VOLT?'))
    
    def get_current(self):
        """Measure output current"""
        return float(self.query('MEAS:CURR?'))
    
    def set_voltage(self, voltage):
        """Set output voltage"""
        self.write(f'VOLT {voltage}')
        
    def set_current(self, current):
        """Set current limit"""
        self.write(f'CURR {current}')
        
    def output_on(self):
        """Turn output ON"""
        self.write('OUTP ON')
        
    def output_off(self):
        """Turn output OFF"""
        self.write('OUTP OFF')


# Example usage
if __name__ == "__main__":
    psu = KeysightE36103B("192.168.0.60")
    
    try:
        psu.connect()
        
        # Get ID
        print(f"Instrument: {psu.get_id()}")
        
        # Read measurements
        print(f"Voltage: {psu.get_voltage()} V")
        print(f"Current: {psu.get_current()} A")
        
        # Example: Set voltage and current
        # psu.set_voltage(5.0)
        # psu.set_current(1.0)
        # psu.output_on()
        
    finally:
        psu.disconnect()
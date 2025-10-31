# Keithley 2400 SourceMeter Server-Client System

A comprehensive TCP server-client system for remote control of the Keithley 2400 SourceMeter instrument via GPIB interface.

## Table of Contents
- [Overview](#overview)
- [System Requirements](#system-requirements)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Server Documentation](#server-documentation)
- [Client Documentation](#client-documentation)
- [API Reference](#api-reference)
- [Examples](#examples)
- [Troubleshooting](#troubleshooting)
- [Safety Considerations](#safety-considerations)

## Overview

This system provides:
- **TCP Server** for instrument control with multi-client support
- **Python Client API** for easy integration
- **JSON-based command protocol** for cross-platform compatibility
- **High-level measurement functions** (voltage sweeps, I-V characterization)
- **Raw SCPI command access** for advanced users
- **Automatic safety features** (output disable on disconnect/error)
- **Real-time data visualization** capabilities

### Architecture
```
┌─────────────┐    TCP/JSON    ┌─────────────┐    GPIB    ┌─────────────┐
│   Client    │ ◄───────────► │   Server    │ ◄─────────► │ Keithley    │
│ Application │               │  (Python)   │             │    2400     │
└─────────────┘               └─────────────┘             └─────────────┘
```

## System Requirements

### Hardware
- Keithley 2400 SourceMeter
- GPIB interface (USB-GPIB adapter, PCIe card, etc.)
- Computer with GPIB drivers installed

### Software
- Python 3.7+
- Required Python packages:
  ```
  pyvisa
  matplotlib (for plotting demos)
  ```
- VISA Runtime (one of):
  - NI-VISA Runtime
  - Keysight IO Libraries Suite
  - linux-gpib (Linux)

### Network
- TCP/IP connectivity between server and clients
- Default port: 8888 (configurable)

## Installation

1. **Install Python Dependencies**
   ```bash
   pip install pyvisa matplotlib
   ```

2. **Install VISA Runtime**
   - **Windows**: Download NI-VISA or Keysight IO Libraries
   - **Linux**: Install linux-gpib or NI-VISA Runtime
   - **macOS**: Download NI-VISA Runtime

3. **Verify GPIB Connection**
   ```python
   import pyvisa
   rm = pyvisa.ResourceManager()
   print(rm.list_resources())
   ```

4. **Download Server and Client Files**
   - `Keithley2400Server.py` - Main server application
   - `Keithley2400Client.py` - Client library and demos

## Quick Start

### 1. Start the Server
```bash
# Basic startup (localhost:8888)
python Keithley2400Server.py

# Custom configuration
python Keithley2400Server.py --host 0.0.0.0 --port 8888 --gpib GPIB1::24::INSTR
```

### 2. Run Client Demos
```bash
# Basic operations test
python Keithley2400Client.py --demo basic

# Voltage sweep with I-V plotting
python Keithley2400Client.py --demo sweep

# Current source demonstration
python Keithley2400Client.py --demo current

# Interactive command line interface
python Keithley2400Client.py --demo interactive
```

### 3. Basic Python Client Usage
```python
from Keithley2400Client import Keithley2400Client

# Connect to server
client = Keithley2400Client('localhost', 8888)
client.connect()

# Setup voltage source
client.setup_voltage_source(voltage=5.0, compliance=0.01)
client.set_output("ON")

# Take measurement
response = client.read_measurement()
print(f"Voltage: {response['data']['voltage']}V")
print(f"Current: {response['data']['current']}A")

# Cleanup
client.set_output("OFF")
client.disconnect()
```

## Server Documentation

### Command Line Options
```bash
python Keithley2400Server.py [OPTIONS]

Options:
  --host HOST        Server bind address (default: localhost)
  --port PORT        Server port (default: 8888)
  --gpib ADDRESS     GPIB address (default: GPIB1::24::INSTR)
  -h, --help         Show help message
```

### Server Features
- **Multi-client Support**: Multiple clients can connect simultaneously
- **Thread Safety**: Each client handled in separate thread
- **Automatic Cleanup**: Output disabled on client disconnect or error
- **Comprehensive Logging**: All operations logged with timestamps
- **Error Handling**: Graceful error recovery and reporting

### Server Startup Example
```bash
# Local development
python Keithley2400Server.py

# Production deployment (all interfaces)
python Keithley2400Server.py --host 0.0.0.0 --port 9999

# Different GPIB address
python Keithley2400Server.py --gpib GPIB0::15::INSTR
```

## Client Documentation

### Keithley2400Client Class

#### Connection Methods
```python
client = Keithley2400Client(host='localhost', port=8888)
client.connect()          # Returns True/False
client.disconnect()       # Clean disconnect
```

#### High-Level Methods
```python
# Instrument control
client.setup_voltage_source(voltage, compliance, range_val="AUTO")
client.setup_current_source(current, compliance, range_val="AUTO")
client.set_output("ON" | "OFF")
client.read_measurement()
client.get_status()
client.reset_instrument()

# Advanced measurements
client.voltage_sweep(start, stop, steps, compliance, delay)
```

#### Low-Level Methods
```python
# Raw SCPI commands
client.write_command(scpi_command)
client.query_command(scpi_command)
```

### Demo Modes

#### Basic Demo (`--demo basic`)
Demonstrates:
- Instrument identification
- Reset and setup
- Voltage source configuration
- Output control
- Basic measurements

#### Sweep Demo (`--demo sweep`)
Features:
- Automated voltage sweep
- I-V curve plotting
- Data analysis
- Statistical summary

#### Current Demo (`--demo current`)
Shows:
- Current source setup
- Voltage measurement
- Resistance calculation
- Compliance testing

#### Interactive Mode (`--demo interactive`)
Provides:
- Command-line interface
- Real-time control
- All server functions accessible
- Immediate feedback

## API Reference

### JSON Command Protocol

All communication uses JSON messages with the following structure:

```json
{
  "type": "command_type",
  "data": {
    "parameter1": "value1",
    "parameter2": "value2"
  }
}
```

### Command Types

#### Basic Control Commands

**Reset Instrument**
```json
{"type": "reset"}
```

**Get Status**
```json
{"type": "get_status"}
```
Response:
```json
{
  "status": "success",
  "data": {
    "instrument": "KEITHLEY INSTRUMENTS INC.,MODEL 2400...",
    "output": "OFF",
    "source_function": "VOLT",
    "timestamp": "2025-06-11T10:30:00.123456"
  }
}
```

**Output Control**
```json
{"type": "output", "data": {"state": "ON"}}
{"type": "output", "data": {"state": "OFF"}}
```

#### Measurement Commands

**Take Reading**
```json
{"type": "read"}
```
Response:
```json
{
  "status": "success",
  "data": {
    "voltage": 1.000000,
    "current": 0.000001234,
    "resistance": 810000.0,
    "power": 0.000001234,
    "timestamp": "2025-06-11T10:30:00.123456"
  }
}
```

#### Source Configuration

**Voltage Source Setup**
```json
{
  "type": "setup_voltage_source",
  "data": {
    "voltage": 5.0,
    "compliance": 0.01,
    "range": "AUTO"
  }
}
```

**Current Source Setup**
```json
{
  "type": "setup_current_source",
  "data": {
    "current": 0.001,
    "compliance": 10.0,
    "range": "AUTO"
  }
}
```

#### Advanced Measurements

**Voltage Sweep**
```json
{
  "type": "voltage_sweep",
  "data": {
    "start": -2.0,
    "stop": 2.0,
    "steps": 21,
    "compliance": 0.01,
    "delay": 0.1
  }
}
```

#### Raw SCPI Commands

**Write Command**
```json
{"type": "write", "data": {"command": ":SOUR:VOLT 2.5"}}
```

**Query Command**
```json
{"type": "query", "data": {"command": "*IDN?"}}
```

### Response Format

All responses follow this format:
```json
{
  "status": "success" | "error",
  "message": "Description of result",
  "data": { ... }  // Present for successful queries
}
```

### Error Handling

Errors are returned as:
```json
{
  "status": "error",
  "message": "Error description"
}
```

Common error causes:
- Invalid JSON format
- Unknown command type
- GPIB communication failure
- Instrument timeout
- Invalid parameter values

## Examples

### Example 1: Basic I-V Measurement
```python
from Keithley2400Client import Keithley2400Client

client = Keithley2400Client()
if client.connect():
    # Setup
    client.setup_voltage_source(voltage=0, compliance=0.01)
    client.set_output("ON")
    
    # Measure at different voltages
    voltages = [0, 1, 2, 3, 4, 5]
    results = []
    
    for v in voltages:
        client.write_command(f":SOUR:VOLT {v}")
        time.sleep(0.1)
        
        response = client.read_measurement()
        if response["status"] == "success":
            data = response["data"]
            results.append((v, data["voltage"], data["current"]))
            print(f"{v}V -> {data['current']:.6e}A")
    
    client.set_output("OFF")
    client.disconnect()
```

### Example 2: Automated Resistance Measurement
```python
def measure_resistance(client, test_current=0.001):
    """Measure resistance using current source method"""
    
    # Setup current source
    client.setup_current_source(current=test_current, compliance=10)
    client.set_output("ON")
    
    # Take measurement
    response = client.read_measurement()
    
    client.set_output("OFF")
    
    if response["status"] == "success":
        data = response["data"]
        resistance = data["voltage"] / data["current"]
        return resistance
    else:
        return None

# Usage
client = Keithley2400Client()
if client.connect():
    resistance = measure_resistance(client, 0.001)  # 1mA test current
    print(f"Measured resistance: {resistance:.2f} Ohms")
    client.disconnect()
```

### Example 3: Temperature Coefficient Measurement
```python
def temperature_sweep_measurement(client, voltages, temperatures):
    """Measure I-V curves at different temperatures"""
    
    results = {}
    
    for temp in temperatures:
        print(f"Set temperature to {temp}°C and press Enter...")
        input()
        
        # Perform voltage sweep at this temperature
        response = client.voltage_sweep(
            start=min(voltages),
            stop=max(voltages),
            steps=len(voltages),
            compliance=0.01
        )
        
        if response["status"] == "success":
            results[temp] = response["data"]
    
    return results
```

### Example 4: Custom Measurement Protocol
```python
class CustomMeasurement:
    def __init__(self, client):
        self.client = client
        
    def pulse_measurement(self, pulse_voltage, pulse_width, measure_points=10):
        """Perform pulsed measurement to avoid heating"""
        
        results = []
        
        # Setup
        self.client.setup_voltage_source(voltage=0, compliance=0.1)
        self.client.set_output("ON")
        
        for i in range(measure_points):
            # Apply pulse
            self.client.write_command(f":SOUR:VOLT {pulse_voltage}")
            time.sleep(pulse_width)
            
            # Measure
            response = self.client.read_measurement()
            if response["status"] == "success":
                results.append(response["data"])
            
            # Return to zero
            self.client.write_command(":SOUR:VOLT 0")
            time.sleep(pulse_width * 2)  # Recovery time
        
        self.client.set_output("OFF")
        return results
```

## Troubleshooting

### Common Issues

**1. Server Won't Start**
```
Error: Failed to connect to instrument
```
- Check GPIB address: `python -c "import pyvisa; print(pyvisa.ResourceManager().list_resources())"`
- Verify VISA drivers installed
- Ensure instrument is powered on
- Check GPIB cable connections

**2. Client Can't Connect**
```
Connection failed: Connection refused
```
- Verify server is running
- Check host/port settings
- Confirm firewall settings
- Test with: `telnet localhost 8888`

**3. Measurements Return Zero**
```
Current: 0.000000000A (always zero)
```
- Check test connections (likely open circuit)
- Verify compliance settings
- Ensure proper range selection
- Check for broken wires

**4. Communication Timeouts**
```
Communication error: timeout
```
- Increase timeout in client
- Check network stability
- Verify server isn't overloaded
- Restart server if needed

**5. GPIB Errors**
```
VISA error: VI_ERROR_TMO
```
- Check GPIB cables
- Verify termination
- Reduce communication speed
- Check for address conflicts

### Debug Mode

Enable detailed logging:
```python
import logging
logging.basicConfig(level=logging.DEBUG)
```

### Network Testing

Test TCP connection:
```bash
# Test server response
echo '{"type":"get_status"}' | nc localhost 8888
```

### GPIB Testing

Verify instrument communication:
```python
import pyvisa
rm = pyvisa.ResourceManager()
instrument = rm.open_resource('GPIB1::24::INSTR')
print(instrument.query('*IDN?'))
```

## Safety Considerations

### Electrical Safety
- **Always set appropriate compliance limits** to protect devices under test
- **Turn output OFF** when connecting/disconnecting devices
- **Use proper voltage/current ranges** for your measurements
- **Verify connections** before applying power

### Software Safety Features
- **Automatic output disable** on client disconnect
- **Compliance checking** in all source modes
- **Error recovery** with safe defaults
- **Timeout protection** prevents stuck operations

### Best Practices
1. **Start with low voltages/currents** when testing new devices
2. **Use voltage sweeps** rather than step changes for sensitive devices
3. **Monitor compliance** indicators during measurements
4. **Keep measurement times short** to avoid device heating
5. **Always clean up** (turn output off) after measurements

### Emergency Procedures
If something goes wrong:
1. **Disconnect immediately**: `client.set_output("OFF")`
2. **Stop server**: Press Ctrl+C in server terminal
3. **Power cycle instrument** if needed
4. **Check device under test** for damage

## Advanced Usage

### Multi-Client Scenarios
The server supports multiple simultaneous clients:
```python
# Client 1: Data acquisition
client1 = Keithley2400Client()
client1.connect()
# Perform measurements...

# Client 2: Real-time monitoring
client2 = Keithley2400Client()
client2.connect()
# Monitor status...
```

### Integration with Other Instruments
```python
# Example: Combine with temperature controller
temp_controller = TemperatureController()
keithley_client = Keithley2400Client()

for temperature in temp_range:
    temp_controller.set_temperature(temperature)
    time.sleep(stabilization_time)
    
    iv_data = keithley_client.voltage_sweep(...)
    save_data(temperature, iv_data)
```

### Data Logging
```python
import csv
import datetime

def log_measurement(filename, measurement_data):
    with open(filename, 'a', newline='') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow([
            datetime.datetime.now(),
            measurement_data['voltage'],
            measurement_data['current'],
            measurement_data['power']
        ])
```

### Remote Access
For remote laboratory access:
```bash
# Start server on all interfaces
python Keithley2400Server.py --host 0.0.0.0 --port 8888

# Connect from remote machine
client = Keithley2400Client('lab-server.example.com', 8888)
```

## Contributing

To extend the system:

1. **Add new measurement functions** to the server's `process_command()` method
2. **Create corresponding client methods** in `Keithley2400Client`
3. **Update this documentation** with new features
4. **Add example usage** in the examples section

### Adding Custom Commands

Server side:
```python
def process_command(self, command):
    # ... existing code ...
    elif cmd_type == 'my_custom_measurement':
        # Implement custom measurement
        result = self.my_custom_function(cmd_data)
        return {"status": "success", "data": result}
```

Client side:
```python
def my_custom_measurement(self, parameters):
    command = {
        "type": "my_custom_measurement",
        "data": parameters
    }
    return self.send_command(command)
```

---

**Version**: 1.0  
**Last Updated**: June 11, 2025  
**Compatibility**: Keithley 2400 Series SourceMeters  
**License**: MIT  

For support or questions, please refer to the troubleshooting section or create an issue in the project repository.
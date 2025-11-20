# TCP Server Creation Guide for TcpClientManager

This guide shows how to create a TCP server that sends data to the bobcat_engine TcpClientManager system.

## Table of Contents
1. [Protocol Overview](#protocol-overview)
2. [Single-Channel Server (Simple)](#single-channel-server-simple)
3. [Multi-Channel Server (Advanced)](#multi-channel-server-advanced)
4. [Client Configuration](#client-configuration)
5. [Testing Your Server](#testing-your-server)

---

## Protocol Overview

The TcpClientManager supports two protocol types:

### **Single-Channel Protocol**
- **Format**: One float value per line, terminated with newline
- **Example**: `"1.234\n5.678\n9.012\n"`
- **Use case**: Simple single-value data streams (voltage, temperature, etc.)

### **Multi-Channel Protocol**
- **Format**: CSV data wrapped in markers: `$START,val1,val2,...,valN,$END`
- **Example**: `"$START,99.5,103.2,25.1,0.558,0.061,-69.8,$END"`
- **Use case**: Multiple related measurements sent together (sensor arrays, multi-parameter devices)

---

## Single-Channel Server (Simple)

### Python Example

```python
import socket
import time
import random

HOST = "127.0.0.1"
PORT = 8888

def run_single_channel_server():
    """Simple single-value TCP server"""

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(1)

    print(f"[Server] Listening on {HOST}:{PORT}")
    print("[Server] Waiting for client connection...")

    conn, address = server.accept()
    print(f"[Server] Client connected: {address}")

    try:
        while True:
            # Generate a sample value (replace with your actual data source)
            voltage = 5.0 + random.uniform(-0.5, 0.5)

            # Format: single float + newline
            message = f"{voltage:.6f}\n"

            # Send to client
            conn.send(message.encode())

            # Update rate (adjust as needed)
            time.sleep(0.01)  # 100 Hz

    except KeyboardInterrupt:
        print("\n[Server] Shutting down...")
    except Exception as e:
        print(f"[Server] Error: {e}")
    finally:
        conn.close()
        server.close()

if __name__ == "__main__":
    run_single_channel_server()
```

### C++ Example

```cpp
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

void RunSingleChannelServer() {
    // Initialize Winsock
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Create socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    // Bind to port
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 1);

    std::cout << "[Server] Listening on port 8888..." << std::endl;

    // Accept connection
    SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
    std::cout << "[Server] Client connected!" << std::endl;

    // Send data loop
    float voltage = 5.0f;
    while (true) {
        // Format: single float + newline
        std::string message = std::to_string(voltage) + "\n";

        // Send to client
        send(clientSocket, message.c_str(), message.length(), 0);

        // Update value (simulate changing data)
        voltage += 0.01f;
        if (voltage > 10.0f) voltage = 5.0f;

        // 100 Hz update rate
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    closesocket(clientSocket);
    closesocket(serverSocket);
    WSACleanup();
}
```

---

## Multi-Channel Server (Advanced)

### Python Example (Based on FNW_SERVER_V08)

```python
import socket
import time
import random

HOST = "127.0.0.1"
PORT = 65432

# Define your channel names (for documentation)
CHANNEL_NAMES = [
    "SLED_Current_mA",
    "Photo_Current_uA",
    "SLED_Temp_C",
    "Target_SAG_PWR_V",
    "SAG_PWR_V",
    "TEC_Current_mA"
]

def generate_sample_data():
    """Generate sample multi-channel data"""
    return [
        random.uniform(90, 110),     # SLED_Current_mA
        random.uniform(100, 110),    # Photo_Current_uA
        random.uniform(24, 26),      # SLED_Temp_C
        random.uniform(0.5, 0.6),    # Target_SAG_PWR_V
        random.uniform(0.05, 0.07),  # SAG_PWR_V
        random.uniform(-80, -60)     # TEC_Current_mA
    ]

def run_multi_channel_server():
    """Multi-channel TCP server using $START...$END protocol"""

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen(1)

    print(f"[Server] Multi-channel server listening on {HOST}:{PORT}")
    print(f"[Server] Channels: {len(CHANNEL_NAMES)}")
    for i, name in enumerate(CHANNEL_NAMES):
        print(f"  [{i}] {name}")
    print("\n[Server] Waiting for client connection...")

    conn, address = server.accept()
    conn.settimeout(0.01)  # Non-blocking with small timeout

    print(f"[Server] Client connected: {address}\n")

    sample_count = 0

    try:
        while True:
            # Generate data from your sensors/devices
            values = generate_sample_data()

            # Format: $START,val1,val2,...,valN,$END
            values_str = ",".join([f"{v:f}" for v in values])
            message = f"$START,{values_str},$END"

            # Send to client
            try:
                conn.send(message.encode())
                sample_count += 1

                # Print progress every 200 samples
                if sample_count % 200 == 0:
                    print(f"[Server] Sent {sample_count} samples")
                    for i, val in enumerate(values):
                        print(f"  {CHANNEL_NAMES[i]:25s}: {val:.3f}")
                    print()

            except socket.error as e:
                print(f"[Server] Client disconnected: {e}")
                break

            # Update rate (200 Hz in original FNW_SERVER_V08)
            time.sleep(0.005)

    except KeyboardInterrupt:
        print("\n[Server] Shutting down...")
    except Exception as e:
        print(f"[Server] Error: {e}")
    finally:
        conn.close()
        server.close()

if __name__ == "__main__":
    run_multi_channel_server()
```

### C++ Example

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <thread>
#include <random>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

class MultiChannelServer {
public:
    MultiChannelServer(const std::string& host, int port)
        : m_host(host), m_port(port) {}

    void Run() {
        // Initialize Winsock
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        // Create socket
        SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        // Bind to port
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(m_port);
        inet_pton(AF_INET, m_host.c_str(), &serverAddr.sin_addr);

        bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
        listen(serverSocket, 1);

        std::cout << "[Server] Multi-channel server listening on "
                  << m_host << ":" << m_port << std::endl;

        // Accept connection
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        std::cout << "[Server] Client connected!" << std::endl;

        // Random number generator for sample data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        // Send data loop
        int sampleCount = 0;
        while (true) {
            // Generate sample data (6 channels)
            std::vector<float> values = {
                90.0f + dist(gen) * 20.0f,   // SLED_Current_mA
                100.0f + dist(gen) * 10.0f,  // Photo_Current_uA
                24.0f + dist(gen) * 2.0f,    // SLED_Temp_C
                0.5f + dist(gen) * 0.1f,     // Target_SAG_PWR_V
                0.05f + dist(gen) * 0.02f,   // SAG_PWR_V
                -70.0f - dist(gen) * 10.0f   // TEC_Current_mA
            };

            // Format: $START,val1,val2,...,val6,$END
            std::string message = FormatMultiChannelMessage(values);

            // Send to client
            int result = send(clientSocket, message.c_str(), message.length(), 0);
            if (result == SOCKET_ERROR) {
                std::cout << "[Server] Client disconnected" << std::endl;
                break;
            }

            sampleCount++;

            // Print progress
            if (sampleCount % 200 == 0) {
                std::cout << "[Server] Sent " << sampleCount << " samples" << std::endl;
            }

            // 200 Hz update rate
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        closesocket(clientSocket);
        closesocket(serverSocket);
        WSACleanup();
    }

private:
    std::string FormatMultiChannelMessage(const std::vector<float>& values) {
        std::ostringstream oss;
        oss << "$START";

        for (size_t i = 0; i < values.size(); ++i) {
            oss << "," << values[i];
        }

        oss << ",$END";
        return oss.str();
    }

    std::string m_host;
    int m_port;
};

int main() {
    MultiChannelServer server("127.0.0.1", 65432);
    server.Run();
    return 0;
}
```

---

## Client Configuration

### For Single-Channel Server

Edit `config/tcp_clients.json`:

```json
{
  "tcp_clients": [
    {
      "id": "my_voltage_sensor",
      "name": "Voltage Monitor",
      "host": "127.0.0.1",
      "port": 8888,
      "protocol_type": "single",
      "data_channel": "Voltage",
      "auto_connect": true,
      "auto_reconnect": true,
      "description": "My voltage measurement device"
    }
  ]
}
```

**Access the data in C++:**
```cpp
float voltage = dataStore->GetValue("Voltage");
```

### For Multi-Channel Server

Edit `config/tcp_clients.json`:

```json
{
  "tcp_clients": [
    {
      "id": "my_sensor_array",
      "name": "Multi-Sensor Array",
      "host": "127.0.0.1",
      "port": 65432,
      "protocol_type": "multi",
      "data_channels": [
        "Sensor_1",
        "Sensor_2",
        "Sensor_3",
        "Sensor_4",
        "Sensor_5",
        "Sensor_6"
      ],
      "auto_connect": true,
      "auto_reconnect": true,
      "description": "6-channel sensor array"
    }
  ]
}
```

**Access the data in C++:**
```cpp
float sensor1 = dataStore->GetValue("Sensor_1");
float sensor2 = dataStore->GetValue("Sensor_2");
// ... etc
```

---

## Testing Your Server

### Step 1: Start Your Server

**Python:**
```bash
python your_server.py
```

**C++:**
```bash
./your_server.exe
```

### Step 2: Test with Python Test Client

Use the provided test client from `C:\SOFTWARE\SiphogSW\test_client_v08.py`:

```bash
cd C:\SOFTWARE\SiphogSW
python test_client_v08.py
```

### Step 3: Connect bobcat_engine

1. Update `config/tcp_clients.json` with your server settings
2. Build and run bobcat_engine
3. The TcpClientManager will automatically connect (if `auto_connect: true`)
4. Data will appear in DataStore

### Step 4: Verify Data Flow

Check console output for:
```
[TcpClientManager] Added client: my_sensor_array (127.0.0.1:65432, multi mode)
[TcpClientManager] Connected client: my_sensor_array (multi mode)
[TcpDataClient] Connected to 127.0.0.1:65432
```

---

## Protocol Rules

### ✅ DO:
- Send complete messages: `$START,1,2,3,4,5,6,$END`
- Use consistent channel count
- Send data at regular intervals (recommended: 10-200 Hz)
- Handle client disconnections gracefully

### ❌ DON'T:
- Split `$START` and `$END` across multiple send() calls if possible
- Send incomplete messages
- Change channel count mid-stream
- Send non-numeric values

---

## Advanced: Integration with Hardware

### Example: Arduino/Microcontroller

```cpp
// Arduino example - sends 3 channels over Serial-to-TCP bridge
void loop() {
    float temp = readTemperature();
    float pressure = readPressure();
    float humidity = readHumidity();

    // Format multi-channel message
    Serial.print("$START,");
    Serial.print(temp, 6);
    Serial.print(",");
    Serial.print(pressure, 6);
    Serial.print(",");
    Serial.print(humidity, 6);
    Serial.println(",$END");

    delay(10); // 100 Hz
}
```

### Example: LabVIEW

1. Use TCP/IP functions in LabVIEW
2. Format string: `"$START," + value1 + "," + value2 + ",...,$END"`
3. Send via TCP Write function
4. Loop at desired rate

---

## Troubleshooting

### Problem: Client won't connect
- Check firewall settings
- Verify server is listening: `netstat -an | findstr <PORT>`
- Confirm host/port in config matches server

### Problem: Data not appearing in DataStore
- Check console for parse errors
- Verify channel count matches config
- Ensure message format is exact: `$START,val1,val2,$END`

### Problem: Performance issues
- Reduce send rate (increase `time.sleep()`)
- Check network latency
- Verify client isn't blocking on full buffer

---

## Example: Complete Working System

See `C:\SOFTWARE\SiphogSW\FNW_SERVER_V08_test_plot.py` for a complete reference implementation of a multi-channel server with:
- Hardware integration (SiPhOG device)
- Non-blocking server
- Live parameter updates
- Error handling
- GUI monitoring

---

## Summary

| Feature | Single-Channel | Multi-Channel |
|---------|---------------|---------------|
| Format | `"value\n"` | `"$START,v1,v2,...,$END"` |
| Config | `"protocol_type": "single"` | `"protocol_type": "multi"` |
| Data Storage | 1 DataStore channel | N DataStore channels |
| Use Case | Simple sensors | Sensor arrays, complex devices |

For most applications, start with single-channel for simplicity. Use multi-channel when:
- Multiple related values change together
- Synchronized timestamps are critical
- Network bandwidth is limited
- Single device produces multiple measurements

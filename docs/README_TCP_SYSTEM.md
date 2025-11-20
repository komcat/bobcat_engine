# TCP Data Streaming System - Quick Start

This directory contains documentation and templates for creating TCP servers that work with the bobcat_engine TcpClientManager.

## 📚 Documentation Files

| File | Description |
|------|-------------|
| **TCP_SERVER_GUIDE.md** | Complete guide with theory, examples, and troubleshooting |
| **server_template_single.py** | Ready-to-use single-channel server template |
| **server_template_multi.py** | Ready-to-use multi-channel server template |
| **client_config_examples.json** | Configuration examples for various scenarios |

## 🚀 Quick Start

### Option 1: Single-Channel Server (Simple)

**1. Copy and customize the template:**
```bash
cp server_template_single.py my_server.py
```

**2. Edit `my_server.py`:**
```python
def generate_data():
    # Replace this with your actual sensor
    return read_my_sensor()
```

**3. Run the server:**
```bash
python my_server.py
```

**4. Configure bobcat_engine** in `config/tcp_clients.json`:
```json
{
  "tcp_clients": [{
    "id": "my_sensor",
    "protocol_type": "single",
    "data_channel": "MySensorValue",
    "host": "127.0.0.1",
    "port": 8888,
    "auto_connect": true
  }]
}
```

**5. Access data in C++:**
```cpp
float value = dataStore->GetValue("MySensorValue");
```

---

### Option 2: Multi-Channel Server (Advanced)

**1. Copy and customize the template:**
```bash
cp server_template_multi.py my_multi_server.py
```

**2. Edit `my_multi_server.py`:**
```python
CHANNEL_NAMES = [
    "Sensor_1",
    "Sensor_2",
    "Sensor_3"
]

def generate_data():
    # Return list of values matching CHANNEL_NAMES
    return [
        read_sensor_1(),
        read_sensor_2(),
        read_sensor_3()
    ]
```

**3. Run the server:**
```bash
python my_multi_server.py
```

**4. Configure bobcat_engine** in `config/tcp_clients.json`:
```json
{
  "tcp_clients": [{
    "id": "my_sensors",
    "protocol_type": "multi",
    "data_channels": [
      "Sensor_1",
      "Sensor_2",
      "Sensor_3"
    ],
    "host": "127.0.0.1",
    "port": 65432,
    "auto_connect": true
  }]
}
```

**5. Access data in C++:**
```cpp
float s1 = dataStore->GetValue("Sensor_1");
float s2 = dataStore->GetValue("Sensor_2");
float s3 = dataStore->GetValue("Sensor_3");
```

---

## 📖 Protocol Formats

### Single-Channel
```
Format: "value\n"
Example: "1.234\n5.678\n"
```

### Multi-Channel
```
Format: "$START,val1,val2,...,valN,$END"
Example: "$START,1.234,5.678,9.012,$END"
```

---

## 🔧 Configuration Fields

| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Unique client identifier |
| `protocol_type` | "single" \| "multi" | Protocol format |
| `data_channel` | string | DataStore channel (single-channel only) |
| `data_channels` | array | DataStore channels (multi-channel only) |
| `host` | string | Server IP address |
| `port` | number | Server port (1024-65535) |
| `auto_connect` | boolean | Connect on startup |
| `auto_reconnect` | boolean | Auto-reconnect on disconnect |

---

## 💡 Use Cases

### When to use Single-Channel:
- ✅ One value from one sensor
- ✅ Simple data streams
- ✅ Minimal network traffic
- ✅ Easy to debug

### When to use Multi-Channel:
- ✅ Multiple related measurements
- ✅ Synchronized timestamps needed
- ✅ Sensor arrays (temperature, pressure, etc.)
- ✅ Complex devices (like SiPhOG)

---

## 🧪 Testing

### Test with provided Python client:
```bash
cd C:\SOFTWARE\SiphogSW
python test_client_v08.py
```

### Monitor console output:
```
[TcpClientManager] Connected client: my_sensor (single mode)
[TcpDataClient] Connected to 127.0.0.1:8888
```

---

## 📦 Example: SiPhOG Server (Multi-Channel)

The FNW_SERVER_V08 is a complete working example:

**Server:** `C:\SOFTWARE\SiphogSW\FNW_SERVER_V08_test_plot.py`
- 6 channels
- 200 Hz update rate
- Hardware integration
- GUI monitoring

**Configuration:** Already in `config/tcp_clients.json`
```json
{
  "id": "siphog_01",
  "protocol_type": "multi",
  "data_channels": [
    "SLED_Current_mA",
    "Photo_Current_uA",
    "SLED_Temp_C",
    "Target_SAG_PWR_V",
    "SAG_PWR_V",
    "TEC_Current_mA"
  ]
}
```

---

## ❓ Common Questions

**Q: Can I mix single and multi-channel clients?**
A: Yes! The TcpClientManager supports multiple clients with different protocols.

**Q: What happens if my server sends the wrong number of channels?**
A: The client logs a warning but continues. Make sure channel count matches config.

**Q: Can I change channel names at runtime?**
A: No, channels are configured at startup. Restart bobcat_engine to reload config.

**Q: What's the maximum update rate?**
A: Tested up to 200 Hz. Higher rates depend on network and processing capacity.

**Q: How do I debug connection issues?**
A: Check console output, verify firewall settings, use `netstat -an | findstr PORT`

---

## 📞 Support

For detailed information, see:
- **TCP_SERVER_GUIDE.md** - Complete technical documentation
- **client_config_examples.json** - More configuration examples
- **FNW_SERVER_V08_test_plot.py** - Working reference implementation

---

## 🔄 Data Flow Diagram

```
Your Hardware/Sensor
        ↓
   Your Python Server
   (server_template_*.py)
        ↓
   TCP/IP Network
        ↓
   TcpDataClient
   (parses protocol)
        ↓
   TcpClientManager
   (routes to channels)
        ↓
   DataStore
   (stores values)
        ↓
   Your Application
   (reads from DataStore)
```

---

## ⚡ Performance Tips

1. **Adjust update rate** to match your needs (10-200 Hz typical)
2. **Use multi-channel** when sending related data together
3. **Enable auto_reconnect** for robustness
4. **Monitor console** for connection status
5. **Test locally first** (127.0.0.1) before remote connections

---

## 🎯 Summary

1. Choose protocol type (single or multi-channel)
2. Copy appropriate template
3. Customize data generation function
4. Update config/tcp_clients.json
5. Run server and bobcat_engine
6. Access data via DataStore

That's it! You now have real-time TCP data streaming into your application. 🎉

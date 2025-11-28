# Lighting Manager Integration Plan

## Overview

Integrate a LightingManager into bobcat_engine to control CCS PD3-10024-8-EI(A) LED controllers via TCP/IP.

## Architecture

Following the existing manager patterns in the codebase:

```
LightingManager
    ├── ILightingController (interface)
    │   └── CCSPD3Adapter (concrete implementation)
    ├── LightingJsonLoader (configuration loader)
    └── LightingChannel (channel configuration)
```

## File Structure

```
include/lighting/
    ├── LightingManager.h
    ├── ILightingController.h
    ├── CCSPD3Adapter.h
    ├── LightingJsonLoader.h
    └── LightingTypes.h

src/lighting/
    ├── LightingManager.cpp
    ├── CCSPD3Adapter.cpp
    └── LightingJsonLoader.cpp

config/
    └── lighting_config.json
```

---

## 1. LightingTypes.h - Data Structures

```cpp
// include/lighting/LightingTypes.h
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Lighting {

// Channel configuration
struct ChannelConfig {
    int channelNumber;          // 0-7 (L1-L8)
    std::string name;           // e.g., "Collimate", "Red", "Green", "Blue"
    std::string color;          // Optional: color identifier
    int defaultIntensity;       // Default intensity 0-255
    bool enabled;               // Channel enabled/disabled
};

// Controller device configuration
struct ControllerConfig {
    std::string deviceId;       // Unique device identifier
    std::string name;           // Display name
    std::string controllerType; // "CCS_PD3" etc.
    std::string ipAddress;      // IP address
    int port;                   // TCP port (default 40001)
    bool enabled;               // Device enabled/disabled
    std::vector<ChannelConfig> channels;
};

// Channel status
struct ChannelStatus {
    int channelNumber;
    std::string name;
    int intensity;              // Current intensity 0-255
    int mode;                   // Lighting mode (0=continuous, 1-10=strobe)
    bool isOn;                  // ON/OFF state
};

// Controller status
struct ControllerStatus {
    std::string deviceId;
    bool connected;
    bool hasError;
    int errorCode;
    std::vector<ChannelStatus> channelStatuses;
};

// Operation result
enum class LightingResult {
    Success,
    NotConnected,
    InvalidChannel,
    InvalidIntensity,
    CommunicationError,
    DeviceNotFound,
    Timeout
};

} // namespace Lighting
```

---

## 2. ILightingController.h - Hardware Interface

```cpp
// include/lighting/ILightingController.h
#pragma once
#include "LightingTypes.h"
#include <memory>
#include <string>

namespace Lighting {

class ILightingController {
public:
    virtual ~ILightingController() = default;

    // Lifecycle
    virtual bool Initialize(const ControllerConfig& config) = 0;
    virtual bool Connect() = 0;
    virtual bool Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    // Channel Control
    virtual LightingResult SetChannelIntensity(int channel, int intensity) = 0;
    virtual LightingResult SetChannelOn(int channel, bool on) = 0;
    virtual LightingResult SetAllChannelsIntensity(int intensity) = 0;
    virtual LightingResult SetAllChannelsOff() = 0;

    // Status
    virtual ChannelStatus GetChannelStatus(int channel) = 0;
    virtual ControllerStatus GetControllerStatus() = 0;
    virtual LightingResult CheckError() = 0;

    // Information
    virtual std::string GetDeviceId() const = 0;
    virtual std::string GetName() const = 0;
    virtual std::string GetControllerType() const = 0;
    virtual std::string GetLastError() const = 0;
};

} // namespace Lighting
```

---

## 3. CCSPD3Adapter.h - CCS PD3 Implementation

```cpp
// include/lighting/CCSPD3Adapter.h
#pragma once
#include "ILightingController.h"
#include <mutex>

#ifdef _WIN32
#include <WinSock2.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

namespace Lighting {

class CCSPD3Adapter : public ILightingController {
public:
    CCSPD3Adapter();
    ~CCSPD3Adapter() override;

    // ILightingController interface
    bool Initialize(const ControllerConfig& config) override;
    bool Connect() override;
    bool Disconnect() override;
    bool IsConnected() const override;

    LightingResult SetChannelIntensity(int channel, int intensity) override;
    LightingResult SetChannelOn(int channel, bool on) override;
    LightingResult SetAllChannelsIntensity(int intensity) override;
    LightingResult SetAllChannelsOff() override;

    ChannelStatus GetChannelStatus(int channel) override;
    ControllerStatus GetControllerStatus() override;
    LightingResult CheckError() override;

    std::string GetDeviceId() const override;
    std::string GetName() const override;
    std::string GetControllerType() const override { return "CCS_PD3"; }
    std::string GetLastError() const override;

private:
    // Protocol helpers
    std::string CalculateChecksum(const std::string& data);
    std::string BuildCommand(const std::string& channel,
                             const std::string& instruction,
                             const std::string& data = "");
    std::string SendCommand(const std::string& command);
    bool ParseStatusResponse(const std::string& response, ChannelStatus& status);
    bool ParseErrorResponse(const std::string& response, int& errorCode);

    // Configuration
    ControllerConfig m_config;

    // Connection
    SOCKET m_socket;
    bool m_connected;
    std::string m_lastError;
    mutable std::mutex m_mutex;

    // Constants
    static constexpr int DEFAULT_PORT = 40001;
    static constexpr int TIMEOUT_MS = 5000;
};

} // namespace Lighting
```

---

## 4. LightingManager.h - Main Manager Class

```cpp
// include/lighting/LightingManager.h
#pragma once
#include "ILightingController.h"
#include "LightingTypes.h"
#include <map>
#include <memory>
#include <vector>
#include <string>

namespace Lighting {

class LightingManager {
public:
    LightingManager();
    ~LightingManager();

    // Device Management
    bool AddController(const ControllerConfig& config);
    bool RemoveController(const std::string& deviceId);
    ILightingController* GetController(const std::string& deviceId);
    std::vector<std::string> GetControllerIds() const;

    // Connection Management
    bool Connect(const std::string& deviceId);
    bool Disconnect(const std::string& deviceId);
    int ConnectAll();
    int ConnectEnabledDevices();
    void DisconnectAll();
    bool IsConnected(const std::string& deviceId) const;

    // Channel Control by Device
    LightingResult SetIntensity(const std::string& deviceId, int channel, int intensity);
    LightingResult SetIntensityByName(const std::string& deviceId,
                                       const std::string& channelName,
                                       int intensity);
    LightingResult SetIntensityPercent(const std::string& deviceId, int channel, double percent);
    LightingResult SetOn(const std::string& deviceId, int channel, bool on);
    LightingResult SetAllOff(const std::string& deviceId);
    LightingResult SetAllIntensity(const std::string& deviceId, int intensity);

    // Global Control (all controllers)
    void AllOff();

    // Status
    ChannelStatus GetChannelStatus(const std::string& deviceId, int channel);
    ControllerStatus GetControllerStatus(const std::string& deviceId);
    std::vector<ControllerStatus> GetAllStatus();

    // Channel Name Resolution
    int GetChannelByName(const std::string& deviceId, const std::string& name) const;
    std::string GetChannelName(const std::string& deviceId, int channel) const;
    std::vector<ChannelConfig> GetChannelConfigs(const std::string& deviceId) const;

private:
    std::unique_ptr<ILightingController> CreateController(const std::string& type);

    std::map<std::string, std::unique_ptr<ILightingController>> m_controllers;
    std::map<std::string, ControllerConfig> m_configs;
};

} // namespace Lighting
```

---

## 5. LightingJsonLoader.h - Configuration Loader

```cpp
// include/lighting/LightingJsonLoader.h
#pragma once
#include "LightingManager.h"
#include <string>

namespace Lighting {

class LightingJsonLoader {
public:
    static int LoadFromFile(LightingManager& manager, const std::string& filepath);
    static bool SaveToFile(const LightingManager& manager, const std::string& filepath);

private:
    static std::vector<ControllerConfig> ParseJson(const std::string& jsonContent);
    static ControllerConfig ParseControllerConfig(const nlohmann::json& j);
    static ChannelConfig ParseChannelConfig(const nlohmann::json& j);
};

} // namespace Lighting
```

---

## 6. JSON Configuration Format

```json
// config/lighting_config.json
{
  "lighting_controllers": [
    {
      "device_id": "led-controller-1",
      "name": "Main LED Controller",
      "controller_type": "CCS_PD3",
      "ip_address": "192.168.0.2",
      "port": 40001,
      "enabled": true,
      "channels": [
        {
          "channel": 0,
          "name": "Collimate",
          "color": "white",
          "default_intensity": 128,
          "enabled": true
        },
        {
          "channel": 1,
          "name": "Red",
          "color": "red",
          "default_intensity": 128,
          "enabled": true
        },
        {
          "channel": 2,
          "name": "Green",
          "color": "green",
          "default_intensity": 128,
          "enabled": true
        },
        {
          "channel": 3,
          "name": "Blue",
          "color": "blue",
          "default_intensity": 128,
          "enabled": true
        },
        {
          "channel": 4,
          "name": "L5",
          "color": "",
          "default_intensity": 0,
          "enabled": false
        },
        {
          "channel": 5,
          "name": "L6",
          "color": "",
          "default_intensity": 0,
          "enabled": false
        },
        {
          "channel": 6,
          "name": "L7",
          "color": "",
          "default_intensity": 0,
          "enabled": false
        },
        {
          "channel": 7,
          "name": "L8",
          "color": "",
          "default_intensity": 0,
          "enabled": false
        }
      ]
    }
  ]
}
```

---

## 7. ApplicationContext Integration

```cpp
// include/context/ApplicationContext.h
// Add to existing struct:

struct ApplicationContext {
    // ... existing managers ...

    Lighting::LightingManager* lightingManager = nullptr;  // ADD THIS

    // ... rest of struct ...
};
```

---

## 8. Application.cpp Integration

```cpp
// Application.cpp - Add initialization method

void Application::initializeLightingManager() {
    std::cout << "[Application] Initializing Lighting manager..." << std::endl;

    try {
        // 1. Create manager
        m_lightingManager = std::make_unique<Lighting::LightingManager>();
        m_context.lightingManager = m_lightingManager.get();
        m_logManager->Info("LightingManager created", "Application");

        // 2. Load JSON config (try local first, then default)
        std::string configPath;
        int loadedCount = 0;

        loadedCount = Lighting::LightingJsonLoader::LoadFromFile(
            *m_lightingManager, "config/lighting_config.local.json");

        if (loadedCount > 0) {
            configPath = "config/lighting_config.local.json";
            m_logManager->Info("Loading Lighting config from LOCAL file", "Application");
        } else {
            loadedCount = Lighting::LightingJsonLoader::LoadFromFile(
                *m_lightingManager, "config/lighting_config.json");

            if (loadedCount > 0) {
                configPath = "config/lighting_config.json";
                m_logManager->Info("Loading Lighting config from DEFAULT file", "Application");
            } else {
                m_logManager->Warning(
                    "No Lighting config found - Lighting control not configured",
                    "Application");
                return;
            }
        }

        m_logManager->Info(
            "Loaded " + std::to_string(loadedCount) + " lighting controller(s) from " + configPath,
            "Application");

        // 3. Deferred connection (happens in background)
        m_logManager->Info("Lighting device connection deferred to background thread", "Application");

    } catch (const std::exception& e) {
        m_logManager->Error(
            std::string("Lighting manager initialization failed: ") + e.what(),
            "Application"
        );
    }
}

// Add to initializeManagers():
void Application::initializeManagers() {
    // ... existing initializations ...

    initializeLightingManager();  // ADD THIS

    // ... rest of initializations ...
}

// Add to connectHardwareAsync():
void Application::connectHardwareAsync() {
    // ... existing connections ...

    // Connect lighting controllers
    if (m_lightingManager) {
        int connected = m_lightingManager->ConnectEnabledDevices();
        m_logManager->Info(
            "Connected " + std::to_string(connected) + " lighting controller(s)",
            "Application");
    }
}

// Add member variable to Application.h:
private:
    std::unique_ptr<Lighting::LightingManager> m_lightingManager;
```

---

## 9. CMakeLists.txt Integration

```cmake
# Add to existing CMakeLists.txt

# Lighting Manager sources
set(LIGHTING_SOURCES
    src/lighting/LightingManager.cpp
    src/lighting/CCSPD3Adapter.cpp
    src/lighting/LightingJsonLoader.cpp
)

set(LIGHTING_HEADERS
    include/lighting/LightingManager.h
    include/lighting/ILightingController.h
    include/lighting/CCSPD3Adapter.h
    include/lighting/LightingJsonLoader.h
    include/lighting/LightingTypes.h
)

# Add to main target sources
target_sources(bobcat_engine PRIVATE
    ${LIGHTING_SOURCES}
    ${LIGHTING_HEADERS}
)

# Include directory (if not already included)
target_include_directories(bobcat_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lighting
)
```

---

## 10. Implementation Order

### Phase 1: Core Types & Interface (Day 1)
1. Create `include/lighting/` directory
2. Implement `LightingTypes.h`
3. Implement `ILightingController.h`

### Phase 2: CCS PD3 Adapter (Day 1-2)
1. Implement `CCSPD3Adapter.h`
2. Implement `CCSPD3Adapter.cpp`
   - Socket connection
   - Command building with checksum
   - Response parsing
3. Test standalone with Python test script

### Phase 3: Manager & Loader (Day 2)
1. Implement `LightingManager.h`
2. Implement `LightingManager.cpp`
3. Implement `LightingJsonLoader.h`
4. Implement `LightingJsonLoader.cpp`
5. Create `config/lighting_config.json`

### Phase 4: Application Integration (Day 3)
1. Update `ApplicationContext.h`
2. Update `Application.h` - add member variable
3. Update `Application.cpp`:
   - Add `initializeLightingManager()`
   - Add to `initializeManagers()`
   - Add to `connectHardwareAsync()`
4. Update `CMakeLists.txt`

### Phase 5: Testing (Day 3)
1. Build and compile
2. Test connection
3. Test channel control
4. Test status retrieval

---

## 11. Usage Examples

```cpp
// Get manager from context
auto* lightingManager = context.lightingManager;

// Connect to all enabled controllers
lightingManager->ConnectEnabledDevices();

// Set channel by number (0-7)
lightingManager->SetIntensity("led-controller-1", 0, 128);  // Collimate at 50%

// Set channel by name
lightingManager->SetIntensityByName("led-controller-1", "Red", 200);

// Set by percentage
lightingManager->SetIntensityPercent("led-controller-1", 2, 75.0);  // Green at 75%

// Turn off specific channel
lightingManager->SetOn("led-controller-1", 3, false);  // Blue off

// Turn off all channels on a controller
lightingManager->SetAllOff("led-controller-1");

// Turn off all controllers
lightingManager->AllOff();

// Get status
auto status = lightingManager->GetControllerStatus("led-controller-1");
for (const auto& ch : status.channelStatuses) {
    std::cout << ch.name << ": " << ch.intensity << " ("
              << (ch.isOn ? "ON" : "OFF") << ")" << std::endl;
}
```

---

## 12. Future Enhancements (Not in Scope)

- SequenceEngine command handler (LightingCommandHandler)
- UI page for lighting control
- Strobe mode support
- Multi-controller synchronization
- Presets/scenes

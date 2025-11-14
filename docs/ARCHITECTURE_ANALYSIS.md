# Bobcat Engine - Architecture Analysis

## Current Architecture Review

### Current Initialization Flow

```
Application::run()
  └─> Application::initialize()
      ├─> Create QApplication
      ├─> Load fonts & theme
      ├─> initializeManagers() [BLOCKING - Takes 5-10 seconds]
      │   ├─> initializeLogManager()
      │   ├─> initializeDataStore()
      │   ├─> initializeTcpClientManager()
      │   ├─> initializeCameraManager()        [SLOW: Camera connection ~2-3s]
      │   ├─> initializeMotionManager()        [SLOW: Motion controller connection ~2s]
      │   ├─> initializeIOManager()            [SLOW: IO device connection ~1s]
      │   ├─> initializePneumaticManager()
      │   ├─> initializeTECManager()
      │   ├─> initializePowerSupplySystem()
      │   ├─> initializeSMUSystem()
      │   ├─> initializeAuxWindowManager()
      │   ├─> initializeProductPositionManager()
      │   ├─> initializeSequenceEngine()
      │   ├─> initializeScanController()
      │   └─> initializeCoordinateSystemManager()
      │
      └─> createMainWindow()                   [UI finally appears]
```

### Current Problems

1. **Blocking Initialization**: All managers initialize synchronously before UI appears
2. **No Interface Abstraction**: Direct class dependencies everywhere
3. **Tight Coupling**: ApplicationContext is a giant struct with raw pointers
4. **Manual Registration**: Each manager manually added to context
5. **Order Dependencies**: Implicit initialization order (e.g., SequenceEngine must be last)
6. **No Lifecycle Management**: No unified start/stop/pause interface

---

## Current Dependency Graph

```
┌─────────────────────────────────────────────────────────────┐
│                    ApplicationContext                        │
│  (Dumb struct with 15+ raw pointers)                        │
└─────────────────────────────────────────────────────────────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
   ┌────▼────┐        ┌────▼────┐        ┌────▼────┐
   │ Camera  │        │ Motion  │        │   I/O   │
   │ Manager │        │ Manager │        │ Manager │
   └─────────┘        └─────────┘        └─────────┘
        │                   │                   │
        └───────────────────┼───────────────────┘
                            │
                      ┌─────▼──────┐
                      │  Sequence  │
                      │   Engine   │
                      │ (depends on│
                      │ EVERYTHING)│
                      └────────────┘
```

### Key Issues:
- **Sequence Engine depends on ALL managers** (tight coupling)
- **No abstraction layer** between managers and context
- **Difficult to test** individual components in isolation
- **Can't lazy-load** or defer non-critical managers

---

## Proposed Architecture: Interface-Based Design

### 1. Core Interfaces

```cpp
// include/core/IManager.h
#pragma once
#include <string>
#include <functional>

enum class ManagerPriority {
    CRITICAL = 0,   // Must load before UI (LogManager, DataStore)
    HIGH = 1,       // Should load before UI (MotionManager, CameraManager)
    NORMAL = 2,     // Can load in background (TEC, PSU, SMU)
    LOW = 3         // Can lazy-load (CoordinateSystem, Scanning)
};

enum class ManagerState {
    UNINITIALIZED,
    INITIALIZING,
    INITIALIZED,
    RUNNING,
    PAUSED,
    STOPPED,
    ERROR
};

class IManager {
public:
    virtual ~IManager() = default;

    // Lifecycle
    virtual bool Initialize() = 0;
    virtual bool Start() = 0;
    virtual bool Stop() = 0;
    virtual void Shutdown() = 0;

    // State management
    virtual ManagerState GetState() const = 0;
    virtual std::string GetName() const = 0;
    virtual ManagerPriority GetPriority() const = 0;

    // Dependencies
    virtual std::vector<std::string> GetDependencies() const { return {}; }

    // Optional: Pause/Resume for managers that support it
    virtual bool Pause() { return true; }
    virtual bool Resume() { return true; }

    // Progress reporting for async initialization
    using ProgressCallback = std::function<void(int percent, std::string message)>;
    virtual void SetProgressCallback(ProgressCallback callback) {}
};
```

### 2. Manager Registry

```cpp
// include/core/ManagerRegistry.h
#pragma once
#include "IManager.h"
#include <memory>
#include <map>
#include <vector>

class ManagerRegistry {
public:
    static ManagerRegistry& Instance();

    // Registration
    void RegisterManager(const std::string& name,
                        std::unique_ptr<IManager> manager);

    // Retrieval
    IManager* GetManager(const std::string& name);

    template<typename T>
    T* GetManager(const std::string& name) {
        return dynamic_cast<T*>(GetManager(name));
    }

    // Lifecycle management
    bool InitializeAll(bool async = false);
    bool InitializeCritical();  // Only CRITICAL priority
    bool InitializeByPriority(ManagerPriority maxPriority);

    bool StartAll();
    bool StopAll();
    void ShutdownAll();

    // Query
    std::vector<std::string> GetManagerNames() const;
    std::vector<IManager*> GetManagersByPriority(ManagerPriority priority);
    bool IsManagerInitialized(const std::string& name) const;

    // Dependency resolution
    bool CheckDependencies(const std::string& name);
    std::vector<std::string> GetInitializationOrder();

private:
    ManagerRegistry() = default;
    std::map<std::string, std::unique_ptr<IManager>> m_managers;
};
```

### 3. Example Manager Implementation

```cpp
// include/camera/CameraManager.h (refactored)
#pragma once
#include "core/IManager.h"

class CameraManager : public IManager {
public:
    // IManager interface
    bool Initialize() override;
    bool Start() override;
    bool Stop() override;
    void Shutdown() override;

    ManagerState GetState() const override { return m_state; }
    std::string GetName() const override { return "CameraManager"; }
    ManagerPriority GetPriority() const override { return ManagerPriority::HIGH; }

    std::vector<std::string> GetDependencies() const override {
        return {"LogManager", "DataStore"};
    }

    // Camera-specific API
    void ConnectAllCameras();
    void StartGrabbingAll();
    // ... existing camera methods ...

private:
    ManagerState m_state = ManagerState::UNINITIALIZED;
    // ... existing members ...
};
```

### 4. Refactored Application

```cpp
// Application.cpp (simplified)
#include "core/ManagerRegistry.h"

bool Application::initialize() {
    // 1. Create QApplication & UI essentials
    m_qApplication = std::make_unique<QApplication>(m_argc, m_argv);
    setupThemeAndFonts();

    // 2. Register ALL managers (fast - no initialization yet)
    registerManagers();

    // 3. Initialize ONLY critical managers (fast ~100ms)
    auto& registry = ManagerRegistry::Instance();
    if (!registry.InitializeCritical()) {
        return false;
    }

    // 4. Show UI immediately!
    createMainWindow();
    m_mainWindow->show();

    // 5. Initialize remaining managers in background
    std::thread([&registry]() {
        registry.InitializeByPriority(ManagerPriority::HIGH);
        registry.InitializeByPriority(ManagerPriority::NORMAL);
        registry.InitializeByPriority(ManagerPriority::LOW);
    }).detach();

    return true;
}

void Application::registerManagers() {
    auto& registry = ManagerRegistry::Instance();

    // Critical managers (must be ready before UI)
    registry.RegisterManager("LogManager",
        std::make_unique<LogManager>());
    registry.RegisterManager("DataStore",
        std::make_unique<DataStore>());

    // High priority (load in background, needed soon)
    registry.RegisterManager("CameraManager",
        std::make_unique<CameraManager>());
    registry.RegisterManager("MotionManager",
        std::make_unique<UnifiedMotionManager>());
    registry.RegisterManager("IOManager",
        std::make_unique<EziIOManager>());

    // Normal priority (load when needed)
    registry.RegisterManager("TECManager",
        std::make_unique<TECManager>());
    registry.RegisterManager("PSUManager",
        std::make_unique<PowerSupplyManager>(this));
    registry.RegisterManager("SMUManager",
        std::make_unique<SMUManager>(this));

    // Low priority (lazy load)
    registry.RegisterManager("ScanController",
        std::make_unique<AdaptiveScanController>(...));
}
```

---

## Benefits of Interface-Based Architecture

### 1. **Fast UI Startup**
- UI appears in <1 second
- Critical managers initialize first (~100ms)
- Hardware connections happen in background
- User can interact with UI while managers load

### 2. **Decoupling**
- Managers depend on interfaces, not concrete classes
- Easy to swap implementations (e.g., mock managers for testing)
- Clear dependency contracts

### 3. **Testability**
```cpp
// Easy unit testing
TEST(MotionManagerTest, BasicMovement) {
    auto mockLogger = std::make_unique<MockLogManager>();
    auto motion = std::make_unique<UnifiedMotionManager>();

    ManagerRegistry::Instance().RegisterManager("LogManager", std::move(mockLogger));
    ManagerRegistry::Instance().RegisterManager("MotionManager", std::move(motion));

    EXPECT_TRUE(motion->Initialize());
    EXPECT_TRUE(motion->MoveAbsolute("gantry", "X", 100.0));
}
```

### 4. **Automatic Dependency Resolution**
```cpp
// SequenceEngine declares dependencies
std::vector<std::string> SequenceEngine::GetDependencies() const {
    return {
        "LogManager",
        "MotionManager",
        "IOManager",
        "CameraManager",
        "DataStore"
    };
}

// Registry ensures correct initialization order automatically
auto initOrder = registry.GetInitializationOrder();
// Result: ["LogManager", "DataStore", "MotionManager", "IOManager",
//          "CameraManager", "SequenceEngine"]
```

### 5. **Runtime Control**
```cpp
// Pause/Resume managers during critical operations
void Application::onEmergencyStop() {
    auto motion = registry.GetManager("MotionManager");
    motion->Pause();  // Stop all motion

    auto camera = registry.GetManager("CameraManager");
    camera->Pause();  // Pause grabbing
}

void Application::onResume() {
    registry.ResumeAll();
}
```

### 6. **Plugin Architecture Ready**
```cpp
// Easy to add new managers at runtime
void Application::loadPlugin(const std::string& pluginPath) {
    auto plugin = PluginLoader::Load(pluginPath);
    auto manager = plugin->CreateManager();

    registry.RegisterManager(plugin->GetName(), std::move(manager));
    registry.InitializeManager(plugin->GetName());
}
```

---

## Migration Strategy

### Phase 1: Add Interfaces (Low Risk)
1. Create `IManager` interface
2. Create `ManagerRegistry` class
3. Keep existing code working alongside new system

### Phase 2: Migrate Critical Managers
1. Refactor `LogManager` to implement `IManager`
2. Refactor `DataStore` to implement `IManager`
3. Test thoroughly

### Phase 3: Migrate Hardware Managers
1. Refactor `CameraManager`, `MotionManager`, `IOManager`
2. Add async initialization support
3. Test UI startup time

### Phase 4: Migrate Remaining Managers
1. Refactor remaining managers
2. Remove old `ApplicationContext` struct
3. Complete migration

### Phase 5: Enable Advanced Features
1. Add background initialization
2. Add progress reporting UI
3. Add pause/resume capabilities

---

## Immediate Quick Wins (No Refactoring)

If you want faster UI **NOW** without big refactoring:

### Option A: Defer Hardware Connections
```cpp
void Application::initializeManagers() {
    // Fast initialization (no hardware connections)
    initializeLogManager();
    initializeDataStore();
    initializeTcpClientManager();  // Don't auto-connect

    // Create managers but don't connect
    m_cameraManager = std::make_unique<CameraManager>();
    m_motionManager = std::make_unique<UnifiedMotionManager>();
    m_ioManager = std::make_unique<EziIOManager>();

    // Add to context
    m_context.cameraManager = m_cameraManager.get();
    m_context.motionManager = m_motionManager.get();
    m_context.ioManager = m_ioManager.get();

    // ... other managers (no connections) ...
}

void Application::createMainWindow() {
    m_mainWindow = std::make_unique<MainWindow>(&m_context);
    m_mainWindow->show();

    // NOW connect hardware in background
    QTimer::singleShot(100, [this]() {
        connectHardwareAsync();
    });
}

void Application::connectHardwareAsync() {
    std::thread([this]() {
        m_cameraManager->ConnectAllCameras();
        m_motionManager->ConnectEnabledDevices();
        m_ioManager->ConnectEnabledDevices();
        // ... etc
    }).detach();
}
```

### Option B: Show Splash Screen
```cpp
class SplashScreen : public QWidget {
public:
    void updateProgress(int percent, QString message);
};

bool Application::initialize() {
    m_qApplication = std::make_unique<QApplication>(m_argc, m_argv);

    // Show splash immediately
    auto splash = new SplashScreen();
    splash->show();
    m_qApplication->processEvents();

    // Initialize with progress updates
    splash->updateProgress(10, "Initializing logging...");
    initializeLogManager();

    splash->updateProgress(20, "Connecting cameras...");
    initializeCameraManager();

    // ... etc ...

    splash->updateProgress(100, "Ready!");
    delete splash;

    createMainWindow();
}
```

---

## Recommendation

### Short Term (This Week)
Implement **Option A** above - defer hardware connections until after UI appears.
- **Effort**: 2-3 hours
- **Result**: UI appears in <1 second instead of 5-10 seconds
- **Risk**: Low

### Medium Term (Next Sprint)
Start **Phase 1** of interface migration:
- Create `IManager` interface
- Create `ManagerRegistry`
- Migrate `LogManager` and `DataStore` as proof of concept
- **Effort**: 1-2 days
- **Result**: Foundation for scalable architecture

### Long Term (Next Release)
Complete full migration to interface-based architecture:
- All managers implement `IManager`
- Automatic dependency resolution
- Background initialization
- Plugin support ready
- **Effort**: 1-2 weeks
- **Result**: Professional, maintainable, testable architecture

---

## Conclusion

Your current architecture works but has scalability issues:
- ✅ Simple and straightforward
- ❌ Slow startup
- ❌ Tight coupling
- ❌ Hard to test
- ❌ Hard to extend

Interface-based architecture provides:
- ✅ Fast UI startup (<1 second)
- ✅ Loose coupling
- ✅ Easy testing
- ✅ Easy to extend
- ✅ Professional design

**Start with quick wins, then gradually migrate to better architecture.**

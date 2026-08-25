// ApplicationContext.h
#pragma once

#include <functional>

class CameraManager;
class LogManager;
class DataStore;
class TcpClientManager;
class UnifiedMotionManager;
class EziIOManager;
class PneumaticManager;
class SequenceEngine;
class ProductPositionManager;
class AdaptiveScanController;
class ProductPositionManager;
class TECManager;
class AuxWindowManager;  // ✅ ADD THIS
class PowerSupplyManager;
class SMUManager;  // ✅ ADD SMU Manager
class SMUPollingManager;  // ✅ ADD SMU Polling Manager
class CoordinateSystemManager;
class DeviceUnderTest;
class SafetyWatchManager;
class DeviceLinkManager;
class InterlockManager;
class GdiCameraWindowManager;  // GDI-based camera monitor window
class QtCameraWindow;          // Qt-based camera monitor window
class SwirBeamWindowManager;   // SWIR beam processing window
class ApplicationSettings;  // Application-wide configurable settings
class SafetyEventLog;       // Structured safety event logging
class BeamServerClient;     // Custom TCP server client for beam detection
class CameraConfigManager;  // Camera configuration (per-camera calibration)
class BeamResultBroker;     // Pub-sub broker for beam detection results
class BeamOptimizerManager; // Beam optimization coordinator
class ScanActivityTracker;  // Reference-counted scan-busy broadcaster (drives TopBar indicator)
class ScriptEngine;         // Lua 5.4 scripting engine
class IoWatchManager;       // IO manual-override watch layer
class TransformationMatrixLoader;  // Per-device global<->local frame matrices
class StateSnapshotManager;        // Labeled state snapshot capture (see include/snapshot/)

namespace Lighting {
class LightingManager;
}

struct ApplicationContext
{
  // Hardware managers
  CameraManager* cameraManager = nullptr;
  LogManager* logManager = nullptr;
  DataStore* dataStore = nullptr;
  TcpClientManager* tcpClientManager = nullptr;
  UnifiedMotionManager* motionManager = nullptr;
  EziIOManager* ioManager = nullptr;
  PneumaticManager* pneumaticManager = nullptr;
  SequenceEngine* sequenceEngine = nullptr;
  ProductPositionManager* productPositionManager = nullptr;  // Single instance
  AdaptiveScanController* scanController = nullptr;
  TECManager* tecManager = nullptr;
  AuxWindowManager* auxWindowManager = nullptr;
  PowerSupplyManager* powerSupplyManager = nullptr;
  SMUManager* smuManager = nullptr;  // ✅ ADD SMU Manager
  SMUPollingManager* smuPollingManager = nullptr;  // ✅ ADD SMU Polling Manager
  CoordinateSystemManager* coordinateSystemManager = nullptr;
  DeviceUnderTest* currentDUT = nullptr;  // Current active Device Under Test
  Lighting::LightingManager* lightingManager = nullptr;  // LED lighting controller
  SafetyWatchManager* safetyWatchManager = nullptr;  // Safety monitoring layer
  DeviceLinkManager* deviceLinkManager = nullptr;  // Linked device motion manager
  InterlockManager* interlockManager = nullptr;    // Motion interlock rules
  GdiCameraWindowManager* gdiCameraWindowManager = nullptr;  // GDI camera monitor window
  GdiCameraWindowManager* gdiCameraWindowManager2 = nullptr; // Second GDI camera monitor window
  QtCameraWindow* qtCameraWindow = nullptr;                  // Qt camera monitor window (null until built)
  QtCameraWindow* qtCameraWindow2 = nullptr;                 // Second Qt camera monitor window (null until built)
  // Lazy builders for the two Qt camera monitors. Constructing a
  // QtCameraWindow (side panel, viewport, vision controller, quick-move
  // panel) costs ~0.5s each, so they are built on first use instead of at
  // boot. Call these before reading qtCameraWindow / qtCameraWindow2 from
  // any path that intends to SHOW the window; read the raw pointers only
  // when a null (never-opened) window is an acceptable answer.
  // Set by Application; may be empty in tools that build a partial context.
  std::function<QtCameraWindow*()> ensureQtCameraWindow;
  std::function<QtCameraWindow*()> ensureQtCameraWindow2;
  SwirBeamWindowManager* swirBeamWindowManager = nullptr;    // SWIR beam processing window
  ApplicationSettings* applicationSettings = nullptr;  // Application-wide settings from INI
  SafetyEventLog* safetyEventLog = nullptr;            // Structured safety event logging
  BeamServerClient* beamServerClient = nullptr;        // Custom TCP server for beam detection
  CameraConfigManager* cameraConfigManager = nullptr;  // Camera config (per-camera calibration)
  BeamResultBroker* beamResultBroker = nullptr;          // Beam detection pub-sub broker
  BeamOptimizerManager* beamOptimizerManager = nullptr;  // Beam optimization coordinator
  ScanActivityTracker* scanActivityTracker = nullptr;    // Reference-counted scan-busy broadcaster
  ScriptEngine* scriptEngine = nullptr;                  // Lua 5.4 scripting engine
  IoWatchManager* ioWatchManager = nullptr;              // IO manual-override watch layer
  TransformationMatrixLoader* transformationMatrixLoader = nullptr;  // Per-device global<->local frame matrices
  StateSnapshotManager* stateSnapshotManager = nullptr;              // On-demand labeled state snapshots

  // System configuration
  bool simulationMode = false;  // When true, skip hardware connections
};
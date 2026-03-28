// ApplicationContext.h
#pragma once

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
class GdiCameraWindowManager;  // GDI-based camera monitor window
class SwirBeamWindowManager;   // SWIR beam processing window
class ApplicationSettings;  // Application-wide configurable settings
class SafetyEventLog;       // Structured safety event logging
class BeamServerClient;     // Custom TCP server client for beam detection
class CameraConfigManager;  // Camera configuration (per-camera calibration)
class BeamResultBroker;     // Pub-sub broker for beam detection results
class BeamOptimizerManager; // Beam optimization coordinator

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
  GdiCameraWindowManager* gdiCameraWindowManager = nullptr;  // GDI camera monitor window
  GdiCameraWindowManager* gdiCameraWindowManager2 = nullptr; // Second GDI camera monitor window
  SwirBeamWindowManager* swirBeamWindowManager = nullptr;    // SWIR beam processing window
  ApplicationSettings* applicationSettings = nullptr;  // Application-wide settings from INI
  SafetyEventLog* safetyEventLog = nullptr;            // Structured safety event logging
  BeamServerClient* beamServerClient = nullptr;        // Custom TCP server for beam detection
  CameraConfigManager* cameraConfigManager = nullptr;  // Camera config (per-camera calibration)
  BeamResultBroker* beamResultBroker = nullptr;          // Beam detection pub-sub broker
  BeamOptimizerManager* beamOptimizerManager = nullptr;  // Beam optimization coordinator

  // System configuration
  bool simulationMode = false;  // When true, skip hardware connections
};
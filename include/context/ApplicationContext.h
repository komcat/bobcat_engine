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

  // System configuration
  bool simulationMode = false;  // When true, skip hardware connections
};
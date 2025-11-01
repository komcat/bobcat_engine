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
class CoordinateSystemManager;

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
  CoordinateSystemManager* coordinateSystemManager = nullptr;

  // System configuration
  bool simulationMode = false;  // When true, skip hardware connections
};
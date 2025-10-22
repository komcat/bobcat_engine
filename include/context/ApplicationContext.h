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

struct ApplicationContext
{
  CameraManager* cameraManager = nullptr;
  LogManager* logManager = nullptr;
  DataStore* dataStore = nullptr;
  TcpClientManager* tcpClientManager = nullptr;
  UnifiedMotionManager* motionManager = nullptr;
  EziIOManager* ioManager = nullptr;
  PneumaticManager* pneumaticManager = nullptr;  
  SequenceEngine* sequenceEngine = nullptr;  
  ProductPositionManager* productManager;  
  AdaptiveScanController* scanController = nullptr;  
  ProductPositionManager* productPositionManager = nullptr;  
  TECManager* tecManager = nullptr;   

};
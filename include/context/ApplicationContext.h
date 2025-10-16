// ApplicationContext.h
#pragma once

class CameraManager;
class LogManager;
class DataStore;
class TcpClientManager;
class UnifiedMotionManager;
class EziIOManager;
class PneumaticManager;  // ✅ ADD THIS
class SequenceEngine;  // ✅ ADD THIS

struct ApplicationContext
{
  CameraManager* cameraManager = nullptr;
  LogManager* logManager = nullptr;
  DataStore* dataStore = nullptr;
  TcpClientManager* tcpClientManager = nullptr;
  UnifiedMotionManager* motionManager = nullptr;
  EziIOManager* ioManager = nullptr;
  PneumaticManager* pneumaticManager = nullptr;  // ✅ ADD THIS
  SequenceEngine* sequenceEngine = nullptr;  // ✅ ADD THIS
};
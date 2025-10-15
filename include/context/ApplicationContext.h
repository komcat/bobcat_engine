// ApplicationContext.h
#pragma once

class CameraManager;
class LogManager;  // ✅ NEW
class DataStore;           // ← NEW
class TcpClientManager;    // ← NEW
class UnifiedMotionManager;  // Add this
class EziIOManager;  // Add IO manager

struct ApplicationContext {
  CameraManager* cameraManager = nullptr;
  LogManager* logManager = nullptr;  // ✅ NEW
  DataStore* dataStore = nullptr;              // ← NEW
  TcpClientManager* tcpClientManager = nullptr; // ← NEW
  UnifiedMotionManager* motionManager = nullptr;  // Add this
  EziIOManager* ioManager = nullptr;  // Add IO manager

  bool isValid() const {
    return cameraManager != nullptr && logManager != nullptr;
  }
};
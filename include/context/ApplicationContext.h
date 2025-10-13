// ApplicationContext.h
#pragma once

class CameraManager;
class LogManager;  // ✅ NEW
class DataStore;           // ← NEW
class TcpClientManager;    // ← NEW

struct ApplicationContext {
  CameraManager* cameraManager = nullptr;
  LogManager* logManager = nullptr;  // ✅ NEW
  DataStore* dataStore = nullptr;              // ← NEW
  TcpClientManager* tcpClientManager = nullptr; // ← NEW

  bool isValid() const {
    return cameraManager != nullptr && logManager != nullptr;
  }
};
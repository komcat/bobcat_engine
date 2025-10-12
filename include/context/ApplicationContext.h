// ApplicationContext.h
#pragma once

class CameraManager;
class LogManager;  // ✅ NEW

struct ApplicationContext {
  CameraManager* cameraManager = nullptr;
  LogManager* logManager = nullptr;  // ✅ NEW

  bool isValid() const {
    return cameraManager != nullptr && logManager != nullptr;
  }
};
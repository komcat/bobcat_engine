// ApplicationContext.h
#pragma once

// Forward declaration only
class CameraManager;

/**
 * @brief Central access point to all application managers
 *
 * Provides pointers to shared managers.
 * Owned and populated by the Application class.
 */
struct ApplicationContext {
  CameraManager* cameraManager = nullptr;

  // Validation helper
  bool isValid() const {
    return cameraManager != nullptr;
  }
};
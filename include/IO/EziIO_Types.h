// EziIO_Types.h - Common types and enums for EziIO system
#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Error codes for EziIO operations
 */
enum class EziIOError {
  SUCCESS = 0,
  DEVICE_NOT_FOUND,
  DEVICE_DISCONNECTED,
  OPERATION_FAILED,
  INVALID_PIN,
  NOT_INITIALIZED
};

/**
 * @brief Device configuration structure
 */
struct EziIODeviceConfig {
  int DeviceId;
  std::string Name;
  std::string IpAddress;
  int InputCount;
  int OutputCount;
  bool Enabled = true;

  EziIODeviceConfig()
    : DeviceId(0), Name(""), IpAddress(""), InputCount(0), OutputCount(0), Enabled(true) {
  }

  EziIODeviceConfig(int id, const std::string& name, const std::string& ip,
    int inputs, int outputs, bool enabled = true)
    : DeviceId(id), Name(name), IpAddress(ip),
    InputCount(inputs), OutputCount(outputs), Enabled(enabled) {
  }
};

/**
 * @brief IO Status structure
 * Uses uint64_t to support up to 64 inputs/outputs (e.g., 48 in/48 out)
 */
struct IOStatus {
  uint64_t Inputs = 0;
  uint64_t Latch = 0;
  uint64_t Outputs = 0;
  uint64_t OutputStatus = 0;
  bool Valid = false;
};

/**
 * @brief Helper function to get error string
 */
inline std::string GetErrorString(EziIOError error) {
  switch (error) {
  case EziIOError::SUCCESS:
    return "Success";
  case EziIOError::DEVICE_NOT_FOUND:
    return "Device not found";
  case EziIOError::DEVICE_DISCONNECTED:
    return "Device disconnected";
  case EziIOError::OPERATION_FAILED:
    return "Operation failed";
  case EziIOError::INVALID_PIN:
    return "Invalid pin number";
  case EziIOError::NOT_INITIALIZED:
    return "Not initialized";
  default:
    return "Unknown error";
  }
}
// ErrorCode.h
// Unified error code enumeration for cross-subsystem reporting and safety event logging.
// Existing per-subsystem enums (EziIOError, LightingResult, etc.) remain unchanged.
#pragma once

#include <cstdint>
#include <string>

enum class ErrorCode : uint16_t {
  // 0x0000: General
  Success             = 0x0000,
  Unknown             = 0x0001,
  InvalidParameter    = 0x0002,
  Timeout             = 0x0003,
  NotInitialized      = 0x0004,
  NotConnected        = 0x0005,
  OperationFailed     = 0x0006,
  NullPointer         = 0x0007,
  ConfigLoadFailed    = 0x0008,

  // 0x0100: Motion
  MotionDeviceNotFound      = 0x0100,
  MotionAxisError           = 0x0101,
  MotionHomingFailed        = 0x0102,
  MotionLimitReached        = 0x0103,
  MotionPositionNotFound    = 0x0104,
  MotionCommunicationError  = 0x0105,

  // 0x0200: IO
  IODeviceNotFound       = 0x0200,
  IODeviceDisconnected   = 0x0201,
  IOInvalidPin           = 0x0202,

  // 0x0300: Pneumatic
  PneumSlideNotFound  = 0x0300,
  PneumTimeout        = 0x0301,
  PneumHardwareError  = 0x0302,

  // 0x0400: Camera
  CameraNotFound           = 0x0400,
  CameraGrabFailed         = 0x0401,
  CameraCommunicationError = 0x0402,

  // 0x0500: TEC
  TECDeviceError      = 0x0500,
  TECTemperatureError = 0x0501,

  // 0x0600: PSU
  PSUDeviceError  = 0x0600,
  PSUOverCurrent  = 0x0601,

  // 0x0700: SMU
  SMUDeviceError      = 0x0700,
  SMUMeasurementError = 0x0701,

  // 0x0800: Lighting
  LightingNotConnected      = 0x0800,
  LightingInvalidChannel    = 0x0801,
  LightingCommunicationError = 0x0802,

  // 0x0900: Sequence/Recipe
  SequenceValidationFailed = 0x0900,
  SequenceExecutionFailed  = 0x0901,
  SequenceStoppedByUser    = 0x0902,
  RecipeLoadFailed         = 0x0903,

  // 0x0A00: Vision
  VisionPluginFailed = 0x0A00,
  VisionNoImage      = 0x0A01,

  // 0x0B00: Custom Server
  CustomServerNotConnected    = 0x0B00,
  CustomServerConnectionFailed = 0x0B01,
  CustomServerSendFailed      = 0x0B02,
  CustomServerRecvFailed      = 0x0B03,
  CustomServerProtocolError   = 0x0B04,

  // 0x0F00: Safety
  SafetyFlagRaised   = 0x0F00,
  SafetyCriticalStop = 0x0F01,
  SafetyEStopTriggered = 0x0F02,
  SafetyWatchdogTimeout = 0x0F03,
  SafetyEStopCascadeComplete = 0x0F04,
  SafetyManualEStop = 0x0F05
};

enum class ErrorCategory : uint8_t {
  General   = 0x00,
  Motion    = 0x01,
  IO        = 0x02,
  Pneumatic = 0x03,
  Camera    = 0x04,
  TEC       = 0x05,
  PSU       = 0x06,
  SMU       = 0x07,
  Lighting  = 0x08,
  Sequence  = 0x09,
  Vision       = 0x0A,
  CustomServer = 0x0B,
  Safety       = 0x0F
};

inline ErrorCategory GetErrorCategory(ErrorCode code) {
  return static_cast<ErrorCategory>(static_cast<uint16_t>(code) >> 8);
}

inline std::string ErrorCategoryToString(ErrorCategory cat) {
  switch (cat) {
    case ErrorCategory::General:   return "General";
    case ErrorCategory::Motion:    return "Motion";
    case ErrorCategory::IO:        return "IO";
    case ErrorCategory::Pneumatic: return "Pneumatic";
    case ErrorCategory::Camera:    return "Camera";
    case ErrorCategory::TEC:       return "TEC";
    case ErrorCategory::PSU:       return "PSU";
    case ErrorCategory::SMU:       return "SMU";
    case ErrorCategory::Lighting:  return "Lighting";
    case ErrorCategory::Sequence:  return "Sequence";
    case ErrorCategory::Vision:       return "Vision";
    case ErrorCategory::CustomServer: return "CustomServer";
    case ErrorCategory::Safety:       return "Safety";
    default:                       return "Unknown";
  }
}

inline std::string ErrorCodeToString(ErrorCode code) {
  switch (code) {
    case ErrorCode::Success:                    return "Success";
    case ErrorCode::Unknown:                    return "Unknown";
    case ErrorCode::InvalidParameter:           return "InvalidParameter";
    case ErrorCode::Timeout:                    return "Timeout";
    case ErrorCode::NotInitialized:             return "NotInitialized";
    case ErrorCode::NotConnected:               return "NotConnected";
    case ErrorCode::OperationFailed:            return "OperationFailed";
    case ErrorCode::NullPointer:                return "NullPointer";
    case ErrorCode::ConfigLoadFailed:           return "ConfigLoadFailed";
    case ErrorCode::MotionDeviceNotFound:       return "MotionDeviceNotFound";
    case ErrorCode::MotionAxisError:            return "MotionAxisError";
    case ErrorCode::MotionHomingFailed:         return "MotionHomingFailed";
    case ErrorCode::MotionLimitReached:         return "MotionLimitReached";
    case ErrorCode::MotionPositionNotFound:     return "MotionPositionNotFound";
    case ErrorCode::MotionCommunicationError:   return "MotionCommunicationError";
    case ErrorCode::IODeviceNotFound:           return "IODeviceNotFound";
    case ErrorCode::IODeviceDisconnected:       return "IODeviceDisconnected";
    case ErrorCode::IOInvalidPin:               return "IOInvalidPin";
    case ErrorCode::PneumSlideNotFound:         return "PneumSlideNotFound";
    case ErrorCode::PneumTimeout:               return "PneumTimeout";
    case ErrorCode::PneumHardwareError:         return "PneumHardwareError";
    case ErrorCode::CameraNotFound:             return "CameraNotFound";
    case ErrorCode::CameraGrabFailed:           return "CameraGrabFailed";
    case ErrorCode::CameraCommunicationError:   return "CameraCommunicationError";
    case ErrorCode::TECDeviceError:             return "TECDeviceError";
    case ErrorCode::TECTemperatureError:        return "TECTemperatureError";
    case ErrorCode::PSUDeviceError:             return "PSUDeviceError";
    case ErrorCode::PSUOverCurrent:             return "PSUOverCurrent";
    case ErrorCode::SMUDeviceError:             return "SMUDeviceError";
    case ErrorCode::SMUMeasurementError:        return "SMUMeasurementError";
    case ErrorCode::LightingNotConnected:       return "LightingNotConnected";
    case ErrorCode::LightingInvalidChannel:     return "LightingInvalidChannel";
    case ErrorCode::LightingCommunicationError: return "LightingCommunicationError";
    case ErrorCode::SequenceValidationFailed:   return "SequenceValidationFailed";
    case ErrorCode::SequenceExecutionFailed:    return "SequenceExecutionFailed";
    case ErrorCode::SequenceStoppedByUser:      return "SequenceStoppedByUser";
    case ErrorCode::RecipeLoadFailed:           return "RecipeLoadFailed";
    case ErrorCode::VisionPluginFailed:         return "VisionPluginFailed";
    case ErrorCode::VisionNoImage:              return "VisionNoImage";
    case ErrorCode::CustomServerNotConnected:   return "CustomServerNotConnected";
    case ErrorCode::CustomServerConnectionFailed: return "CustomServerConnectionFailed";
    case ErrorCode::CustomServerSendFailed:     return "CustomServerSendFailed";
    case ErrorCode::CustomServerRecvFailed:     return "CustomServerRecvFailed";
    case ErrorCode::CustomServerProtocolError:  return "CustomServerProtocolError";
    case ErrorCode::SafetyFlagRaised:           return "SafetyFlagRaised";
    case ErrorCode::SafetyCriticalStop:         return "SafetyCriticalStop";
    case ErrorCode::SafetyEStopTriggered:       return "SafetyEStopTriggered";
    case ErrorCode::SafetyWatchdogTimeout:        return "SafetyWatchdogTimeout";
    case ErrorCode::SafetyEStopCascadeComplete: return "SafetyEStopCascadeComplete";
    case ErrorCode::SafetyManualEStop:          return "SafetyManualEStop";
    default:                                    return "Unknown(0x" + std::to_string(static_cast<uint16_t>(code)) + ")";
  }
}

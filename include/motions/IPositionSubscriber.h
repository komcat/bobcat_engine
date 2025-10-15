// include/motions/IPositionSubscriber.h
#pragma once
#include <string>
#include <map>

class IPositionSubscriber {
public:
  virtual ~IPositionSubscriber() = default;

  // Called when positions update (batch update)
  virtual void OnPositionsUpdate(const std::string& deviceName,
    const std::map<std::string, double>& positions) = 0;

  // Optional: Called when motion status changes
  virtual void OnMotionStatusChange(const std::string& deviceName,
    const std::string& axis,
    bool isMoving) {
  }
};
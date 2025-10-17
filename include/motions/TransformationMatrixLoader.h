// TransformationMatrixLoader.h
#pragma once

#include <string>
#include <unordered_map>
#include <array>

class TransformationMatrixLoader
{
public:
  // 3x3 transformation matrix stored as flat array (row-major order)
  using Matrix3x3 = std::array<double, 9>;

  TransformationMatrixLoader() = default;
  ~TransformationMatrixLoader() = default;

  // Load transformation matrices from JSON file
  // Returns true on success, false on failure
  bool LoadFromFile(const std::string& filePath);

  // Get transformation matrix for a specific device
  // Returns nullptr if device not found
  const Matrix3x3* GetMatrix(const std::string& deviceId) const;

  // Check if a device exists in loaded matrices
  bool HasDevice(const std::string& deviceId) const;

  // Get list of all loaded device IDs
  std::vector<std::string> GetDeviceIds() const;

  // Clear all loaded matrices
  void Clear();

private:
  // Map of DeviceId -> Transformation Matrix
  std::unordered_map<std::string, Matrix3x3> m_matrices;
};
// TransformationMatrixLoader.cpp
#include "TransformationMatrixLoader.h"
#include <fstream>
#include <iostream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

bool TransformationMatrixLoader::LoadFromFile(const std::string& filePath)
{
  // Clear existing data
  Clear();

  // Open and read file
  QFile file(QString::fromStdString(filePath));
  if (!file.open(QIODevice::ReadOnly)) {
    std::cerr << "[TransformationMatrixLoader] Failed to open file: " << filePath << std::endl;
    return false;
  }

  QByteArray data = file.readAll();
  file.close();

  // Parse JSON
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    std::cerr << "[TransformationMatrixLoader] JSON parse error: "
      << parseError.errorString().toStdString() << std::endl;
    return false;
  }

  if (!doc.isArray()) {
    std::cerr << "[TransformationMatrixLoader] JSON root is not an array" << std::endl;
    return false;
  }

  QJsonArray devices = doc.array();

  // Parse each device entry
  for (const QJsonValue& value : devices) {
    if (!value.isObject()) {
      continue;
    }

    QJsonObject deviceObj = value.toObject();

    // Get DeviceId
    if (!deviceObj.contains("DeviceId")) {
      std::cerr << "[TransformationMatrixLoader] Missing DeviceId in entry" << std::endl;
      continue;
    }

    std::string deviceId = deviceObj["DeviceId"].toString().toStdString();

    // Get Matrix object
    if (!deviceObj.contains("Matrix") || !deviceObj["Matrix"].isObject()) {
      std::cerr << "[TransformationMatrixLoader] Missing or invalid Matrix for device: "
        << deviceId << std::endl;
      continue;
    }

    QJsonObject matrixObj = deviceObj["Matrix"].toObject();

    // Extract matrix elements (3x3 matrix stored row-major)
    Matrix3x3 matrix;
    matrix[0] = matrixObj["M11"].toDouble(0.0);  // Row 1
    matrix[1] = matrixObj["M12"].toDouble(0.0);
    matrix[2] = matrixObj["M13"].toDouble(0.0);
    matrix[3] = matrixObj["M21"].toDouble(0.0);  // Row 2
    matrix[4] = matrixObj["M22"].toDouble(0.0);
    matrix[5] = matrixObj["M23"].toDouble(0.0);
    matrix[6] = matrixObj["M31"].toDouble(0.0);  // Row 3
    matrix[7] = matrixObj["M32"].toDouble(0.0);
    matrix[8] = matrixObj["M33"].toDouble(0.0);

    // Store in map
    m_matrices[deviceId] = matrix;

    std::cout << "[TransformationMatrixLoader] Loaded matrix for device: " << deviceId << std::endl;
  }

  std::cout << "[TransformationMatrixLoader] Successfully loaded " << m_matrices.size()
    << " transformation matrices" << std::endl;

  return !m_matrices.empty();
}

const TransformationMatrixLoader::Matrix3x3* TransformationMatrixLoader::GetMatrix(
  const std::string& deviceId) const
{
  auto it = m_matrices.find(deviceId);
  if (it != m_matrices.end()) {
    return &(it->second);
  }
  return nullptr;
}

bool TransformationMatrixLoader::HasDevice(const std::string& deviceId) const
{
  return m_matrices.find(deviceId) != m_matrices.end();
}

std::vector<std::string> TransformationMatrixLoader::GetDeviceIds() const
{
  std::vector<std::string> ids;
  ids.reserve(m_matrices.size());

  for (const auto& [deviceId, matrix] : m_matrices) {
    ids.push_back(deviceId);
  }

  return ids;
}

void TransformationMatrixLoader::Clear()
{
  m_matrices.clear();
}
// CoordinateTransformer.cpp
#include "CoordinateTransformer.h"
#include <iostream>
#include <iomanip>

CoordinateTransformer::Vector6D CoordinateTransformer::TransformGlobalToLocal(
  const Vector6D& globalCoords,
  const Matrix3x3& matrix)
{
  Vector6D result;

  // Transform XYZ using the 3x3 matrix
  MultiplyMatrixVector3(matrix,
    globalCoords.x, globalCoords.y, globalCoords.z,
    result.x, result.y, result.z);

  // UVW pass through unchanged (no transformation)
  result.u = globalCoords.u;
  result.v = globalCoords.v;
  result.w = globalCoords.w;

  return result;
}

void CoordinateTransformer::MultiplyMatrixVector3(
  const Matrix3x3& matrix,
  double inX, double inY, double inZ,
  double& outX, double& outY, double& outZ)
{
  // Matrix is stored in row-major order:
  // [0 1 2]   [M11 M12 M13]
  // [3 4 5] = [M21 M22 M23]
  // [6 7 8]   [M31 M32 M33]

  // Row 1: M11*x + M12*y + M13*z
  outX = matrix[0] * inX + matrix[1] * inY + matrix[2] * inZ;

  // Row 2: M21*x + M22*y + M23*z
  outY = matrix[3] * inX + matrix[4] * inY + matrix[5] * inZ;

  // Row 3: M31*x + M32*y + M33*z
  outZ = matrix[6] * inX + matrix[7] * inY + matrix[8] * inZ;
}

void CoordinateTransformer::PrintMatrix(const Matrix3x3& matrix)
{
  std::cout << "Matrix 3x3:" << std::endl;
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "  [" << matrix[0] << " " << matrix[1] << " " << matrix[2] << "]" << std::endl;
  std::cout << "  [" << matrix[3] << " " << matrix[4] << " " << matrix[5] << "]" << std::endl;
  std::cout << "  [" << matrix[6] << " " << matrix[7] << " " << matrix[8] << "]" << std::endl;
}

void CoordinateTransformer::PrintVector(const Vector6D& vec, const std::string& label)
{
  std::cout << label << ": ["
    << std::fixed << std::setprecision(4)
    << "x=" << vec.x << ", "
    << "y=" << vec.y << ", "
    << "z=" << vec.z << ", "
    << "u=" << vec.u << ", "
    << "v=" << vec.v << ", "
    << "w=" << vec.w << "]" << std::endl;
}
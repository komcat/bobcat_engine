// CoordinateTransformer.h
#pragma once

#include <string> 
#include <array>

class CoordinateTransformer
{
public:
  // 6D vector for coordinates (X, Y, Z, U, V, W)
  struct Vector6D {
    double x;
    double y;
    double z;
    double u;
    double v;
    double w;

    Vector6D() : x(0.0), y(0.0), z(0.0), u(0.0), v(0.0), w(0.0) {}
    Vector6D(double x_, double y_, double z_, double u_, double v_, double w_)
      : x(x_), y(y_), z(z_), u(u_), v(v_), w(w_) {
    }
  };

  // 3x3 transformation matrix (row-major order) - applies to XYZ only
  using Matrix3x3 = std::array<double, 9>;

  CoordinateTransformer() = default;
  ~CoordinateTransformer() = default;

  // Transform global coordinates to local device coordinates
  // matrix: 3x3 transformation matrix (applies to XYZ only, UVW pass through unchanged)
  // globalCoords: Input vector in global coordinate system
  // Returns: Transformed vector in local device coordinates
  static Vector6D TransformGlobalToLocal(const Vector6D& globalCoords,
    const Matrix3x3& matrix);

  // Transform local device coordinates to global coordinates
  // Uses the transpose of the matrix (inverse for orthogonal rotation matrices)
  // matrix: 3x3 transformation matrix (applies to XYZ only, UVW pass through unchanged)
  // localCoords: Input vector in local device coordinate system
  // Returns: Transformed vector in global coordinates
  static Vector6D TransformLocalToGlobal(const Vector6D& localCoords,
    const Matrix3x3& matrix);

  // Helper: Print matrix (for debugging)
  static void PrintMatrix(const Matrix3x3& matrix);

  // Helper: Print vector (for debugging)
  static void PrintVector(const Vector6D& vec, const std::string& label = "Vector");

private:
  // Matrix multiplication for XYZ: result = matrix * [x, y, z]
  static void MultiplyMatrixVector3(const Matrix3x3& matrix,
    double inX, double inY, double inZ,
    double& outX, double& outY, double& outZ);
};
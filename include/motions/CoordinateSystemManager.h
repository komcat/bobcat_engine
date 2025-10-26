// CoordinateSystemManager.h
#pragma once

#include "MotionTypes.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

/**
 * @brief Vector3D - Simple 3D vector for axis directions
 */
struct Vector3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vector3D() : x(0), y(0), z(0) {}
    Vector3D(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}

    // Vector operations
    double magnitude() const;
    Vector3D normalize() const;
    Vector3D cross(const Vector3D& other) const;
    double dot(const Vector3D& other) const;
};

/**
 * @brief Point3D - 3D point representation
 */
struct Point3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Point3D() : x(0), y(0), z(0) {}
    Point3D(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}
    Point3D(const PositionStruct& pos) : x(pos.x), y(pos.y), z(pos.z) {}
};

/**
 * @brief CoordinateSystem - Represents a local coordinate frame
 *
 * This defines a custom coordinate system with:
 * - Custom origin point (in global coordinates)
 * - Custom X, Y, Z axis orientations (unit vectors)
 * - Transformation methods between local and global coordinates
 */
struct CoordinateSystem {
    std::string name;                   // System name (e.g., "PartA_System")
    std::string deviceName;             // Device this system applies to (e.g., "gantry-main")

    Point3D origin_global;              // Origin in global coordinates
    Vector3D x_axis_unit;               // X-axis direction (unit vector)
    Vector3D y_axis_unit;               // Y-axis direction (unit vector)
    Vector3D z_axis_unit;               // Z-axis direction (unit vector)

    std::string creation_method;        // "2point" or "3point"
    std::string creation_date;          // ISO 8601 date string

    // Metadata for 2-point method
    struct TwoPointData {
        Point3D point1;
        Point3D point2;
        double perpendicularOffset;     // Offset in mm
        std::string selectedAxis;       // "X" or "Y" - which axis is line(P1-P2)
    };

    // Metadata for 3-point method
    struct ThreePointData {
        Point3D point1;
        Point3D point2;
        Point3D point3;
        std::string selectedAxis;       // "X" or "Y" - which edge is X axis
    };

    TwoPointData twoPointData;
    ThreePointData threePointData;

    CoordinateSystem() = default;
};

/**
 * @brief CoordinateSystemManager - Manages multiple coordinate systems
 *
 * Features:
 * - Create systems using 2-point or 3-point methods
 * - Save/load systems to/from JSON files
 * - Transform coordinates between local and global frames
 * - Manage multiple named systems
 */
class CoordinateSystemManager {
public:
    CoordinateSystemManager();
    ~CoordinateSystemManager();

    // ============================================================================
    // SYSTEM CREATION
    // ============================================================================

    /**
     * @brief Create coordinate system using 2-point + perpendicular offset method
     * @param name System name
     * @param deviceName Device this system applies to
     * @param point1 First point (global coordinates)
     * @param point2 Second point (global coordinates)
     * @param perpendicularOffset Offset perpendicular to line(P1-P2) in mm
     * @param selectedAxis "X" or "Y" - which axis should line(P1-P2) represent
     * @return true if successful
     */
    bool CreateSystemTwoPoint(
        const std::string& name,
        const std::string& deviceName,
        const Point3D& point1,
        const Point3D& point2,
        double perpendicularOffset,
        const std::string& selectedAxis);

    /**
     * @brief Create coordinate system using 3-point rectangle method
     * @param name System name
     * @param deviceName Device this system applies to
     * @param point1 First corner point
     * @param point2 Second corner point
     * @param point3 Third corner point
     * @param selectedAxis "X" or "Y" - which edge should be X axis
     * @return true if successful
     */
    bool CreateSystemThreePoint(
        const std::string& name,
        const std::string& deviceName,
        const Point3D& point1,
        const Point3D& point2,
        const Point3D& point3,
        const std::string& selectedAxis);

    // ============================================================================
    // COORDINATE TRANSFORMATION
    // ============================================================================

    /**
     * @brief Transform from local coordinates to global coordinates
     * @param systemName Name of the coordinate system
     * @param localPos Local position (X, Y, Z in system coordinates)
     * @param globalPos Output global position
     * @return true if successful
     */
    bool LocalToGlobal(
        const std::string& systemName,
        const Point3D& localPos,
        Point3D& globalPos) const;

    /**
     * @brief Transform from global coordinates to local coordinates
     * @param systemName Name of the coordinate system
     * @param globalPos Global position
     * @param localPos Output local position
     * @return true if successful
     */
    bool GlobalToLocal(
        const std::string& systemName,
        const Point3D& globalPos,
        Point3D& localPos) const;

    // ============================================================================
    // SYSTEM MANAGEMENT
    // ============================================================================

    /**
     * @brief Get list of all system names
     */
    std::vector<std::string> GetAllSystemNames() const;

    /**
     * @brief Get list of systems for a specific device
     */
    std::vector<std::string> GetSystemNamesForDevice(const std::string& deviceName) const;

    /**
     * @brief Check if system exists
     */
    bool HasSystem(const std::string& systemName) const;

    /**
     * @brief Get system by name
     */
    bool GetSystem(const std::string& systemName, CoordinateSystem& outSystem) const;

    /**
     * @brief Delete a system
     */
    bool DeleteSystem(const std::string& systemName);

    /**
     * @brief Clear all systems
     */
    void ClearAll();

    // ============================================================================
    // FILE I/O
    // ============================================================================

    /**
     * @brief Save system to JSON file
     * @param systemName System to save
     * @param productName Product name (creates folder config/[ProductName]/)
     * @return true if successful
     */
    bool SaveSystem(const std::string& systemName, const std::string& productName);

    /**
     * @brief Load system from JSON file
     * @param filePath Full path to .system.json file
     * @return true if successful
     */
    bool LoadSystem(const std::string& filePath);

    /**
     * @brief Load all systems from a product folder
     * @param productName Product name (looks in config/[ProductName]/)
     * @return Number of systems loaded
     */
    int LoadAllSystemsForProduct(const std::string& productName);

    /**
     * @brief Get available system files for a product
     * @param productName Product name
     * @return Vector of file paths to .system.json files
     */
    std::vector<std::string> GetAvailableSystemFiles(const std::string& productName) const;

private:
    // Storage: systemName -> CoordinateSystem
    std::map<std::string, CoordinateSystem> m_systems;

    // Helper methods
    std::string getCurrentDateTime() const;
    bool validateSystemName(const std::string& name) const;

    // Geometry helpers for system creation
    Point3D calculateMidpoint(const Point3D& p1, const Point3D& p2) const;
    Vector3D calculateDirection(const Point3D& from, const Point3D& to) const;
    Point3D offsetPointPerpendicular(
        const Point3D& midpoint,
        const Vector3D& lineDirection,
        double offset) const;
    Point3D calculateRectangleCenter(
        const Point3D& p1,
        const Point3D& p2,
        const Point3D& p3) const;

    // JSON serialization
    bool systemToJson(const CoordinateSystem& system, std::string& jsonStr) const;
    bool jsonToSystem(const std::string& jsonStr, CoordinateSystem& system) const;
};

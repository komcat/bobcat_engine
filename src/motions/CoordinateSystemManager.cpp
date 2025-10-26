// CoordinateSystemManager.cpp
#include "CoordinateSystemManager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <algorithm>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================================
// Vector3D Implementation
// ============================================================================

double Vector3D::magnitude() const {
    return std::sqrt(x * x + y * y + z * z);
}

Vector3D Vector3D::normalize() const {
    double mag = magnitude();
    if (mag < 1e-10) {
        return Vector3D(0, 0, 0);
    }
    return Vector3D(x / mag, y / mag, z / mag);
}

Vector3D Vector3D::cross(const Vector3D& other) const {
    return Vector3D(
        y * other.z - z * other.y,
        z * other.x - x * other.z,
        x * other.y - y * other.x
    );
}

double Vector3D::dot(const Vector3D& other) const {
    return x * other.x + y * other.y + z * other.z;
}

// ============================================================================
// CoordinateSystemManager Implementation
// ============================================================================

CoordinateSystemManager::CoordinateSystemManager() {
}

CoordinateSystemManager::~CoordinateSystemManager() {
}

// ============================================================================
// SYSTEM CREATION - 2-Point Method
// ============================================================================

bool CoordinateSystemManager::CreateSystemTwoPoint(
    const std::string& name,
    const std::string& deviceName,
    const Point3D& point1,
    const Point3D& point2,
    double perpendicularOffset,
    const std::string& selectedAxis)
{
    if (!validateSystemName(name)) {
        return false;
    }

    if (selectedAxis != "X" && selectedAxis != "Y") {
        return false;
    }

    CoordinateSystem system;
    system.name = name;
    system.deviceName = deviceName;
    system.creation_method = "2point";
    system.creation_date = getCurrentDateTime();

    // Store metadata
    system.twoPointData.point1 = point1;
    system.twoPointData.point2 = point2;
    system.twoPointData.perpendicularOffset = perpendicularOffset;
    system.twoPointData.selectedAxis = selectedAxis;

    // Calculate origin
    Point3D midpoint = calculateMidpoint(point1, point2);
    Vector3D lineDirection = calculateDirection(point1, point2);
    Point3D origin = offsetPointPerpendicular(midpoint, lineDirection, perpendicularOffset);

    system.origin_global = origin;

    // Define axes based on selection
    Vector3D lineAxisUnit = lineDirection.normalize();

    if (selectedAxis == "X") {
        // Line(P1-P2) is X axis
        system.x_axis_unit = lineAxisUnit;

        // Y axis is perpendicular to X in XY plane
        // For 2D in XY plane: if X = (dx, dy, 0), then Y_perpendicular = (-dy, dx, 0)
        system.y_axis_unit = Vector3D(-lineAxisUnit.y, lineAxisUnit.x, 0).normalize();

        // Z axis is cross product X × Y
        system.z_axis_unit = system.x_axis_unit.cross(system.y_axis_unit).normalize();
    }
    else { // selectedAxis == "Y"
        // Line(P1-P2) is Y axis
        system.y_axis_unit = lineAxisUnit;

        // X axis is perpendicular to Y in XY plane
        system.x_axis_unit = Vector3D(lineAxisUnit.y, -lineAxisUnit.x, 0).normalize();

        // Z axis is cross product X × Y
        system.z_axis_unit = system.x_axis_unit.cross(system.y_axis_unit).normalize();
    }

    m_systems[name] = system;
    return true;
}

// ============================================================================
// SYSTEM CREATION - 3-Point Method
// ============================================================================

bool CoordinateSystemManager::CreateSystemThreePoint(
    const std::string& name,
    const std::string& deviceName,
    const Point3D& point1,
    const Point3D& point2,
    const Point3D& point3,
    const std::string& selectedAxis)
{
    if (!validateSystemName(name)) {
        return false;
    }

    if (selectedAxis != "X" && selectedAxis != "Y") {
        return false;
    }

    CoordinateSystem system;
    system.name = name;
    system.deviceName = deviceName;
    system.creation_method = "3point";
    system.creation_date = getCurrentDateTime();

    // Store metadata
    system.threePointData.point1 = point1;
    system.threePointData.point2 = point2;
    system.threePointData.point3 = point3;
    system.threePointData.selectedAxis = selectedAxis;

    // Calculate rectangle center as origin
    Point3D center = calculateRectangleCenter(point1, point2, point3);
    system.origin_global = center;

    // Calculate edge vectors
    Vector3D edge12 = calculateDirection(point1, point2);
    Vector3D edge23 = calculateDirection(point2, point3);

    // Normalize edges
    Vector3D edge12_unit = edge12.normalize();
    Vector3D edge23_unit = edge23.normalize();

    if (selectedAxis == "X") {
        // Edge 1-2 is X axis
        system.x_axis_unit = edge12_unit;
        system.y_axis_unit = edge23_unit;
    }
    else { // selectedAxis == "Y"
        // Edge 1-2 is Y axis
        system.y_axis_unit = edge12_unit;
        system.x_axis_unit = edge23_unit;
    }

    // Z axis is cross product X × Y
    system.z_axis_unit = system.x_axis_unit.cross(system.y_axis_unit).normalize();

    m_systems[name] = system;
    return true;
}

// ============================================================================
// COORDINATE TRANSFORMATION
// ============================================================================

bool CoordinateSystemManager::LocalToGlobal(
    const std::string& systemName,
    const Point3D& localPos,
    Point3D& globalPos) const
{
    auto it = m_systems.find(systemName);
    if (it == m_systems.end()) {
        return false;
    }

    const CoordinateSystem& system = it->second;

    // Transform: Global = Origin + localX * X_axis + localY * Y_axis + localZ * Z_axis
    globalPos.x = system.origin_global.x +
        localPos.x * system.x_axis_unit.x +
        localPos.y * system.y_axis_unit.x +
        localPos.z * system.z_axis_unit.x;

    globalPos.y = system.origin_global.y +
        localPos.x * system.x_axis_unit.y +
        localPos.y * system.y_axis_unit.y +
        localPos.z * system.z_axis_unit.y;

    globalPos.z = system.origin_global.z +
        localPos.x * system.x_axis_unit.z +
        localPos.y * system.y_axis_unit.z +
        localPos.z * system.z_axis_unit.z;

    return true;
}

bool CoordinateSystemManager::GlobalToLocal(
    const std::string& systemName,
    const Point3D& globalPos,
    Point3D& localPos) const
{
    auto it = m_systems.find(systemName);
    if (it == m_systems.end()) {
        return false;
    }

    const CoordinateSystem& system = it->second;

    // Vector from origin to global point
    Vector3D relativePos(
        globalPos.x - system.origin_global.x,
        globalPos.y - system.origin_global.y,
        globalPos.z - system.origin_global.z
    );

    // Project onto each axis (dot product)
    localPos.x = relativePos.dot(system.x_axis_unit);
    localPos.y = relativePos.dot(system.y_axis_unit);
    localPos.z = relativePos.dot(system.z_axis_unit);

    return true;
}

// ============================================================================
// SYSTEM MANAGEMENT
// ============================================================================

std::vector<std::string> CoordinateSystemManager::GetAllSystemNames() const {
    std::vector<std::string> names;
    for (const auto& pair : m_systems) {
        names.push_back(pair.first);
    }
    return names;
}

std::vector<std::string> CoordinateSystemManager::GetSystemNamesForDevice(const std::string& deviceName) const {
    std::vector<std::string> names;
    for (const auto& pair : m_systems) {
        if (pair.second.deviceName == deviceName) {
            names.push_back(pair.first);
        }
    }
    return names;
}

bool CoordinateSystemManager::HasSystem(const std::string& systemName) const {
    return m_systems.find(systemName) != m_systems.end();
}

bool CoordinateSystemManager::GetSystem(const std::string& systemName, CoordinateSystem& outSystem) const {
    auto it = m_systems.find(systemName);
    if (it == m_systems.end()) {
        return false;
    }
    outSystem = it->second;
    return true;
}

bool CoordinateSystemManager::DeleteSystem(const std::string& systemName) {
    auto it = m_systems.find(systemName);
    if (it == m_systems.end()) {
        return false;
    }
    m_systems.erase(it);
    return true;
}

void CoordinateSystemManager::ClearAll() {
    m_systems.clear();
}

// ============================================================================
// FILE I/O
// ============================================================================

bool CoordinateSystemManager::SaveSystem(const std::string& systemName, const std::string& productName) {
    auto it = m_systems.find(systemName);
    if (it == m_systems.end()) {
        return false;
    }

    // Create directory path: config/[ProductName]/
    std::string dirPath = "config/" + productName;
    fs::create_directories(dirPath);

    // Create file path: config/[ProductName]/[SystemName].system.json
    std::string filePath = dirPath + "/" + systemName + ".system.json";

    // Serialize to JSON
    std::string jsonStr;
    if (!systemToJson(it->second, jsonStr)) {
        return false;
    }

    // Write to file
    std::ofstream outFile(filePath);
    if (!outFile.is_open()) {
        return false;
    }

    outFile << jsonStr;
    outFile.close();

    return true;
}

bool CoordinateSystemManager::LoadSystem(const std::string& filePath) {
    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << inFile.rdbuf();
    std::string jsonStr = buffer.str();
    inFile.close();

    CoordinateSystem system;
    if (!jsonToSystem(jsonStr, system)) {
        return false;
    }

    m_systems[system.name] = system;
    return true;
}

int CoordinateSystemManager::LoadAllSystemsForProduct(const std::string& productName) {
    std::vector<std::string> files = GetAvailableSystemFiles(productName);
    int loadedCount = 0;

    for (const auto& file : files) {
        if (LoadSystem(file)) {
            loadedCount++;
        }
    }

    return loadedCount;
}

std::vector<std::string> CoordinateSystemManager::GetAvailableSystemFiles(const std::string& productName) const {
    std::vector<std::string> files;
    std::string dirPath = "config/" + productName;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        return files;
    }

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            // C++17 compatible check for suffix
            std::string suffix = ".system.json";
            if (filename.size() >= suffix.size() &&
                filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
                files.push_back(entry.path().string());
            }
        }
    }

    return files;
}

// ============================================================================
// HELPER METHODS
// ============================================================================

std::string CoordinateSystemManager::getCurrentDateTime() const {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);

    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

bool CoordinateSystemManager::validateSystemName(const std::string& name) const {
    if (name.empty()) {
        return false;
    }

    // Check for invalid characters (only allow alphanumeric, underscore, hyphen)
    for (char c : name) {
        if (!std::isalnum(c) && c != '_' && c != '-') {
            return false;
        }
    }

    return true;
}

Point3D CoordinateSystemManager::calculateMidpoint(const Point3D& p1, const Point3D& p2) const {
    return Point3D(
        (p1.x + p2.x) / 2.0,
        (p1.y + p2.y) / 2.0,
        (p1.z + p2.z) / 2.0
    );
}

Vector3D CoordinateSystemManager::calculateDirection(const Point3D& from, const Point3D& to) const {
    return Vector3D(
        to.x - from.x,
        to.y - from.y,
        to.z - from.z
    );
}

Point3D CoordinateSystemManager::offsetPointPerpendicular(
    const Point3D& midpoint,
    const Vector3D& lineDirection,
    double offset) const
{
    // Get perpendicular vector in XY plane
    // If line direction is (dx, dy, dz), perpendicular in XY is (-dy, dx, 0)
    Vector3D perpendicular(-lineDirection.y, lineDirection.x, 0);
    Vector3D perpUnit = perpendicular.normalize();

    return Point3D(
        midpoint.x + offset * perpUnit.x,
        midpoint.y + offset * perpUnit.y,
        midpoint.z + offset * perpUnit.z
    );
}

Point3D CoordinateSystemManager::calculateRectangleCenter(
    const Point3D& p1,
    const Point3D& p2,
    const Point3D& p3) const
{
    // For a rectangle with 3 points, the 4th point can be calculated
    // Center = average of all 4 corners
    // If we have P1, P2, P3, then P4 = P1 + P3 - P2
    Point3D p4(
        p1.x + p3.x - p2.x,
        p1.y + p3.y - p2.y,
        p1.z + p3.z - p2.z
    );

    // Center is average of all 4 points
    return Point3D(
        (p1.x + p2.x + p3.x + p4.x) / 4.0,
        (p1.y + p2.y + p3.y + p4.y) / 4.0,
        (p1.z + p2.z + p3.z + p4.z) / 4.0
    );
}

// ============================================================================
// JSON SERIALIZATION
// ============================================================================

bool CoordinateSystemManager::systemToJson(const CoordinateSystem& system, std::string& jsonStr) const {
    try {
        json j;
        j["name"] = system.name;
        j["deviceName"] = system.deviceName;
        j["creation_method"] = system.creation_method;
        j["creation_date"] = system.creation_date;

        // Origin
        j["origin"]["x"] = system.origin_global.x;
        j["origin"]["y"] = system.origin_global.y;
        j["origin"]["z"] = system.origin_global.z;

        // Axes
        j["x_axis"]["x"] = system.x_axis_unit.x;
        j["x_axis"]["y"] = system.x_axis_unit.y;
        j["x_axis"]["z"] = system.x_axis_unit.z;

        j["y_axis"]["x"] = system.y_axis_unit.x;
        j["y_axis"]["y"] = system.y_axis_unit.y;
        j["y_axis"]["z"] = system.y_axis_unit.z;

        j["z_axis"]["x"] = system.z_axis_unit.x;
        j["z_axis"]["y"] = system.z_axis_unit.y;
        j["z_axis"]["z"] = system.z_axis_unit.z;

        // Method-specific data
        if (system.creation_method == "2point") {
            j["method_data"]["point1"]["x"] = system.twoPointData.point1.x;
            j["method_data"]["point1"]["y"] = system.twoPointData.point1.y;
            j["method_data"]["point1"]["z"] = system.twoPointData.point1.z;

            j["method_data"]["point2"]["x"] = system.twoPointData.point2.x;
            j["method_data"]["point2"]["y"] = system.twoPointData.point2.y;
            j["method_data"]["point2"]["z"] = system.twoPointData.point2.z;

            j["method_data"]["perpendicularOffset"] = system.twoPointData.perpendicularOffset;
            j["method_data"]["selectedAxis"] = system.twoPointData.selectedAxis;
        }
        else if (system.creation_method == "3point") {
            j["method_data"]["point1"]["x"] = system.threePointData.point1.x;
            j["method_data"]["point1"]["y"] = system.threePointData.point1.y;
            j["method_data"]["point1"]["z"] = system.threePointData.point1.z;

            j["method_data"]["point2"]["x"] = system.threePointData.point2.x;
            j["method_data"]["point2"]["y"] = system.threePointData.point2.y;
            j["method_data"]["point2"]["z"] = system.threePointData.point2.z;

            j["method_data"]["point3"]["x"] = system.threePointData.point3.x;
            j["method_data"]["point3"]["y"] = system.threePointData.point3.y;
            j["method_data"]["point3"]["z"] = system.threePointData.point3.z;

            j["method_data"]["selectedAxis"] = system.threePointData.selectedAxis;
        }

        jsonStr = j.dump(2); // Pretty print with 2-space indent
        return true;
    }
    catch (...) {
        return false;
    }
}

bool CoordinateSystemManager::jsonToSystem(const std::string& jsonStr, CoordinateSystem& system) const {
    try {
        json j = json::parse(jsonStr);

        system.name = j.at("name").get<std::string>();
        system.deviceName = j.at("deviceName").get<std::string>();
        system.creation_method = j.at("creation_method").get<std::string>();
        system.creation_date = j.at("creation_date").get<std::string>();

        // Origin
        system.origin_global.x = j.at("origin").at("x").get<double>();
        system.origin_global.y = j.at("origin").at("y").get<double>();
        system.origin_global.z = j.at("origin").at("z").get<double>();

        // Axes
        system.x_axis_unit.x = j.at("x_axis").at("x").get<double>();
        system.x_axis_unit.y = j.at("x_axis").at("y").get<double>();
        system.x_axis_unit.z = j.at("x_axis").at("z").get<double>();

        system.y_axis_unit.x = j.at("y_axis").at("x").get<double>();
        system.y_axis_unit.y = j.at("y_axis").at("y").get<double>();
        system.y_axis_unit.z = j.at("y_axis").at("z").get<double>();

        system.z_axis_unit.x = j.at("z_axis").at("x").get<double>();
        system.z_axis_unit.y = j.at("z_axis").at("y").get<double>();
        system.z_axis_unit.z = j.at("z_axis").at("z").get<double>();

        // Method-specific data
        if (system.creation_method == "2point") {
            system.twoPointData.point1.x = j.at("method_data").at("point1").at("x").get<double>();
            system.twoPointData.point1.y = j.at("method_data").at("point1").at("y").get<double>();
            system.twoPointData.point1.z = j.at("method_data").at("point1").at("z").get<double>();

            system.twoPointData.point2.x = j.at("method_data").at("point2").at("x").get<double>();
            system.twoPointData.point2.y = j.at("method_data").at("point2").at("y").get<double>();
            system.twoPointData.point2.z = j.at("method_data").at("point2").at("z").get<double>();

            system.twoPointData.perpendicularOffset = j.at("method_data").at("perpendicularOffset").get<double>();
            system.twoPointData.selectedAxis = j.at("method_data").at("selectedAxis").get<std::string>();
        }
        else if (system.creation_method == "3point") {
            system.threePointData.point1.x = j.at("method_data").at("point1").at("x").get<double>();
            system.threePointData.point1.y = j.at("method_data").at("point1").at("y").get<double>();
            system.threePointData.point1.z = j.at("method_data").at("point1").at("z").get<double>();

            system.threePointData.point2.x = j.at("method_data").at("point2").at("x").get<double>();
            system.threePointData.point2.y = j.at("method_data").at("point2").at("y").get<double>();
            system.threePointData.point2.z = j.at("method_data").at("point2").at("z").get<double>();

            system.threePointData.point3.x = j.at("method_data").at("point3").at("x").get<double>();
            system.threePointData.point3.y = j.at("method_data").at("point3").at("y").get<double>();
            system.threePointData.point3.z = j.at("method_data").at("point3").at("z").get<double>();

            system.threePointData.selectedAxis = j.at("method_data").at("selectedAxis").get<std::string>();
        }

        return true;
    }
    catch (...) {
        return false;
    }
}

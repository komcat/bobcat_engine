// DeviceLinkManager.cpp
#include "DeviceLinkManager.h"
#include "unified_motion_manager.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

DeviceLinkManager::DeviceLinkManager()
{
}

std::string DeviceLinkManager::MakeLinkKey(const std::string& primary, const std::string& linked) const
{
    return primary + "|" + linked;
}

bool DeviceLinkManager::GetPositionInGlobal(const std::string& deviceName,
                                            CoordinateTransformer::Vector6D& outGlobalPos) const
{
    if (!m_motionManager || !m_matrixLoader) {
        std::cerr << "[DeviceLinkManager] Manager or loader not set" << std::endl;
        return false;
    }

    // Get current position in local device coordinates
    PositionStruct localPos = m_motionManager->GetCurrentPosition(deviceName);

    // Get transformation matrix for this device
    const auto* matrix = m_matrixLoader->GetMatrix(deviceName);
    if (!matrix) {
        std::cerr << "[DeviceLinkManager] No transformation matrix for device: " << deviceName << std::endl;
        return false;
    }

    // Convert local to Vector6D
    CoordinateTransformer::Vector6D localVec(
        localPos.x, localPos.y, localPos.z,
        localPos.u, localPos.v, localPos.w
    );

    // Transform from local to global coordinates
    outGlobalPos = CoordinateTransformer::TransformLocalToGlobal(localVec, *matrix);

    return true;
}

bool DeviceLinkManager::RegisterLink(const std::string& primaryDevice, const std::string& linkedDevice)
{
    if (!m_motionManager || !m_matrixLoader) {
        std::cerr << "[DeviceLinkManager] Manager or loader not set" << std::endl;
        return false;
    }

    // Get positions in global coordinates
    CoordinateTransformer::Vector6D primaryGlobal, linkedGlobal;

    if (!GetPositionInGlobal(primaryDevice, primaryGlobal)) {
        std::cerr << "[DeviceLinkManager] Failed to get global position for: " << primaryDevice << std::endl;
        return false;
    }

    if (!GetPositionInGlobal(linkedDevice, linkedGlobal)) {
        std::cerr << "[DeviceLinkManager] Failed to get global position for: " << linkedDevice << std::endl;
        return false;
    }

    // Calculate offset: T0 = primary_global - linked_global
    CoordinateTransformer::Vector6D offset;
    offset.x = primaryGlobal.x - linkedGlobal.x;
    offset.y = primaryGlobal.y - linkedGlobal.y;
    offset.z = primaryGlobal.z - linkedGlobal.z;
    offset.u = primaryGlobal.u - linkedGlobal.u;
    offset.v = primaryGlobal.v - linkedGlobal.v;
    offset.w = primaryGlobal.w - linkedGlobal.w;

    // Generate timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
    std::string timestamp = ss.str();

    // Store the link
    return RegisterLinkWithOffset(primaryDevice, linkedDevice, offset, timestamp);
}

bool DeviceLinkManager::RegisterLinkWithOffset(const std::string& primaryDevice,
                                               const std::string& linkedDevice,
                                               const CoordinateTransformer::Vector6D& offset,
                                               const std::string& timestamp)
{
    DeviceLink link;
    link.primaryDevice = primaryDevice;
    link.linkedDevice = linkedDevice;
    link.registeredOffset = offset;
    link.timestamp = timestamp;

    std::string key = MakeLinkKey(primaryDevice, linkedDevice);
    m_links[key] = link;

    std::cout << "[DeviceLinkManager] Registered link: " << primaryDevice << " -> " << linkedDevice << std::endl;
    std::cout << "  Offset (global): X=" << offset.x << ", Y=" << offset.y << ", Z=" << offset.z << std::endl;

    return true;
}

bool DeviceLinkManager::HasLink(const std::string& primaryDevice, const std::string& linkedDevice) const
{
    std::string key = MakeLinkKey(primaryDevice, linkedDevice);
    return m_links.find(key) != m_links.end();
}

const DeviceLink* DeviceLinkManager::GetLink(const std::string& primaryDevice,
                                             const std::string& linkedDevice) const
{
    std::string key = MakeLinkKey(primaryDevice, linkedDevice);
    auto it = m_links.find(key);
    if (it != m_links.end()) {
        return &(it->second);
    }
    return nullptr;
}

bool DeviceLinkManager::RemoveLink(const std::string& primaryDevice, const std::string& linkedDevice)
{
    std::string key = MakeLinkKey(primaryDevice, linkedDevice);
    auto it = m_links.find(key);
    if (it != m_links.end()) {
        m_links.erase(it);
        std::cout << "[DeviceLinkManager] Removed link: " << primaryDevice << " -> " << linkedDevice << std::endl;
        return true;
    }
    return false;
}

std::vector<DeviceLink> DeviceLinkManager::GetAllLinks() const
{
    std::vector<DeviceLink> result;
    result.reserve(m_links.size());
    for (const auto& [key, link] : m_links) {
        result.push_back(link);
    }
    return result;
}

bool DeviceLinkManager::GetCurrentDrift(const std::string& primaryDevice,
                                        const std::string& linkedDevice,
                                        CoordinateTransformer::Vector6D& outDrift) const
{
    // Get stored link
    const DeviceLink* link = GetLink(primaryDevice, linkedDevice);
    if (!link) {
        std::cerr << "[DeviceLinkManager] No link found for: " << primaryDevice << " -> " << linkedDevice << std::endl;
        return false;
    }

    // Get current positions in global coordinates
    CoordinateTransformer::Vector6D primaryGlobal, linkedGlobal;

    if (!GetPositionInGlobal(primaryDevice, primaryGlobal)) {
        return false;
    }

    if (!GetPositionInGlobal(linkedDevice, linkedGlobal)) {
        return false;
    }

    // DEBUG: Print current positions in global coords
    std::cout << "[DeviceLinkManager] DEBUG: " << primaryDevice << " position (global): X="
              << primaryGlobal.x << ", Y=" << primaryGlobal.y << ", Z=" << primaryGlobal.z << std::endl;
    std::cout << "[DeviceLinkManager] DEBUG: " << linkedDevice << " position (global): X="
              << linkedGlobal.x << ", Y=" << linkedGlobal.y << ", Z=" << linkedGlobal.z << std::endl;
    std::cout << "[DeviceLinkManager] DEBUG: Stored T0 offset: X="
              << link->registeredOffset.x << ", Y=" << link->registeredOffset.y
              << ", Z=" << link->registeredOffset.z << std::endl;

    // Calculate current offset: T1 = primary_global - linked_global
    CoordinateTransformer::Vector6D currentOffset;
    currentOffset.x = primaryGlobal.x - linkedGlobal.x;
    currentOffset.y = primaryGlobal.y - linkedGlobal.y;
    currentOffset.z = primaryGlobal.z - linkedGlobal.z;
    currentOffset.u = primaryGlobal.u - linkedGlobal.u;
    currentOffset.v = primaryGlobal.v - linkedGlobal.v;
    currentOffset.w = primaryGlobal.w - linkedGlobal.w;

    std::cout << "[DeviceLinkManager] DEBUG: Current T1 offset: X="
              << currentOffset.x << ", Y=" << currentOffset.y << ", Z=" << currentOffset.z << std::endl;

    // Calculate drift: Delta = T1 - T0
    outDrift.x = currentOffset.x - link->registeredOffset.x;
    outDrift.y = currentOffset.y - link->registeredOffset.y;
    outDrift.z = currentOffset.z - link->registeredOffset.z;
    outDrift.u = currentOffset.u - link->registeredOffset.u;
    outDrift.v = currentOffset.v - link->registeredOffset.v;
    outDrift.w = currentOffset.w - link->registeredOffset.w;

    return true;
}

bool DeviceLinkManager::GetCompensationMove(const std::string& primaryDevice,
                                            const std::string& linkedDevice,
                                            CoordinateTransformer::Vector6D& outCompensation,
                                            const std::string& axes) const
{
    if (!m_matrixLoader) {
        std::cerr << "[DeviceLinkManager] Matrix loader not set" << std::endl;
        return false;
    }

    // Get the drift in global coordinates
    CoordinateTransformer::Vector6D drift;
    if (!GetCurrentDrift(primaryDevice, linkedDevice, drift)) {
        return false;
    }

    // Apply axis mask (zero out axes we don't want to compensate)
    CoordinateTransformer::Vector6D maskedDrift = drift;
    if (axes.find('X') == std::string::npos && axes.find('x') == std::string::npos) {
        maskedDrift.x = 0.0;
    }
    if (axes.find('Y') == std::string::npos && axes.find('y') == std::string::npos) {
        maskedDrift.y = 0.0;
    }
    if (axes.find('Z') == std::string::npos && axes.find('z') == std::string::npos) {
        maskedDrift.z = 0.0;
    }
    if (axes.find('U') == std::string::npos && axes.find('u') == std::string::npos) {
        maskedDrift.u = 0.0;
    }
    if (axes.find('V') == std::string::npos && axes.find('v') == std::string::npos) {
        maskedDrift.v = 0.0;
    }
    if (axes.find('W') == std::string::npos && axes.find('w') == std::string::npos) {
        maskedDrift.w = 0.0;
    }

    // Get transformation matrix for the linked device
    const auto* matrix = m_matrixLoader->GetMatrix(linkedDevice);
    if (!matrix) {
        std::cerr << "[DeviceLinkManager] No transformation matrix for: " << linkedDevice << std::endl;
        return false;
    }

    // DEBUG: Print the transformation matrix being used
    std::cout << "[DeviceLinkManager] DEBUG: Transformation matrix for " << linkedDevice << ":" << std::endl;
    std::cout << "  [" << (*matrix)[0] << ", " << (*matrix)[1] << ", " << (*matrix)[2] << "]" << std::endl;
    std::cout << "  [" << (*matrix)[3] << ", " << (*matrix)[4] << ", " << (*matrix)[5] << "]" << std::endl;
    std::cout << "  [" << (*matrix)[6] << ", " << (*matrix)[7] << ", " << (*matrix)[8] << "]" << std::endl;

    // Transform drift from global to linked device's local coordinates
    // The linked device needs to move BY the drift amount to follow the primary device.
    // Drift = T1 - T0 = how much primary moved in global coords
    // To restore relationship, linked must move the SAME amount in global coords.

    std::cout << "[DeviceLinkManager] DEBUG: Drift to compensate (global): X=" << maskedDrift.x
              << ", Y=" << maskedDrift.y << ", Z=" << maskedDrift.z << std::endl;

    // Transform to local coordinates of the linked device (NO negation - move WITH drift)
    outCompensation = CoordinateTransformer::TransformGlobalToLocal(maskedDrift, *matrix);

    std::cout << "[DeviceLinkManager] Compensation for " << linkedDevice << ":" << std::endl;
    std::cout << "  Drift (global): X=" << drift.x << ", Y=" << drift.y << ", Z=" << drift.z << std::endl;
    std::cout << "  Compensation (local): X=" << outCompensation.x
              << ", Y=" << outCompensation.y << ", Z=" << outCompensation.z << std::endl;

    return true;
}

bool DeviceLinkManager::SaveToFile(const std::string& filePath) const
{
    QJsonArray linksArray;

    for (const auto& [key, link] : m_links) {
        QJsonObject linkObj;
        linkObj["primaryDevice"] = QString::fromStdString(link.primaryDevice);
        linkObj["linkedDevice"] = QString::fromStdString(link.linkedDevice);
        linkObj["timestamp"] = QString::fromStdString(link.timestamp);

        QJsonObject offsetObj;
        offsetObj["x"] = link.registeredOffset.x;
        offsetObj["y"] = link.registeredOffset.y;
        offsetObj["z"] = link.registeredOffset.z;
        offsetObj["u"] = link.registeredOffset.u;
        offsetObj["v"] = link.registeredOffset.v;
        offsetObj["w"] = link.registeredOffset.w;
        linkObj["offset"] = offsetObj;

        linksArray.append(linkObj);
    }

    QJsonObject root;
    root["links"] = linksArray;

    QJsonDocument doc(root);

    QFile file(QString::fromStdString(filePath));
    if (!file.open(QIODevice::WriteOnly)) {
        std::cerr << "[DeviceLinkManager] Failed to open file for writing: " << filePath << std::endl;
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    std::cout << "[DeviceLinkManager] Saved " << m_links.size() << " links to: " << filePath << std::endl;
    return true;
}

bool DeviceLinkManager::LoadFromFile(const std::string& filePath)
{
    QFile file(QString::fromStdString(filePath));
    if (!file.open(QIODevice::ReadOnly)) {
        // File doesn't exist yet - that's OK, just start empty
        std::cout << "[DeviceLinkManager] No existing links file found, starting empty" << std::endl;
        return true;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        std::cerr << "[DeviceLinkManager] JSON parse error: "
                  << parseError.errorString().toStdString() << std::endl;
        return false;
    }

    if (!doc.isObject()) {
        std::cerr << "[DeviceLinkManager] JSON root is not an object" << std::endl;
        return false;
    }

    QJsonObject root = doc.object();
    if (!root.contains("links") || !root["links"].isArray()) {
        std::cerr << "[DeviceLinkManager] Missing 'links' array in JSON" << std::endl;
        return false;
    }

    QJsonArray linksArray = root["links"].toArray();
    m_links.clear();

    for (const QJsonValue& value : linksArray) {
        if (!value.isObject()) continue;

        QJsonObject linkObj = value.toObject();

        std::string primaryDevice = linkObj["primaryDevice"].toString().toStdString();
        std::string linkedDevice = linkObj["linkedDevice"].toString().toStdString();
        std::string timestamp = linkObj["timestamp"].toString().toStdString();

        QJsonObject offsetObj = linkObj["offset"].toObject();
        CoordinateTransformer::Vector6D offset;
        offset.x = offsetObj["x"].toDouble(0.0);
        offset.y = offsetObj["y"].toDouble(0.0);
        offset.z = offsetObj["z"].toDouble(0.0);
        offset.u = offsetObj["u"].toDouble(0.0);
        offset.v = offsetObj["v"].toDouble(0.0);
        offset.w = offsetObj["w"].toDouble(0.0);

        RegisterLinkWithOffset(primaryDevice, linkedDevice, offset, timestamp);
    }

    std::cout << "[DeviceLinkManager] Loaded " << m_links.size() << " links from: " << filePath << std::endl;
    return true;
}

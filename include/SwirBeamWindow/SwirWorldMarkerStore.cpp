// SwirWorldMarkerStore.cpp
#include "SwirWorldMarkerStore.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

SwirWorldMarkerStore::SwirWorldMarkerStore(GetGantryXYFn getGantry)
    : m_getGantry(std::move(getGantry))
{
}

void SwirWorldMarkerStore::SetFilePath(const std::string& path)
{
    m_filePath = path;
}

bool SwirWorldMarkerStore::Place(SwirMarkerType type)
{
    if (!m_getGantry) return false;
    double x = 0.0, y = 0.0;
    if (!m_getGantry(x, y)) return false;

    SwirWorldMarker marker;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        marker.id = m_nextId++;
        marker.type = type;
        marker.worldX = x;
        marker.worldY = y;
        marker.label = (type == SwirMarkerType::VLine ? "V" : "H")
                       + std::to_string(marker.id);
        m_markers.push_back(marker);
    }
    std::cout << "[SwirWorldMarkers] Placed " << marker.label
              << " at (" << marker.worldX << ", " << marker.worldY << ")"
              << std::endl;
    return true;
}

void SwirWorldMarkerStore::ClearAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_markers.clear();
    m_nextId = 1;
    std::cout << "[SwirWorldMarkers] All markers cleared" << std::endl;
}

std::vector<SwirWorldMarker> SwirWorldMarkerStore::Snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_markers;
}

bool SwirWorldMarkerStore::SaveToFile() const
{
    if (m_filePath.empty()) return false;

    nlohmann::json arr = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& m : m_markers) {
            nlohmann::json j;
            j["id"]     = m.id;
            j["type"]   = (m.type == SwirMarkerType::VLine) ? "VLine" : "HLine";
            j["worldX"] = m.worldX;
            j["worldY"] = m.worldY;
            j["label"]  = m.label;
            arr.push_back(j);
        }
    }

    std::ofstream ofs(m_filePath);
    if (!ofs.is_open()) return false;
    ofs << arr.dump(2);
    return true;
}

bool SwirWorldMarkerStore::LoadFromFile()
{
    if (m_filePath.empty()) return false;
    std::ifstream ifs(m_filePath);
    if (!ifs.is_open()) return false;

    auto arr = nlohmann::json::parse(ifs, nullptr, false);
    if (arr.is_discarded() || !arr.is_array()) return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_markers.clear();
    int maxId = 0;
    for (const auto& j : arr) {
        SwirWorldMarker marker;
        marker.id = j.value("id", 0);
        std::string typeStr = j.value("type", "VLine");
        marker.type = (typeStr == "HLine") ? SwirMarkerType::HLine
                                            : SwirMarkerType::VLine;
        marker.worldX = j.value("worldX", 0.0);
        marker.worldY = j.value("worldY", 0.0);
        marker.label  = j.value("label", "");
        m_markers.push_back(marker);
        if (marker.id > maxId) maxId = marker.id;
    }
    m_nextId = maxId + 1;
    std::cout << "[SwirWorldMarkers] Loaded " << m_markers.size()
              << " markers from " << m_filePath << std::endl;
    return true;
}

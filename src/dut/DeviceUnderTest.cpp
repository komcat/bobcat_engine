#include "dut/DeviceUnderTest.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

// DataEntry helper methods
std::optional<std::string> DeviceUnderTest::DataEntry::getStringValue() const {
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }
    return std::nullopt;
}

std::optional<double> DeviceUnderTest::DataEntry::getDoubleValue() const {
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value);
    }
    return std::nullopt;
}

std::optional<int64_t> DeviceUnderTest::DataEntry::getIntegerValue() const {
    if (std::holds_alternative<int64_t>(value)) {
        return std::get<int64_t>(value);
    }
    return std::nullopt;
}

// Private constructor
DeviceUnderTest::DeviceUnderTest(const std::string& productName, const std::string& serialNumber)
    : productName_(productName), serialNumber_(serialNumber) {
    creationTimestamp_ = getCurrentTimestamp();
    filePath_ = buildFilePath(productName, serialNumber);
}

// Static factory methods
std::optional<DeviceUnderTest> DeviceUnderTest::create(const std::string& productName, const std::string& serialNumber) {
    if (productName.empty() || serialNumber.empty()) {
        std::cerr << "[DeviceUnderTest] Error: Product name and serial number cannot be empty" << std::endl;
        return std::nullopt;
    }

    DeviceUnderTest dut(productName, serialNumber);

    // Ensure directory exists
    fs::path path(dut.filePath_);
    if (!ensureDirectoryExists(path.parent_path().string())) {
        std::cerr << "[DeviceUnderTest] Error: Failed to create directory: " << path.parent_path().string() << std::endl;
        return std::nullopt;
    }

    // Save initial empty state
    if (!dut.saveToFile()) {
        return std::nullopt;
    }

    std::cout << "[DUT] Created new DUT: " << productName << " / " << serialNumber << std::endl;
    return dut;
}

std::optional<DeviceUnderTest> DeviceUnderTest::load(const std::string& productName, const std::string& serialNumber) {
    if (productName.empty() || serialNumber.empty()) {
        std::cerr << "[DeviceUnderTest] Error: Product name and serial number cannot be empty" << std::endl;
        return std::nullopt;
    }

    std::string filePath = buildFilePath(productName, serialNumber);

    if (!fs::exists(filePath)) {
        std::cerr << "[DeviceUnderTest] Error: DUT file not found: " << filePath << std::endl;
        return std::nullopt;
    }

    // Create empty DUT and load from file
    DeviceUnderTest dut(productName, serialNumber);
    if (!dut.loadFromFile()) {
        return std::nullopt;
    }

    std::cout << "[DUT] Loaded DUT: " << productName << " / " << serialNumber
              << " (" << dut.entries_.size() << " entries)" << std::endl;
    return dut;
}

bool DeviceUnderTest::exists(const std::string& productName, const std::string& serialNumber) {
    std::string filePath = buildFilePath(productName, serialNumber);
    return fs::exists(filePath);
}

bool DeviceUnderTest::deleteFile(const std::string& productName, const std::string& serialNumber) {
    std::string filePath = buildFilePath(productName, serialNumber);

    try {
        if (fs::exists(filePath)) {
            fs::remove(filePath);
            std::cout << "[DUT] Deleted DUT file: " << filePath << std::endl;
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[DUT] Error deleting file: " << e.what() << std::endl;
        return false;
    }
}

// Data operations
bool DeviceUnderTest::insertEntry(const std::string& key, const std::string& value) {
    DataEntry entry;
    entry.timestamp = getCurrentTimestamp();
    entry.key = key;
    entry.value = value;
    entry.dataType = "string";

    entries_.push_back(entry);
    if (!saveToFile()) {
        entries_.pop_back();  // Rollback on save failure
        return false;
    }

    std::cout << "[DUT] Inserted entry: " << key << " = " << value << " @ " << entry.timestamp << std::endl;
    return true;
}

bool DeviceUnderTest::insertEntry(const std::string& key, double value) {
    DataEntry entry;
    entry.timestamp = getCurrentTimestamp();
    entry.key = key;
    entry.value = value;
    entry.dataType = "double";

    entries_.push_back(entry);
    if (!saveToFile()) {
        entries_.pop_back();  // Rollback on save failure
        return false;
    }

    std::cout << "[DUT] Inserted entry: " << key << " = " << value << " @ " << entry.timestamp << std::endl;
    return true;
}

bool DeviceUnderTest::insertEntry(const std::string& key, int64_t value) {
    DataEntry entry;
    entry.timestamp = getCurrentTimestamp();
    entry.key = key;
    entry.value = value;
    entry.dataType = "integer";

    entries_.push_back(entry);
    if (!saveToFile()) {
        entries_.pop_back();  // Rollback on save failure
        return false;
    }

    std::cout << "[DUT] Inserted entry: " << key << " = " << value << " @ " << entry.timestamp << std::endl;
    return true;
}

bool DeviceUnderTest::deleteEntry(const std::string& timestamp) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&timestamp](const DataEntry& entry) {
            return entry.timestamp == timestamp;
        });

    if (it != entries_.end()) {
        std::cout << "[DUT] Deleting entry: " << it->key << " @ " << timestamp << std::endl;
        DataEntry backup = *it;  // Backup for potential rollback
        entries_.erase(it);
        if (!saveToFile()) {
            entries_.push_back(backup);  // Rollback on save failure
            return false;
        }
        return true;
    }

    return false;
}

int DeviceUnderTest::deleteEntriesByKey(const std::string& key) {
    auto originalSize = entries_.size();
    std::vector<DataEntry> backup = entries_;  // Backup for potential rollback

    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
            [&key](const DataEntry& entry) {
                return entry.key == key;
            }),
        entries_.end()
    );

    int deletedCount = static_cast<int>(originalSize - entries_.size());

    if (deletedCount > 0) {
        if (!saveToFile()) {
            entries_ = backup;  // Rollback on save failure
            return -1;
        }
        std::cout << "[DUT] Deleted " << deletedCount << " entries with key: " << key << std::endl;
    }

    return deletedCount;
}

std::optional<DeviceUnderTest::DataEntry> DeviceUnderTest::getEntry(const std::string& timestamp) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&timestamp](const DataEntry& entry) {
            return entry.timestamp == timestamp;
        });

    if (it != entries_.end()) {
        return *it;
    }

    return std::nullopt;
}

std::vector<DeviceUnderTest::DataEntry> DeviceUnderTest::getEntriesByKey(const std::string& key) const {
    std::vector<DataEntry> result;

    std::copy_if(entries_.begin(), entries_.end(), std::back_inserter(result),
        [&key](const DataEntry& entry) {
            return entry.key == key;
        });

    return result;
}

std::vector<DeviceUnderTest::DataEntry> DeviceUnderTest::getEntriesInTimeRange(
    const std::string& startTime, const std::string& endTime) const {

    std::vector<DataEntry> result;

    std::copy_if(entries_.begin(), entries_.end(), std::back_inserter(result),
        [&startTime, &endTime](const DataEntry& entry) {
            return entry.timestamp >= startTime && entry.timestamp <= endTime;
        });

    return result;
}

// Save/Load operations
bool DeviceUnderTest::save() {
    return saveToFile();
}

bool DeviceUnderTest::loadFromFile() {
    std::ifstream file(filePath_);
    if (!file.is_open()) {
        std::cerr << "[DeviceUnderTest] Error: Failed to open file for reading: " << filePath_ << std::endl;
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
        *this = fromJson(j);
    } catch (const std::exception& e) {
        std::cerr << "[DeviceUnderTest] Error: Failed to parse JSON from file: " << e.what() << std::endl;
        return false;
    }
    return true;
}

bool DeviceUnderTest::saveToFile() {
    std::ofstream file(filePath_);
    if (!file.is_open()) {
        std::cerr << "[DeviceUnderTest] Error: Failed to open file for writing: " << filePath_ << std::endl;
        return false;
    }

    nlohmann::json j = toJson();
    file << j.dump(4);  // Pretty print with 4-space indent
    return true;
}

// JSON serialization
nlohmann::json DeviceUnderTest::toJson() const {
    nlohmann::json j;
    j["product_name"] = productName_;
    j["serial_number"] = serialNumber_;
    j["creation_timestamp"] = creationTimestamp_;

    nlohmann::json entriesArray = nlohmann::json::array();
    for (const auto& entry : entries_) {
        nlohmann::json entryJson;
        entryJson["timestamp"] = entry.timestamp;
        entryJson["key"] = entry.key;
        entryJson["data_type"] = entry.dataType;

        // Serialize value based on type
        if (entry.dataType == "string") {
            entryJson["value"] = std::get<std::string>(entry.value);
        } else if (entry.dataType == "double") {
            entryJson["value"] = std::get<double>(entry.value);
        } else if (entry.dataType == "integer") {
            entryJson["value"] = std::get<int64_t>(entry.value);
        }

        entriesArray.push_back(entryJson);
    }
    j["entries"] = entriesArray;

    return j;
}

DeviceUnderTest DeviceUnderTest::fromJson(const nlohmann::json& j) {
    std::string productName = j.at("product_name").get<std::string>();
    std::string serialNumber = j.at("serial_number").get<std::string>();

    DeviceUnderTest dut(productName, serialNumber);
    dut.creationTimestamp_ = j.at("creation_timestamp").get<std::string>();

    // Load entries
    if (j.contains("entries") && j["entries"].is_array()) {
        for (const auto& entryJson : j["entries"]) {
            DataEntry entry;
            entry.timestamp = entryJson.at("timestamp").get<std::string>();
            entry.key = entryJson.at("key").get<std::string>();
            entry.dataType = entryJson.at("data_type").get<std::string>();

            // Deserialize value based on type
            if (entry.dataType == "string") {
                entry.value = entryJson.at("value").get<std::string>();
            } else if (entry.dataType == "double") {
                entry.value = entryJson.at("value").get<double>();
            } else if (entry.dataType == "integer") {
                entry.value = entryJson.at("value").get<int64_t>();
            }

            dut.entries_.push_back(entry);
        }
    }

    return dut;
}

// Helper methods
std::string DeviceUnderTest::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
    gmtime_s(&tm_buf, &time_t_now);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';

    return ss.str();
}

std::string DeviceUnderTest::buildFilePath(const std::string& productName, const std::string& serialNumber) {
    // Build path: data/[product_name]/[serial_number].json
    fs::path path = fs::path("data") / productName / (serialNumber + ".json");
    return path.string();
}

bool DeviceUnderTest::ensureDirectoryExists(const std::string& path) {
    try {
        if (!fs::exists(path)) {
            fs::create_directories(path);
            std::cout << "[DUT] Created directory: " << path << std::endl;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DUT] Error creating directory: " << e.what() << std::endl;
        return false;
    }
}

// ✅ NEW: Get image folder path for this DUT instance
std::string DeviceUnderTest::getImageFolderPath() const {
    return getImageFolderPath(productName_, serialNumber_);
}

// ✅ NEW: Static helper to get image folder path
std::string DeviceUnderTest::getImageFolderPath(const std::string& productName, const std::string& serialNumber) {
    // Build path: data/[product_name]/[serial_number]/images/
    fs::path path = fs::path("data") / productName / serialNumber / "images";
    return path.string();
}

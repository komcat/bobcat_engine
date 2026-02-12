#ifndef DEVICE_UNDER_TEST_H
#define DEVICE_UNDER_TEST_H

#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <nlohmann/json.hpp>

/**
 * @brief DeviceUnderTest - Persistent data storage for test devices
 *
 * Stores test data for a specific device identified by product name and serial number.
 * Data is stored in JSON format at: data/[product_name]/[serial_number].json
 *
 * Each entry is uniquely identified by timestamp and can store string, double, or integer values.
 * All modifications are immediately saved to disk.
 *
 * Usage:
 *   auto dut = DeviceUnderTest::create("Product-A", "SN-12345");
 *   dut.insertEntry("temperature", 25.5);
 *   dut.insertEntry("status", "PASS");
 *   dut.save();
 *
 *   auto loaded = DeviceUnderTest::load("Product-A", "SN-12345");
 *   auto entries = loaded.getAllEntries();
 */
class DeviceUnderTest {
public:
    /**
     * @brief Data entry with timestamp, key, and typed value
     */
    struct DataEntry {
        std::string timestamp;     // ISO 8601 format: YYYY-MM-DDTHH:MM:SSZ
        std::string key;           // Entry identifier (not unique - multiple entries can have same key)
        std::variant<std::string, double, int64_t, nlohmann::json> value;
        std::string dataType;      // "string", "double", "integer", "object"

        // Helper methods to get typed values
        std::optional<std::string> getStringValue() const;
        std::optional<double> getDoubleValue() const;
        std::optional<int64_t> getIntegerValue() const;
    };

    // Factory methods for creation/loading
    /**
     * @brief Create a new DUT instance and save to file
     * @param productName Product name (used for directory structure)
     * @param serialNumber Unique serial number for this device
     * @return New DeviceUnderTest instance, or std::nullopt if creation fails
     */
    static std::optional<DeviceUnderTest> create(const std::string& productName, const std::string& serialNumber);

    /**
     * @brief Load existing DUT from file
     * @param productName Product name
     * @param serialNumber Serial number
     * @return Loaded DeviceUnderTest instance, or std::nullopt if file not found or parsing fails
     */
    static std::optional<DeviceUnderTest> load(const std::string& productName, const std::string& serialNumber);

    /**
     * @brief Check if DUT file exists
     */
    static bool exists(const std::string& productName, const std::string& serialNumber);

    /**
     * @brief Delete DUT file from disk
     * @return true if deletion successful, false otherwise
     */
    static bool deleteFile(const std::string& productName, const std::string& serialNumber);

    // Data operations (all auto-save immediately)
    /**
     * @brief Insert new entry with current timestamp
     * @param key Entry identifier (can be duplicated with different timestamps)
     * @param value String, double, or int64_t value
     * @return true if insert and save successful, false otherwise
     */
    bool insertEntry(const std::string& key, const std::string& value);
    bool insertEntry(const std::string& key, double value);
    bool insertEntry(const std::string& key, int64_t value);
    bool insertEntry(const std::string& key, const nlohmann::json& value);

    /**
     * @brief Delete entry by exact timestamp
     * @param timestamp ISO 8601 timestamp of entry to delete
     * @return true if entry found and deleted, false otherwise
     */
    bool deleteEntry(const std::string& timestamp);

    /**
     * @brief Delete all entries with matching key
     * @param key Key to match
     * @return Number of entries deleted, or -1 if save failed
     */
    int deleteEntriesByKey(const std::string& key);

    /**
     * @brief Get entry by timestamp
     */
    std::optional<DataEntry> getEntry(const std::string& timestamp) const;

    /**
     * @brief Get all entries with matching key (may return multiple)
     */
    std::vector<DataEntry> getEntriesByKey(const std::string& key) const;

    /**
     * @brief Get all entries
     */
    std::vector<DataEntry> getAllEntries() const { return entries_; }

    /**
     * @brief Get entries within time range
     * @param startTime ISO 8601 start time (inclusive)
     * @param endTime ISO 8601 end time (inclusive)
     */
    std::vector<DataEntry> getEntriesInTimeRange(const std::string& startTime, const std::string& endTime) const;

    // Manual save (usually not needed - auto-saves on insert/delete)
    /**
     * @brief Manually save current state to JSON file
     * @return true if save successful, false otherwise
     */
    bool save();

    // Accessors
    std::string getProductName() const { return productName_; }
    std::string getSerialNumber() const { return serialNumber_; }
    std::string getCreationTimestamp() const { return creationTimestamp_; }
    std::string getFilePath() const { return filePath_; }
    size_t getEntryCount() const { return entries_.size(); }

    // ✅ NEW: Get image folder path for this DUT
    std::string getImageFolderPath() const;

    // ✅ NEW: Static helper to get image folder path without loading DUT
    static std::string getImageFolderPath(const std::string& productName, const std::string& serialNumber);

    // JSON serialization
    nlohmann::json toJson() const;
    static DeviceUnderTest fromJson(const nlohmann::json& j);

private:
    // Private constructor - use create() or load()
    DeviceUnderTest(const std::string& productName, const std::string& serialNumber);

    std::string productName_;
    std::string serialNumber_;
    std::string creationTimestamp_;
    std::vector<DataEntry> entries_;
    std::string filePath_;  // Cached full path to JSON file

    // Helper methods
    static std::string getCurrentTimestamp();
    static std::string buildFilePath(const std::string& productName, const std::string& serialNumber);
    static bool ensureDirectoryExists(const std::string& path);
    bool loadFromFile();
    bool saveToFile();
};

#endif // DEVICE_UNDER_TEST_H

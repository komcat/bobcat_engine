// AccuracyTrainerLogger.h
// SQLite-based logging for accuracy trainer corner confirmations
#pragma once

#include <string>
#include <mutex>

struct sqlite3;

/**
 * @brief Logs accuracy trainer corner confirmations to a SQLite database.
 *
 * Each confirmed corner position (TL, BR, TR) and computed center is persisted
 * with the original taught position, confirmed position, and the delta between them.
 * Database: logs/accuracy_trainer/accuracy_trainer_log.db
 */
class AccuracyTrainerLogger {
public:
    AccuracyTrainerLogger();
    ~AccuracyTrainerLogger();

    // Non-copyable
    AccuracyTrainerLogger(const AccuracyTrainerLogger&) = delete;
    AccuracyTrainerLogger& operator=(const AccuracyTrainerLogger&) = delete;

    /**
     * @brief Open (or create) the SQLite database and ensure the table exists.
     * @param dbDir Directory for the database file (created if missing)
     * @return true if the database is ready for writes
     */
    [[nodiscard]] bool Initialize(const std::string& dbDir = "logs/accuracy_trainer");

    /**
     * @brief Log a single corner or center confirmation.
     * @param productName   Active product name
     * @param trainingSetName Training set being executed
     * @param serialNumber  DUT serial (empty if unavailable)
     * @param hexDevice     Hex device name (e.g. "hex-left")
     * @param teachMode     0=single, 1=2-corner, 2=3-corner
     * @param cornerName    "TL", "BR", "TR", or "CENTER"
     * @param taughtX/Y/Z  Original taught position
     * @param confirmedX/Y/Z  User-confirmed (or computed) position
     */
    void LogCorner(
        const std::string& productName,
        const std::string& trainingSetName,
        const std::string& serialNumber,
        const std::string& hexDevice,
        int teachMode,
        const std::string& cornerName,
        double taughtX, double taughtY, double taughtZ,
        double confirmedX, double confirmedY, double confirmedZ);

private:
    sqlite3* m_db = nullptr;
    std::mutex m_mutex;

    void EnsureTable();
};

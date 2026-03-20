// AccuracyTrainerLogger.cpp
// SQLite-based logging for accuracy trainer corner confirmations
#include "AccuracyTrainerLogger.h"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <ctime>

AccuracyTrainerLogger::AccuracyTrainerLogger() = default;

AccuracyTrainerLogger::~AccuracyTrainerLogger()
{
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool AccuracyTrainerLogger::Initialize(const std::string& dbDir)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_db) {
        return true;  // Already initialized
    }

    // Create directory if it doesn't exist
    std::filesystem::path dir(dbDir);
    if (!std::filesystem::exists(dir)) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::cout << "[AccuracyTrainerLogger] Failed to create directory: "
                      << dbDir << " (" << ec.message() << ")" << std::endl;
            return false;
        }
    }

    std::filesystem::path dbPath = dir / "accuracy_trainer_log.db";
    int rc = sqlite3_open(dbPath.string().c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::cout << "[AccuracyTrainerLogger] Failed to open database: "
                  << sqlite3_errmsg(m_db) << std::endl;
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }

    // Enable WAL mode for better concurrent performance
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    EnsureTable();

    std::cout << "[AccuracyTrainerLogger] Database opened: " << dbPath.string() << std::endl;
    return true;
}

void AccuracyTrainerLogger::EnsureTable()
{
    const char* sql =
        "CREATE TABLE IF NOT EXISTS hex_corner_log ("
        "    id              INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    timestamp       TEXT NOT NULL,"
        "    product_name    TEXT NOT NULL,"
        "    training_set    TEXT NOT NULL,"
        "    serial_number   TEXT,"
        "    hex_device      TEXT NOT NULL,"
        "    teach_mode      INTEGER NOT NULL,"
        "    corner_name     TEXT NOT NULL,"
        "    taught_x        REAL,"
        "    taught_y        REAL,"
        "    taught_z        REAL,"
        "    confirmed_x     REAL NOT NULL,"
        "    confirmed_y     REAL NOT NULL,"
        "    confirmed_z     REAL,"
        "    delta_x         REAL,"
        "    delta_y         REAL,"
        "    delta_z         REAL"
        ");";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cout << "[AccuracyTrainerLogger] Failed to create table: "
                  << (errMsg ? errMsg : "unknown error") << std::endl;
        sqlite3_free(errMsg);
    }
}

void AccuracyTrainerLogger::LogCorner(
    const std::string& productName,
    const std::string& trainingSetName,
    const std::string& serialNumber,
    const std::string& hexDevice,
    int teachMode,
    const std::string& cornerName,
    double taughtX, double taughtY, double taughtZ,
    double confirmedX, double confirmedY, double confirmedZ)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_db) {
        std::cout << "[AccuracyTrainerLogger] Database not initialized, skipping log" << std::endl;
        return;
    }

    // ISO 8601 timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    localtime_s(&tm_now, &time_t_now);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%S", &tm_now);

    double deltaX = confirmedX - taughtX;
    double deltaY = confirmedY - taughtY;
    double deltaZ = confirmedZ - taughtZ;

    const char* sql =
        "INSERT INTO hex_corner_log "
        "(timestamp, product_name, training_set, serial_number, hex_device, "
        " teach_mode, corner_name, taught_x, taught_y, taught_z, "
        " confirmed_x, confirmed_y, confirmed_z, delta_x, delta_y, delta_z) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cout << "[AccuracyTrainerLogger] Prepare failed: "
                  << sqlite3_errmsg(m_db) << std::endl;
        return;
    }

    sqlite3_bind_text(stmt, 1, timeBuf, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, productName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, trainingSetName.c_str(), -1, SQLITE_TRANSIENT);
    if (serialNumber.empty()) {
        sqlite3_bind_null(stmt, 4);
    } else {
        sqlite3_bind_text(stmt, 4, serialNumber.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(stmt, 5, hexDevice.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, teachMode);
    sqlite3_bind_text(stmt, 7, cornerName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 8, taughtX);
    sqlite3_bind_double(stmt, 9, taughtY);
    sqlite3_bind_double(stmt, 10, taughtZ);
    sqlite3_bind_double(stmt, 11, confirmedX);
    sqlite3_bind_double(stmt, 12, confirmedY);
    sqlite3_bind_double(stmt, 13, confirmedZ);
    sqlite3_bind_double(stmt, 14, deltaX);
    sqlite3_bind_double(stmt, 15, deltaY);
    sqlite3_bind_double(stmt, 16, deltaZ);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cout << "[AccuracyTrainerLogger] Insert failed: "
                  << sqlite3_errmsg(m_db) << std::endl;
    }

    sqlite3_finalize(stmt);
}

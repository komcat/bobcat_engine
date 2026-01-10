// SerialDataLoader.cpp
#include "SerialDataLoader.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextStream>
#include <iostream>

SerialDataLoader::SerialDataLoader(QObject* parent)
    : QObject(parent)
    , m_dataPath("./data")
{
}

SerialDataLoader::~SerialDataLoader()
{
}

void SerialDataLoader::setDataPath(const QString& path)
{
    m_dataPath = path;
}

QStringList SerialDataLoader::discoverProducts()
{
    QStringList products;
    QDir dataDir(m_dataPath);

    if (!dataDir.exists()) {
        emit errorOccurred("Data path does not exist: " + m_dataPath);
        return products;
    }

    QStringList entries = dataDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        // Check if folder contains JSON files
        QDir productDir(dataDir.filePath(entry));
        QStringList jsonFiles = productDir.entryList(QStringList() << "*.json", QDir::Files);
        if (!jsonFiles.isEmpty()) {
            products.append(entry);
        }
    }

    std::cout << "[SerialDataLoader] Discovered " << products.size() << " products" << std::endl;
    return products;
}

bool SerialDataLoader::loadProduct(const QString& productName)
{
    m_records.clear();
    m_currentProduct = productName;

    QString productPath = m_dataPath + "/" + productName;
    QDir productDir(productPath);

    if (!productDir.exists()) {
        emit errorOccurred("Product folder does not exist: " + productPath);
        return false;
    }

    QStringList jsonFiles = productDir.entryList(QStringList() << "*.json", QDir::Files);
    int total = jsonFiles.size();
    int current = 0;

    std::cout << "[SerialDataLoader] Loading " << total << " files from "
              << productPath.toStdString() << std::endl;

    for (const QString& jsonFile : jsonFiles) {
        QString filePath = productDir.filePath(jsonFile);
        SerialRecord record;

        if (loadJsonFile(filePath, record)) {
            m_records.append(record);
        }

        current++;
        emit loadProgress(current, total);
    }

    // Sort by creation date (newest first)
    std::sort(m_records.begin(), m_records.end(),
        [](const SerialRecord& a, const SerialRecord& b) {
            return a.creationTimestamp > b.creationTimestamp;
        });

    emit loadComplete(m_records.size());
    std::cout << "[SerialDataLoader] Loaded " << m_records.size() << " records" << std::endl;
    return true;
}

bool SerialDataLoader::loadJsonFile(const QString& filePath, SerialRecord& record)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "[SerialDataLoader] Failed to open: " << filePath.toStdString() << std::endl;
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        std::cerr << "[SerialDataLoader] Invalid JSON: " << filePath.toStdString() << std::endl;
        return false;
    }

    QJsonObject root = doc.object();

    record.filePath = filePath;
    record.serialNumber = root["serial_number"].toString();
    record.productName = root["product_name"].toString();

    QString timestamp = root["creation_timestamp"].toString();
    record.creationTimestamp = QDateTime::fromString(timestamp, Qt::ISODate);

    // Load entries
    QJsonArray entriesArray = root["entries"].toArray();
    for (const QJsonValue& entryVal : entriesArray) {
        QJsonObject entryObj = entryVal.toObject();

        SerialEntry entry;
        entry.key = entryObj["key"].toString();
        entry.dataType = entryObj["data_type"].toString();
        entry.timestamp = QDateTime::fromString(entryObj["timestamp"].toString(), Qt::ISODate);

        // Parse value based on data type
        if (entry.dataType == "double") {
            entry.value = entryObj["value"].toDouble();
        } else if (entry.dataType == "int") {
            entry.value = entryObj["value"].toInt();
        } else if (entry.dataType == "bool") {
            entry.value = entryObj["value"].toBool();
        } else {
            entry.value = entryObj["value"].toString();
        }

        record.entries.append(entry);
    }

    return true;
}

QVector<SerialRecord> SerialDataLoader::filterByDate(const QDate& from, const QDate& to) const
{
    QVector<SerialRecord> filtered;
    for (const SerialRecord& record : m_records) {
        QDate recordDate = record.creationTimestamp.date();
        if (recordDate >= from && recordDate <= to) {
            filtered.append(record);
        }
    }
    return filtered;
}

QVector<SerialRecord> SerialDataLoader::filterBySerial(const QString& pattern) const
{
    if (pattern.isEmpty()) {
        return m_records;
    }

    QVector<SerialRecord> filtered;
    for (const SerialRecord& record : m_records) {
        if (record.serialNumber.contains(pattern, Qt::CaseInsensitive)) {
            filtered.append(record);
        }
    }
    return filtered;
}

QVector<SerialRecord> SerialDataLoader::filterByKey(const QString& keyName) const
{
    if (keyName.isEmpty()) {
        return m_records;
    }

    QVector<SerialRecord> filtered;
    for (const SerialRecord& record : m_records) {
        for (const SerialEntry& entry : record.entries) {
            if (entry.key.contains(keyName, Qt::CaseInsensitive)) {
                filtered.append(record);
                break;
            }
        }
    }
    return filtered;
}

QVector<SerialRecord> SerialDataLoader::applyFilters(const QDate& fromDate, const QDate& toDate,
                                                       const QString& serialPattern,
                                                       const QString& keyFilter) const
{
    QVector<SerialRecord> filtered;

    for (const SerialRecord& record : m_records) {
        // Date filter
        QDate recordDate = record.creationTimestamp.date();
        if (recordDate < fromDate || recordDate > toDate) {
            continue;
        }

        // Serial filter
        if (!serialPattern.isEmpty() &&
            !record.serialNumber.contains(serialPattern, Qt::CaseInsensitive)) {
            continue;
        }

        // Key filter
        if (!keyFilter.isEmpty()) {
            bool hasKey = false;
            for (const SerialEntry& entry : record.entries) {
                if (entry.key.contains(keyFilter, Qt::CaseInsensitive)) {
                    hasKey = true;
                    break;
                }
            }
            if (!hasKey) {
                continue;
            }
        }

        filtered.append(record);
    }

    return filtered;
}

bool SerialDataLoader::exportToCsv(const QString& filePath, const QVector<SerialRecord>& records) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::cerr << "[SerialDataLoader] Failed to create CSV: " << filePath.toStdString() << std::endl;
        return false;
    }

    QTextStream out(&file);

    // Write header
    out << "Serial,Product,CreatedDate,Key,Value,DataType,EntryTimestamp\n";

    // Write data rows
    for (const SerialRecord& record : records) {
        QString createdDate = record.creationTimestamp.toString("yyyy-MM-dd HH:mm:ss");

        if (record.entries.isEmpty()) {
            // Record with no entries
            out << record.serialNumber << ","
                << record.productName << ","
                << createdDate << ","
                << ",,,\n";
        } else {
            for (const SerialEntry& entry : record.entries) {
                QString entryTimestamp = entry.timestamp.toString("yyyy-MM-dd HH:mm:ss");
                QString valueStr = entry.value.toString();

                // Escape commas in values
                if (valueStr.contains(',') || valueStr.contains('"')) {
                    valueStr = "\"" + valueStr.replace("\"", "\"\"") + "\"";
                }

                out << record.serialNumber << ","
                    << record.productName << ","
                    << createdDate << ","
                    << entry.key << ","
                    << valueStr << ","
                    << entry.dataType << ","
                    << entryTimestamp << "\n";
            }
        }
    }

    file.close();
    std::cout << "[SerialDataLoader] Exported " << records.size() << " records to "
              << filePath.toStdString() << std::endl;
    return true;
}

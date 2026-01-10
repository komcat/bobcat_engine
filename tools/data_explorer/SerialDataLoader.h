// SerialDataLoader.h
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariant>
#include <QDateTime>
#include <QDate>

struct SerialEntry {
    QString key;
    QString dataType;
    QVariant value;
    QDateTime timestamp;
};

struct SerialRecord {
    QString serialNumber;
    QString productName;
    QDateTime creationTimestamp;
    QVector<SerialEntry> entries;
    QString filePath;
};

class SerialDataLoader : public QObject {
    Q_OBJECT

public:
    explicit SerialDataLoader(QObject* parent = nullptr);
    ~SerialDataLoader();

    void setDataPath(const QString& path);
    QString getDataPath() const { return m_dataPath; }

    QStringList discoverProducts();
    bool loadProduct(const QString& productName);

    QVector<SerialRecord> getRecords() const { return m_records; }
    int getRecordCount() const { return m_records.size(); }

    // Filtering
    QVector<SerialRecord> filterByDate(const QDate& from, const QDate& to) const;
    QVector<SerialRecord> filterBySerial(const QString& pattern) const;
    QVector<SerialRecord> filterByKey(const QString& keyName) const;

    // Combined filter
    QVector<SerialRecord> applyFilters(const QDate& fromDate, const QDate& toDate,
                                        const QString& serialPattern,
                                        const QString& keyFilter) const;

    // Export
    bool exportToCsv(const QString& filePath, const QVector<SerialRecord>& records) const;

signals:
    void loadProgress(int current, int total);
    void loadComplete(int recordCount);
    void errorOccurred(QString message);

private:
    bool loadJsonFile(const QString& filePath, SerialRecord& record);

    QString m_dataPath;
    QString m_currentProduct;
    QVector<SerialRecord> m_records;
};

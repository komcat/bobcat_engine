// DataExplorerWindow.h
#pragma once

#include <QMainWindow>
#include <QComboBox>
#include <QDateEdit>
#include <QLineEdit>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>

#include "SerialDataLoader.h"

class DataExplorerWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit DataExplorerWindow(QWidget* parent = nullptr);
    ~DataExplorerWindow();

private slots:
    void onProductChanged();
    void onRefresh();
    void onSerialSelected();
    void onApplyFilter();
    void onExportCsv();
    void onLoadProgress(int current, int total);
    void onLoadComplete(int recordCount);
    void onError(QString message);

private:
    void setupUI();
    void applyDarkTheme();
    void populateSerialList();
    void populateEntryTable(const SerialRecord& record);
    void clearEntryTable();
    void updateStatus(const QString& message);

    // Top toolbar
    QComboBox* m_productCombo;
    QLabel* m_pathLabel;
    QDateEdit* m_fromDate;
    QDateEdit* m_toDate;
    QPushButton* m_applyFilterBtn;

    // Left panel (serial list)
    QLineEdit* m_serialFilter;
    QTableWidget* m_serialTable;

    // Right panel (entry details)
    QLabel* m_detailsLabel;
    QTableWidget* m_entryTable;

    // Bottom toolbar
    QPushButton* m_refreshBtn;
    QPushButton* m_exportBtn;
    QLabel* m_statusLabel;

    // Splitter for resizable panels
    QSplitter* m_splitter;

    // Data
    SerialDataLoader* m_loader;
    QVector<SerialRecord> m_filteredRecords;
};

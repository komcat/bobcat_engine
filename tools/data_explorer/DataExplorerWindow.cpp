// DataExplorerWindow.cpp
#include "DataExplorerWindow.h"
#include "DarkTheme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <iostream>

DataExplorerWindow::DataExplorerWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_loader(new SerialDataLoader(this))
{
    setWindowTitle("SIPHOG Data Explorer");
    setMinimumSize(1000, 700);

    setupUI();
    applyDarkTheme();

    // Connect signals
    connect(m_loader, &SerialDataLoader::loadProgress, this, &DataExplorerWindow::onLoadProgress);
    connect(m_loader, &SerialDataLoader::loadComplete, this, &DataExplorerWindow::onLoadComplete);
    connect(m_loader, &SerialDataLoader::errorOccurred, this, &DataExplorerWindow::onError);

    // Discover products on startup
    QStringList products = m_loader->discoverProducts();
    m_productCombo->addItems(products);

    if (!products.isEmpty()) {
        m_productCombo->setCurrentIndex(0);
        onProductChanged();
    }
}

DataExplorerWindow::~DataExplorerWindow()
{
}

void DataExplorerWindow::setupUI()
{
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    // ========== Top toolbar ==========
    QHBoxLayout* topLayout = new QHBoxLayout();

    QLabel* productLabel = new QLabel("Product:");
    m_productCombo = new QComboBox();
    m_productCombo->setMinimumWidth(150);
    connect(m_productCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DataExplorerWindow::onProductChanged);

    m_pathLabel = new QLabel("Data Path: ./data/");
    m_pathLabel->setStyleSheet("color: #888;");

    topLayout->addWidget(productLabel);
    topLayout->addWidget(m_productCombo);
    topLayout->addSpacing(20);
    topLayout->addWidget(m_pathLabel);
    topLayout->addStretch();

    mainLayout->addLayout(topLayout);

    // ========== Filter row ==========
    QHBoxLayout* filterLayout = new QHBoxLayout();

    QLabel* dateLabel = new QLabel("Date Range:");
    m_fromDate = new QDateEdit();
    m_fromDate->setCalendarPopup(true);
    m_fromDate->setDate(QDate::currentDate().addMonths(-1));
    m_fromDate->setDisplayFormat("yyyy-MM-dd");

    QLabel* toLabel = new QLabel("to");
    m_toDate = new QDateEdit();
    m_toDate->setCalendarPopup(true);
    m_toDate->setDate(QDate::currentDate());
    m_toDate->setDisplayFormat("yyyy-MM-dd");

    m_applyFilterBtn = new QPushButton("Apply Filter");
    m_applyFilterBtn->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; padding: 6px 16px; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #1976D2; }");
    connect(m_applyFilterBtn, &QPushButton::clicked, this, &DataExplorerWindow::onApplyFilter);

    filterLayout->addWidget(dateLabel);
    filterLayout->addWidget(m_fromDate);
    filterLayout->addWidget(toLabel);
    filterLayout->addWidget(m_toDate);
    filterLayout->addSpacing(20);
    filterLayout->addWidget(m_applyFilterBtn);
    filterLayout->addStretch();

    mainLayout->addLayout(filterLayout);

    // ========== Main content (splitter) ==========
    m_splitter = new QSplitter(Qt::Horizontal);

    // Left panel - Serial list
    QWidget* leftPanel = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    QLabel* serialTitle = new QLabel("Serial Numbers");
    serialTitle->setStyleSheet("font-weight: bold; font-size: 14px;");

    m_serialFilter = new QLineEdit();
    m_serialFilter->setPlaceholderText("Filter by serial...");
    connect(m_serialFilter, &QLineEdit::textChanged, this, &DataExplorerWindow::onApplyFilter);

    m_serialTable = new QTableWidget();
    m_serialTable->setColumnCount(2);
    m_serialTable->setHorizontalHeaderLabels({"Serial", "Created"});
    m_serialTable->horizontalHeader()->setStretchLastSection(true);
    m_serialTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_serialTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_serialTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_serialTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_serialTable->verticalHeader()->setVisible(false);
    m_serialTable->setAlternatingRowColors(true);
    connect(m_serialTable, &QTableWidget::itemSelectionChanged, this, &DataExplorerWindow::onSerialSelected);

    leftLayout->addWidget(serialTitle);
    leftLayout->addWidget(m_serialFilter);
    leftLayout->addWidget(m_serialTable);

    // Right panel - Entry details
    QWidget* rightPanel = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_detailsLabel = new QLabel("Select a serial number to view details");
    m_detailsLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 4px;");

    m_entryTable = new QTableWidget();
    m_entryTable->setColumnCount(4);
    m_entryTable->setHorizontalHeaderLabels({"Key", "Value", "Type", "Timestamp"});
    m_entryTable->horizontalHeader()->setStretchLastSection(true);
    m_entryTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_entryTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_entryTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_entryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_entryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_entryTable->verticalHeader()->setVisible(false);
    m_entryTable->setAlternatingRowColors(true);

    rightLayout->addWidget(m_detailsLabel);
    rightLayout->addWidget(m_entryTable);

    m_splitter->addWidget(leftPanel);
    m_splitter->addWidget(rightPanel);
    m_splitter->setSizes({300, 700});

    mainLayout->addWidget(m_splitter, 1);

    // ========== Bottom toolbar ==========
    QHBoxLayout* bottomLayout = new QHBoxLayout();

    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setStyleSheet(
        "QPushButton { padding: 6px 16px; }");
    connect(m_refreshBtn, &QPushButton::clicked, this, &DataExplorerWindow::onRefresh);

    m_exportBtn = new QPushButton("Export CSV");
    m_exportBtn->setStyleSheet(
        "QPushButton { background-color: #4CAF50; color: white; padding: 6px 16px; "
        "border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #388E3C; }");
    connect(m_exportBtn, &QPushButton::clicked, this, &DataExplorerWindow::onExportCsv);

    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("color: #888;");

    bottomLayout->addWidget(m_refreshBtn);
    bottomLayout->addWidget(m_exportBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(m_statusLabel);

    mainLayout->addLayout(bottomLayout);
}

void DataExplorerWindow::applyDarkTheme()
{
    qApp->setStyleSheet(DarkTheme::getStyleSheet());
}

void DataExplorerWindow::onProductChanged()
{
    QString product = m_productCombo->currentText();
    if (product.isEmpty()) return;

    m_pathLabel->setText("Data Path: ./data/" + product + "/");

    // Set date range to cover all time by default
    m_fromDate->setDate(QDate(2020, 1, 1));
    m_toDate->setDate(QDate::currentDate());

    updateStatus("Loading " + product + "...");
    QApplication::processEvents();

    m_loader->loadProduct(product);
}

void DataExplorerWindow::onRefresh()
{
    // Re-discover products
    QString currentProduct = m_productCombo->currentText();
    m_productCombo->clear();

    QStringList products = m_loader->discoverProducts();
    m_productCombo->addItems(products);

    // Restore selection if possible
    int index = m_productCombo->findText(currentProduct);
    if (index >= 0) {
        m_productCombo->setCurrentIndex(index);
    }

    onProductChanged();
}

void DataExplorerWindow::onSerialSelected()
{
    int row = m_serialTable->currentRow();
    if (row < 0 || row >= m_filteredRecords.size()) {
        clearEntryTable();
        return;
    }

    populateEntryTable(m_filteredRecords[row]);
}

void DataExplorerWindow::onApplyFilter()
{
    m_filteredRecords = m_loader->applyFilters(
        m_fromDate->date(),
        m_toDate->date(),
        m_serialFilter->text(),
        ""  // Key filter not implemented in UI yet
    );

    populateSerialList();
    clearEntryTable();
    updateStatus(QString("Showing %1 records").arg(m_filteredRecords.size()));
}

void DataExplorerWindow::onExportCsv()
{
    if (m_filteredRecords.isEmpty()) {
        QMessageBox::warning(this, "Export", "No data to export.");
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export to CSV",
        QString("%1_export.csv").arg(m_productCombo->currentText()),
        "CSV Files (*.csv)"
    );

    if (fileName.isEmpty()) return;

    if (m_loader->exportToCsv(fileName, m_filteredRecords)) {
        QMessageBox::information(this, "Export",
            QString("Exported %1 records to:\n%2").arg(m_filteredRecords.size()).arg(fileName));
    } else {
        QMessageBox::critical(this, "Export Error", "Failed to export data.");
    }
}

void DataExplorerWindow::onLoadProgress(int current, int total)
{
    updateStatus(QString("Loading... %1/%2").arg(current).arg(total));
    QApplication::processEvents();
}

void DataExplorerWindow::onLoadComplete(int recordCount)
{
    onApplyFilter();  // Apply current filters to loaded data
    updateStatus(QString("Loaded %1 records").arg(recordCount));
}

void DataExplorerWindow::onError(QString message)
{
    updateStatus("Error: " + message);
    QMessageBox::warning(this, "Error", message);
}

void DataExplorerWindow::populateSerialList()
{
    m_serialTable->setRowCount(0);

    for (const SerialRecord& record : m_filteredRecords) {
        int row = m_serialTable->rowCount();
        m_serialTable->insertRow(row);

        QTableWidgetItem* serialItem = new QTableWidgetItem(record.serialNumber);
        QTableWidgetItem* dateItem = new QTableWidgetItem(
            record.creationTimestamp.toString("yyyy-MM-dd"));

        m_serialTable->setItem(row, 0, serialItem);
        m_serialTable->setItem(row, 1, dateItem);
    }
}

void DataExplorerWindow::populateEntryTable(const SerialRecord& record)
{
    m_detailsLabel->setText(QString("Serial: %1  |  Created: %2  |  Entries: %3")
        .arg(record.serialNumber)
        .arg(record.creationTimestamp.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(record.entries.size()));

    m_entryTable->setRowCount(0);

    for (const SerialEntry& entry : record.entries) {
        int row = m_entryTable->rowCount();
        m_entryTable->insertRow(row);

        QTableWidgetItem* keyItem = new QTableWidgetItem(entry.key);

        // Format value based on type
        QString valueStr;
        if (entry.dataType == "double") {
            valueStr = QString::number(entry.value.toDouble(), 'g', 10);
            keyItem->setForeground(QColor("#64B5F6"));  // Blue for doubles
        } else if (entry.dataType == "int") {
            valueStr = QString::number(entry.value.toInt());
            keyItem->setForeground(QColor("#64B5F6"));
        } else {
            valueStr = entry.value.toString();
            keyItem->setForeground(QColor("#81C784"));  // Green for strings
        }

        QTableWidgetItem* valueItem = new QTableWidgetItem(valueStr);
        QTableWidgetItem* typeItem = new QTableWidgetItem(entry.dataType);
        QTableWidgetItem* timestampItem = new QTableWidgetItem(
            entry.timestamp.toString("HH:mm:ss"));

        m_entryTable->setItem(row, 0, keyItem);
        m_entryTable->setItem(row, 1, valueItem);
        m_entryTable->setItem(row, 2, typeItem);
        m_entryTable->setItem(row, 3, timestampItem);
    }
}

void DataExplorerWindow::clearEntryTable()
{
    m_detailsLabel->setText("Select a serial number to view details");
    m_entryTable->setRowCount(0);
}

void DataExplorerWindow::updateStatus(const QString& message)
{
    m_statusLabel->setText(message);
}

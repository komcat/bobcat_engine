// RetraceLogViewer.cpp — see header.
#include "RetraceLogViewer.h"
#include "PlacementRetraceLogger.h"
#include "ApplicationContext.h"
#include "product_position_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <iostream>

using json = nlohmann::json;

namespace {

// Safely pull a numeric from nested JSON (returns def if missing/wrong type).
double jn(const json& j, std::initializer_list<const char*> path, double def = 0.0) {
    const json* cur = &j;
    for (const char* key : path) {
        if (!cur->is_object() || !cur->contains(key)) return def;
        cur = &(*cur)[key];
    }
    if (cur->is_number()) return cur->get<double>();
    return def;
}

QString js(const json& j, const char* key, const char* def = "") {
    if (j.is_object() && j.contains(key) && j[key].is_string()) {
        return QString::fromStdString(j[key].get<std::string>());
    }
    return def;
}

} // namespace

RetraceLogViewer::RetraceLogViewer(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
{
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(15, 15, 15, 15);
    root->setSpacing(8);

    QLabel* title = new QLabel("Retrace Log");
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #fff;");
    root->addWidget(title);

    // Header row: path + controls
    QHBoxLayout* headerRow = new QHBoxLayout();
    m_pathLabel = new QLabel("(no set loaded)");
    m_pathLabel->setStyleSheet(
        "color: #aabbcc; font-family: monospace; font-size: 11px; "
        "background-color: #1a1f2e; border: 1px solid #3d5a8c; "
        "border-radius: 3px; padding: 4px 8px;");
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerRow->addWidget(m_pathLabel, 1);

    m_refreshBtn = new QPushButton("Refresh");
    m_refreshBtn->setFixedSize(80, 26);
    m_refreshBtn->setStyleSheet(
        "QPushButton { background-color: #2d3e5f; color: white; border: 1px solid #4d6e9f; "
        "border-radius: 3px; padding: 2px 8px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3d5a8c; }");
    connect(m_refreshBtn, &QPushButton::clicked, this, &RetraceLogViewer::onRefresh);
    headerRow->addWidget(m_refreshBtn);

    m_openBtn = new QPushButton("Open in Explorer");
    m_openBtn->setFixedSize(140, 26);
    m_openBtn->setStyleSheet(m_refreshBtn->styleSheet());
    connect(m_openBtn, &QPushButton::clicked, this, &RetraceLogViewer::onOpenInExplorer);
    headerRow->addWidget(m_openBtn);

    root->addLayout(headerRow);

    m_countLabel = new QLabel("0 entries");
    m_countLabel->setStyleSheet("color: #888; font-size: 11px;");
    root->addWidget(m_countLabel);

    // Table
    m_table = new QTableWidget();
    m_table->setColumnCount(9);
    QStringList headers;
    headers << "Timestamp" << "Outcome" << "Mode" << "ΔU (°)" << "Δθ (°)"
            << "Corr dX (mm)" << "Corr dY (mm)"
            << "Corrected X" << "Corrected Y";
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setStyleSheet(
        "QTableWidget { background-color: #1a1f2e; color: #eee; gridline-color: #2d3e5f; "
        "alternate-background-color: #222a3d; }"
        "QHeaderView::section { background-color: #2d3e5f; color: white; "
        "padding: 4px; border: 1px solid #3d5a8c; font-weight: bold; }"
        "QTableWidget::item:selected { background-color: #3d5a8c; }");
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &RetraceLogViewer::onRowSelectionChanged);
    root->addWidget(m_table, 2);

    QLabel* detailTitle = new QLabel("Entry Detail (JSON)");
    detailTitle->setStyleSheet("color: #aabbcc; font-size: 12px; font-weight: bold;");
    root->addWidget(detailTitle);

    m_detail = new QPlainTextEdit();
    m_detail->setReadOnly(true);
    m_detail->setStyleSheet(
        "QPlainTextEdit { background-color: #0f141f; color: #d0d8e0; "
        "font-family: Consolas, monospace; font-size: 11px; "
        "border: 1px solid #2d3e5f; border-radius: 3px; }");
    root->addWidget(m_detail, 1);
}

void RetraceLogViewer::SetData(const PlacementCorrectionSet& set)
{
    m_currentSet = set;
    void* ppm = (m_context && m_context->productPositionManager)
        ? static_cast<void*>(m_context->productPositionManager)
        : nullptr;
    m_currentLogPath = PlacementRetraceLogger::ResolvePathForSet(m_currentSet, ppm);
    m_pathLabel->setText(QString::fromStdString(m_currentLogPath));
    reload();
}

void RetraceLogViewer::onRefresh()
{
    reload();
}

void RetraceLogViewer::onOpenInExplorer()
{
    if (m_currentLogPath.empty()) return;
    QFileInfo fi(QString::fromStdString(m_currentLogPath));
    QString dir = fi.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void RetraceLogViewer::reload()
{
    m_entries = PlacementRetraceLogger::ReadAll(m_currentLogPath);
    m_countLabel->setText(QString("%1 entries").arg(m_entries.size()));
    m_table->setRowCount(0);
    m_detail->clear();

    // Populate most-recent first
    m_table->setRowCount(static_cast<int>(m_entries.size()));
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        int reverseIdx = static_cast<int>(m_entries.size()) - 1 - i;
        populateRow(i, m_entries[reverseIdx]);
    }
    m_table->resizeColumnsToContents();
}

void RetraceLogViewer::populateRow(int row, const json& entry)
{
    auto set = [&](int col, const QString& txt, const QColor& fg = QColor()) {
        auto* item = new QTableWidgetItem(txt);
        if (fg.isValid()) item->setForeground(fg);
        m_table->setItem(row, col, item);
    };

    QString outcome = js(entry, "outcome");
    QColor outcomeFg;
    if      (outcome == "saved")   outcomeFg = QColor("#27ae60");
    else if (outcome == "skipped") outcomeFg = QColor("#f39c12");
    else if (outcome == "ended")   outcomeFg = QColor("#95a5a6");
    else if (outcome == "failed")  outcomeFg = QColor("#e74c3c");

    set(0, js(entry, "timestamp"));
    set(1, outcome, outcomeFg);
    set(2, js(entry, "mode"));
    set(3, QString::number(jn(entry, {"uCorrectionDeg"}), 'f', 4));
    set(4, QString::number(jn(entry, {"frameDeltaThetaDeg"}), 'f', 4));
    set(5, QString::number(jn(entry, {"correctionGlobal", "dx"}), 'f', 4));
    set(6, QString::number(jn(entry, {"correctionGlobal", "dy"}), 'f', 4));
    set(7, QString::number(jn(entry, {"corrected", "x"}), 'f', 4));
    set(8, QString::number(jn(entry, {"corrected", "y"}), 'f', 4));
}

void RetraceLogViewer::onRowSelectionChanged()
{
    auto items = m_table->selectedItems();
    if (items.isEmpty() || m_entries.empty()) {
        m_detail->clear();
        return;
    }
    int row = items.first()->row();
    int reverseIdx = static_cast<int>(m_entries.size()) - 1 - row;
    if (reverseIdx < 0 || reverseIdx >= static_cast<int>(m_entries.size())) return;
    m_detail->setPlainText(
        QString::fromStdString(m_entries[reverseIdx].dump(2)));
}

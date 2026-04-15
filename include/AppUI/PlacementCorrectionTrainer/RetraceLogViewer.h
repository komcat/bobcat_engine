// RetraceLogViewer.h — read-only viewer for the retrace JSONL audit log.
//
// Shows the contents of {productDir}/placement_correction/{set}.retrace_log.jsonl
// as a table (most recent at top) plus a JSON detail pane for the selected row.
#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <nlohmann/json.hpp>
#include "PlacementCorrectionData.h"

struct ApplicationContext;

class RetraceLogViewer : public QWidget {
    Q_OBJECT

public:
    explicit RetraceLogViewer(ApplicationContext* context, QWidget* parent = nullptr);

    // Called by the parent page whenever the current training set changes.
    // Resolves the log file path and reloads the table.
    void SetData(const PlacementCorrectionSet& set);

private slots:
    void onRefresh();
    void onRowSelectionChanged();
    void onOpenInExplorer();

private:
    void reload();
    void populateRow(int row, const nlohmann::json& entry);

    ApplicationContext* m_context;
    PlacementCorrectionSet m_currentSet;
    std::string m_currentLogPath;
    std::vector<nlohmann::json> m_entries;

    QLabel* m_pathLabel = nullptr;
    QLabel* m_countLabel = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_openBtn = nullptr;
    QTableWidget* m_table = nullptr;
    QPlainTextEdit* m_detail = nullptr;
};

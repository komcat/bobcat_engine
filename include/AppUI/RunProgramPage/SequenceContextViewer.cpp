// SequenceContextViewer.cpp
#include "SequenceContextViewer.h"

#include "ProgramWorker.h"
#include "SequenceContext.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>
#include <iostream>
#include <map>
#include <vector>

namespace {

constexpr int kLivePollIntervalMs   = 500;
constexpr int kCvAutoRefreshMs      = 500;
constexpr const char* kColWidthKey0 = "ContextTree/ColumnWidth0";
constexpr const char* kColWidthKey1 = "ContextTree/ColumnWidth1";
constexpr const char* kLargeFontKey = "ContextTree/LargeFont";
constexpr const char* kQSettingsOrg = "Fabrinet";
constexpr const char* kQSettingsApp = "BobcatEngine";

}  // namespace

SequenceContextViewer::SequenceContextViewer(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

SequenceContextViewer::~SequenceContextViewer() = default;

void SequenceContextViewer::buildUi()
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(5, 5, 5, 5);
    outerLayout->setSpacing(8);

    // Header row: Filter + Expand/Collapse + Font toggle + status label.
    auto* headerLayout = new QHBoxLayout();

    auto* filterLabel = new QLabel("Filter:", this);
    filterLabel->setStyleSheet("color: #d4d4d4; font-weight: bold; font-size: 18pt;");
    headerLayout->addWidget(filterLabel);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Type to filter variables...");
    m_filterEdit->setStyleSheet(
        "QLineEdit { "
        "  background-color: #3c3c3c; "
        "  color: #d4d4d4; "
        "  border: 1px solid #555555; "
        "  border-radius: 3px; "
        "  padding: 5px; "
        "  min-width: 200px; "
        "  font-size: 18pt; "
        "} "
        "QLineEdit:focus { border: 1px solid #0e639c; }");
    headerLayout->addWidget(m_filterEdit, 1);

    const QString smallButtonStyle =
        "QPushButton { "
        "  background-color: #3c3c3c; color: #d4d4d4; "
        "  border: 1px solid #555555; border-radius: 3px; "
        "  padding: 4px 10px; font-size: 14pt; "
        "} "
        "QPushButton:hover { background-color: #505050; } "
        "QPushButton:pressed { background-color: #606060; }";

    m_expandAllButton = new QPushButton("Expand All", this);
    m_expandAllButton->setStyleSheet(smallButtonStyle);
    headerLayout->addWidget(m_expandAllButton);

    m_collapseAllButton = new QPushButton("Collapse All", this);
    m_collapseAllButton->setStyleSheet(smallButtonStyle);
    headerLayout->addWidget(m_collapseAllButton);

    QSettings fontSettings(kQSettingsOrg, kQSettingsApp);
    m_largeFont = fontSettings.value(kLargeFontKey, false).toBool();
    m_fontToggleButton = new QPushButton(m_largeFont ? "Aa-" : "Aa+", this);
    m_fontToggleButton->setToolTip(m_largeFont ? "Switch to small font"
                                                : "Switch to large font");
    m_fontToggleButton->setStyleSheet(smallButtonStyle);
    headerLayout->addWidget(m_fontToggleButton);

    m_statusLabel = new QLabel("No context data", this);
    m_statusLabel->setStyleSheet("color: #888888; font-style: italic; font-size: 18pt;");
    headerLayout->addWidget(m_statusLabel);

    outerLayout->addLayout(headerLayout);

    // Tree widget
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({"Variable", "Value", "Type"});
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setAlternatingRowColors(false);
    m_tree->setIndentation(20);

    QSettings settings(kQSettingsOrg, kQSettingsApp);
    int col0Width = settings.value(kColWidthKey0, 300).toInt();
    int col1Width = settings.value(kColWidthKey1, 450).toInt();
    m_tree->setColumnWidth(0, col0Width);
    m_tree->setColumnWidth(1, col1Width);
    m_tree->header()->setStretchLastSection(true);

    connect(m_tree->header(), &QHeaderView::sectionResized,
            this, [](int logicalIndex, int /*old*/, int newSize) {
        if (logicalIndex < 2) {
            QSettings s(kQSettingsOrg, kQSettingsApp);
            s.setValue(QString("ContextTree/ColumnWidth%1").arg(logicalIndex), newSize);
        }
    });

    m_tree->setStyleSheet(
        "QTreeWidget { "
        "  background-color: #1e1e1e; border: 1px solid #3f3f46; "
        "  color: #d4d4d4; font-size: 18pt; "
        "} "
        "QTreeWidget::item { padding: 4px; } "
        "QTreeWidget::item:alternate { background-color: #252526; } "
        "QTreeWidget::item:selected { background-color: #0e639c; color: white; } "
        "QTreeWidget::branch { background: transparent; border-image: none; image: none; } "
        "QTreeWidget::branch:has-children:!has-siblings:closed,"
        "QTreeWidget::branch:closed:has-children:has-siblings { "
        "  border-image: none; image: url(assets/icons/down_arrow.png); } "
        "QTreeWidget::branch:open:has-children:!has-siblings,"
        "QTreeWidget::branch:open:has-children:has-siblings { "
        "  border-image: none; image: url(assets/icons/up_arrow.png); } "
        "QHeaderView::section { "
        "  background-color: #2d2d30; color: #d4d4d4; padding: 8px; "
        "  border: 1px solid #3f3f46; font-weight: bold; font-size: 18pt; "
        "}");
    outerLayout->addWidget(m_tree, 1);

    // Action button row
    auto* buttonLayout = new QHBoxLayout();

    m_refreshCvButton = new QPushButton("Refresh CV", this);
    m_refreshCvButton->setCheckable(true);
    m_refreshCvButton->setToolTip(
        "Click to inject cv.* values from continuous vision.\n"
        "Toggle ON for auto-refresh every 500ms.");
    m_refreshCvButton->setStyleSheet(
        "QPushButton { "
        "  background-color: #2d5a27; color: #d4d4d4; "
        "  border: 1px solid #3a7d32; border-radius: 3px; "
        "  padding: 8px 16px; font-size: 18pt; "
        "} "
        "QPushButton:hover { background-color: #3a7d32; } "
        "QPushButton:pressed { background-color: #4a9d42; } "
        "QPushButton:checked { "
        "  background-color: #4a9d42; border: 2px solid #6abf62; "
        "}");
    buttonLayout->addWidget(m_refreshCvButton);

    buttonLayout->addStretch();

    const QString actionBtnStyle =
        "QPushButton { "
        "  background-color: #3c3c3c; color: #d4d4d4; "
        "  border: 1px solid #555555; border-radius: 3px; "
        "  padding: 8px 16px; font-size: 18pt; "
        "} "
        "QPushButton:hover { background-color: #505050; } "
        "QPushButton:pressed { background-color: #606060; }";

    m_copyButton = new QPushButton("Copy to Clipboard", this);
    m_copyButton->setStyleSheet(actionBtnStyle);
    buttonLayout->addWidget(m_copyButton);

    m_exportButton = new QPushButton("Export to CSV", this);
    m_exportButton->setStyleSheet(actionBtnStyle);
    buttonLayout->addWidget(m_exportButton);

    outerLayout->addLayout(buttonLayout);

    // Wire signals.
    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &SequenceContextViewer::onFilterChanged);
    connect(m_copyButton, &QPushButton::clicked,
            this, &SequenceContextViewer::onCopyToClipboard);
    connect(m_exportButton, &QPushButton::clicked,
            this, &SequenceContextViewer::onExportToCsv);
    connect(m_expandAllButton, &QPushButton::clicked,
            this, [this]() { m_tree->expandAll(); });
    connect(m_collapseAllButton, &QPushButton::clicked,
            this, [this]() { m_tree->collapseAll(); });
    connect(m_fontToggleButton, &QPushButton::clicked, this, [this]() {
        m_largeFont = !m_largeFont;
        QSettings s(kQSettingsOrg, kQSettingsApp);
        s.setValue(kLargeFontKey, m_largeFont);
        m_fontToggleButton->setText(m_largeFont ? "Aa-" : "Aa+");
        m_fontToggleButton->setToolTip(m_largeFont ? "Switch to small font"
                                                    : "Switch to large font");
        applyFontSize();
    });
    connect(m_refreshCvButton, &QPushButton::clicked,
            this, &SequenceContextViewer::onRefreshCvClicked);

    // Live-polling timer (created up front, started/stopped on demand).
    m_liveRefreshTimer = new QTimer(this);
    m_liveRefreshTimer->setInterval(kLivePollIntervalMs);
    connect(m_liveRefreshTimer, &QTimer::timeout,
            this, [this]() { refreshFromWorker(false); });

    applyFontSize();
}

void SequenceContextViewer::SetCvPopulator(CvPopulatorFn fn)
{
    m_cvPopulator = std::move(fn);
}

void SequenceContextViewer::SetWorker(ProgramWorker* worker)
{
    m_worker = worker;
}

void SequenceContextViewer::StartLivePolling(int intervalMs)
{
    // Stop CV auto-refresh when recipe execution starts.
    if (m_cvAutoRefreshTimer && m_cvAutoRefreshTimer->isActive()) {
        m_cvAutoRefreshTimer->stop();
        m_refreshCvButton->setChecked(false);
        m_refreshCvButton->setText("Refresh CV");
        emit cvAutoRefreshStopped();
    }
    if (!m_liveRefreshTimer || m_liveRefreshTimer->isActive()) return;
    m_liveRefreshTimer->setInterval(intervalMs);
    std::cout << "[SequenceContextViewer] Starting live refresh ("
              << intervalMs << "ms)" << std::endl;
    refreshFromWorker(false);
    m_liveRefreshTimer->start();
}

void SequenceContextViewer::StopLivePolling()
{
    if (m_liveRefreshTimer && m_liveRefreshTimer->isActive()) {
        std::cout << "[SequenceContextViewer] Stopping live refresh" << std::endl;
        m_liveRefreshTimer->stop();
        refreshFromWorker(false);  // Final update to show end state
    }
}

void SequenceContextViewer::ForceRefresh()
{
    refreshFromWorker(true);
}

void SequenceContextViewer::StopCvAutoRefresh()
{
    if (m_cvAutoRefreshTimer && m_cvAutoRefreshTimer->isActive()) {
        m_cvAutoRefreshTimer->stop();
    }
    if (m_refreshCvButton->isChecked()) {
        m_refreshCvButton->setChecked(false);
        m_refreshCvButton->setText("Refresh CV");
        emit cvAutoRefreshStopped();
    }
}

bool SequenceContextViewer::IsLivePolling() const
{
    return m_liveRefreshTimer && m_liveRefreshTimer->isActive();
}

void SequenceContextViewer::onFilterChanged(const QString& /*filter*/)
{
    if (m_cvAutoRefreshTimer && m_cvAutoRefreshTimer->isActive() && m_standaloneCvContext) {
        renderTree(m_standaloneCvContext.get(), "", " (live)");
    } else {
        refreshFromWorker(false);
    }
}

void SequenceContextViewer::refreshFromWorker(bool forceUpdate)
{
    // If worker isn't actively running, just update the status label and bail.
    if (!forceUpdate && (!m_worker || !m_worker->isRunning())) {
        if (m_tree->topLevelItemCount() > 0) {
            m_statusLabel->setText("variables (last run)");
        } else {
            m_statusLabel->setText("No context data");
        }
        m_statusLabel->setStyleSheet("color: #888888; font-style: italic;");
        return;
    }

    SequenceContext* context = m_worker ? m_worker->getCurrentContext() : nullptr;
    if (!context) {
        if (m_tree->topLevelItemCount() > 0) {
            m_statusLabel->setText("variables (last run)");
        } else {
            m_statusLabel->setText("No context data");
        }
        m_statusLabel->setStyleSheet("color: #888888; font-style: italic;");
        return;
    }
    renderTree(context, "", "");
}

void SequenceContextViewer::onRefreshCvClicked(bool checked)
{
    if (checked) {
        // Toggle ON: immediate refresh + start auto-refresh.
        runCvPopulatorOnce();
        if (!m_cvAutoRefreshTimer) {
            m_cvAutoRefreshTimer = new QTimer(this);
            m_cvAutoRefreshTimer->setInterval(kCvAutoRefreshMs);
            connect(m_cvAutoRefreshTimer, &QTimer::timeout,
                    this, [this]() { runCvPopulatorOnce(); });
        }
        m_cvAutoRefreshTimer->start();
        m_refreshCvButton->setText("Refresh CV (Auto)");
        emit cvAutoRefreshStarted();
    } else {
        if (m_cvAutoRefreshTimer) m_cvAutoRefreshTimer->stop();
        m_refreshCvButton->setText("Refresh CV");
        emit cvAutoRefreshStopped();
    }
}

void SequenceContextViewer::runCvPopulatorOnce()
{
    if (!m_cvPopulator) {
        m_statusLabel->setText("Refresh CV: no populator wired");
        m_statusLabel->setStyleSheet(
            QString("color: #f44747; font-style: italic; font-size: %1;")
                .arg(m_largeFont ? "18pt" : "9pt"));
        return;
    }
    if (!m_standaloneCvContext) {
        m_standaloneCvContext = std::make_unique<SequenceContext>();
    }
    QString errorMsg;
    if (!m_cvPopulator(*m_standaloneCvContext, errorMsg)) {
        m_statusLabel->setText(errorMsg);
        m_statusLabel->setStyleSheet(
            QString("color: #cca700; font-style: italic; font-size: %1;")
                .arg(m_largeFont ? "18pt" : "9pt"));
        return;
    }
    renderTree(m_standaloneCvContext.get(), "", " (live)");
}

void SequenceContextViewer::onCopyToClipboard()
{
    if (!m_tree || m_tree->topLevelItemCount() == 0) {
        std::cout << "[SequenceContextViewer] No context data to copy" << std::endl;
        return;
    }
    struct Entry { QString variable; QString value; QString type; };
    std::vector<Entry> entries;
    std::function<void(QTreeWidgetItem*, const QString&)> collect;
    collect = [&](QTreeWidgetItem* item, const QString& prefix) {
        QString fullPath = prefix.isEmpty() ? item->text(0) : prefix + "." + item->text(0);
        if (item->childCount() == 0) {
            entries.push_back({fullPath, item->text(1), item->text(2)});
        } else {
            for (int i = 0; i < item->childCount(); ++i) {
                collect(item->child(i), fullPath);
            }
        }
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        collect(m_tree->topLevelItem(i), "");
    }

    QString text = "Variable\tValue\tType\n";
    for (const auto& e : entries) {
        text += QString("%1\t%2\t%3\n").arg(e.variable, e.value, e.type);
    }
    QApplication::clipboard()->setText(text);

    std::cout << "[SequenceContextViewer] Copied " << entries.size()
              << " context variables to clipboard" << std::endl;
    m_copyButton->setText("Copied!");
    QTimer::singleShot(1500, this, [this]() { m_copyButton->setText("Copy to Clipboard"); });
}

void SequenceContextViewer::onExportToCsv()
{
    if (!m_tree || m_tree->topLevelItemCount() == 0) {
        std::cout << "[SequenceContextViewer] No context data to export" << std::endl;
        return;
    }
    struct Entry { QString variable; QString value; QString type; };
    std::vector<Entry> entries;
    std::function<void(QTreeWidgetItem*, const QString&)> collect;
    collect = [&](QTreeWidgetItem* item, const QString& prefix) {
        QString fullPath = prefix.isEmpty() ? item->text(0) : prefix + "." + item->text(0);
        if (item->childCount() == 0) {
            entries.push_back({fullPath, item->text(1), item->text(2)});
        } else {
            for (int i = 0; i < item->childCount(); ++i) {
                collect(item->child(i), fullPath);
            }
        }
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        collect(m_tree->topLevelItem(i), "");
    }

    QString defaultName = QString("context_export_%1.csv")
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filePath = QFileDialog::getSaveFileName(this, "Export Context to CSV",
                                                     defaultName,
                                                     "CSV Files (*.csv);;All Files (*)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::cerr << "[SequenceContextViewer] Failed to open: "
                  << filePath.toStdString() << std::endl;
        return;
    }
    QTextStream stream(&file);
    stream << "Variable,Value,Type\n";
    auto escape = [](const QString& s) {
        QString e = s;
        e.replace("\"", "\"\"");
        if (e.contains(',') || e.contains('"') || e.contains('\n')) {
            e = QString("\"%1\"").arg(e);
        }
        return e;
    };
    for (const auto& e : entries) {
        stream << escape(e.variable) << "," << escape(e.value) << ","
               << escape(e.type) << "\n";
    }
    file.close();
    std::cout << "[SequenceContextViewer] Exported " << entries.size()
              << " variables to " << filePath.toStdString() << std::endl;
    m_exportButton->setText("Exported!");
    QTimer::singleShot(1500, this, [this]() { m_exportButton->setText("Export to CSV"); });
}

void SequenceContextViewer::renderTree(SequenceContext* context,
                                       const QString& varLabelPrefix,
                                       const QString& varLabelSuffix)
{
    if (!context) return;
    const QString filterText = m_filterEdit->text().toLower().trimmed();
    auto summary = context->GetSummary();

    QSet<QString> expandedPaths;
    QSet<QString> selectedPaths;
    QSet<QString> allPreviousPaths;
    const bool wasEmpty = (m_tree->topLevelItemCount() == 0);

    if (!wasEmpty) {
        std::function<void(QTreeWidgetItem*, const QString&)> collectState;
        collectState = [&](QTreeWidgetItem* item, const QString& path) {
            allPreviousPaths.insert(path);
            if (item->isExpanded()) expandedPaths.insert(path);
            if (item->isSelected()) selectedPaths.insert(path);
            for (int i = 0; i < item->childCount(); ++i) {
                QString childPath = path + "." + item->child(i)->text(0);
                collectState(item->child(i), childPath);
            }
        };
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            auto* item = m_tree->topLevelItem(i);
            collectState(item, item->text(0));
        }
    }

    m_tree->setUpdatesEnabled(false);
    m_tree->clear();

    int totalCount = 0;
    int shownCount = 0;
    std::map<QString, QTreeWidgetItem*> nodeMap;

    for (const auto& [key, value] : summary.capturedValues) {
        ++totalCount;
        QString keyStr = QString::fromStdString(key);
        if (!filterText.isEmpty() && !keyStr.toLower().contains(filterText)) continue;
        ++shownCount;

        QStringList parts = keyStr.split('.');
        QTreeWidgetItem* parent = nullptr;
        QString currentPath;

        for (int i = 0; i < parts.size() - 1; ++i) {
            currentPath = currentPath.isEmpty() ? parts[i] : currentPath + "." + parts[i];
            auto it = nodeMap.find(currentPath);
            if (it != nodeMap.end()) {
                parent = it->second;
            } else {
                QTreeWidgetItem* node =
                    parent ? new QTreeWidgetItem(parent)
                            : new QTreeWidgetItem(m_tree);
                node->setText(0, parts[i]);
                node->setForeground(0, QColor(78, 201, 176));
                QFont font = node->font(0);
                font.setBold(true);
                node->setFont(0, font);
                nodeMap[currentPath] = node;
                parent = node;
            }
        }

        QTreeWidgetItem* leaf =
            parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        leaf->setText(0, parts.last());
        leaf->setForeground(0, QColor(86, 156, 214));

        QString valueStr;
        QString typeStr;
        QColor valueColor;
        switch (value.type) {
            case SequenceContext::ValueType::NUMERIC:
                valueStr = QString::number(value.numericValue, 'g', 8);
                typeStr = "Numeric";
                valueColor = QColor(181, 206, 168);
                break;
            case SequenceContext::ValueType::STRING:
                valueStr = QString::fromStdString(value.stringValue);
                typeStr = "String";
                valueColor = QColor(206, 145, 120);
                break;
            case SequenceContext::ValueType::BOOLEAN:
                valueStr = value.booleanValue ? "true" : "false";
                typeStr = "Boolean";
                valueColor = QColor(86, 156, 214);
                break;
            case SequenceContext::ValueType::IMAGE_FRAME:
                valueStr = "[Image Frame]";
                if (!value.tempFilePath.empty()) {
                    valueStr += QString(" (%1)")
                                    .arg(QString::fromStdString(value.tempFilePath));
                }
                typeStr = "Image";
                valueColor = QColor(220, 220, 170);
                break;
            default:
                valueStr = "(none)";
                typeStr = "None";
                valueColor = QColor(128, 128, 128);
                break;
        }
        leaf->setText(1, valueStr);
        leaf->setForeground(1, valueColor);
        leaf->setText(2, typeStr);
        leaf->setForeground(2, QColor(128, 128, 128));
    }

    if (wasEmpty) {
        m_tree->expandAll();
    } else {
        std::function<void(QTreeWidgetItem*, const QString&)> restoreState;
        restoreState = [&](QTreeWidgetItem* item, const QString& path) {
            if (allPreviousPaths.contains(path)) {
                item->setExpanded(expandedPaths.contains(path));
            } else {
                item->setExpanded(true);  // New nodes default to expanded
            }
            if (selectedPaths.contains(path)) item->setSelected(true);
            for (int i = 0; i < item->childCount(); ++i) {
                QString childPath = path + "." + item->child(i)->text(0);
                restoreState(item->child(i), childPath);
            }
        };
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            auto* item = m_tree->topLevelItem(i);
            restoreState(item, item->text(0));
        }
    }

    m_tree->setUpdatesEnabled(true);

    m_statusLabel->setStyleSheet(
        QString("color: #4ec9b0; font-size: %1;").arg(m_largeFont ? "18pt" : "9pt"));
    if (filterText.isEmpty()) {
        m_statusLabel->setText(QString("%1 %2variables%3")
                                    .arg(totalCount)
                                    .arg(varLabelPrefix)
                                    .arg(varLabelSuffix));
    } else {
        m_statusLabel->setText(QString("%1 of %2 %3variables%4")
                                    .arg(shownCount).arg(totalCount)
                                    .arg(varLabelPrefix).arg(varLabelSuffix));
    }
}

void SequenceContextViewer::applyFontSize()
{
    const QString contentSize = m_largeFont ? "18pt" : "9pt";
    const QString buttonSize  = m_largeFont ? "14pt" : "9pt";
    const QString headerPad   = m_largeFont ? "8px"  : "4px";
    const QString btnPad      = m_largeFont ? "8px 16px" : "4px 10px";

    auto* filterLabel = findChild<QLabel*>();
    if (filterLabel && filterLabel->text() == "Filter:") {
        filterLabel->setStyleSheet(
            QString("color: #d4d4d4; font-weight: bold; font-size: %1;").arg(contentSize));
    }

    m_filterEdit->setStyleSheet(
        QString("QLineEdit { "
                "  background-color: #3c3c3c; color: #d4d4d4; "
                "  border: 1px solid #555555; border-radius: 3px; "
                "  padding: 5px; min-width: 200px; font-size: %1; "
                "} "
                "QLineEdit:focus { border: 1px solid #0e639c; }").arg(contentSize));

    const QString smallBtnStyle =
        QString("QPushButton { "
                "  background-color: #3c3c3c; color: #d4d4d4; "
                "  border: 1px solid #555555; border-radius: 3px; "
                "  padding: 4px 10px; font-size: %1; "
                "} "
                "QPushButton:hover { background-color: #505050; } "
                "QPushButton:pressed { background-color: #606060; }").arg(buttonSize);
    m_expandAllButton->setStyleSheet(smallBtnStyle);
    m_collapseAllButton->setStyleSheet(smallBtnStyle);
    m_fontToggleButton->setStyleSheet(smallBtnStyle);

    m_statusLabel->setStyleSheet(
        QString("color: #888888; font-style: italic; font-size: %1;").arg(contentSize));

    m_tree->setStyleSheet(
        QString("QTreeWidget { "
                "  background-color: #1e1e1e; border: 1px solid #3f3f46; "
                "  color: #d4d4d4; font-size: %1; "
                "} "
                "QTreeWidget::item { padding: 4px; } "
                "QTreeWidget::item:alternate { background-color: #252526; } "
                "QTreeWidget::item:selected { background-color: #0e639c; color: white; } "
                "QTreeWidget::branch { background: transparent; } "
                "QTreeWidget::branch:has-children:!has-siblings:closed,"
                "QTreeWidget::branch:closed:has-children:has-siblings { "
                "  border-image: none; image: url(assets/icons/down_arrow.png); } "
                "QTreeWidget::branch:open:has-children:!has-siblings,"
                "QTreeWidget::branch:open:has-children:has-siblings { "
                "  border-image: none; image: url(assets/icons/up_arrow.png); } "
                "QHeaderView::section { "
                "  background-color: #2d2d30; color: #d4d4d4; padding: %2; "
                "  border: 1px solid #3f3f46; font-weight: bold; font-size: %1; "
                "}").arg(contentSize, headerPad));

    const QString actionBtnStyle =
        QString("QPushButton { "
                "  background-color: #3c3c3c; color: #d4d4d4; "
                "  border: 1px solid #555555; border-radius: 3px; "
                "  padding: %1; font-size: %2; "
                "} "
                "QPushButton:hover { background-color: #505050; } "
                "QPushButton:pressed { background-color: #606060; }").arg(btnPad, contentSize);
    m_copyButton->setStyleSheet(actionBtnStyle);
    m_exportButton->setStyleSheet(actionBtnStyle);

    m_refreshCvButton->setStyleSheet(
        QString("QPushButton { "
                "  background-color: #2d5a27; color: #d4d4d4; "
                "  border: 1px solid #3a7d32; border-radius: 3px; "
                "  padding: %1; font-size: %2; "
                "} "
                "QPushButton:hover { background-color: #3a7d32; } "
                "QPushButton:pressed { background-color: #4a9d42; } "
                "QPushButton:checked { "
                "  background-color: #4a9d42; border: 2px solid #6abf62; "
                "}").arg(btnPad, contentSize));
}

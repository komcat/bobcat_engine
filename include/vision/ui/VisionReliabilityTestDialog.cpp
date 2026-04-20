// VisionReliabilityTestDialog.cpp
#ifdef HAS_OPENCV

#include "VisionReliabilityTestDialog.h"
#include "../VisionManager.h"
#include "../VisionProcessingWorker.h"
#include "../VisionROI.h"
#include "../VisionResult.h"
#include "ApplicationContext.h"
#include "CameraManager.h"
#include "LiveVideoSubscriber.h"
#include "LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <set>

VisionReliabilityTestDialog::VisionReliabilityTestDialog(
    ApplicationContext* context,
    const QString& pluginName,
    std::shared_ptr<LiveVideoSubscriber> frameSubscriber,
    bool useSearchBox,
    int searchStartX, int searchStartY,
    int searchEndX, int searchEndY,
    QWidget* parent)
    : QDialog(parent)
    , m_context(context)
    , m_pluginName(pluginName)
    , m_frameSubscriber(frameSubscriber)
    , m_useSearchBox(useSearchBox)
    , m_searchStartX(searchStartX)
    , m_searchStartY(searchStartY)
    , m_searchEndX(searchEndX)
    , m_searchEndY(searchEndY)
{
    setupUI();

    // Create own dedicated worker
    m_worker = new VisionProcessingWorker(this);
    m_worker->startWorkerThread();

    // Connect worker signals
    connect(m_worker, &VisionProcessingWorker::processingFinished,
            this, &VisionReliabilityTestDialog::onIterationFinished);
    connect(m_worker, &VisionProcessingWorker::processingError,
            this, &VisionReliabilityTestDialog::onIterationError);

    // Setup iteration timer (single-shot, fires between iterations)
    m_nextIterationTimer = new QTimer(this);
    m_nextIterationTimer->setSingleShot(true);
    connect(m_nextIterationTimer, &QTimer::timeout,
            this, &VisionReliabilityTestDialog::startNextIteration);

    setRunningState(false);
}

VisionReliabilityTestDialog::~VisionReliabilityTestDialog()
{
    m_stopRequested = true;
    if (m_nextIterationTimer) {
        m_nextIterationTimer->stop();
    }
    if (m_worker) {
        m_worker->requestCancel();
        m_worker->stopWorkerThread();
    }
}

void VisionReliabilityTestDialog::setupUI()
{
    setWindowTitle("Vision Reliability Test - " + m_pluginName);
    setMinimumSize(800, 600);
    resize(900, 700);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // === Control bar ===
    QHBoxLayout* controlLayout = new QHBoxLayout();

    QLabel* iterLabel = new QLabel("Iterations:");
    controlLayout->addWidget(iterLabel);

    m_iterationsSpin = new QSpinBox();
    m_iterationsSpin->setRange(1, 1000);
    m_iterationsSpin->setValue(10);
    m_iterationsSpin->setFixedWidth(80);
    controlLayout->addWidget(m_iterationsSpin);

    controlLayout->addSpacing(10);

    m_startBtn = new QPushButton("Start");
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: #2E7D32; color: white; font-weight: bold; padding: 6px 16px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #388E3C; }"
        "QPushButton:disabled { background-color: #555; color: #999; }");
    connect(m_startBtn, &QPushButton::clicked, this, &VisionReliabilityTestDialog::onStartClicked);
    controlLayout->addWidget(m_startBtn);

    m_stopBtn = new QPushButton("Stop");
    m_stopBtn->setStyleSheet(
        "QPushButton { background-color: #C62828; color: white; font-weight: bold; padding: 6px 16px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #E53935; }"
        "QPushButton:disabled { background-color: #555; color: #999; }");
    connect(m_stopBtn, &QPushButton::clicked, this, &VisionReliabilityTestDialog::onStopClicked);
    controlLayout->addWidget(m_stopBtn);

    controlLayout->addStretch();
    mainLayout->addLayout(controlLayout);

    // === Progress bar ===
    QHBoxLayout* progressLayout = new QHBoxLayout();
    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    progressLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setFixedWidth(200);
    m_statusLabel->setStyleSheet("color: #aaa;");
    progressLayout->addWidget(m_statusLabel);

    mainLayout->addLayout(progressLayout);

    // === Results table ===
    QGroupBox* tableGroup = new QGroupBox("Per-Iteration Results");
    QVBoxLayout* tableLayout = new QVBoxLayout(tableGroup);

    m_resultsTable = new QTableWidget(0, 3);
    m_resultsTable->setHorizontalHeaderLabels({"#", "Pass", "Time (ms)"});
    // Interactive resize: ResizeToContents recomputes column widths on every
    // setItem() call, which became the dominant cost for >10 iterations.
    // Columns are sized once when the schema changes via resizeColumnsToContents().
    m_resultsTable->horizontalHeader()->setStretchLastSection(true);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_resultsTable->setAlternatingRowColors(true);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->verticalHeader()->setVisible(false);
    tableLayout->addWidget(m_resultsTable);

    mainLayout->addWidget(tableGroup, 1);

    // === Statistics summary ===
    QGroupBox* statsGroup = new QGroupBox("Statistics Summary");
    QVBoxLayout* statsLayout = new QVBoxLayout(statsGroup);

    m_statisticsText = new QTextEdit();
    m_statisticsText->setReadOnly(true);
    m_statisticsText->setMaximumHeight(180);
    m_statisticsText->setStyleSheet(
        "QTextEdit { background-color: #1e1e1e; color: #d4d4d4; font-family: 'Consolas', monospace; font-size: 12px; }");
    m_statisticsText->setPlainText("Run a test to see statistics...");
    statsLayout->addWidget(m_statisticsText);

    mainLayout->addWidget(statsGroup);

    // === Export button ===
    QHBoxLayout* exportLayout = new QHBoxLayout();
    exportLayout->addStretch();

    m_exportBtn = new QPushButton("Export CSV");
    m_exportBtn->setStyleSheet(
        "QPushButton { background-color: #1565C0; color: white; font-weight: bold; padding: 6px 16px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #1976D2; }"
        "QPushButton:disabled { background-color: #555; color: #999; }");
    m_exportBtn->setEnabled(false);
    connect(m_exportBtn, &QPushButton::clicked, this, &VisionReliabilityTestDialog::onExportCsvClicked);
    exportLayout->addWidget(m_exportBtn);

    mainLayout->addLayout(exportLayout);
}

void VisionReliabilityTestDialog::setRunningState(bool running)
{
    m_isRunning = running;
    m_startBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
    m_iterationsSpin->setEnabled(!running);
    m_exportBtn->setEnabled(!running && !m_results.empty());
}

void VisionReliabilityTestDialog::onStartClicked()
{
    // Reset state
    m_results.clear();
    m_allMeasurementKeys.clear();
    m_activeCountColumns.clear();
    m_currentIteration = 0;
    m_totalIterations = m_iterationsSpin->value();
    m_stopRequested = false;

    // Reset table — only fixed columns, dynamic columns are added as iterations run
    m_resultsTable->setRowCount(0);
    m_resultsTable->setColumnCount(3);
    m_resultsTable->setHorizontalHeaderLabels({"#", "Pass", "Time (ms)"});

    // Reset stats
    m_statisticsText->setPlainText("Running...");

    // Reset progress
    m_progressBar->setRange(0, m_totalIterations);
    m_progressBar->setValue(0);

    setRunningState(true);

    std::cout << "[ReliabilityTest] Starting " << m_totalIterations
              << " iterations with plugin: " << m_pluginName.toStdString() << std::endl;

    startNextIteration();
}

void VisionReliabilityTestDialog::onStopClicked()
{
    m_stopRequested = true;
    m_nextIterationTimer->stop();

    if (m_worker && m_worker->isProcessing()) {
        m_worker->requestCancel();
    }

    m_statusLabel->setText("Stopped by user");
    m_statusLabel->setStyleSheet("color: #FFA500;");
    setRunningState(false);

    // Update statistics with whatever we have so far. The table is already
    // in sync because each iteration appended its own row.
    if (!m_results.empty()) {
        collectAllMeasurementKeys();
        collectActiveCountColumns();
        updateStatisticsSummary();
    }

    std::cout << "[ReliabilityTest] Stopped after " << m_currentIteration
              << "/" << m_totalIterations << " iterations" << std::endl;
}

void VisionReliabilityTestDialog::startNextIteration()
{
    // Bounded check — MISRA compliant (no unbounded loops)
    if (m_stopRequested || m_currentIteration >= m_totalIterations) {
        // Done
        setRunningState(false);
        if (!m_stopRequested) {
            m_statusLabel->setText(QString("Completed %1 iterations").arg(m_totalIterations));
            m_statusLabel->setStyleSheet("color: #4CAF50;");
        }

        // Final statistics
        if (!m_results.empty()) {
            collectAllMeasurementKeys();
            collectActiveCountColumns();
            updateStatisticsSummary();
        }
        return;
    }

    m_currentIteration++;
    updateProgressUI();

    captureAndExecute();
}

void VisionReliabilityTestDialog::captureAndExecute()
{
    // Reuse whatever image the VisionManager already has as input. The user
    // verified it with a manual execute before opening this dialog, so it's
    // the right image to stress-test the plugin on.
    //
    // Rationale: earlier versions of this method re-grabbed a fresh camera
    // frame each iteration. That had two problems:
    //   1. It introduced camera noise as a confound — we were measuring
    //      camera stability, not plugin stability.
    //   2. It silently bypassed "Test Mode: Inject Image", so the test was
    //      running on a live frame even though the user had loaded a
    //      specific test image to validate against.
    // Reliability testing should isolate the plugin's behavior, not the
    // camera's — use the cached input image unconditionally.
    cv::Mat inputImage = VisionManager::instance().getInputImage();

    if (inputImage.empty()) {
        // Record as a failure
        ReliabilityIterationResult iterResult;
        iterResult.iteration = m_currentIteration;
        iterResult.success = false;
        iterResult.errorMessage = "No image available";
        m_results.push_back(iterResult);
        addResultRow(iterResult);

        // Continue to next iteration
        m_nextIterationTimer->start(50);
        return;
    }

    // Get current ROI
    VisionROI currentROI = VisionManager::instance().getROI();

    // Execute plugin with the same search-box configuration the wizard had
    // when this dialog was opened — otherwise plugins like PatternMatchEdge
    // can produce 0 matches because the manual run was relying on a search box.
    m_worker->executePlugin(m_pluginName, inputImage, currentROI,
                            m_useSearchBox,
                            m_searchStartX, m_searchStartY,
                            m_searchEndX, m_searchEndY);
}

void VisionReliabilityTestDialog::onIterationFinished(const VisionResult& result,
                                                       const QString& /*pluginName*/)
{
    ReliabilityIterationResult iterResult;
    iterResult.iteration = m_currentIteration;
    iterResult.success = result.success;
    iterResult.processingTimeMs = result.processingTimeMs;
    iterResult.circleCount = static_cast<int>(result.circles.size());
    iterResult.pointCount = static_cast<int>(result.points.size());
    iterResult.lineCount = static_cast<int>(result.lines.size());
    iterResult.rectangleCount = static_cast<int>(result.rectangles.size());
    iterResult.beamCount = static_cast<int>(result.beams.size());
    iterResult.measurements = result.measurements;
    iterResult.errorMessage = result.errorMessage;

    // Flatten every geometric result into the measurements map so the
    // dynamic table picks it up automatically. Naming convention:
    //   <type><index>_<field>  (e.g. pt0_x, line1_angle, circ0_radius)
    // If a result has a non-empty name, the index is replaced with the name.
    auto label = [](const std::string& prefix, int idx, const std::string& name) {
        std::string base = prefix;
        if (!name.empty()) {
            base += name;
        } else {
            base += std::to_string(idx);
        }
        return base;
    };

    for (size_t i = 0; i < result.points.size(); ++i) {
        const auto& p = result.points[i];
        std::string base = label("pt", static_cast<int>(i), p.name);
        iterResult.measurements[base + "_x"] = p.pixelCoord.x;
        iterResult.measurements[base + "_y"] = p.pixelCoord.y;
        if (p.confidence != 1.0) {
            iterResult.measurements[base + "_conf"] = p.confidence;
        }
    }

    for (size_t i = 0; i < result.lines.size(); ++i) {
        const auto& l = result.lines[i];
        std::string base = label("line", static_cast<int>(i), l.name);
        iterResult.measurements[base + "_angle_deg"] = l.angleDeg;
        iterResult.measurements[base + "_length_px"] = l.lengthPx;
        iterResult.measurements[base + "_x1"] = l.startPx.x;
        iterResult.measurements[base + "_y1"] = l.startPx.y;
        iterResult.measurements[base + "_x2"] = l.endPx.x;
        iterResult.measurements[base + "_y2"] = l.endPx.y;
    }

    for (size_t i = 0; i < result.circles.size(); ++i) {
        const auto& c = result.circles[i];
        std::string base = label("circ", static_cast<int>(i), c.name);
        iterResult.measurements[base + "_cx"] = c.centerPx.x;
        iterResult.measurements[base + "_cy"] = c.centerPx.y;
        iterResult.measurements[base + "_radius_px"] = c.radiusPx;
        if (c.radiusMm > 0.0) {
            iterResult.measurements[base + "_radius_mm"] = c.radiusMm;
        }
    }

    for (size_t i = 0; i < result.rectangles.size(); ++i) {
        const auto& r = result.rectangles[i];
        std::string base = label("rect", static_cast<int>(i), r.name);
        iterResult.measurements[base + "_cx"] = r.centerPx.x;
        iterResult.measurements[base + "_cy"] = r.centerPx.y;
        iterResult.measurements[base + "_w"] = r.widthPx;
        iterResult.measurements[base + "_h"] = r.heightPx;
        iterResult.measurements[base + "_angle_deg"] = r.angleDeg;
    }

    for (size_t i = 0; i < result.beams.size(); ++i) {
        const auto& b = result.beams[i];
        std::string base = label("beam", static_cast<int>(i), b.name);
        iterResult.measurements[base + "_cx"] = b.centerPx.x;
        iterResult.measurements[base + "_cy"] = b.centerPx.y;
        iterResult.measurements[base + "_fwhm_major_px"] = b.fwhmMajorPx;
        iterResult.measurements[base + "_fwhm_minor_px"] = b.fwhmMinorPx;
        iterResult.measurements[base + "_mean_intensity"] = b.meanIntensity;
        iterResult.measurements[base + "_max_intensity"] = b.maxIntensity;
        iterResult.measurements[base + "_angle_deg"] = b.angleDeg;
        iterResult.measurements[base + "_ellipticity"] = b.ellipticity;
    }

    m_results.push_back(iterResult);

    // Detect if the dynamic column schema (measurement keys / active count columns)
    // changed this iteration. For most plugins the schema stabilizes after the
    // first successful iteration — skipping the full rebuild on every iteration
    // turns the previously O(N²) loop into an O(N) append.
    std::vector<std::string> prevKeys = m_allMeasurementKeys;
    std::vector<std::string> prevCountCols = m_activeCountColumns;
    collectAllMeasurementKeys();
    collectActiveCountColumns();
    bool schemaChanged = (prevKeys != m_allMeasurementKeys) ||
                         (prevCountCols != m_activeCountColumns);

    if (schemaChanged) {
        rebuildTableColumns();
    } else {
        addResultRow(iterResult);
    }

    // Yield to the event loop; the previous 100 ms added 10 s to a 100-iter run
    // for no benefit (worker is already async).
    if (!m_stopRequested) {
        m_nextIterationTimer->start(1);
    }
}

void VisionReliabilityTestDialog::onIterationError(const QString& errorMessage)
{
    ReliabilityIterationResult iterResult;
    iterResult.iteration = m_currentIteration;
    iterResult.success = false;
    iterResult.errorMessage = errorMessage.toStdString();

    m_results.push_back(iterResult);

    std::vector<std::string> prevKeys = m_allMeasurementKeys;
    std::vector<std::string> prevCountCols = m_activeCountColumns;
    collectAllMeasurementKeys();
    collectActiveCountColumns();
    bool schemaChanged = (prevKeys != m_allMeasurementKeys) ||
                         (prevCountCols != m_activeCountColumns);

    if (schemaChanged) {
        rebuildTableColumns();
    } else {
        addResultRow(iterResult);
    }

    if (!m_stopRequested) {
        m_nextIterationTimer->start(1);
    }
}

void VisionReliabilityTestDialog::addResultRow(const ReliabilityIterationResult& result)
{
    int row = m_resultsTable->rowCount();
    m_resultsTable->insertRow(row);
    fillRowCells(row, result);
    m_resultsTable->scrollToBottom();
}

void VisionReliabilityTestDialog::fillRowCells(int row, const ReliabilityIterationResult& result)
{
    const int fixedCols = 3;
    const int countCols = static_cast<int>(m_activeCountColumns.size());

    m_resultsTable->setItem(row, 0, new QTableWidgetItem(QString::number(result.iteration)));

    QTableWidgetItem* passItem = new QTableWidgetItem(result.success ? "PASS" : "FAIL");
    passItem->setForeground(result.success ? QColor("#4CAF50") : QColor("#F44336"));
    passItem->setTextAlignment(Qt::AlignCenter);
    m_resultsTable->setItem(row, 1, passItem);

    m_resultsTable->setItem(row, 2, new QTableWidgetItem(
        QString::number(result.processingTimeMs, 'f', 2)));

    auto countValue = [](const std::string& name, const ReliabilityIterationResult& r) -> int {
        if (name == "Circles")    return r.circleCount;
        if (name == "Points")     return r.pointCount;
        if (name == "Lines")      return r.lineCount;
        if (name == "Rectangles") return r.rectangleCount;
        if (name == "Beams")      return r.beamCount;
        return 0;
    };

    for (int c = 0; c < countCols; ++c) {
        const auto& name = m_activeCountColumns[static_cast<size_t>(c)];
        m_resultsTable->setItem(row, fixedCols + c,
            new QTableWidgetItem(QString::number(countValue(name, result))));
    }

    for (size_t c = 0; c < m_allMeasurementKeys.size(); ++c) {
        const auto& key = m_allMeasurementKeys[c];
        auto it = result.measurements.find(key);
        QString val = "-";
        if (it != result.measurements.end()) {
            val = QString::number(it->second, 'f', 4);
        }
        m_resultsTable->setItem(row, fixedCols + countCols + static_cast<int>(c),
            new QTableWidgetItem(val));
    }
}

void VisionReliabilityTestDialog::updateProgressUI()
{
    m_progressBar->setValue(m_currentIteration);
    m_statusLabel->setText(QString("Running iteration %1/%2...")
                               .arg(m_currentIteration)
                               .arg(m_totalIterations));
    m_statusLabel->setStyleSheet("color: #2196F3;");
}

void VisionReliabilityTestDialog::collectAllMeasurementKeys()
{
    std::set<std::string> keySet;
    for (const auto& r : m_results) {
        for (const auto& kv : r.measurements) {
            keySet.insert(kv.first);
        }
    }
    m_allMeasurementKeys.assign(keySet.begin(), keySet.end());
}

void VisionReliabilityTestDialog::collectActiveCountColumns()
{
    // A count column is "active" only if at least one iteration produced > 0
    // of that object type — keeps the table free of irrelevant zero columns.
    m_activeCountColumns.clear();

    auto anyNonZero = [&](auto getter) {
        for (const auto& r : m_results) {
            if (getter(r) > 0) return true;
        }
        return false;
    };

    if (anyNonZero([](const ReliabilityIterationResult& r) { return r.circleCount; })) {
        m_activeCountColumns.push_back("Circles");
    }
    if (anyNonZero([](const ReliabilityIterationResult& r) { return r.pointCount; })) {
        m_activeCountColumns.push_back("Points");
    }
    if (anyNonZero([](const ReliabilityIterationResult& r) { return r.lineCount; })) {
        m_activeCountColumns.push_back("Lines");
    }
    if (anyNonZero([](const ReliabilityIterationResult& r) { return r.rectangleCount; })) {
        m_activeCountColumns.push_back("Rectangles");
    }
    if (anyNonZero([](const ReliabilityIterationResult& r) { return r.beamCount; })) {
        m_activeCountColumns.push_back("Beams");
    }
}

void VisionReliabilityTestDialog::rebuildTableColumns()
{
    const int fixedCols = 3;  // #, Pass, Time
    const int countCols = static_cast<int>(m_activeCountColumns.size());
    const int measCols  = static_cast<int>(m_allMeasurementKeys.size());
    const int totalCols = fixedCols + countCols + measCols;

    // Suppress repaints for the duration of the rebuild — without this,
    // every setItem() call can trigger a visual update.
    m_resultsTable->setUpdatesEnabled(false);

    m_resultsTable->setColumnCount(totalCols);

    QStringList headers = {"#", "Pass", "Time (ms)"};
    for (const auto& name : m_activeCountColumns) {
        headers << QString::fromStdString(name);
    }
    for (const auto& key : m_allMeasurementKeys) {
        headers << QString::fromStdString(key);
    }
    m_resultsTable->setHorizontalHeaderLabels(headers);

    // Re-populate every row under the new column schema. Called only when
    // the measurement-key set or active count columns change — usually once,
    // on the first successful iteration.
    m_resultsTable->setRowCount(0);
    for (size_t i = 0; i < m_results.size(); ++i) {
        m_resultsTable->insertRow(static_cast<int>(i));
        fillRowCells(static_cast<int>(i), m_results[i]);
    }

    // Size columns once after the bulk fill rather than per-setItem.
    m_resultsTable->resizeColumnsToContents();

    m_resultsTable->setUpdatesEnabled(true);
    m_resultsTable->scrollToBottom();
}

void VisionReliabilityTestDialog::updateStatisticsSummary()
{
    if (m_results.empty()) {
        m_statisticsText->setPlainText("No results yet.");
        return;
    }

    int total = static_cast<int>(m_results.size());
    int passCount = 0;
    std::vector<double> processingTimes;

    for (const auto& r : m_results) {
        if (r.success) passCount++;
        processingTimes.push_back(r.processingTimeMs);
    }

    double passRate = (total > 0) ? (static_cast<double>(passCount) / total * 100.0) : 0.0;

    QString text;
    text += QString("=== Reliability Test Summary ===\n");
    text += QString("Total: %1  |  Pass: %2  |  Fail: %3  |  Pass Rate: %4%\n\n")
                .arg(total).arg(passCount).arg(total - passCount)
                .arg(passRate, 0, 'f', 1);

    // Processing time stats
    auto timeStats = computeStats("Processing Time (ms)", processingTimes);
    text += QString("Processing Time (ms):  Mean: %1  StdDev: %2  Min: %3  Max: %4\n")
                .arg(timeStats.mean, 0, 'f', 2)
                .arg(timeStats.stdDev, 0, 'f', 2)
                .arg(timeStats.min, 0, 'f', 2)
                .arg(timeStats.max, 0, 'f', 2);

    // Object count stats — only for column types the plugin actually produced
    auto countValue = [](const std::string& name, const ReliabilityIterationResult& r) -> int {
        if (name == "Circles")    return r.circleCount;
        if (name == "Points")     return r.pointCount;
        if (name == "Lines")      return r.lineCount;
        if (name == "Rectangles") return r.rectangleCount;
        if (name == "Beams")      return r.beamCount;
        return 0;
    };

    for (const auto& name : m_activeCountColumns) {
        std::vector<double> vals;
        for (const auto& r : m_results) {
            vals.push_back(static_cast<double>(countValue(name, r)));
        }
        auto stats = computeStats(name, vals);
        text += QString("%1:  Mean: %2  Min: %3  Max: %4\n")
                    .arg(QString::fromStdString(name))
                    .arg(stats.mean, 0, 'f', 1)
                    .arg(stats.min, 0, 'f', 0)
                    .arg(stats.max, 0, 'f', 0);
    }

    // Measurement stats
    if (!m_allMeasurementKeys.empty()) {
        text += "\n--- Measurements ---\n";
        for (const auto& key : m_allMeasurementKeys) {
            std::vector<double> vals;
            for (const auto& r : m_results) {
                auto it = r.measurements.find(key);
                if (it != r.measurements.end()) {
                    vals.push_back(it->second);
                }
            }
            if (!vals.empty()) {
                auto stats = computeStats(key, vals);
                text += QString("%1:  Mean: %2  StdDev: %3  Min: %4  Max: %5  (n=%6)\n")
                            .arg(QString::fromStdString(key))
                            .arg(stats.mean, 0, 'f', 4)
                            .arg(stats.stdDev, 0, 'f', 4)
                            .arg(stats.min, 0, 'f', 4)
                            .arg(stats.max, 0, 'f', 4)
                            .arg(stats.count);
            }
        }
    }

    m_statisticsText->setPlainText(text);
}

MeasurementStatistics VisionReliabilityTestDialog::computeStats(
    const std::string& key, const std::vector<double>& values)
{
    MeasurementStatistics stats;
    stats.key = key;
    stats.count = static_cast<int>(values.size());

    if (values.empty()) return stats;

    double sum = 0.0;
    double sumSq = 0.0;
    stats.min = values[0];
    stats.max = values[0];

    for (double v : values) {
        sum += v;
        sumSq += v * v;
        if (v < stats.min) stats.min = v;
        if (v > stats.max) stats.max = v;
    }

    stats.mean = sum / values.size();

    // Population stddev with float rounding guard
    double variance = (sumSq / values.size()) - (stats.mean * stats.mean);
    stats.stdDev = (variance > 0.0) ? std::sqrt(variance) : 0.0;

    return stats;
}

void VisionReliabilityTestDialog::onExportCsvClicked()
{
    if (m_results.empty()) {
        QMessageBox::information(this, "Export", "No results to export.");
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(
        this, "Export Reliability Test Results", "", "CSV Files (*.csv);;All Files (*)");

    if (filePath.isEmpty()) return;

    std::ofstream file(filePath.toStdString());
    if (!file.is_open()) {
        QMessageBox::warning(this, "Export Error", "Cannot open file for writing:\n" + filePath);
        return;
    }

    // Ensure measurement keys and active count columns are collected
    collectAllMeasurementKeys();
    collectActiveCountColumns();

    auto countValue = [](const std::string& name, const ReliabilityIterationResult& r) -> int {
        if (name == "Circles")    return r.circleCount;
        if (name == "Points")     return r.pointCount;
        if (name == "Lines")      return r.lineCount;
        if (name == "Rectangles") return r.rectangleCount;
        if (name == "Beams")      return r.beamCount;
        return 0;
    };

    // Write header
    file << "Iteration,Pass,ProcessingTime_ms";
    for (const auto& name : m_activeCountColumns) {
        file << "," << name;
    }
    for (const auto& key : m_allMeasurementKeys) {
        file << "," << key;
    }
    file << "\n";

    // Write data rows
    for (const auto& r : m_results) {
        file << r.iteration << ","
             << (r.success ? "PASS" : "FAIL") << ","
             << r.processingTimeMs;

        for (const auto& name : m_activeCountColumns) {
            file << "," << countValue(name, r);
        }

        for (const auto& key : m_allMeasurementKeys) {
            auto it = r.measurements.find(key);
            if (it != r.measurements.end()) {
                file << "," << it->second;
            } else {
                file << ",";
            }
        }
        file << "\n";
    }

    // Write summary block as comments
    int total = static_cast<int>(m_results.size());
    int passCount = 0;
    for (const auto& r : m_results) {
        if (r.success) passCount++;
    }
    double passRate = (total > 0) ? (static_cast<double>(passCount) / total * 100.0) : 0.0;

    file << "\n";
    file << "# === Reliability Test Summary ===\n";
    file << "# Plugin: " << m_pluginName.toStdString() << "\n";
    file << "# Total: " << total << "  Pass: " << passCount
         << "  Fail: " << (total - passCount)
         << "  Pass Rate: " << passRate << "%\n";

    // Processing time stats
    std::vector<double> times;
    for (const auto& r : m_results) {
        times.push_back(r.processingTimeMs);
    }
    auto timeStats = computeStats("ProcessingTime", times);
    file << "# Processing Time (ms): Mean=" << timeStats.mean
         << " StdDev=" << timeStats.stdDev
         << " Min=" << timeStats.min
         << " Max=" << timeStats.max << "\n";

    file.close();

    std::cout << "[ReliabilityTest] Results exported to: " << filePath.toStdString() << std::endl;
    QMessageBox::information(this, "Export Complete",
                             QString("Results exported to:\n%1").arg(filePath));
}

#endif // HAS_OPENCV

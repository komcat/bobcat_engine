// VisionReliabilityTestDialog.h
#pragma once

#ifdef HAS_OPENCV

#include <QDialog>
#include <QTableWidget>
#include <QProgressBar>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <vector>
#include <map>
#include <string>
#include <memory>

struct ApplicationContext;
class VisionProcessingWorker;
class LiveVideoSubscriber;
struct VisionResult;
class VisionROI;

// Per-iteration result data
struct ReliabilityIterationResult {
    int iteration = 0;
    bool success = false;
    double processingTimeMs = 0.0;
    int circleCount = 0;
    int pointCount = 0;
    int lineCount = 0;
    int rectangleCount = 0;
    int beamCount = 0;
    std::map<std::string, double> measurements;
    std::string errorMessage;
};

// Aggregate statistics for a numeric measurement
struct MeasurementStatistics {
    std::string key;
    int count = 0;
    double mean = 0.0;
    double stdDev = 0.0;
    double min = 0.0;
    double max = 0.0;
};

class VisionReliabilityTestDialog : public QDialog {
    Q_OBJECT

public:
    explicit VisionReliabilityTestDialog(ApplicationContext* context,
                                         const QString& pluginName,
                                         std::shared_ptr<LiveVideoSubscriber> frameSubscriber,
                                         QWidget* parent = nullptr);
    ~VisionReliabilityTestDialog() override;

private slots:
    void onStartClicked();
    void onStopClicked();
    void onExportCsvClicked();
    void onIterationFinished(const VisionResult& result, const QString& pluginName);
    void onIterationError(const QString& errorMessage);
    void startNextIteration();

private:
    void setupUI();
    void captureAndExecute();
    void addResultRow(const ReliabilityIterationResult& result);
    void updateProgressUI();
    void collectAllMeasurementKeys();
    void updateStatisticsSummary();
    void rebuildTableColumns();
    void setRunningState(bool running);

    // Statistics helpers
    static MeasurementStatistics computeStats(const std::string& key,
                                               const std::vector<double>& values);

    ApplicationContext* m_context;
    QString m_pluginName;
    std::shared_ptr<LiveVideoSubscriber> m_frameSubscriber;

    // Own dedicated worker (NOT shared with VisionManagerPage)
    VisionProcessingWorker* m_worker = nullptr;

    // Iteration state
    std::vector<ReliabilityIterationResult> m_results;
    QTimer* m_nextIterationTimer = nullptr;
    int m_totalIterations = 10;
    int m_currentIteration = 0;
    bool m_stopRequested = false;
    bool m_isRunning = false;

    // All unique measurement keys discovered across iterations
    std::vector<std::string> m_allMeasurementKeys;

    // UI widgets
    QSpinBox* m_iterationsSpin = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTableWidget* m_resultsTable = nullptr;
    QTextEdit* m_statisticsText = nullptr;
    QPushButton* m_exportBtn = nullptr;
};

#endif // HAS_OPENCV

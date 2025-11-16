// VisionProcessingWorker.cpp
#ifdef HAS_OPENCV

#include "VisionProcessingWorker.h"
#include "VisionManager.h"
#include "VisionROI.h"
#include <QCoreApplication>
#include <iostream>
#include <chrono>

// Register metatypes for Qt signal/slot system
static bool registerMetaTypes() {
    qRegisterMetaType<VisionResult>("VisionResult");
    qRegisterMetaType<VisionROI>("VisionROI");
    qRegisterMetaType<cv::Mat>("cv::Mat");
    return true;
}

static bool metaTypesRegistered = registerMetaTypes();

VisionProcessingWorker::VisionProcessingWorker(QObject* parent)
    : QObject(parent)
{
    // Create worker thread
    m_thread = new QThread(this);

    // Move this worker object to the thread
    this->moveToThread(m_thread);

    // Connect internal signal to processing slot (runs in worker thread)
    connect(this, &VisionProcessingWorker::requestProcessing,
            this, &VisionProcessingWorker::doProcessing, Qt::QueuedConnection);

    std::cout << "[VisionProcessingWorker] Worker created" << std::endl;
}

VisionProcessingWorker::~VisionProcessingWorker()
{
    stopWorkerThread();
    std::cout << "[VisionProcessingWorker] Worker destroyed" << std::endl;
}

void VisionProcessingWorker::startWorkerThread()
{
    if (!m_thread->isRunning()) {
        m_thread->start();
        m_threadRunning.store(true);
        std::cout << "[VisionProcessingWorker] Worker thread started" << std::endl;
    }
}

void VisionProcessingWorker::stopWorkerThread()
{
    if (m_thread && m_thread->isRunning()) {
        m_cancelRequested.store(true);
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            std::cerr << "[VisionProcessingWorker] Thread did not stop in time, terminating" << std::endl;
            m_thread->terminate();
            m_thread->wait();
        }
        m_threadRunning.store(false);
        std::cout << "[VisionProcessingWorker] Worker thread stopped" << std::endl;
    }
}

bool VisionProcessingWorker::isWorkerRunning() const
{
    return m_thread && m_thread->isRunning();
}

void VisionProcessingWorker::executePlugin(const QString& pluginName, const cv::Mat& inputImage,
                                           const VisionROI& roi, bool useSearchBox,
                                           int searchStartX, int searchStartY,
                                           int searchEndX, int searchEndY)
{
    if (m_isProcessing.load()) {
        emit processingError("Processing already in progress");
        return;
    }

    if (!isWorkerRunning()) {
        startWorkerThread();
    }

    // Reset flags
    m_cancelRequested.store(false);
    m_isProcessing.store(true);

    // Clone image for thread safety
    cv::Mat imageCopy = inputImage.clone();

    // Emit signal to trigger processing in worker thread
    emit requestProcessing(pluginName, imageCopy, roi, useSearchBox,
                          searchStartX, searchStartY, searchEndX, searchEndY);
}

void VisionProcessingWorker::cancelProcessing()
{
    if (m_isProcessing.load()) {
        m_cancelRequested.store(true);
        std::cout << "[VisionProcessingWorker] Cancel requested" << std::endl;
    }
}

void VisionProcessingWorker::doProcessing(const QString& pluginName, const cv::Mat& inputImage,
                                          const VisionROI& roi, bool useSearchBox,
                                          int searchStartX, int searchStartY,
                                          int searchEndX, int searchEndY)
{
    std::cout << "[VisionProcessingWorker] Starting processing in thread: "
              << QThread::currentThreadId() << std::endl;

    emit processingStarted(pluginName);

    try {
        VisionResult result;

        if (useSearchBox) {
            // Grid search mode
            emit processingProgress(0, "Starting grid search...");
            result = executeGridSearch(pluginName, inputImage, roi,
                                       searchStartX, searchStartY, searchEndX, searchEndY);
        } else {
            // Single execution mode
            emit processingProgress(10, "Preparing image...");

            // Check for cancellation
            if (m_cancelRequested.load()) {
                emit processingCancelled();
                m_isProcessing.store(false);
                return;
            }

            // Set input image in VisionManager (thread-safe operation)
            VisionManager::instance().setInputImage(inputImage);

            // Set ROI
            if (roi.isValid()) {
                VisionManager::instance().setROI(roi);
            }

            emit processingProgress(30, "Executing plugin...");

            // Check for cancellation
            if (m_cancelRequested.load()) {
                emit processingCancelled();
                m_isProcessing.store(false);
                return;
            }

            // Execute plugin
            result = VisionManager::instance().executePlugin(pluginName);

            emit processingProgress(100, "Processing complete");
        }

        // Check for cancellation one more time
        if (m_cancelRequested.load()) {
            emit processingCancelled();
        } else {
            emit processingFinished(result, pluginName);
        }

    } catch (const std::exception& e) {
        std::cerr << "[VisionProcessingWorker] Exception: " << e.what() << std::endl;
        emit processingError(QString("Processing error: %1").arg(e.what()));
    } catch (...) {
        std::cerr << "[VisionProcessingWorker] Unknown exception" << std::endl;
        emit processingError("Unknown processing error");
    }

    m_isProcessing.store(false);
    std::cout << "[VisionProcessingWorker] Processing finished" << std::endl;
}

VisionResult VisionProcessingWorker::executeGridSearch(const QString& pluginName,
                                                       const cv::Mat& inputImage,
                                                       const VisionROI& roi,
                                                       int searchStartX, int searchStartY,
                                                       int searchEndX, int searchEndY)
{
    VisionResult bestResult;
    bestResult.success = false;

    // Set input image
    VisionManager::instance().setInputImage(inputImage);

    // Create search box
    VisionSearchBox searchBox(searchStartX, searchStartY, searchEndX, searchEndY);
    int roiWidth = roi.getWidth();
    int roiHeight = roi.getHeight();

    // Store original ROI
    VisionROI originalROI = roi;

    double bestScore = -1.0;
    int bestGridIndex = -1;

    // Iterate through 9 grid positions
    for (int gridIndex = 0; gridIndex < 9; ++gridIndex) {
        // Check for cancellation
        if (m_cancelRequested.load()) {
            std::cout << "[VisionProcessingWorker] Grid search cancelled at grid " << gridIndex << std::endl;
            VisionManager::instance().setROI(originalROI);
            return bestResult;
        }

        // Update progress
        int progress = 10 + (gridIndex * 90) / 9;
        QString progressMsg = QString("Searching grid %1/9...").arg(gridIndex + 1);
        emit processingProgress(progress, progressMsg);

        // Get ROI for this grid cell
        VisionROI gridROI = searchBox.getROIForGridCell(gridIndex, roiWidth, roiHeight);
        VisionManager::instance().setROI(gridROI);

        // Execute plugin
        VisionResult gridResult = VisionManager::instance().executePlugin(pluginName);

        // Calculate score
        double score = 0.0;
        if (gridResult.success) {
            score += gridResult.circles.size() * 10.0;
            score += gridResult.lines.size() * 5.0;
            score += gridResult.points.size() * 8.0;

            // Check for confidence/score measurements
            if (gridResult.measurements.count("confidence") > 0) {
                score += gridResult.measurements.at("confidence") * 50.0;
            }
            if (gridResult.measurements.count("best_score") > 0) {
                score += gridResult.measurements.at("best_score") * 100.0;
            }
            if (gridResult.measurements.count("circularity") > 0) {
                score += gridResult.measurements.at("circularity") * 100.0;
            }
        }

        // Track best result
        if (score > bestScore) {
            bestScore = score;
            bestResult = gridResult;
            bestGridIndex = gridIndex;
        }

        std::cout << "[VisionProcessingWorker] Grid " << gridIndex << " score: " << score << std::endl;
    }

    // Restore original ROI
    VisionManager::instance().setROI(originalROI);

    // Add grid search metadata to result
    if (bestGridIndex >= 0) {
        bestResult.addMetadata("grid_search", "true");
        bestResult.addMetadata("best_grid_index", std::to_string(bestGridIndex));
        bestResult.addMetadata("best_grid_label", VisionSearchBox::getGridLabel(bestGridIndex));
        bestResult.addMeasurement("grid_best_score", bestScore);
    }

    emit processingProgress(100, "Grid search complete");

    return bestResult;
}

#endif // HAS_OPENCV

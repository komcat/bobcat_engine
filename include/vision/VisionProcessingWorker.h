// VisionProcessingWorker.h
#pragma once

#ifdef HAS_OPENCV

#include <QObject>
#include <QThread>
#include <QString>
#include <atomic>
#include <memory>
#include <opencv2/core.hpp>
#include "VisionResult.h"
#include "VisionROI.h"

/**
 * @brief Worker class for executing vision processing in a background thread
 *
 * This class enables non-blocking vision processing by running plugin execution
 * in a separate thread. Communication with the UI thread is done via Qt signals/slots.
 *
 * Usage:
 *   VisionProcessingWorker* worker = new VisionProcessingWorker();
 *   connect(worker, &VisionProcessingWorker::processingFinished, this, &MyClass::onResultReady);
 *   worker->executePlugin("CircleDetection", image, roi);
 */
class VisionProcessingWorker : public QObject {
    Q_OBJECT

public:
    explicit VisionProcessingWorker(QObject* parent = nullptr);
    ~VisionProcessingWorker();

    // Thread lifecycle
    void startWorkerThread();
    void stopWorkerThread();
    bool isWorkerRunning() const;

    // Processing state
    bool isProcessing() const { return m_isProcessing.load(); }
    void requestCancel() { m_cancelRequested.store(true); }
    bool isCancelRequested() const { return m_cancelRequested.load(); }

signals:
    // Progress and status signals (emitted from worker thread, received in UI thread)
    void processingStarted(const QString& pluginName);
    void processingProgress(int percent, const QString& message);
    void processingFinished(const VisionResult& result, const QString& pluginName);
    void processingError(const QString& errorMessage);
    void processingCancelled();

    // Internal signal for thread communication
    void requestProcessing(const QString& pluginName, const cv::Mat& inputImage,
                          const VisionROI& roi, bool useSearchBox,
                          int searchStartX, int searchStartY, int searchEndX, int searchEndY);

public slots:
    /**
     * @brief Execute a vision plugin asynchronously
     * @param pluginName Name of the plugin to execute
     * @param inputImage Input image (will be cloned for thread safety)
     * @param roi Region of Interest settings
     * @param useSearchBox Whether to use grid search with search box
     * @param searchStartX/Y/endX/endY Search box coordinates (if useSearchBox is true)
     */
    void executePlugin(const QString& pluginName, const cv::Mat& inputImage,
                      const VisionROI& roi, bool useSearchBox = false,
                      int searchStartX = 0, int searchStartY = 0,
                      int searchEndX = 0, int searchEndY = 0);

    /**
     * @brief Cancel the current processing operation
     */
    void cancelProcessing();

private slots:
    // Actual processing (runs in worker thread)
    void doProcessing(const QString& pluginName, const cv::Mat& inputImage,
                     const VisionROI& roi, bool useSearchBox,
                     int searchStartX, int searchStartY, int searchEndX, int searchEndY);

private:
    // Worker thread
    QThread* m_thread = nullptr;

    // Thread-safe flags
    std::atomic<bool> m_isProcessing{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_threadRunning{false};

    // Helper method for grid search
    VisionResult executeGridSearch(const QString& pluginName, const cv::Mat& inputImage,
                                   const VisionROI& roi, int searchStartX, int searchStartY,
                                   int searchEndX, int searchEndY);
};

#endif // HAS_OPENCV

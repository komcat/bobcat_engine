// SequenceContext.h - Execution-scoped data storage for sequence runs
#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>
#include <functional>
#include "CameraFrameData.h"

#ifdef HAS_OPENCV
#include <opencv2/core.hpp>
#include <optional>
#endif

// Forward declaration
class DataStore;

/**
 * @brief Execution-scoped data storage for sequence runs
 *
 * Stores temporary data during sequence execution including:
 * - Action return values
 * - Intermediate calculations
 * - Conditional evaluation results
 * - Execution metadata (timestamps, iteration counts, etc.)
 *
 * Lifetime: Created before execution, destroyed after completion
 * Thread-safe: Yes (for async execution support)
 *
 * NOT for persistent instrument data (use DataStore for that)
 */
class SequenceContext {
public:
    // Value types that can be stored
    enum class ValueType {
        NONE,
        NUMERIC,
        STRING,
        BOOLEAN,
        IMAGE_FRAME  // ✅ NEW: Store captured camera frames
    };

    // Stored value container
    struct Value {
        ValueType type = ValueType::NONE;
        float numericValue = 0.0f;
        std::string stringValue;
        bool booleanValue = false;
        std::shared_ptr<CameraFrameData> frameData;  // ✅ NEW: Image frame storage
        std::string tempFilePath;  // ✅ NEW: Temp file path for saved frame
#ifdef HAS_OPENCV
        std::shared_ptr<cv::Mat> imageFrame;  // ✅ cv::Mat storage for vision results
#endif

        std::chrono::system_clock::time_point timestamp;
        std::string sourceDevice;   // Device that produced this value
        std::string sourceMethod;   // Method that produced this value

        // Constructors
        Value();
        Value(float v, const std::string& device = "", const std::string& method = "");
        Value(const std::string& v, const std::string& device = "", const std::string& method = "");
        Value(bool v, const std::string& device = "", const std::string& method = "");
        Value(const CameraFrameData& frame, const std::string& device = "", const std::string& method = "");  // ✅ NEW
#ifdef HAS_OPENCV
        Value(const cv::Mat& image, const std::string& device = "", const std::string& method = "");  // ✅ cv::Mat constructor
#endif

        // Conversion helpers
        float AsFloat() const;
        bool AsBool() const;
        std::string AsString() const;
        std::shared_ptr<CameraFrameData> AsFrameData() const;  // ✅ NEW
    };

    SequenceContext();
    ~SequenceContext();

    // ======================================================================
    // VALUE OPERATIONS
    // ======================================================================

    // Set values
    void SetValue(const std::string& key, float value,
                  const std::string& device = "", const std::string& method = "");
    void SetValue(const std::string& key, const std::string& value,
                  const std::string& device = "", const std::string& method = "");
    void SetValue(const std::string& key, bool value,
                  const std::string& device = "", const std::string& method = "");
    void SetValue(const std::string& key, const Value& value);

    // ✅ NEW: Set captured frame (with optional temp file save)
    void SetFrameData(const std::string& key, const CameraFrameData& frameData,
                      bool saveToTempFile = true,
                      const std::string& device = "", const std::string& method = "");

#ifdef HAS_OPENCV
    // ✅ NEW: Set/Get cv::Mat image frames (for vision results)
    void SetImageFrame(const std::string& key, const cv::Mat& image,
                       const std::string& device = "", const std::string& method = "");
    std::optional<cv::Mat> GetImageFrame(const std::string& key) const;
#endif

    // Get values
    bool TryGetValue(const std::string& key, Value& outValue) const;
    Value GetValue(const std::string& key) const;
    float GetNumeric(const std::string& key, float defaultValue = 0.0f) const;
    std::string GetString(const std::string& key, const std::string& defaultValue = "") const;
    bool GetBoolean(const std::string& key, bool defaultValue = false) const;
    std::shared_ptr<CameraFrameData> GetFrameData(const std::string& key) const;  // ✅ NEW
    std::string GetFrameTempFilePath(const std::string& key) const;  // ✅ NEW

    // Check existence
    bool HasValue(const std::string& key) const;
    ValueType GetValueType(const std::string& key) const;

    // List all keys
    std::vector<std::string> GetAllKeys() const;

    // Clear operations
    void ClearValue(const std::string& key);
    void ClearAll();

    // ======================================================================
    // EXECUTION METADATA
    // ======================================================================

    // Execution tracking
    void SetExecutionStartTime();
    void SetExecutionEndTime();
    std::chrono::milliseconds GetExecutionDuration() const;

    void SetSequenceName(const std::string& name);
    std::string GetSequenceName() const;

    void IncrementStepCount();
    int GetStepCount() const;

    void SetCurrentStep(int stepIndex, const std::string& stepName);
    int GetCurrentStepIndex() const;
    std::string GetCurrentStepName() const;

    // Loop tracking (for nested loop support)
    void PushLoopIteration(int iteration);
    void PopLoopIteration();
    int GetCurrentLoopIteration() const;
    int GetLoopDepth() const;

    // ======================================================================
    // DATASTORE EXPORT (Optional)
    // ======================================================================

    // Export selected values to global DataStore
    void ExportToDataStore(DataStore* dataStore, const std::string& key);
    void ExportAllToDataStore(DataStore* dataStore, const std::string& prefix = "");

    // ======================================================================
    // EXECUTION CONTROL
    // ======================================================================

    // Request sequence to stop at next checkpoint
    void RequestStop() { m_stopRequested = true; }
    bool IsStopRequested() const { return m_stopRequested; }
    void ClearStopFlag() { m_stopRequested = false; }

    // Callback to check engine stop flag (set by SequenceEngine)
    // This allows command handlers to check stop during long-running operations
    using StopFlagCallback = std::function<bool()>;
    void SetEngineStopCallback(StopFlagCallback callback) { m_engineStopCallback = callback; }

    // Callback for wait progress updates (set by RecipeWorker/ProgramWorker)
    // Called during Wait/Delay commands to show countdown
    // Parameters: remainingMs, totalMs
    using WaitProgressCallback = std::function<void(int, int)>;
    void SetWaitProgressCallback(WaitProgressCallback callback) { m_waitProgressCallback = callback; }

    // Check if stop requested from ANY source (context OR engine)
    bool IsStopRequestedAny() const {
        if (m_stopRequested) return true;
        if (m_engineStopCallback && m_engineStopCallback()) return true;
        return false;
    }

    // Loop execution mode - auto-skip user interactions (prompts, manual adjust, etc.)
    void SetLoopExecutionMode(bool enabled) { m_loopExecutionMode = enabled; }
    bool IsLoopExecutionMode() const { return m_loopExecutionMode; }

    // Notify wait progress (used by SystemCommandHandler)
    void NotifyWaitProgress(int remainingMs, int totalMs) const {
        if (m_waitProgressCallback) {
            m_waitProgressCallback(remainingMs, totalMs);
        }
    }

    // ======================================================================
    // RESULT COLLECTION
    // ======================================================================

    // Get execution summary
    struct ExecutionSummary {
        std::string sequenceName;
        std::chrono::system_clock::time_point startTime;
        std::chrono::system_clock::time_point endTime;
        int totalSteps;
        std::map<std::string, Value> capturedValues;
    };

    ExecutionSummary GetSummary() const;

    // Serialize for logging/debugging
    std::string ToJson() const;
    void SaveToFile(const std::string& filepath) const;

    // ✅ NEW: Generate unique temp file path for captured frames
    static std::string GenerateTempFilePath(const std::string& cameraName);

private:
    mutable std::mutex m_mutex;

    // Data storage
    std::map<std::string, Value> m_values;

    // Execution metadata
    std::string m_sequenceName;
    std::chrono::system_clock::time_point m_startTime;
    std::chrono::system_clock::time_point m_endTime;
    int m_stepCount;
    int m_currentStepIndex;
    std::string m_currentStepName;

    // Loop tracking stack
    std::vector<int> m_loopIterationStack;

    // Execution control
    bool m_stopRequested = false;
    bool m_loopExecutionMode = false;  // Auto-skip user interactions in loop mode
    StopFlagCallback m_engineStopCallback;
    WaitProgressCallback m_waitProgressCallback;
};

// SequenceContext.cpp
#include "SequenceContext.h"
#include "DataStore.h"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <ctime>
#include <cmath>
#include <filesystem>
#include <iostream>

// ✅ For PNG saving via Qt
#include <QImage>
#include <QPixmap>

//=============================================================================
// VALUE IMPLEMENTATION
//=============================================================================

SequenceContext::Value::Value()
    : type(ValueType::NONE)
    , numericValue(0.0f)
    , booleanValue(false)
    , timestamp(std::chrono::system_clock::now())
{
}

SequenceContext::Value::Value(float v, const std::string& device, const std::string& method)
    : type(ValueType::NUMERIC)
    , numericValue(v)
    , booleanValue(v != 0.0f)
    , timestamp(std::chrono::system_clock::now())
    , sourceDevice(device)
    , sourceMethod(method)
{
    stringValue = std::to_string(v);
}

SequenceContext::Value::Value(const std::string& v, const std::string& device, const std::string& method)
    : type(ValueType::STRING)
    , stringValue(v)
    , timestamp(std::chrono::system_clock::now())
    , sourceDevice(device)
    , sourceMethod(method)
{
    // Try to parse as number
    try {
        numericValue = std::stof(v);
    } catch (...) {
        numericValue = 0.0f;
    }
    booleanValue = !v.empty() && v != "0" && v != "false" && v != "False" && v != "FALSE";
}

SequenceContext::Value::Value(bool v, const std::string& device, const std::string& method)
    : type(ValueType::BOOLEAN)
    , numericValue(v ? 1.0f : 0.0f)
    , stringValue(v ? "true" : "false")
    , booleanValue(v)
    , timestamp(std::chrono::system_clock::now())
    , sourceDevice(device)
    , sourceMethod(method)
{
}

// ✅ NEW: CameraFrameData constructor
SequenceContext::Value::Value(const CameraFrameData& frame, const std::string& device, const std::string& method)
    : type(ValueType::IMAGE_FRAME)
    , numericValue(0.0f)
    , booleanValue(frame.IsValid())
    , frameData(std::make_shared<CameraFrameData>(frame))
    , timestamp(std::chrono::system_clock::now())
    , sourceDevice(device)
    , sourceMethod(method)
{
    stringValue = frame.cameraId + " (" + std::to_string(frame.width) + "x" + std::to_string(frame.height) + ")";
}

float SequenceContext::Value::AsFloat() const {
    return numericValue;
}

bool SequenceContext::Value::AsBool() const {
    return booleanValue;
}

std::string SequenceContext::Value::AsString() const {
    switch (type) {
        case ValueType::NUMERIC:
            return std::to_string(numericValue);
        case ValueType::STRING:
            return stringValue;
        case ValueType::BOOLEAN:
            return booleanValue ? "true" : "false";
        case ValueType::IMAGE_FRAME:
            return stringValue;  // Camera ID and dimensions
        default:
            return "";
    }
}

// ✅ NEW: AsFrameData helper
std::shared_ptr<CameraFrameData> SequenceContext::Value::AsFrameData() const {
    if (type == ValueType::IMAGE_FRAME) {
        return frameData;
    }
    return nullptr;
}

//=============================================================================
// SEQUENCE CONTEXT IMPLEMENTATION
//=============================================================================

SequenceContext::SequenceContext()
    : m_stepCount(0)
    , m_currentStepIndex(-1)
{
    m_startTime = std::chrono::system_clock::now();
    m_endTime = m_startTime;
}

SequenceContext::~SequenceContext()
{
}

//=============================================================================
// VALUE OPERATIONS
//=============================================================================

void SequenceContext::SetValue(const std::string& key, float value,
                               const std::string& device, const std::string& method)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_values[key] = Value(value, device, method);
}

void SequenceContext::SetValue(const std::string& key, const std::string& value,
                               const std::string& device, const std::string& method)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_values[key] = Value(value, device, method);
}

void SequenceContext::SetValue(const std::string& key, bool value,
                               const std::string& device, const std::string& method)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_values[key] = Value(value, device, method);
}

void SequenceContext::SetValue(const std::string& key, const Value& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_values[key] = value;
}

bool SequenceContext::TryGetValue(const std::string& key, Value& outValue) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        outValue = it->second;
        return true;
    }
    return false;
}

SequenceContext::Value SequenceContext::GetValue(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second;
    }
    return Value();
}

float SequenceContext::GetNumeric(const std::string& key, float defaultValue) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second.AsFloat();
    }
    return defaultValue;
}

std::string SequenceContext::GetString(const std::string& key, const std::string& defaultValue) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second.AsString();
    }
    return defaultValue;
}

bool SequenceContext::GetBoolean(const std::string& key, bool defaultValue) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second.AsBool();
    }
    return defaultValue;
}

// ✅ NEW: Set captured frame data
void SequenceContext::SetFrameData(const std::string& key, const CameraFrameData& frameData,
                                    bool saveToTempFile,
                                    const std::string& device, const std::string& method)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    Value value(frameData, device, method);

    // Save to temp file if requested
    if (saveToTempFile && frameData.IsValid()) {
        std::string tempPath = GenerateTempFilePath(frameData.cameraId);

        // Create directory if needed
        std::filesystem::path path(tempPath);
        std::filesystem::create_directories(path.parent_path());

        // Create QImage from raw data
        QImage::Format format;
        if (frameData.channels == 3) {
            format = QImage::Format_RGB888;
        } else if (frameData.channels == 1) {
            format = QImage::Format_Grayscale8;
        } else if (frameData.channels == 4) {
            format = QImage::Format_RGBA8888;
        } else {
            std::cerr << "[SequenceContext] Unsupported channel count: " << frameData.channels << std::endl;
            m_values[key] = value;
            return;
        }

        QImage image(
            frameData.imageData.data(),
            frameData.width,
            frameData.height,
            frameData.width * frameData.channels,
            format
        );

        // Save as PNG
        if (image.save(QString::fromStdString(tempPath), "PNG")) {
            value.tempFilePath = tempPath;
            std::cout << "[SequenceContext] Saved frame to temp file: " << tempPath << std::endl;
        } else {
            std::cerr << "[SequenceContext] Failed to save frame to: " << tempPath << std::endl;
        }
    }

    m_values[key] = value;
}

// ✅ NEW: Get captured frame data
std::shared_ptr<CameraFrameData> SequenceContext::GetFrameData(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_values.find(key);
    if (it != m_values.end() && it->second.type == ValueType::IMAGE_FRAME) {
        return it->second.frameData;
    }
    return nullptr;
}

// ✅ NEW: Get temp file path for saved frame
std::string SequenceContext::GetFrameTempFilePath(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_values.find(key);
    if (it != m_values.end() && it->second.type == ValueType::IMAGE_FRAME) {
        return it->second.tempFilePath;
    }
    return "";
}

// ✅ NEW: Generate unique temp file path
std::string SequenceContext::GenerateTempFilePath(const std::string& cameraName)
{
    // Get current date
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);

    char dateBuffer[32];
    std::strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", &tm_now);

    // Generate unique instance ID using timestamp
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::string instanceId = std::to_string(ms);

    // Build path: temp/[date]/[instanceID].png
    std::filesystem::path tempPath = std::filesystem::path("temp") / dateBuffer / (instanceId + ".png");

    return tempPath.string();
}

bool SequenceContext::HasValue(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_values.find(key) != m_values.end();
}

SequenceContext::ValueType SequenceContext::GetValueType(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        return it->second.type;
    }
    return ValueType::NONE;
}

std::vector<std::string> SequenceContext::GetAllKeys() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> keys;
    keys.reserve(m_values.size());
    for (const auto& pair : m_values) {
        keys.push_back(pair.first);
    }
    return keys;
}

void SequenceContext::ClearValue(const std::string& key)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_values.erase(key);
}

void SequenceContext::ClearAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_values.clear();
}

//=============================================================================
// EXECUTION METADATA
//=============================================================================

void SequenceContext::SetExecutionStartTime()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_startTime = std::chrono::system_clock::now();
}

void SequenceContext::SetExecutionEndTime()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_endTime = std::chrono::system_clock::now();
}

std::chrono::milliseconds SequenceContext::GetExecutionDuration() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::chrono::duration_cast<std::chrono::milliseconds>(m_endTime - m_startTime);
}

void SequenceContext::SetSequenceName(const std::string& name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sequenceName = name;
}

std::string SequenceContext::GetSequenceName() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sequenceName;
}

void SequenceContext::IncrementStepCount()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stepCount++;
}

int SequenceContext::GetStepCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stepCount;
}

void SequenceContext::SetCurrentStep(int stepIndex, const std::string& stepName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentStepIndex = stepIndex;
    m_currentStepName = stepName;
}

int SequenceContext::GetCurrentStepIndex() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentStepIndex;
}

std::string SequenceContext::GetCurrentStepName() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentStepName;
}

void SequenceContext::PushLoopIteration(int iteration)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_loopIterationStack.push_back(iteration);
}

void SequenceContext::PopLoopIteration()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_loopIterationStack.empty()) {
        m_loopIterationStack.pop_back();
    }
}

int SequenceContext::GetCurrentLoopIteration() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_loopIterationStack.empty() ? -1 : m_loopIterationStack.back();
}

int SequenceContext::GetLoopDepth() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_loopIterationStack.size());
}

//=============================================================================
// DATASTORE EXPORT
//=============================================================================

void SequenceContext::ExportToDataStore(DataStore* dataStore, const std::string& key)
{
    if (!dataStore) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        dataStore->SetValue(key, it->second.AsFloat());
    }
}

void SequenceContext::ExportAllToDataStore(DataStore* dataStore, const std::string& prefix)
{
    if (!dataStore) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& pair : m_values) {
        std::string exportKey = prefix + pair.first;
        dataStore->SetValue(exportKey, pair.second.AsFloat());
    }
}

//=============================================================================
// RESULT COLLECTION
//=============================================================================

SequenceContext::ExecutionSummary SequenceContext::GetSummary() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ExecutionSummary summary;
    summary.sequenceName = m_sequenceName;
    summary.startTime = m_startTime;
    summary.endTime = m_endTime;
    summary.totalSteps = m_stepCount;
    summary.capturedValues = m_values;

    return summary;
}

std::string SequenceContext::ToJson() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ostringstream json;
    json << "{\n";
    json << "  \"sequenceName\": \"" << m_sequenceName << "\",\n";
    json << "  \"totalSteps\": " << m_stepCount << ",\n";

    // Duration
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(m_endTime - m_startTime);
    json << "  \"executionDurationMs\": " << duration.count() << ",\n";

    // Timestamps
    auto start_time_t = std::chrono::system_clock::to_time_t(m_startTime);
    auto end_time_t = std::chrono::system_clock::to_time_t(m_endTime);

    char start_buf[100], end_buf[100];
    std::strftime(start_buf, sizeof(start_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&start_time_t));
    std::strftime(end_buf, sizeof(end_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&end_time_t));

    json << "  \"startTime\": \"" << start_buf << "\",\n";
    json << "  \"endTime\": \"" << end_buf << "\",\n";

    // Values
    json << "  \"capturedValues\": {\n";
    bool first = true;
    for (const auto& pair : m_values) {
        if (!first) json << ",\n";
        first = false;

        json << "    \"" << pair.first << "\": {\n";
        json << "      \"value\": ";

        if (pair.second.type == ValueType::NUMERIC) {
            json << pair.second.numericValue;
        } else if (pair.second.type == ValueType::STRING) {
            json << "\"" << pair.second.stringValue << "\"";
        } else if (pair.second.type == ValueType::BOOLEAN) {
            json << (pair.second.booleanValue ? "true" : "false");
        } else {
            json << "null";
        }

        json << ",\n";
        json << "      \"type\": \"";
        switch (pair.second.type) {
            case ValueType::NUMERIC: json << "numeric"; break;
            case ValueType::STRING: json << "string"; break;
            case ValueType::BOOLEAN: json << "boolean"; break;
            default: json << "none"; break;
        }
        json << "\",\n";
        json << "      \"source\": \"" << pair.second.sourceDevice << "." << pair.second.sourceMethod << "\"\n";
        json << "    }";
    }
    json << "\n  }\n";
    json << "}\n";

    return json.str();
}

void SequenceContext::SaveToFile(const std::string& filepath) const
{
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << ToJson();
        file.close();
    }
}

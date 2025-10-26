// SequenceContext.cpp
#include "SequenceContext.h"
#include "DataStore.h"
#include <sstream>
#include <iomanip>
#include <fstream>
#include <ctime>
#include <cmath>

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
        default:
            return "";
    }
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

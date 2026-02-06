#include "RelativePositionCommandHandler.h"
#include "unified_motion_manager.h"
#include "SequenceContext.h"
#include "LogManager.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

bool RelativePositionCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // Note: manager parameter is unused - we use m_motionManager set via SetMotionManager

    if (methodName == "CaptureBaseline") {
        return HandleCaptureBaseline(params, errorMessage, context);
    }
    else if (methodName == "GetDelta") {
        return HandleGetDelta(params, errorMessage, context);
    }
    else if (methodName == "ClearBaseline") {
        return HandleClearBaseline(params, errorMessage);
    }
    else if (methodName == "ClearAllBaselines") {
        return HandleClearAllBaselines(errorMessage);
    }
    else if (methodName == "ListBaselines") {
        return HandleListBaselines(errorMessage);
    }

    errorMessage = "Unknown method: " + methodName +
                   ". Supported: CaptureBaseline, GetDelta, ClearBaseline, ClearAllBaselines, ListBaselines";
    return false;
}

bool RelativePositionCommandHandler::HandleCaptureBaseline(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // Validate required parameters
    auto uidIt = params.find("uid");
    auto parentIt = params.find("parent");
    auto childIt = params.find("child");

    if (uidIt == params.end()) {
        errorMessage = "CaptureBaseline: Missing required parameter 'uid'";
        return false;
    }
    if (parentIt == params.end()) {
        errorMessage = "CaptureBaseline: Missing required parameter 'parent'";
        return false;
    }
    if (childIt == params.end()) {
        errorMessage = "CaptureBaseline: Missing required parameter 'child'";
        return false;
    }

    const std::string& uid = uidIt->second;
    const std::string& parentDevice = parentIt->second;
    const std::string& childDevice = childIt->second;

    // Check motion manager is set
    if (!m_motionManager) {
        errorMessage = "CaptureBaseline: Motion manager not set";
        return false;
    }

    // Get current positions
    PositionStruct parentPos = m_motionManager->GetCurrentPosition(parentDevice);
    PositionStruct childPos = m_motionManager->GetCurrentPosition(childDevice);

    // Calculate relative offset (child relative to parent)
    PositionStruct relativeOffset;
    relativeOffset.x = childPos.x - parentPos.x;
    relativeOffset.y = childPos.y - parentPos.y;
    relativeOffset.z = childPos.z - parentPos.z;
    relativeOffset.u = childPos.u - parentPos.u;
    relativeOffset.v = childPos.v - parentPos.v;
    relativeOffset.w = childPos.w - parentPos.w;

    // Create baseline data
    BaselineData baseline;
    baseline.parentDevice = parentDevice;
    baseline.childDevice = childDevice;
    baseline.parentPosition = parentPos;
    baseline.childPosition = childPos;
    baseline.relativeOffset = relativeOffset;
    baseline.timestamp = GetCurrentTimestamp();

    // Store baseline (thread-safe)
    {
        std::lock_guard<std::mutex> lock(m_baselinesMutex);
        m_baselines[uid] = baseline;
    }

    // Log the capture
    std::ostringstream msg;
    msg << "Captured baseline '" << uid << "' - "
        << "Parent(" << parentDevice << "): ["
        << parentPos.x << ", " << parentPos.y << ", " << parentPos.z << "] | "
        << "Child(" << childDevice << "): ["
        << childPos.x << ", " << childPos.y << ", " << childPos.z << "] | "
        << "Offset: [" << relativeOffset.x << ", " << relativeOffset.y << ", " << relativeOffset.z << "]";

    if (m_logger) {
        m_logger->Info(msg.str(), "RelPos");
    }
    std::cout << "[RelPos] " << msg.str() << std::endl;

    // Optionally store baseline info in context
    if (context) {
        context->SetValue(uid + "_parent", parentDevice, "relpos", "CaptureBaseline");
        context->SetValue(uid + "_child", childDevice, "relpos", "CaptureBaseline");
        context->SetValue(uid + "_baseline_x", static_cast<float>(relativeOffset.x), "relpos", "CaptureBaseline");
        context->SetValue(uid + "_baseline_y", static_cast<float>(relativeOffset.y), "relpos", "CaptureBaseline");
        context->SetValue(uid + "_baseline_z", static_cast<float>(relativeOffset.z), "relpos", "CaptureBaseline");
    }

    return true;
}

bool RelativePositionCommandHandler::HandleGetDelta(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    // Validate required parameters
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
        errorMessage = "GetDelta: Missing required parameter 'uid'";
        return false;
    }

    if (!context) {
        errorMessage = "GetDelta: Sequence context is required to store results";
        return false;
    }

    if (!m_motionManager) {
        errorMessage = "GetDelta: Motion manager not set";
        return false;
    }

    const std::string& uid = uidIt->second;

    // Get output prefix (defaults to "delta")
    std::string outputPrefix = "delta";
    auto prefixIt = params.find("outputPrefix");
    if (prefixIt != params.end()) {
        outputPrefix = prefixIt->second;
    }

    // Find the baseline
    BaselineData baseline;
    {
        std::lock_guard<std::mutex> lock(m_baselinesMutex);
        auto it = m_baselines.find(uid);
        if (it == m_baselines.end()) {
            errorMessage = "GetDelta: Baseline '" + uid + "' not found. Call CaptureBaseline first.";
            return false;
        }
        baseline = it->second;
    }

    // Get current positions
    PositionStruct currentParentPos = m_motionManager->GetCurrentPosition(baseline.parentDevice);
    PositionStruct currentChildPos = m_motionManager->GetCurrentPosition(baseline.childDevice);

    // Calculate current relative offset
    PositionStruct currentOffset;
    currentOffset.x = currentChildPos.x - currentParentPos.x;
    currentOffset.y = currentChildPos.y - currentParentPos.y;
    currentOffset.z = currentChildPos.z - currentParentPos.z;
    currentOffset.u = currentChildPos.u - currentParentPos.u;
    currentOffset.v = currentChildPos.v - currentParentPos.v;
    currentOffset.w = currentChildPos.w - currentParentPos.w;

    // Calculate delta (drift from baseline)
    PositionStruct delta;
    delta.x = currentOffset.x - baseline.relativeOffset.x;
    delta.y = currentOffset.y - baseline.relativeOffset.y;
    delta.z = currentOffset.z - baseline.relativeOffset.z;
    delta.u = currentOffset.u - baseline.relativeOffset.u;
    delta.v = currentOffset.v - baseline.relativeOffset.v;
    delta.w = currentOffset.w - baseline.relativeOffset.w;

    // Store results in context with prefix
    context->SetValue(outputPrefix + "_x", static_cast<float>(delta.x), "relpos", "GetDelta");
    context->SetValue(outputPrefix + "_y", static_cast<float>(delta.y), "relpos", "GetDelta");
    context->SetValue(outputPrefix + "_z", static_cast<float>(delta.z), "relpos", "GetDelta");
    context->SetValue(outputPrefix + "_u", static_cast<float>(delta.u), "relpos", "GetDelta");
    context->SetValue(outputPrefix + "_v", static_cast<float>(delta.v), "relpos", "GetDelta");
    context->SetValue(outputPrefix + "_w", static_cast<float>(delta.w), "relpos", "GetDelta");

    // Also store the baseline UID reference
    context->SetValue(outputPrefix + "_baseline_uid", uid, "relpos", "GetDelta");

    // Log the delta
    std::ostringstream msg;
    msg << "Delta from baseline '" << uid << "': ["
        << delta.x << ", " << delta.y << ", " << delta.z << "] "
        << "(U:" << delta.u << ", V:" << delta.v << ", W:" << delta.w << ") "
        << "-> stored as " << outputPrefix << "_[x/y/z/u/v/w]";

    if (m_logger) {
        m_logger->Info(msg.str(), "RelPos");
    }
    std::cout << "[RelPos] " << msg.str() << std::endl;

    return true;
}

bool RelativePositionCommandHandler::HandleClearBaseline(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage)
{
    auto uidIt = params.find("uid");
    if (uidIt == params.end()) {
        errorMessage = "ClearBaseline: Missing required parameter 'uid'";
        return false;
    }

    const std::string& uid = uidIt->second;

    {
        std::lock_guard<std::mutex> lock(m_baselinesMutex);
        auto it = m_baselines.find(uid);
        if (it == m_baselines.end()) {
            errorMessage = "ClearBaseline: Baseline '" + uid + "' not found";
            return false;
        }
        m_baselines.erase(it);
    }

    if (m_logger) {
        m_logger->Info("Cleared baseline '" + uid + "'", "RelPos");
    }
    std::cout << "[RelPos] Cleared baseline '" << uid << "'" << std::endl;

    return true;
}

bool RelativePositionCommandHandler::HandleClearAllBaselines(std::string& /*errorMessage*/)
{

    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(m_baselinesMutex);
        count = m_baselines.size();
        m_baselines.clear();
    }

    std::string msg = "Cleared all baselines (" + std::to_string(count) + " removed)";
    if (m_logger) {
        m_logger->Info(msg, "RelPos");
    }
    std::cout << "[RelPos] " << msg << std::endl;

    return true;
}

bool RelativePositionCommandHandler::HandleListBaselines(std::string& /*errorMessage*/)
{

    std::lock_guard<std::mutex> lock(m_baselinesMutex);

    if (m_baselines.empty()) {
        std::string msg = "No baselines stored";
        if (m_logger) {
            m_logger->Info(msg, "RelPos");
        }
        std::cout << "[RelPos] " << msg << std::endl;
        return true;
    }

    std::ostringstream msg;
    msg << "Stored baselines (" << m_baselines.size() << "):\n";
    for (const auto& [uid, baseline] : m_baselines) {
        msg << "  - " << uid << ": "
            << baseline.parentDevice << " -> " << baseline.childDevice
            << " (captured: " << baseline.timestamp << ")\n";
    }

    if (m_logger) {
        m_logger->Info(msg.str(), "RelPos");
    }
    std::cout << "[RelPos] " << msg.str() << std::endl;

    return true;
}

std::string RelativePositionCommandHandler::GetCurrentTimestamp() const
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

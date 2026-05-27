// InterlockManager.cpp
#include "InterlockManager.h"
#include "unified_motion_manager.h"
#include "../pneumatics/PneumaticManager.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

namespace {
    // String <-> PneumaticPosition for JSON round-trip
    PneumaticPosition ParsePneumaticPosition(const std::string& s) {
        if (s == "Extended"  || s == "EXTENDED")  return PneumaticPosition::EXTENDED;
        if (s == "Retracted" || s == "RETRACTED") return PneumaticPosition::RETRACTED;
        if (s == "Extending" || s == "EXTENDING") return PneumaticPosition::EXTENDING;
        if (s == "Retracting"|| s == "RETRACTING")return PneumaticPosition::RETRACTING;
        if (s == "Unknown"   || s == "UNKNOWN")   return PneumaticPosition::UNKNOWN;
        if (s == "Hardware Error" || s == "HARDWARE_ERROR") return PneumaticPosition::HARDWARE_ERROR;
        if (s == "Timeout"   || s == "TIMEOUT")   return PneumaticPosition::TIMEOUT;
        return PneumaticPosition::RETRACTED;  // safe default
    }

    // Uppercase + dedupe axis string, returns "" if input empty (caller treats as "all")
    std::string NormalizeAxes(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (u == 'X' || u == 'Y' || u == 'Z' || u == 'U' || u == 'V' || u == 'W') {
                if (out.find(u) == std::string::npos) out.push_back(u);
            }
        }
        return out;
    }

    // Returns true if `affected` ∩ `blocked` is non-empty.
    // Either side empty means "all axes" (full set XYZUVW), so empty-vs-anything intersects.
    bool AxesIntersect(const std::string& affected, const std::string& blocked) {
        if (affected.empty() || blocked.empty()) return true;
        for (char a : affected) {
            if (blocked.find(a) != std::string::npos) return true;
        }
        return false;
    }
}

InterlockManager::InterlockManager() = default;
InterlockManager::~InterlockManager() = default;

bool InterlockManager::AddRule(const InterlockRule& rule) {
    if (m_rules.size() >= MAX_INTERLOCK_RULES) {
        return false;
    }
    m_rules.push_back(rule);
    return true;
}

void InterlockManager::RemoveRule(size_t index) {
    if (index < m_rules.size()) {
        m_rules.erase(m_rules.begin() + static_cast<ptrdiff_t>(index));
    }
}

void InterlockManager::UpdateRule(size_t index, const InterlockRule& rule) {
    if (index < m_rules.size()) {
        m_rules[index] = rule;
    }
}

void InterlockManager::SetRuleEnabled(size_t index, bool enabled) {
    if (index < m_rules.size()) {
        m_rules[index].enabled = enabled;
    }
}

bool InterlockManager::AddPneumaticRule(const PneumaticInterlockRule& rule) {
    if (m_pneumaticRules.size() >= MAX_PNEUMATIC_INTERLOCK_RULES) {
        return false;
    }
    m_pneumaticRules.push_back(rule);
    return true;
}

void InterlockManager::RemovePneumaticRule(size_t index) {
    if (index < m_pneumaticRules.size()) {
        m_pneumaticRules.erase(m_pneumaticRules.begin() + static_cast<ptrdiff_t>(index));
    }
}

void InterlockManager::UpdatePneumaticRule(size_t index, const PneumaticInterlockRule& rule) {
    if (index < m_pneumaticRules.size()) {
        m_pneumaticRules[index] = rule;
    }
}

void InterlockManager::SetPneumaticRuleEnabled(size_t index, bool enabled) {
    if (index < m_pneumaticRules.size()) {
        m_pneumaticRules[index].enabled = enabled;
    }
}

std::pair<bool, std::string> InterlockManager::CheckMove(const std::string& deviceName,
                                                         const std::string& axesAffected) const {
    const std::string affected = NormalizeAxes(axesAffected);

    // Position rules
    if (m_motionManager) {
        for (size_t i = 0; i < m_rules.size(); ++i) {
            const auto& rule = m_rules[i];
            if (!rule.enabled) continue;
            if (rule.blockedDevice != deviceName) continue;

            double currentValue = 0.0;
            if (!m_motionManager->GetPosition(rule.monitorDevice, rule.monitorAxis, currentValue)) {
                return { false, "Rule " + std::to_string(i + 1) + ": cannot read " +
                    rule.monitorDevice + " " + rule.monitorAxis + " position (fail-safe block)" };
            }

            if (EvaluateCondition(currentValue, rule.op, rule.threshold)) {
                std::ostringstream oss;
                oss << "Rule " << (i + 1) << ": " << rule.monitorDevice << " "
                    << rule.monitorAxis << " = " << currentValue << " "
                    << rule.op << " " << rule.threshold
                    << " -> " << rule.blockedDevice << " blocked";
                return { false, oss.str() };
            }
        }
    }

    // Pneumatic rules
    if (m_pneumaticManager) {
        for (size_t i = 0; i < m_pneumaticRules.size(); ++i) {
            const auto& r = m_pneumaticRules[i];
            if (!r.enabled) continue;
            if (r.blockedDevice != deviceName) continue;

            const std::string blocked = NormalizeAxes(r.blockedAxes);
            if (!AxesIntersect(affected, blocked)) continue;  // move doesn't touch blocked axes

            PneumaticPosition pos = m_pneumaticManager->GetPosition(r.slideName);
            if (pos == r.requiredState) continue;

            std::ostringstream oss;
            oss << "Pneumatic rule " << (i + 1) << ": " << r.slideName
                << " is " << GetPositionString(pos)
                << " (required " << GetPositionString(r.requiredState) << ")"
                << " -> " << r.blockedDevice;
            if (!blocked.empty()) {
                oss << " axes[" << blocked << "]";
            }
            oss << " blocked";
            return { false, oss.str() };
        }
    }

    return { true, "" };
}

bool InterlockManager::IsRuleTriggered(size_t index) const {
    if (index >= m_rules.size() || !m_motionManager) {
        return false;
    }

    const auto& rule = m_rules[index];
    if (!rule.enabled) {
        return false;
    }

    double currentValue = 0.0;
    if (!m_motionManager->GetPosition(rule.monitorDevice, rule.monitorAxis, currentValue)) {
        return true; // fail-safe: treat as triggered
    }

    return EvaluateCondition(currentValue, rule.op, rule.threshold);
}

bool InterlockManager::IsPneumaticRuleTriggered(size_t index) const {
    if (index >= m_pneumaticRules.size() || !m_pneumaticManager) {
        return false;
    }
    const auto& r = m_pneumaticRules[index];
    if (!r.enabled) {
        return false;
    }
    PneumaticPosition pos = m_pneumaticManager->GetPosition(r.slideName);
    return pos != r.requiredState;
}

bool InterlockManager::EvaluateCondition(double value, const std::string& op, double threshold) const {
    if (op == "<")  return value < threshold;
    if (op == ">")  return value > threshold;
    if (op == "<=") return value <= threshold;
    if (op == ">=") return value >= threshold;
    return false;
}

bool InterlockManager::LoadConfig(const std::string& configPath) {
    // Build .local.json path for saving (always save to local)
    std::string localPath = configPath;
    auto dotPos = localPath.rfind(".json");
    if (dotPos != std::string::npos) {
        localPath = localPath.substr(0, dotPos) + ".local.json";
    }
    m_configPath = localPath;  // Always save to .local.json

    // Try .local.json first, fall back to default
    std::string pathToLoad = localPath;
    std::ifstream testFile(localPath);
    if (!testFile.good()) {
        pathToLoad = configPath;
    }
    testFile.close();

    std::ifstream file(pathToLoad);
    if (!file.is_open()) {
        // No config file yet - that's fine, start with empty rules
        std::cout << "[InterlockManager] No config file found at "
                  << localPath << " or " << configPath
                  << " - starting with empty rules" << std::endl;
        return true;
    }

    json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        std::cerr << "[InterlockManager] Failed to parse " << pathToLoad
                  << ": " << e.what() << std::endl;
        file.close();
        return false;
    }
    file.close();

    m_rules.clear();
    if (j.contains("rules") && j["rules"].is_array()) {
        for (const auto& ruleJson : j["rules"]) {
            InterlockRule rule;
            rule.enabled = ruleJson.value("enabled", true);
            rule.monitorDevice = ruleJson.value("monitor_device", "");
            rule.monitorAxis = ruleJson.value("monitor_axis", "");
            rule.op = ruleJson.value("operator", "<");
            rule.threshold = ruleJson.value("threshold", 0.0);
            rule.blockedDevice = ruleJson.value("blocked_device", "");

            if (m_rules.size() < MAX_INTERLOCK_RULES) {
                m_rules.push_back(rule);
            }
        }
    }

    m_pneumaticRules.clear();
    if (j.contains("pneumatic_rules") && j["pneumatic_rules"].is_array()) {
        for (const auto& ruleJson : j["pneumatic_rules"]) {
            PneumaticInterlockRule rule;
            rule.enabled = ruleJson.value("enabled", true);
            rule.slideName = ruleJson.value("slide_name", "");
            rule.requiredState = ParsePneumaticPosition(ruleJson.value("required_state", "Retracted"));
            rule.blockedDevice = ruleJson.value("blocked_device", "");
            rule.blockedAxes = ruleJson.value("blocked_axes", "");

            if (m_pneumaticRules.size() < MAX_PNEUMATIC_INTERLOCK_RULES) {
                m_pneumaticRules.push_back(rule);
            }
        }
    }

    std::cout << "[InterlockManager] Loaded " << pathToLoad
              << ": " << m_rules.size() << " position rule(s), "
              << m_pneumaticRules.size() << " pneumatic rule(s)" << std::endl;
    for (size_t i = 0; i < m_pneumaticRules.size(); ++i) {
        const auto& r = m_pneumaticRules[i];
        std::cout << "[InterlockManager]   pneumatic[" << i << "]: "
                  << (r.enabled ? "enabled" : "disabled")
                  << " | slide=" << r.slideName
                  << " required=" << GetPositionString(r.requiredState)
                  << " -> block " << r.blockedDevice
                  << " axes=" << (r.blockedAxes.empty() ? "(all)" : r.blockedAxes)
                  << std::endl;
    }

    return true;
}

bool InterlockManager::SaveConfig(const std::string& configPath) const {
    std::string savePath = configPath;
    if (savePath.empty()) {
        savePath = m_configPath;
    }
    if (savePath.empty()) {
        return false;
    }

    json j;
    json rulesArray = json::array();

    for (const auto& rule : m_rules) {
        json ruleJson;
        ruleJson["enabled"] = rule.enabled;
        ruleJson["monitor_device"] = rule.monitorDevice;
        ruleJson["monitor_axis"] = rule.monitorAxis;
        ruleJson["operator"] = rule.op;
        ruleJson["threshold"] = rule.threshold;
        ruleJson["blocked_device"] = rule.blockedDevice;
        rulesArray.push_back(ruleJson);
    }

    j["rules"] = rulesArray;

    json pneumaticArray = json::array();
    for (const auto& r : m_pneumaticRules) {
        json ruleJson;
        ruleJson["enabled"] = r.enabled;
        ruleJson["slide_name"] = r.slideName;
        ruleJson["required_state"] = GetPositionString(r.requiredState);
        ruleJson["blocked_device"] = r.blockedDevice;
        ruleJson["blocked_axes"] = r.blockedAxes;
        pneumaticArray.push_back(ruleJson);
    }
    j["pneumatic_rules"] = pneumaticArray;

    std::ofstream file(savePath);
    if (!file.is_open()) {
        return false;
    }

    file << j.dump(4);
    file.close();
    return true;
}

// InterlockManager.cpp
#include "InterlockManager.h"
#include "unified_motion_manager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

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

std::pair<bool, std::string> InterlockManager::CheckMove(const std::string& deviceName) const {
    if (!m_motionManager) {
        return { true, "" };
    }

    for (size_t i = 0; i < m_rules.size(); ++i) {
        const auto& rule = m_rules[i];
        if (!rule.enabled) {
            continue;
        }
        if (rule.blockedDevice != deviceName) {
            continue;
        }

        // Read the monitor device's axis position
        double currentValue = 0.0;
        if (!m_motionManager->GetPosition(rule.monitorDevice, rule.monitorAxis, currentValue)) {
            // Cannot read position - fail-safe: block the move
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
        return true;
    }

    json j;
    file >> j;
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

    std::ofstream file(savePath);
    if (!file.is_open()) {
        return false;
    }

    file << j.dump(4);
    file.close();
    return true;
}

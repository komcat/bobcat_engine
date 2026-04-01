// InterlockManager.h
// Position-based interlock system: blocks moves on a device when another device's
// axis position meets a condition. Supports up to 3 rules.
#pragma once

#include <string>
#include <vector>
#include <utility>

// Forward declaration to avoid circular dependency
class UnifiedMotionManager;

static constexpr int MAX_INTERLOCK_RULES = 3;

struct InterlockRule {
    bool enabled = true;
    std::string monitorDevice;    // Device to watch (e.g. "hex-left")
    std::string monitorAxis;      // Axis to watch (e.g. "X")
    std::string op;               // Comparison operator: "<", ">", "<=", ">="
    double threshold = 0.0;       // Threshold value (e.g. -7.0)
    std::string blockedDevice;    // Device to block (e.g. "gantry-main")
};

class InterlockManager {
public:
    InterlockManager();
    ~InterlockManager();

    // Motion manager reference (for position queries during CheckMove)
    void SetMotionManager(UnifiedMotionManager* mgr) { m_motionManager = mgr; }

    // Rule management (max 3 rules)
    bool AddRule(const InterlockRule& rule);
    void RemoveRule(size_t index);
    void UpdateRule(size_t index, const InterlockRule& rule);
    void SetRuleEnabled(size_t index, bool enabled);
    const std::vector<InterlockRule>& GetRules() const { return m_rules; }
    size_t GetRuleCount() const { return m_rules.size(); }

    // Check if a move is allowed on the given device
    // Returns {allowed, reason}. If blocked, reason describes why.
    std::pair<bool, std::string> CheckMove(const std::string& deviceName) const;

    // Evaluate a single rule against current positions (for UI status display)
    // Returns true if the rule's condition is currently triggered (i.e. blocking)
    bool IsRuleTriggered(size_t index) const;

    // Config persistence
    bool LoadConfig(const std::string& configPath);
    bool SaveConfig(const std::string& configPath) const;

private:
    bool EvaluateCondition(double value, const std::string& op, double threshold) const;

    std::vector<InterlockRule> m_rules;
    UnifiedMotionManager* m_motionManager = nullptr;
    std::string m_configPath;
};

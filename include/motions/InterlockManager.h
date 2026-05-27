// InterlockManager.h
// Position-based interlock system: blocks moves on a device when another device's
// axis position meets a condition. Supports up to 3 position rules and 3
// pneumatic-state rules (block moves when a named slide is not in a required state).
#pragma once

#include "../pneumatics/Pneumatic_Types.h"  // PneumaticPosition

#include <string>
#include <vector>
#include <utility>

// Forward declarations to avoid circular dependencies
class UnifiedMotionManager;
class PneumaticManager;

static constexpr int MAX_INTERLOCK_RULES = 3;
static constexpr int MAX_PNEUMATIC_INTERLOCK_RULES = 3;

struct InterlockRule {
    bool enabled = true;
    std::string monitorDevice;    // Device to watch (e.g. "hex-left")
    std::string monitorAxis;      // Axis to watch (e.g. "X")
    std::string op;               // Comparison operator: "<", ">", "<=", ">="
    double threshold = 0.0;       // Threshold value (e.g. -7.0)
    std::string blockedDevice;    // Device to block (e.g. "gantry-main")
};

// Pneumatic-state interlock: blocks moves on `blockedDevice` whenever `slideName`
// is not in `requiredState`. Optional `blockedAxes` carves out axes that remain
// free (e.g. allow Z descent through an extended dispenser while blocking XYUVW).
// Empty `blockedAxes` means "block all axes" (the conservative default).
struct PneumaticInterlockRule {
    bool enabled = true;
    std::string slideName;                              // e.g. "Dispenser_Head"
    PneumaticPosition requiredState = PneumaticPosition::RETRACTED;
    std::string blockedDevice;                          // e.g. "gantry-main"
    std::string blockedAxes;                            // e.g. "XYUVW" (empty = all)
};

class InterlockManager {
public:
    InterlockManager();
    ~InterlockManager();

    // Motion / pneumatic manager references (queried during CheckMove)
    void SetMotionManager(UnifiedMotionManager* mgr) { m_motionManager = mgr; }
    void SetPneumaticManager(PneumaticManager* mgr) { m_pneumaticManager = mgr; }

    // Position-rule management (max 3 rules)
    bool AddRule(const InterlockRule& rule);
    void RemoveRule(size_t index);
    void UpdateRule(size_t index, const InterlockRule& rule);
    void SetRuleEnabled(size_t index, bool enabled);
    const std::vector<InterlockRule>& GetRules() const { return m_rules; }
    size_t GetRuleCount() const { return m_rules.size(); }

    // Pneumatic-rule management (max 3 rules)
    bool AddPneumaticRule(const PneumaticInterlockRule& rule);
    void RemovePneumaticRule(size_t index);
    void UpdatePneumaticRule(size_t index, const PneumaticInterlockRule& rule);
    void SetPneumaticRuleEnabled(size_t index, bool enabled);
    const std::vector<PneumaticInterlockRule>& GetPneumaticRules() const { return m_pneumaticRules; }
    size_t GetPneumaticRuleCount() const { return m_pneumaticRules.size(); }

    // Check if a move is allowed on the given device.
    // `axesAffected` is an upper-case axis string the caller is about to move
    // (e.g. "XYZ", "Z"); pneumatic rules only block when their blockedAxes
    // intersect this set. Empty `axesAffected` means "all axes" (XYZUVW).
    // Returns {allowed, reason}. If blocked, reason describes why.
    std::pair<bool, std::string> CheckMove(const std::string& deviceName,
                                           const std::string& axesAffected = "") const;

    // Evaluate a single position rule against current positions (for UI status display)
    bool IsRuleTriggered(size_t index) const;

    // Evaluate a single pneumatic rule against current slide state (for UI status display)
    bool IsPneumaticRuleTriggered(size_t index) const;

    // Config persistence
    bool LoadConfig(const std::string& configPath);
    bool SaveConfig(const std::string& configPath) const;

private:
    bool EvaluateCondition(double value, const std::string& op, double threshold) const;

    std::vector<InterlockRule> m_rules;
    std::vector<PneumaticInterlockRule> m_pneumaticRules;
    UnifiedMotionManager* m_motionManager = nullptr;
    PneumaticManager* m_pneumaticManager = nullptr;
    std::string m_configPath;
};

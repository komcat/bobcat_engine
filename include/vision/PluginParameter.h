// PluginParameter.h
#pragma once

#ifdef HAS_OPENCV

#include <string>
#include <vector>
#include <variant>
#include "nlohmann/json.hpp"

/**
 * @brief Parameter descriptor for automatic UI generation
 *
 * Defines metadata about plugin parameters so UI can auto-generate
 * appropriate input widgets (spinboxes, checkboxes, comboboxes, etc.)
 */
struct ParameterDescriptor {
    enum class Type {
        Int,        // Integer value (QSpinBox)
        Double,     // Floating point (QDoubleSpinBox)
        Bool,       // Boolean (QCheckBox)
        String,     // Text (QLineEdit)
        Choice      // Enumerated choice (QComboBox)
    };

    std::string key;            // JSON key name (e.g., "minRadius")
    std::string displayName;    // Human-readable name (e.g., "Min Radius")
    std::string tooltip;        // Tooltip description
    Type type = Type::Double;   // Parameter type
    std::string unit = "";      // Unit of measurement (e.g., "px", "mm", "°")
    std::string group = "";     // Group name for organizing parameters

    // Value constraints (interpret based on type)
    double minValue = 0.0;
    double maxValue = 100.0;
    double step = 1.0;
    double defaultValue = 0.0;

    // For Choice type
    std::vector<std::string> choices;      // Choice labels
    std::vector<int> choiceValues;         // Corresponding int values

    // Helper constructors
    static ParameterDescriptor createInt(
        const std::string& key,
        const std::string& displayName,
        int minVal, int maxVal, int defaultVal,
        const std::string& tooltip = "",
        const std::string& unit = "",
        int step = 1,
        const std::string& group = "")
    {
        ParameterDescriptor pd;
        pd.key = key;
        pd.displayName = displayName;
        pd.tooltip = tooltip;
        pd.type = Type::Int;
        pd.unit = unit;
        pd.group = group;
        pd.minValue = static_cast<double>(minVal);
        pd.maxValue = static_cast<double>(maxVal);
        pd.step = static_cast<double>(step);
        pd.defaultValue = static_cast<double>(defaultVal);
        return pd;
    }

    static ParameterDescriptor createDouble(
        const std::string& key,
        const std::string& displayName,
        double minVal, double maxVal, double defaultVal,
        const std::string& tooltip = "",
        const std::string& unit = "",
        double step = 1.0,
        const std::string& group = "")
    {
        ParameterDescriptor pd;
        pd.key = key;
        pd.displayName = displayName;
        pd.tooltip = tooltip;
        pd.type = Type::Double;
        pd.unit = unit;
        pd.group = group;
        pd.minValue = minVal;
        pd.maxValue = maxVal;
        pd.step = step;
        pd.defaultValue = defaultVal;
        return pd;
    }

    static ParameterDescriptor createBool(
        const std::string& key,
        const std::string& displayName,
        bool defaultVal,
        const std::string& tooltip = "",
        const std::string& group = "")
    {
        ParameterDescriptor pd;
        pd.key = key;
        pd.displayName = displayName;
        pd.tooltip = tooltip;
        pd.type = Type::Bool;
        pd.group = group;
        pd.defaultValue = defaultVal ? 1.0 : 0.0;
        return pd;
    }

    static ParameterDescriptor createChoice(
        const std::string& key,
        const std::string& displayName,
        const std::vector<std::string>& choices,
        const std::vector<int>& values,
        int defaultIndex,
        const std::string& tooltip = "",
        const std::string& group = "")
    {
        ParameterDescriptor pd;
        pd.key = key;
        pd.displayName = displayName;
        pd.tooltip = tooltip;
        pd.type = Type::Choice;
        pd.group = group;
        pd.choices = choices;
        pd.choiceValues = values;
        pd.defaultValue = static_cast<double>(defaultIndex);
        return pd;
    }

    // Convert to JSON for serialization
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["key"] = key;
        j["displayName"] = displayName;
        j["tooltip"] = tooltip;
        j["type"] = static_cast<int>(type);
        j["unit"] = unit;
        j["group"] = group;
        j["minValue"] = minValue;
        j["maxValue"] = maxValue;
        j["step"] = step;
        j["defaultValue"] = defaultValue;
        if (type == Type::Choice) {
            j["choices"] = choices;
            j["choiceValues"] = choiceValues;
        }
        return j;
    }

    // Create from JSON
    static ParameterDescriptor fromJson(const nlohmann::json& j) {
        ParameterDescriptor pd;
        pd.key = j.value("key", "");
        pd.displayName = j.value("displayName", "");
        pd.tooltip = j.value("tooltip", "");
        pd.type = static_cast<Type>(j.value("type", 0));
        pd.unit = j.value("unit", "");
        pd.group = j.value("group", "");
        pd.minValue = j.value("minValue", 0.0);
        pd.maxValue = j.value("maxValue", 100.0);
        pd.step = j.value("step", 1.0);
        pd.defaultValue = j.value("defaultValue", 0.0);
        if (pd.type == Type::Choice) {
            pd.choices = j.value("choices", std::vector<std::string>{});
            pd.choiceValues = j.value("choiceValues", std::vector<int>{});
        }
        return pd;
    }
};

/**
 * @brief Collection of parameter descriptors for a plugin
 */
struct ParameterSchema {
    std::string pluginName;
    std::string pluginCategory;
    std::vector<ParameterDescriptor> parameters;

    // Get all unique group names
    std::vector<std::string> getGroups() const {
        std::vector<std::string> groups;
        for (const auto& param : parameters) {
            if (!param.group.empty()) {
                bool found = false;
                for (const auto& g : groups) {
                    if (g == param.group) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    groups.push_back(param.group);
                }
            }
        }
        return groups;
    }

    // Get parameters by group
    std::vector<ParameterDescriptor> getParametersByGroup(const std::string& group) const {
        std::vector<ParameterDescriptor> result;
        for (const auto& param : parameters) {
            if (param.group == group) {
                result.push_back(param);
            }
        }
        return result;
    }

    // Get ungrouped parameters
    std::vector<ParameterDescriptor> getUngroupedParameters() const {
        std::vector<ParameterDescriptor> result;
        for (const auto& param : parameters) {
            if (param.group.empty()) {
                result.push_back(param);
            }
        }
        return result;
    }

    // Convert to JSON
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["pluginName"] = pluginName;
        j["pluginCategory"] = pluginCategory;
        j["parameters"] = nlohmann::json::array();
        for (const auto& param : parameters) {
            j["parameters"].push_back(param.toJson());
        }
        return j;
    }

    // Create from JSON
    static ParameterSchema fromJson(const nlohmann::json& j) {
        ParameterSchema schema;
        schema.pluginName = j.value("pluginName", "");
        schema.pluginCategory = j.value("pluginCategory", "");
        if (j.contains("parameters") && j["parameters"].is_array()) {
            for (const auto& pj : j["parameters"]) {
                schema.parameters.push_back(ParameterDescriptor::fromJson(pj));
            }
        }
        return schema;
    }
};

#endif // HAS_OPENCV

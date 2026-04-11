// ProcessSummary.cpp
#include "ProcessSummary.h"
#include "RecipeManager.h"

#include <QStringList>
#include <unordered_map>

namespace {

// Read a parameter value, returning empty string if the key is missing.
QString getParam(const ProcessInstance* step, const char* key)
{
  auto it = step->parameters.find(key);
  if (it == step->parameters.end()) {
    return QString();
  }
  return QString::fromStdString(it->second);
}

// Join non-empty fragments with " . " (middle dot).
QString joinFragments(std::initializer_list<QString> parts)
{
  QString out;
  for (const QString& p : parts) {
    if (p.isEmpty()) {
      continue;
    }
    if (!out.isEmpty()) {
      out += QString::fromUtf8(" \xC2\xB7 ");  // " · "
    }
    out += p;
  }
  return out;
}

// Strip a directory prefix from a path-like string for compact display.
QString basename(const QString& path)
{
  if (path.isEmpty()) {
    return path;
  }
  int slash = path.lastIndexOf(QChar('/'));
  int back  = path.lastIndexOf(QChar('\\'));
  int idx = (slash > back) ? slash : back;
  return (idx >= 0) ? path.mid(idx + 1) : path;
}

// Truncate to maxLen characters with an ellipsis.
QString elide(const QString& s, int maxLen)
{
  if (s.size() <= maxLen) {
    return s;
  }
  return s.left(maxLen - 1) + QChar(0x2026);  // …
}

// Generic fallback: pick the first 1-2 interesting params and join them.
QString genericSummary(const ProcessInstance* step)
{
  // Skip these meta-keys; they appear in many plugins but are not useful in
  // the Details column.
  static const std::unordered_map<std::string, bool> kSkip = {
    {"delay_before_ms", true}, {"delay_after_ms", true},
    {"timeout_ms", true},      {"settling_time_ms", true},
    {"inbounce_delay_ms", true}, {"debounce_delay_ms", true},
    {"value_type", true},      {"power_state", true},
    {"auto_start", true},
  };

  QStringList parts;
  for (const auto& [k, v] : step->parameters) {
    if (kSkip.count(k) > 0) {
      continue;
    }
    if (v.empty()) {
      continue;
    }
    parts << QString::fromStdString(k + "=" + v);
    if (parts.size() >= 2) {
      break;
    }
  }
  return parts.join(QString::fromUtf8(" \xC2\xB7 "));
}

}  // namespace

QString summarizeProcess(const ProcessInstance* step)
{
  if (step == nullptr) {
    return QString();
  }

  const std::string& t = step->processType;

  // ---------------------- Control Flow ----------------------
  // Block-ends and Else have nothing meaningful to summarize.
  if (t == "Conditional End" || t == "Loop End" || t == "Group End" ||
      t == "Conditional Else") {
    return QString();
  }
  if (t == "Conditional If") {
    QString key = getParam(step, "context_key");
    QString op  = getParam(step, "comparison");
    QString th  = getParam(step, "threshold");
    return elide(QString("if %1 %2 %3").arg(key, op, th).simplified(), 40);
  }

  // ---------------------- Motion Control ----------------------
  if (t == "Set Velocity") {
    return elide(joinFragments({getParam(step, "device"),
                                getParam(step, "speed") + " mm/s"}), 40);
  }
  if (t == "Move to Position") {
    return elide(joinFragments({getParam(step, "device"),
                                getParam(step, "target_position")}), 40);
  }
  if (t == "Move Relative") {
    QString axis = getParam(step, "axis");
    QString dist = getParam(step, "distance");
    QString axisDist = axis.isEmpty() ? dist : (axis + " " + dist + "mm");
    return elide(joinFragments({getParam(step, "device"), axisDist}), 40);
  }
  if (t == "Move Relative Multi-Axis") {
    QStringList axes;
    QString dx = getParam(step, "distance_x");
    QString dy = getParam(step, "distance_y");
    QString dz = getParam(step, "distance_z");
    if (!dx.isEmpty() && dx != "0") axes << "X" + dx;
    if (!dy.isEmpty() && dy != "0") axes << "Y" + dy;
    if (!dz.isEmpty() && dz != "0") axes << "Z" + dz;
    return elide(joinFragments({getParam(step, "device"), axes.join(" ")}), 40);
  }
  if (t == "Move Relative Theta") {
    return elide(joinFragments({getParam(step, "device"),
                                getParam(step, "axis"),
                                getParam(step, "angle") + " deg"}), 40);
  }
  if (t == "Pick") {
    return elide(joinFragments({getParam(step, "gripper_device"),
                                getParam(step, "pick_node")}), 40);
  }
  if (t == "Place") {
    return elide(joinFragments({getParam(step, "gripper_device"),
                                getParam(step, "place_node")}), 40);
  }
  if (t == "Pick and Place") {
    return elide(joinFragments({getParam(step, "pick_node"),
                                QString::fromUtf8("\xE2\x86\x92"),  // →
                                getParam(step, "place_node")}), 40);
  }
  if (t == "Axis Scan") {
    QString axis = getParam(step, "axis");
    QString range = getParam(step, "range_um");
    QString seek = getParam(step, "seek_mode");
    QString rangeText = range.isEmpty() ? QString() : (range + "µm " + seek);
    return elide(joinFragments({getParam(step, "device"), axis, rangeText}), 40);
  }
  if (t == "Grid Scan") {
    return elide(joinFragments({getParam(step, "device"),
                                getParam(step, "plane"),
                                getParam(step, "mode")}), 40);
  }
  if (t == "Grid Scan Extended" || t == "Grid Scan Dual Channel") {
    return elide(joinFragments({getParam(step, "device"),
                                getParam(step, "channel")}), 40);
  }
  if (t == "XCAN" || t == "XCAN Extended") {
    QString axes = getParam(step, "axes");
    QString channel = getParam(step, "channel");
    QString capture = getParam(step, "capture_as");
    QString tail;
    if (!capture.isEmpty()) {
      tail = QString::fromUtf8("\xE2\x86\x92 ctx:") + capture;  // → ctx:
    }
    return elide(joinFragments({getParam(step, "device"), axes, channel, tail}), 40);
  }
  if (t == "Fast Align XY" || t == "Fast Align Z-XY") {
    return elide(joinFragments({getParam(step, "device"),
                                getParam(step, "channel")}), 40);
  }

  // ---------------------- I/O & Pneumatics ----------------------
  if (t == "Set Output") {
    QString state = getParam(step, "state");
    QString stateText = (state == "true") ? "ON" : (state == "false" ? "OFF" : state);
    QString pin = getParam(step, "pin");
    QString pinState = pin.isEmpty() ? stateText : (pin + " = " + stateText);
    return elide(joinFragments({getParam(step, "io_device"), pinState}), 40);
  }
  if (t == "Set Output Name") {
    return elide(joinFragments({getParam(step, "io_device"),
                                getParam(step, "pin_name"),
                                getParam(step, "state")}), 40);
  }
  if (t == "Slide Extend" || t == "Slide Retract") {
    return elide(getParam(step, "slide_name"), 40);
  }

  // ---------------------- TEC & Laser ----------------------
  if (t == "Set TEC Temp") {
    QString temp = getParam(step, "temperature");
    QString tempText = temp.isEmpty() ? QString() : (temp + QString::fromUtf8("\xC2\xB0" "C"));
    return elide(joinFragments({getParam(step, "device"), tempText}), 40);
  }
  if (t == "Set Laser Current") {
    QString cur = getParam(step, "current_ma");
    QString curText = cur.isEmpty() ? QString() : (cur + " mA");
    return elide(joinFragments({getParam(step, "device"), curText}), 40);
  }
  if (t == "Read Temperature" || t == "Read Laser Current") {
    return elide(getParam(step, "device"), 40);
  }

  // ---------------------- Power Supply ----------------------
  if (t == "PSU Output On/Off") {
    QString chState = getParam(step, "channel");
    QString out = getParam(step, "output_state");
    if (!chState.isEmpty()) chState = "ch" + chState;
    QString chOut = chState.isEmpty() ? out : (chState + " " + out);
    return elide(joinFragments({getParam(step, "psu_device"), chOut}), 40);
  }
  if (t == "PSU Set Voltage") {
    QString ch = getParam(step, "channel");
    QString v  = getParam(step, "voltage");
    QString vText = v.isEmpty() ? QString() : (v + "V");
    QString chV = ch.isEmpty() ? vText : ("ch" + ch + " = " + vText);
    return elide(joinFragments({getParam(step, "psu_device"), chV}), 40);
  }
  if (t == "PSU Set Current") {
    QString ch = getParam(step, "channel");
    QString c  = getParam(step, "current");
    QString cText = c.isEmpty() ? QString() : (c + "mA");
    QString chC = ch.isEmpty() ? cText : ("ch" + ch + " = " + cText);
    return elide(joinFragments({getParam(step, "psu_device"), chC}), 40);
  }
  if (t == "PSU Read Voltage" || t == "PSU Read Current") {
    return elide(getParam(step, "psu_device"), 40);
  }

  // ---------------------- SMU Control ----------------------
  if (t == "SMU Read Current" || t == "SMU Read Voltage") {
    QString ch = getParam(step, "channel");
    QString cap = getParam(step, "captureAs");
    QString chText = ch.isEmpty() ? QString() : ("ch" + ch);
    QString tail;
    if (!cap.isEmpty()) {
      tail = QString::fromUtf8("\xE2\x86\x92 ctx:") + cap;
    }
    return elide(joinFragments({getParam(step, "device"), chText, tail}), 40);
  }
  if (t == "SMU Set Voltage") {
    QString v = getParam(step, "voltage");
    return elide(joinFragments({getParam(step, "device"),
                                v.isEmpty() ? QString() : (v + "V")}), 40);
  }
  if (t == "SMU Set Current") {
    QString c = getParam(step, "current");
    return elide(joinFragments({getParam(step, "device"),
                                c.isEmpty() ? QString() : (c + "A")}), 40);
  }
  if (t == "SMU Start Polling") {
    return elide(joinFragments({getParam(step, "device"),
                                getParam(step, "mode"),
                                getParam(step, "intervalMs") + "ms"}), 40);
  }
  if (t == "SMU Stop Polling") {
    return elide(getParam(step, "device"), 40);
  }

  // ---------------------- Data ----------------------
  if (t == "Set Context Value") {
    QString k = getParam(step, "key");
    QString v = getParam(step, "value");
    if (k.isEmpty()) return QString();
    return elide(k + " = " + v, 40);
  }
  if (t == "Set Context Boolean") {
    QString k = getParam(step, "key");
    QString v = getParam(step, "value");
    return elide(k + " = " + v, 40);
  }
  if (t == "Read DataStore") {
    return elide(getParam(step, "channel_name"), 40);
  }

  // ---------------------- System ----------------------
  if (t == "Wait") {
    QString ms = getParam(step, "duration_ms");
    return ms.isEmpty() ? QString() : (ms + " ms");
  }
  if (t == "Log Message") {
    return elide(getParam(step, "message"), 40);
  }
  if (t == "Manual Adjust") {
    QString step_um = getParam(step, "step_size_um");
    QString stepText = step_um.isEmpty() ? QString() : ("step " + step_um + "µm");
    return elide(joinFragments({getParam(step, "device_name"), stepText}), 40);
  }
  if (t == "Ask User") {
    return elide(getParam(step, "title"), 40);
  }
  if (t == "Comment") {
    return elide(QString("# ") + getParam(step, "text"), 40);
  }
  if (t == "Stop Sequence" || t == "Group Start") {
    return QString();
  }

  // ---------------------- Vision / Camera ----------------------
  if (t == "Execute Vision Profile" || t == "Execute Vision Profile (from File)" ||
      t == "Load Vision Profile") {
    QString prof = basename(getParam(step, "profile_path"));
    QString cam  = getParam(step, "camera_name");
    return elide(joinFragments({prof, cam}), 40);
  }
  if (t == "Capture to File") {
    return elide(joinFragments({getParam(step, "camera_name"),
                                basename(getParam(step, "filename"))}), 40);
  }
  if (t == "Capture to Context") {
    return elide(joinFragments({getParam(step, "camera_name"),
                                getParam(step, "context_key")}), 40);
  }

  // ---------------------- Lighting ----------------------
  if (t == "Light: Set Intensity") {
    QString ch = getParam(step, "channel");
    QString it = getParam(step, "intensity");
    return elide(joinFragments({getParam(step, "device_name"),
                                ch.isEmpty() ? QString() : ("ch" + ch),
                                it}), 40);
  }
  if (t == "Light: Turn On" || t == "Light: Turn Off") {
    return elide(joinFragments({getParam(step, "device_name"),
                                getParam(step, "channel")}), 40);
  }

  // ---------------------- Calibration ----------------------
  if (t == "Placement Correction Retrace") {
    return elide(getParam(step, "training_set_name"), 40);
  }

  // ---------------------- Sub-Recipe ----------------------
  if (t == "Sub-Recipe") {
    return elide(basename(getParam(step, "recipe_file")), 40);
  }

  // ---------------------- Generic fallback ----------------------
  return elide(genericSummary(step), 40);
}

QString categoryColor(const std::string& category)
{
  // Single source of truth for category -> stripe color.
  // Categories must match those returned by RecipeManager::getProcessCategories().
  if (category == "Motion Control" || category == "Motion") return "#4A90E2";  // blue
  if (category == "I/O & Pneumatics")                       return "#F5A623";  // amber
  if (category == "TEC & Laser")                            return "#D0021B";  // red
  if (category == "Power Supply")                           return "#7ED321";  // green
  if (category == "SMU Control")                            return "#50E3C2";  // teal
  if (category == "Vision" || category == "Camera")         return "#9013FE";  // purple
  if (category == "Data")                                   return "#B8E986";  // lime
  if (category == "System")                                 return "#F8E71C";  // yellow
  if (category == "Control Flow")                           return "#B8B8B8";  // gray
  if (category == "Calibration")                            return "#BD10E0";  // magenta
  if (category == "Dispensing")                             return "#FF7043";  // orange
  if (category == "Aux Window")                             return "#4DD0E1";  // cyan
  if (category == "Sub-Recipe")                             return "#FFD54F";  // gold
  if (category == "Custom Server")                          return "#90A4AE";  // slate
  if (category == "Comment")                                return "transparent";
  return "transparent";
}

// StateSnapshotManager.cpp
// See header for shape and contract.
#include "StateSnapshotManager.h"

#include "ApplicationContext.h"

// Hardware managers used by the dumpers.
#include "unified_motion_manager.h"
#include "TECManager.h"
#include "PowerSupplyManager.h"
#include "IPowerSupply.h"
#include "SMUManager.h"
#include "ISMUDevice.h"
#include "LightingManager.h"
#include "LightingTypes.h"
#include "EziIOManager.h"
#include "IEziIODevice.h"
#include "PneumaticManager.h"
#include "Pneumatic_Types.h"
#include "DataStore.h"
#include "DeviceUnderTest.h"
#include "product_position_manager.h"
#include "CameraManager.h"
#include "CameraCommandHandler.h"
#include "LogManager.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Cap on DataStore keys serialized per snapshot. The store can balloon during
// long runs and we'd rather truncate (and say so) than blow up the JSON.
// ---------------------------------------------------------------------------
namespace {
constexpr size_t kMaxDataStoreEntries = 500;
}

StateSnapshotManager::StateSnapshotManager(ApplicationContext* ctx)
  : m_ctx(ctx) {}

// ---------------------------------------------------------------------------
// Filename helpers
// ---------------------------------------------------------------------------
std::string StateSnapshotManager::SanitizeLabel(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
      out.push_back(c);
    } else if (c == ' ' || c == '.' || c == '/' || c == '\\') {
      out.push_back('-');
    }
    // else: drop
  }
  if (out.empty()) out = "snapshot";
  if (out.size() > 64) out.resize(64);
  return out;
}

std::string StateSnapshotManager::NowFilenameStamp() {
  auto now = std::chrono::system_clock::now();
  auto t   = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
  return buf;
}

std::string StateSnapshotManager::NowIso8601() {
  auto now = std::chrono::system_clock::now();
  auto t   = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  localtime_s(&tm, &t);
  char buf[40];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  return buf;
}

std::string StateSnapshotManager::ResolveProductName() const {
  if (!m_ctx) return "_no_product";
  if (m_ctx->currentDUT) {
    auto p = m_ctx->currentDUT->getProductName();
    if (!p.empty()) return p;
  }
  if (m_ctx->productPositionManager && m_ctx->productPositionManager->HasLoadedProduct()) {
    auto p = m_ctx->productPositionManager->GetCurrentProductName();
    if (!p.empty()) return p;
  }
  return "_no_product";
}

std::string StateSnapshotManager::GetSnapshotRoot() const {
  fs::path p = fs::path("data") / ResolveProductName() / "snapshots";
  return p.string();
}

// ---------------------------------------------------------------------------
// Per-subsystem dumpers. All failures get swallowed into root["errors"];
// the snapshot itself never fails because a single hardware read failed.
// ---------------------------------------------------------------------------

void StateSnapshotManager::DumpMeta(json& root, const SnapshotOptions& opts) const {
  root["schema_version"] = 1;
  root["timestamp"]      = NowIso8601();
  root["label"]          = opts.label;
  root["tags"]           = opts.tags;
  root["note"]           = opts.note;
  root["source"]         = opts.source;
  if (!opts.recipe_name.empty()) {
    root["recipe"] = {
      {"name",          opts.recipe_name},
      {"step_index",    opts.recipe_step_index},
      {"step_nickname", opts.recipe_step_nickname}
    };
  }
  root["product"]        = ResolveProductName();
  root["skipped"]        = json::object();
  root["errors"]         = json::array();
}

void StateSnapshotManager::DumpDut(json& root) const {
  if (!m_ctx || !m_ctx->currentDUT) {
    root["dut"] = nullptr;
    return;
  }
  try {
    root["dut"] = {
      {"serial",   m_ctx->currentDUT->getSerialNumber()},
      {"product",  m_ctx->currentDUT->getProductName()},
      {"created",  m_ctx->currentDUT->getCreationTimestamp()},
      {"entries",  m_ctx->currentDUT->getEntryCount()}
    };
  } catch (...) {
    root["errors"].push_back("dut: read failed");
  }
}

void StateSnapshotManager::DumpMotion(json& root) const {
  if (!m_ctx || !m_ctx->motionManager) return;
  json devices = json::object();
  try {
    auto names = m_ctx->motionManager->GetAllDeviceNames();
    for (const auto& name : names) {
      json dev = json::object();
      try {
        std::string axes = m_ctx->motionManager->GetInstalledAxes(name);
        dev["installed_axes"] = axes;

        // Prefer cached (non-blocking) read; fall back to live query if the
        // device has never been polled.
        PositionStruct pos;
        bool gotPos = m_ctx->motionManager->GetCachedPosition(name, pos);
        if (!gotPos) {
          pos = m_ctx->motionManager->GetCurrentPosition(name);
          gotPos = true;
          dev["position_source"] = "live";
        } else {
          dev["position_source"] = "cached";
        }
        if (axes.find('X') != std::string::npos) dev["x"] = pos.x;
        if (axes.find('Y') != std::string::npos) dev["y"] = pos.y;
        if (axes.find('Z') != std::string::npos) dev["z"] = pos.z;
        if (axes.find('U') != std::string::npos) dev["u"] = pos.u;
        if (axes.find('V') != std::string::npos) dev["v"] = pos.v;
        if (axes.find('W') != std::string::npos) dev["w"] = pos.w;
      } catch (...) {
        dev["error"] = "read failed";
      }
      devices[name] = dev;
    }
  } catch (...) {
    root["errors"].push_back("motion: enumerate failed");
  }
  root["motion"] = devices;
}

void StateSnapshotManager::DumpTec(json& root) const {
  if (!m_ctx || !m_ctx->tecManager) return;
  json devices = json::object();
  try {
    auto names = m_ctx->tecManager->GetAllDeviceNames();
    for (const auto& name : names) {
      try {
        TECStatus s = m_ctx->tecManager->GetDeviceStatus(name);
        if (!s.Connected) {
          root["skipped"]["tec"].push_back(name);
          continue;
        }
        devices[name] = {
          {"connected",         s.Connected},
          {"tec_on",            s.TECOn},
          {"laser_on",          s.LaserOn},
          {"temperature_c",     s.CurrentTemperature},
          {"tec_current_a",     s.CurrentTECCurrent},
          {"tec_voltage_v",     s.CurrentTECVoltage},
          {"laser_current_ma",  s.CurrentLaserCurrent},
          {"setpoint_temp_c",   s.SetpointTemperature},
          {"setpoint_laser_ma", s.SetpointLaserCurrent}
        };
      } catch (...) {
        devices[name] = { {"error", "read failed"} };
      }
    }
  } catch (...) {
    root["errors"].push_back("tec: enumerate failed");
  }
  root["tec"] = devices;
}

void StateSnapshotManager::DumpPsu(json& root) const {
  if (!m_ctx || !m_ctx->powerSupplyManager) return;
  json devices = json::object();
  try {
    auto names = m_ctx->powerSupplyManager->getDeviceNames();
    for (const auto& name : names) {
      PowerSupplyPtr psu = m_ctx->powerSupplyManager->getPowerSupply(name);
      if (!psu || !psu->isConnected()) {
        root["skipped"]["psu"].push_back(name);
        continue;
      }
      int channels = m_ctx->powerSupplyManager->getDeviceChannelCount(name);
      if (channels <= 0) channels = 1;
      json dev = json::object();
      dev["channels"] = channels;
      json chs = json::object();
      for (int ch = 1; ch <= channels; ++ch) {
        json one = json::object();
        try { one["voltage_v"]      = psu->readVoltage(ch);     } catch (...) {}
        try { one["current_a"]      = psu->readCurrent(ch);     } catch (...) {}
        try { one["set_voltage_v"]  = psu->readSetVoltage(ch);  } catch (...) {}
        try { one["set_current_a"]  = psu->readSetCurrent(ch);  } catch (...) {}
        try { one["output_on"]      = psu->readOutputState(ch); } catch (...) {}
        chs["ch" + std::to_string(ch)] = one;
      }
      dev["channels_data"] = chs;
      devices[name] = dev;
    }
  } catch (...) {
    root["errors"].push_back("psu: enumerate failed");
  }
  root["psu"] = devices;
}

void StateSnapshotManager::DumpSmu(json& root) const {
  if (!m_ctx || !m_ctx->smuManager) return;
  json devices = json::object();
  try {
    auto names = m_ctx->smuManager->GetDeviceNames();
    for (const auto& name : names) {
      SMUDevicePtr smu = m_ctx->smuManager->GetDevice(name);
      if (!smu || !smu->IsConnected()) {
        root["skipped"]["smu"].push_back(name);
        continue;
      }
      json dev = json::object();
      try {
        dev["device_id"]   = smu->GetDeviceID();
        dev["output_on"]   = smu->IsOutputOn();
        dev["channels"]    = smu->GetChannelCount();
        auto enabled = smu->GetEnabledChannels();
        json chs = json::object();
        for (int ch : enabled) {
          json one = json::object();
          try { one["current_a"] = smu->ReadChannel(ch); } catch (...) {}
          chs["ch" + std::to_string(ch)] = one;
        }
        dev["channels_data"] = chs;
      } catch (...) {
        dev["error"] = "read failed";
      }
      devices[name] = dev;
    }
  } catch (...) {
    root["errors"].push_back("smu: enumerate failed");
  }
  root["smu"] = devices;
}

void StateSnapshotManager::DumpLighting(json& root) const {
  if (!m_ctx || !m_ctx->lightingManager) return;
  json devices = json::object();
  try {
    auto ids = m_ctx->lightingManager->GetControllerIds();
    for (const auto& id : ids) {
      if (!m_ctx->lightingManager->IsConnected(id)) {
        root["skipped"]["lighting"].push_back(id);
        continue;
      }
      try {
        Lighting::ControllerStatus s = m_ctx->lightingManager->GetControllerStatus(id);
        json dev = {
          {"device_name", s.deviceName},
          {"connected",   s.connected},
          {"has_error",   s.hasError},
          {"error_code",  s.errorCode}
        };
        json chs = json::object();
        for (const auto& cs : s.channelStatuses) {
          chs[cs.name.empty() ? ("ch" + std::to_string(cs.channelNumber)) : cs.name] = {
            {"channel",   cs.channelNumber},
            {"intensity", cs.intensity},
            {"percent",   Lighting::IntensityToPercent(cs.intensity)},
            {"mode",      cs.mode},
            {"on",        cs.isOn}
          };
        }
        dev["channels"] = chs;
        devices[id] = dev;
      } catch (...) {
        devices[id] = { {"error", "read failed"} };
      }
    }
  } catch (...) {
    root["errors"].push_back("lighting: enumerate failed");
  }
  root["lighting"] = devices;
}

void StateSnapshotManager::DumpIo(json& root) const {
  if (!m_ctx || !m_ctx->ioManager) return;
  json devices = json::object();
  try {
    auto names = m_ctx->ioManager->GetAllDeviceNames();
    for (const auto& name : names) {
      IEziIODevice* dev = m_ctx->ioManager->GetDevice(name);
      if (!dev || !dev->IsConnected()) {
        root["skipped"]["io"].push_back(name);
        continue;
      }
      json d = json::object();
      try {
        uint64_t inputs = 0, latch = 0, outputs = 0, status = 0;
        // Cached reads — populated by the polling thread, no hardware round-trip.
        dev->GetLastInputStatus(inputs, latch);
        dev->GetLastOutputStatus(outputs, status);
        d["raw_inputs"]  = inputs;
        d["raw_outputs"] = outputs;
        d["input_count"]  = dev->GetInputCount();
        d["output_count"] = dev->GetOutputCount();

        // Named pins only — anonymous bits are encoded in raw_inputs/outputs.
        json inMap = json::object();
        for (const auto& pinName : dev->GetAllInputPinNames()) {
          int pin = dev->GetInputPinByName(pinName);
          if (pin >= 0 && pin < 64) {
            inMap[pinName] = ((inputs >> pin) & 1ULL) != 0ULL;
          }
        }
        json outMap = json::object();
        for (const auto& pinName : dev->GetAllOutputPinNames()) {
          int pin = dev->GetOutputPinByName(pinName);
          if (pin >= 0 && pin < 64) {
            outMap[pinName] = ((outputs >> pin) & 1ULL) != 0ULL;
          }
        }
        d["inputs"]  = inMap;
        d["outputs"] = outMap;
      } catch (...) {
        d["error"] = "read failed";
      }
      devices[name] = d;
    }
  } catch (...) {
    root["errors"].push_back("io: enumerate failed");
  }
  root["io"] = devices;
}

void StateSnapshotManager::DumpPneumatic(json& root) const {
  if (!m_ctx || !m_ctx->pneumaticManager) return;
  json slides = json::object();
  try {
    auto names = m_ctx->pneumaticManager->GetAllSlideNames();
    for (const auto& name : names) {
      try {
        PneumaticStatus s = m_ctx->pneumaticManager->GetStatus(name);
        slides[name] = {
          {"position",         GetPositionString(s.Position)},
          {"output_active",    s.OutputActive},
          {"extended_sensor",  s.ExtendedSensorActive},
          {"retracted_sensor", s.RetractedSensorActive},
          {"valid",            s.Valid}
        };
      } catch (...) {
        slides[name] = { {"error", "read failed"} };
      }
    }
  } catch (...) {
    root["errors"].push_back("pneumatic: enumerate failed");
  }
  root["pneumatic"] = slides;
}

void StateSnapshotManager::DumpDataStore(json& root) const {
  if (!m_ctx || !m_ctx->dataStore) return;
  json values = json::object();
  size_t total = 0;
  bool truncated = false;
  try {
    auto keys = m_ctx->dataStore->GetAllKeys();
    total = keys.size();
    for (size_t i = 0; i < keys.size(); ++i) {
      if (i >= kMaxDataStoreEntries) { truncated = true; break; }
      try {
        values[keys[i]] = m_ctx->dataStore->GetValue(keys[i], 0.0f);
      } catch (...) {}
    }
  } catch (...) {
    root["errors"].push_back("datastore: enumerate failed");
  }
  root["datastore"] = {
    {"total_keys", total},
    {"truncated",  truncated},
    {"max_entries", kMaxDataStoreEntries},
    {"values",     values}
  };
}

// ---------------------------------------------------------------------------
// Camera capture — dispatches CaptureToFile per-camera via a temporary
// CameraCommandHandler. Anything that fails (camera not grabbing, save
// error, ...) is silently appended to skipped.cameras rather than aborting.
// ---------------------------------------------------------------------------
std::vector<std::string> StateSnapshotManager::CaptureCameras(json& root,
                                                              const std::string& snapshotDir,
                                                              const std::string& baseName) {
  std::vector<std::string> saved;
  if (!m_ctx || !m_ctx->cameraManager) return saved;

  CameraCommandHandler cam(m_ctx);
  // CameraCommandHandler logs unconditionally in a few branches — share our
  // logger so a frame-capture failure doesn't NPE on m_logger.
  cam.SetLogger(m_ctx->logManager);
  auto ids = m_ctx->cameraManager->GetCameraIds();

  // CaptureToFile builds the path as data/<product>/<subfolder>/<filename>.
  // Our snapshotDir is already data/<product>/snapshots[/...], so we point
  // subfolder = "snapshots" and only pass the leaf filename.
  // ResolveProductName() must match what CaptureToFile would resolve to
  // (active DUT first, then loaded product); we pass it explicitly so
  // a snapshot taken without a DUT still lands next to the JSON.
  std::string product = ResolveProductName();

  for (const auto& id : ids) {
    auto* hw = m_ctx->cameraManager->GetCameraHardware(id);
    if (!hw) {
      root["skipped"]["cameras"].push_back(id);
      continue;
    }

    std::string leaf = baseName + "_" + id + ".png";
    std::map<std::string, std::string> params = {
      {"product_name", product},
      {"subfolder",    "snapshots"},
      {"flat",         "true"},
      {"filename",     leaf}
    };

    std::string err;
    bool ok = cam.ExecuteCommand(m_ctx->cameraManager, id, "CaptureToFile",
                                 params, err, nullptr);
    if (ok) {
      saved.push_back((fs::path(snapshotDir) / leaf).string());
    } else {
      root["skipped"]["cameras"].push_back(id);
    }
  }
  return saved;
}

// ---------------------------------------------------------------------------
// Index file: one JSON object per line summarizing each snapshot so the
// directory is greppable without parsing every snapshot file.
// ---------------------------------------------------------------------------
void StateSnapshotManager::AppendIndexLine(json& root,
                                           const std::string& snapshotDir,
                                           const SnapshotResult& result,
                                           const SnapshotOptions& opts) const {
  fs::path indexPath = fs::path(snapshotDir) / "index.jsonl";
  json summary = {
    {"timestamp", result.timestamp_iso},
    {"label",     result.label},
    {"tags",      opts.tags},
    {"source",    opts.source},
    {"file",      fs::path(result.snapshot_path).filename().string()},
    {"images",    static_cast<int>(result.image_paths.size())},
    {"dut",       (m_ctx && m_ctx->currentDUT) ? m_ctx->currentDUT->getSerialNumber() : std::string{}}
  };
  try {
    std::ofstream f(indexPath, std::ios::app);
    if (!f) {
      root["errors"].push_back("index: open failed");
      return;
    }
    f << summary.dump() << '\n';
  } catch (...) {
    root["errors"].push_back("index: write failed");
  }
}

// ---------------------------------------------------------------------------
// Capture — main entry point.
// ---------------------------------------------------------------------------
std::optional<SnapshotResult> StateSnapshotManager::Capture(const SnapshotOptions& opts) {
  if (!m_ctx) return std::nullopt;

  std::lock_guard<std::mutex> lock(m_mutex);

  SnapshotResult result;
  result.label         = opts.label;
  result.timestamp_iso = NowIso8601();

  // Build the directory & base filename.
  std::string product   = ResolveProductName();
  std::string stamp     = NowFilenameStamp();
  std::string safeLabel = SanitizeLabel(opts.label.empty() ? std::string("snapshot") : opts.label);
  std::string baseName  = stamp + "_" + safeLabel;
  fs::path    dir       = fs::path("data") / product / "snapshots";

  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) {
    result.error = "create_directories failed: " + ec.message();
    if (m_ctx->logManager) {
      m_ctx->logManager->Error("Snapshot dir create failed: " + ec.message(), "Snapshot");
    }
    return result;
  }
  m_lastDirCreated = dir.string();

  // If the base name collides (same second + same label), append _2, _3 ...
  fs::path jsonPath = dir / (baseName + ".json");
  for (int n = 2; fs::exists(jsonPath) && n < 1000; ++n) {
    jsonPath = dir / (baseName + "_" + std::to_string(n) + ".json");
  }
  // Update baseName to match the actual leaf so camera files line up.
  baseName = jsonPath.stem().string();

  // Build the JSON.
  json root;
  DumpMeta(root, opts);
  DumpDut(root);
  DumpMotion(root);
  DumpTec(root);
  DumpPsu(root);
  DumpSmu(root);
  DumpLighting(root);
  DumpIo(root);
  DumpPneumatic(root);
  DumpDataStore(root);

  if (opts.capture_image) {
    result.image_paths = CaptureCameras(root, dir.string(), baseName);
    root["images"] = result.image_paths;
  }

  // Serialize.
  try {
    std::ofstream f(jsonPath);
    if (!f) {
      result.error = "open snapshot file failed";
      return result;
    }
    f << root.dump(2);
  } catch (const std::exception& e) {
    result.error = std::string("write snapshot file failed: ") + e.what();
    return result;
  } catch (...) {
    result.error = "write snapshot file failed (unknown)";
    return result;
  }

  result.snapshot_path = jsonPath.string();
  result.success       = true;

  AppendIndexLine(root, dir.string(), result, opts);

  if (m_ctx->logManager) {
    m_ctx->logManager->Info(
      "Snapshot captured: " + result.snapshot_path +
        " (label='" + result.label + "', images=" +
        std::to_string(result.image_paths.size()) + ")",
      "Snapshot");
  }
  return result;
}

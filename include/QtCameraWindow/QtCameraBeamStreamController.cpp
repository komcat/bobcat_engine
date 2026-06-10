// QtCameraBeamStreamController.cpp
#include "QtCameraBeamStreamController.h"

#include "QtCameraViewport.h"
#include "customserver/BeamServerClient.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMetaObject>
#include <QSettings>
#include <QString>
#include <QVBoxLayout>

#include <chrono>
#include <fstream>
#include <iostream>

namespace {

const char* kGroupStyle =
    "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
    "            margin-top: 6px; padding-top: 4px; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 8px; }";

const char* kComboStyle =
    "QComboBox { background-color: #3e3e42; border: 1px solid #555; "
    "            color: #cccccc; padding: 2px 4px; }"
    "QComboBox QAbstractItemView { background-color: #3e3e42; color: #cccccc; }";

}  // namespace

QtCameraBeamStreamController::QtCameraBeamStreamController(
    QtCameraViewport* viewport,
    QWidget* parent)
  : QGroupBox("Beam Streaming", parent)
  , m_viewport(viewport)
{
  setStyleSheet(kGroupStyle);
  setupUI();
}

QtCameraBeamStreamController::~QtCameraBeamStreamController()
{
  shutdown();
}

void QtCameraBeamStreamController::shutdown()
{
  stopStreaming();
}

void QtCameraBeamStreamController::setupUI()
{
  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(4);
  layout->setContentsMargins(6, 8, 6, 6);

  m_enableCheck = new QCheckBox("Enable Beam Stream");
  layout->addWidget(m_enableCheck);

  layout->addWidget(new QLabel("Camera:"));

  m_cameraCombo = new QComboBox();
  m_cameraCombo->setStyleSheet(kComboStyle);
  layout->addWidget(m_cameraCombo);

  connect(m_enableCheck, &QCheckBox::toggled,
          this, &QtCameraBeamStreamController::onEnableToggled);
  connect(m_cameraCombo, &QComboBox::currentTextChanged,
          this, &QtCameraBeamStreamController::onCameraChanged);
}

void QtCameraBeamStreamController::enqueueFrame(const std::vector<uint8_t>& rgbData,
                                                uint32_t width, uint32_t height,
                                                const std::string& cameraId)
{
  if (!m_streamEnabled.load() || !m_threadRunning.load()) return;
  if (cameraId != m_streamCameraId) return;

  std::lock_guard<std::mutex> bLock(m_frameQueueMutex);
  if (m_frameQueue.size() < 2) {
    m_frameQueue.push(rgbData);
    m_queuedWidth  = width;
    m_queuedHeight = height;
  }
  m_frameQueueCV.notify_one();
}

void QtCameraBeamStreamController::setCurrentPrimaryFrameSize(uint32_t width, uint32_t height)
{
  m_currentPrimaryWidth.store(width);
  m_currentPrimaryHeight.store(height);
}

void QtCameraBeamStreamController::pushOverlayToViewport()
{
  if (!m_viewport) return;
  if (m_streamEnabled.load()) {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_viewport->setBeamOverlayData(true, m_lastResult, m_fps.load(),
                                   m_config.umPerPxX, m_config.umPerPxY);
  } else {
    m_viewport->setBeamOverlayData(false, nlohmann::json(), 0.0,
                                   m_config.umPerPxX, m_config.umPerPxY);
  }
}

void QtCameraBeamStreamController::refreshCameraList(const std::vector<std::string>& cameraIds)
{
  if (!m_cameraCombo) return;

  m_cameraCombo->blockSignals(true);
  const QString prevText = m_cameraCombo->currentText();
  m_cameraCombo->clear();
  m_cameraCombo->addItem("");
  for (const auto& id : cameraIds) {
    m_cameraCombo->addItem(QString::fromStdString(id));
  }
  const int restore = m_cameraCombo->findText(prevText);
  if (restore >= 0) m_cameraCombo->setCurrentIndex(restore);
  m_cameraCombo->blockSignals(false);
}

void QtCameraBeamStreamController::setStreamingEnabled(bool enabled,
                                                       const std::string& cameraId)
{
  // Point the combo at the requested camera before enabling so the stream
  // pulls from it. If the id isn't in the list yet (combo not refreshed),
  // set the filter id directly so enqueueFrame still accepts its frames.
  if (enabled && !cameraId.empty() && m_cameraCombo) {
    const int idx = m_cameraCombo->findText(QString::fromStdString(cameraId));
    if (idx >= 0) {
      m_cameraCombo->setCurrentIndex(idx);  // fires onCameraChanged -> m_streamCameraId
    } else {
      m_streamCameraId = cameraId;
    }
  }
  if (!m_enableCheck) return;
  // Toggling the checkbox runs the existing onEnableToggled() start/stop path.
  // A no-op when already in the requested state.
  if (m_enableCheck->isChecked() != enabled) {
    m_enableCheck->setChecked(enabled);
  }
}

bool QtCameraBeamStreamController::isStreamActive() const
{
  return m_streamEnabled.load() && m_threadRunning.load();
}

nlohmann::json QtCameraBeamStreamController::getLatestResult() const
{
  std::lock_guard<std::mutex> lock(m_resultMutex);
  return m_lastResult;
}

uint64_t QtCameraBeamStreamController::getResultSequence() const
{
  return m_resultSequence.load();
}

double QtCameraBeamStreamController::getFps() const
{
  return m_fps.load();
}

void QtCameraBeamStreamController::loadConfig()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  m_config.host       = settings.value("BeamServer/Host",       "127.0.0.1").toString().toStdString();
  m_config.port       = settings.value("BeamServer/StreamPort", 5567).toInt();
  m_config.umPerPxX   = settings.value("BeamServer/UmPerPxX",   0.25).toDouble();
  m_config.umPerPxY   = settings.value("BeamServer/UmPerPxY",   0.25).toDouble();
  m_config.rotate180  = settings.value("BeamServer/Rotate180",  false).toBool();
  m_config.intervalMs = settings.value("BeamServer/IntervalMs", 100).toInt();

  // Fallback: config/beam_stream.json
  const std::vector<std::string> paths = {
    "config/beam_stream.json",
    "./config/beam_stream.json",
    "../config/beam_stream.json"
  };
  for (const auto& path : paths) {
    std::ifstream f(path);
    if (!f.is_open()) continue;
    nlohmann::json j;
    try {
      f >> j;
    } catch (...) {
      continue;
    }
    m_config.host       = j.value("host",        m_config.host);
    m_config.port       = j.value("port",        m_config.port);
    m_config.umPerPxX   = j.value("um_per_px_x", m_config.umPerPxX);
    m_config.umPerPxY   = j.value("um_per_px_y", m_config.umPerPxY);
    m_config.rotate180  = j.value("rotate180",   m_config.rotate180);
    m_config.intervalMs = j.value("interval_ms", m_config.intervalMs);
    break;
  }
}

void QtCameraBeamStreamController::startStreaming()
{
  if (m_threadRunning.load()) return;

  loadConfig();

  m_streamClient = std::make_unique<BeamServerClient>();
  if (!m_streamClient->Connect(m_config.host, m_config.port)) {
    std::cerr << "[BeamStream] failed to connect to "
              << m_config.host << ":" << m_config.port << std::endl;
    m_streamClient.reset();
    return;
  }

  const uint32_t frameW = m_currentPrimaryWidth.load();
  const uint32_t frameH = m_currentPrimaryHeight.load();

  // Send STREAM_START
  const nlohmann::json startResp = m_streamClient->SendStreamStart(
      static_cast<int>(frameW), static_cast<int>(frameH),
      "mono8",
      m_config.rotate180,
      m_config.umPerPxX,
      m_config.umPerPxY,
      m_config.intervalMs);

  if (startResp.empty()) {
    std::cerr << "[BeamStream] STREAM_START failed" << std::endl;
    m_streamClient->Disconnect();
    m_streamClient.reset();
    return;
  }

  m_streamEnabled.store(true);
  m_threadRunning.store(true);
  m_thread = std::thread(&QtCameraBeamStreamController::threadLoop, this);
  std::cout << "[BeamStream] streaming started ("
            << m_config.host << ":" << m_config.port << ")" << std::endl;
}

void QtCameraBeamStreamController::stopStreaming()
{
  if (!m_threadRunning.load()) return;
  m_streamEnabled.store(false);
  m_threadRunning.store(false);
  m_frameQueueCV.notify_all();
  if (m_thread.joinable()) {
    m_thread.join();
  }

  // Send STREAM_STOP if still connected
  if (m_streamClient && m_streamClient->IsConnected()) {
    m_streamClient->SendStreamStop();
    m_streamClient->Disconnect();
  }
  m_streamClient.reset();

  // Clear queue
  {
    std::lock_guard<std::mutex> lock(m_frameQueueMutex);
    while (!m_frameQueue.empty()) m_frameQueue.pop();
  }

  std::cout << "[BeamStream] streaming stopped" << std::endl;
}

void QtCameraBeamStreamController::threadLoop()
{
  uint32_t seqNum = 0;
  auto fpsStart   = std::chrono::steady_clock::now();
  uint32_t fpsCount = 0;

  for (int iter = 0; iter < 10000000 && m_threadRunning.load(); ++iter) {
    std::vector<uint8_t> frameData;
    uint32_t width  = 0;
    uint32_t height = 0;

    {
      std::unique_lock<std::mutex> lock(m_frameQueueMutex);
      m_frameQueueCV.wait_for(lock,
          std::chrono::milliseconds(m_config.intervalMs),
          [this] {
            return !m_frameQueue.empty() || !m_threadRunning.load();
          });

      if (!m_threadRunning.load()) break;
      if (m_frameQueue.empty()) continue;

      frameData = std::move(m_frameQueue.front());
      m_frameQueue.pop();
      width  = m_queuedWidth;
      height = m_queuedHeight;
    }

    if (width == 0 || height == 0 || frameData.empty()) continue;
    if (!m_streamClient || !m_streamClient->IsConnected()) break;

    // Convert RGB to mono8 grayscale: simple average of R,G,B channels
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> grayData(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i) {
      const uint32_t r = frameData[i * 3 + 0];
      const uint32_t g = frameData[i * 3 + 1];
      const uint32_t b = frameData[i * 3 + 2];
      grayData[i] = static_cast<uint8_t>((r + g + b) / 3u);
    }

    const nlohmann::json resp = m_streamClient->SendStreamFrame(grayData, seqNum++);

    if (!resp.empty()) {
      {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_lastResult = resp;
      }
      m_resultSequence.fetch_add(1, std::memory_order_relaxed);

      // Trigger viewport repaint
      if (m_viewport) {
        QMetaObject::invokeMethod(m_viewport, "update", Qt::QueuedConnection);
      }
    }

    // Update FPS counter
    ++fpsCount;
    const auto now     = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - fpsStart).count();
    if (elapsed >= 1000) {
      m_fps.store(
          static_cast<double>(fpsCount) * 1000.0 / static_cast<double>(elapsed));
      fpsCount = 0;
      fpsStart = now;
    }
  }
}

void QtCameraBeamStreamController::onEnableToggled(bool checked)
{
  if (checked) {
    // Auto-select first camera if none selected
    if (m_cameraCombo && m_streamCameraId.empty() &&
        m_cameraCombo->count() > 1) {
      m_cameraCombo->setCurrentIndex(1); // skip empty first entry
    }
    m_streamEnabled.store(true);
    startStreaming();
  } else {
    stopStreaming();
    m_streamEnabled.store(false);
  }
  std::cout << "[BeamStream] streaming "
            << (checked ? "enabled" : "disabled") << std::endl;
}

void QtCameraBeamStreamController::onCameraChanged(const QString& cameraId)
{
  m_streamCameraId = cameraId.toStdString();
}

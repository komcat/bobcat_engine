// MoeScreeningWidget.cpp - see MoeScreeningWidget.h for the overview.

#include "MoeScreeningWidget.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "ScriptEngine.h"
#include "ScriptWorker.h"
#include "SequenceEngine.h"
#include "SequencePromptDialog.h"
#include "CameraManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QFont>
#include <QFileInfo>
#include <QStringList>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <QCoreApplication>
#include <QMetaObject>
#include <QApplication>
#include <QThread>
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>
#include <Qt>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <cmath>
#include <future>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {
constexpr const char* kMoeScreenScript    = "moe_screen_baseline.lua";
constexpr const char* kMoeCaptureAllScript = "moe_screen_capture_all.lua";
constexpr const char* kReportScript        = "make_moe_screen_report.py";
constexpr const char* kSwirCamera      = "swir_camera";
constexpr int         kPollMs          = 1000;
constexpr int         kDefaultExpUs    = 10000;   // default IR exposure (µs)
// Matches the camera SetExposure command ceiling (used by the run), so the
// live Apply and the run stay in sync.
constexpr int         kMaxExpUs        = 1000000;  // 1 s

// Poynt angle color bands (matching M4AASerialDataWidget): green < 0.5,
// yellow 0.5..<1.0, red >= 1.0 (on |value|).
constexpr double kPoyntGreenMax = 0.5;
constexpr double kPoyntRedMin   = 1.0;
const QString    kColorOk      = "#4caf50";
const QString    kColorWarn    = "#ffca28";
const QString    kColorBad     = "#ef5350";
const QString    kColorNeutral = "#888888";

// Large value-cell font. Setting a stylesheet replaces the prior style, so
// setFont() does not survive a per-update color change — the full font must be
// baked into every setStyleSheet() call for the value cells (as M4AA does).
const QString    kValueFontCss =
    "font-family:'RobotoMono',Consolas,monospace;font-size:20pt;font-weight:bold;";

const QString kYellowStyle =
    "QPushButton{background:#ffe600;color:#111;font-weight:bold;padding:6px;}"
    "QPushButton:disabled{background:#7a7400;color:#444;}";
const QString kGreenStyle =
    "QPushButton{background:#3ddc50;color:#062b0c;font-weight:bold;padding:8px;}"
    "QPushButton:disabled{background:#2a6e32;color:#0a3;}";

// Sanitize a string for use as a single path segment.
QString sanitizeSegment(QString s) {
  static const QString illegal = QStringLiteral("<>:\"/\\|?*");
  s = s.trimmed();
  for (QChar& c : s) {
    if (c.unicode() < 0x20 || illegal.contains(c)) c = QChar('_');
  }
  return s.isEmpty() ? QStringLiteral("unknown") : s;
}

// Minimal RFC-4180-aware CSV line splitter (handles quoted fields).
QStringList splitCsvLine(const QString& line) {
  QStringList out;
  QString cur;
  bool inQuotes = false;
  for (int i = 0; i < line.size(); ++i) {
    const QChar ch = line.at(i);
    if (inQuotes) {
      if (ch == '"') {
        if (i + 1 < line.size() && line.at(i + 1) == '"') { cur += '"'; ++i; }
        else inQuotes = false;
      } else {
        cur += ch;
      }
    } else {
      if (ch == '"') inQuotes = true;
      else if (ch == ',') { out << cur; cur.clear(); }
      else cur += ch;
    }
  }
  out << cur;
  return out;
}
}  // namespace

MoeScreeningWidget::MoeScreeningWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent), m_context(context) {
  setupUI();
  setBusy(false);

  m_timer = new QTimer(this);
  m_timer->setInterval(kPollMs);
  connect(m_timer, &QTimer::timeout, this, &MoeScreeningWidget::onTick);
  m_timer->start();
  onTick();
}

MoeScreeningWidget::~MoeScreeningWidget() {
  if (m_context && m_context->scriptEngine && m_context->scriptEngine->IsRunning()) {
    m_context->scriptEngine->RequestStop();
  }
  if (m_scriptWorker) {
    m_scriptWorker->requestStop();
    m_scriptWorker->wait(2000);
  }
}

void MoeScreeningWidget::setupUI() {
  auto* root = new QVBoxLayout(this);

  // --- Button grid ---------------------------------------------------------
  auto* grid = new QGridLayout();
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(8);

  m_gantrySafeBtn = new QPushButton("Gantry\nsafe");
  m_gantrySafeBtn->setToolTip("Move gantry-main to the 'safe' named position.");

  m_moveScreenerA1Btn = new QPushButton("Move to\nscreener-A1");
  m_moveIrA1Btn       = new QPushButton("Move\nscreen-A1-IR");
  m_saveScreenerA1Btn = new QPushButton("Save\nscreener-A1");
  m_saveIrA1Btn       = new QPushButton("Save\nscreen-A1-IR");

  m_moveScreenerA2Btn = new QPushButton("Move to\nscreener-A2");
  m_moveIrA2Btn       = new QPushButton("Move\nscreen-A2-IR");
  m_saveScreenerA2Btn = new QPushButton("Save\nscreener-A2");
  m_saveIrA2Btn       = new QPushButton("Save\nscreen-A2-IR");

  m_saveScreenerA1Btn->setStyleSheet(kYellowStyle);
  m_saveIrA1Btn->setStyleSheet(kYellowStyle);
  m_saveScreenerA2Btn->setStyleSheet(kYellowStyle);
  m_saveIrA2Btn->setStyleSheet(kYellowStyle);

  grid->addWidget(m_gantrySafeBtn,     0, 0);
  grid->addWidget(m_moveScreenerA1Btn, 0, 1);
  grid->addWidget(m_moveIrA1Btn,       0, 2);
  grid->addWidget(m_saveScreenerA1Btn, 0, 3);
  grid->addWidget(m_saveIrA1Btn,       0, 4);
  grid->addWidget(m_moveScreenerA2Btn, 1, 1);
  grid->addWidget(m_moveIrA2Btn,       1, 2);
  grid->addWidget(m_saveScreenerA2Btn, 1, 3);
  grid->addWidget(m_saveIrA2Btn,       1, 4);
  root->addLayout(grid);

  // Move handlers.
  connect(m_gantrySafeBtn,     &QPushButton::clicked, this,
          [this]{ moveToNamedGantryPosition("safe"); });
  connect(m_moveScreenerA1Btn, &QPushButton::clicked, this,
          [this]{ moveToNamedGantryPosition("screener-A1"); });
  connect(m_moveIrA1Btn,       &QPushButton::clicked, this,
          [this]{ applyExposure(m_expA1Spin->value());
                  moveToNamedGantryPosition("screen-A1-IR"); });
  connect(m_moveScreenerA2Btn, &QPushButton::clicked, this,
          [this]{ moveToNamedGantryPosition("screener-A2"); });
  connect(m_moveIrA2Btn,       &QPushButton::clicked, this,
          [this]{ applyExposure(m_expA2Spin->value());
                  moveToNamedGantryPosition("screen-A2-IR"); });

  // Save handlers.
  connect(m_saveScreenerA1Btn, &QPushButton::clicked, this,
          [this]{ saveCurrentToNamedGantryPosition("screener-A1"); });
  connect(m_saveIrA1Btn,       &QPushButton::clicked, this,
          [this]{ saveCurrentToNamedGantryPosition("screen-A1-IR"); });
  connect(m_saveScreenerA2Btn, &QPushButton::clicked, this,
          [this]{ saveCurrentToNamedGantryPosition("screener-A2"); });
  connect(m_saveIrA2Btn,       &QPushButton::clicked, this,
          [this]{ saveCurrentToNamedGantryPosition("screen-A2-IR"); });

  // --- IR camera exposure fields (µs, per IR position) ---------------------
  auto makeExpSpin = []() {
    auto* sp = new QSpinBox();
    sp->setRange(1, kMaxExpUs);
    sp->setValue(kDefaultExpUs);
    sp->setSingleStep(500);
    sp->setSuffix(" us");
    sp->setGroupSeparatorShown(true);
    sp->setToolTip("SWIR/IR camera exposure applied at this position at the "
                   "start of every Measure MOE run.");
    return sp;
  };
  auto* expRow = new QHBoxLayout();
  m_expA1Spin = makeExpSpin();
  m_expA2Spin = makeExpSpin();
  m_applyA1Btn = new QPushButton("Apply");
  m_applyA2Btn = new QPushButton("Apply");
  m_applyA1Btn->setToolTip("Apply this exposure to swir_camera now (live).");
  m_applyA2Btn->setToolTip("Apply this exposure to swir_camera now (live).");
  expRow->addWidget(new QLabel("IR exposure  screen-A1-IR:"));
  expRow->addWidget(m_expA1Spin);
  expRow->addWidget(m_applyA1Btn);
  expRow->addSpacing(16);
  expRow->addWidget(new QLabel("screen-A2-IR:"));
  expRow->addWidget(m_expA2Spin);
  expRow->addWidget(m_applyA2Btn);
  expRow->addSpacing(24);
  m_scanSettingsBtn = new QPushButton();
  m_scanSettingsBtn->setToolTip(
      "Quick-edit the beam-fit Z-scan: step size (µm) and number of steps. "
      "Applied to the next Measure MOE run; saved per product.");
  expRow->addWidget(m_scanSettingsBtn);
  expRow->addStretch(1);
  root->addLayout(expRow);
  refreshScanButtonText();
  connect(m_scanSettingsBtn, &QPushButton::clicked, this,
          &MoeScreeningWidget::openScanSettingsDialog);

  connect(m_applyA1Btn, &QPushButton::clicked, this,
          [this]{ applyExposure(m_expA1Spin->value()); });
  connect(m_applyA2Btn, &QPushButton::clicked, this,
          [this]{ applyExposure(m_expA2Spin->value()); });

  // Persist exposure edits for the active product.
  connect(m_expA1Spin, qOverload<int>(&QSpinBox::valueChanged), this,
          [this]{ saveExposures(); });
  connect(m_expA2Spin, qOverload<int>(&QSpinBox::valueChanged), this,
          [this]{ saveExposures(); });

  // --- Measure row ---------------------------------------------------------
  auto* measureRow = new QHBoxLayout();
  m_measureBtn = new QPushButton("Measure MOE");
  m_measureBtn->setStyleSheet(kGreenStyle);
  m_measureBtn->setToolTip(
      "Prompt for a MOE ID (not a DUT id), then run moe_screen_baseline.lua: "
      "measure IR beams at screen-A1-IR and screen-A2-IR and log to "
      "data/<product>/moe-screen/<MOE_ID>.csv.");
  m_measureStatsBtn = new QPushButton("Measure\nstatistics");
  m_measureStatsBtn->setStyleSheet(kGreenStyle);
  m_repeatSpin = new QSpinBox();
  m_repeatSpin->setRange(1, 1000);
  m_repeatSpin->setValue(10);
  m_repeatSpin->setSuffix(" x");
  m_repeatSpin->setToolTip("Number of times the A1 + A2 measurement is repeated "
                           "by the statistics run.");
  m_measureStatsBtn->setToolTip(
      "Prompt for a MOE ID, then repeat the A1/A2 beam measurement the configured "
      "number of times, appending one CSV row per location per run (with a "
      "run_index column) to data/<product>/moe-screen/<MOE_ID>.csv.");

  m_stopBtn = new QPushButton("Stop");
  m_stopBtn->setStyleSheet(
      "QPushButton{background:#e53935;color:#fff;font-weight:bold;padding:8px;}"
      "QPushButton:disabled{background:#5a2320;color:#a77;}");
  m_stopBtn->setToolTip("Abort the running measurement script — it stops at the "
                        "next Lua line (a move or scan already in progress "
                        "finishes first).");
  m_stopBtn->setEnabled(false);

  m_busyLabel = new QLabel("idle");
  m_busyLabel->setStyleSheet("color:#888;");
  measureRow->addWidget(m_measureBtn);
  measureRow->addSpacing(16);
  measureRow->addWidget(m_measureStatsBtn);
  measureRow->addWidget(new QLabel("repeat"));
  measureRow->addWidget(m_repeatSpin);
  measureRow->addSpacing(16);
  measureRow->addWidget(m_stopBtn);
  measureRow->addWidget(m_busyLabel, 1);
  root->addLayout(measureRow);
  connect(m_measureBtn, &QPushButton::clicked, this, &MoeScreeningWidget::onMeasureMoe);
  connect(m_measureStatsBtn, &QPushButton::clicked, this,
          &MoeScreeningWidget::onMeasureStats);
  connect(m_stopBtn, &QPushButton::clicked, this, &MoeScreeningWidget::onStopScript);

  // --- Compact activity log ------------------------------------------------
  m_log = new QPlainTextEdit();
  m_log->setReadOnly(true);
  m_log->setMaximumBlockCount(500);
  m_log->setMaximumHeight(110);
  m_log->setStyleSheet("QPlainTextEdit{background:#111;color:#ddd;"
                       "font-family:'RobotoMono','Consolas',monospace;font-size:11px;}");
  root->addWidget(m_log);

  // --- Read-back panel -----------------------------------------------------
  auto* panel = new QGroupBox("MOE measurement");
  panel->setStyleSheet(
      "QGroupBox{background:#1c1c1c;color:#ddd;border:1px solid #333;"
      "margin-top:8px;font-size:16px;font-weight:bold;}"
      "QGroupBox::title{subcontrol-origin:margin;left:8px;}");
  auto* pv = new QVBoxLayout(panel);

  m_panelHeader = new QLabel("Poynt X / Y  (deg)");
  m_panelHeader->setStyleSheet("color:#bbb;font-weight:bold;font-size:20px;");
  m_panelStatus = new QLabel("● idle");
  m_panelStatus->setStyleSheet("color:#888;font-size:13px;");
  pv->addWidget(m_panelHeader);
  pv->addWidget(m_panelStatus);

  // Large monospace font for the value cells so the operator can read them from
  // a distance. Color is restyled per-update via setStyleSheet (which does not
  // touch the QFont), so the size set here persists across value refreshes.
  QFont mono("RobotoMono");
  mono.setStyleHint(QFont::Monospace);
  mono.setPointSize(20);
  mono.setBold(true);

  auto makeValue = [&mono]() {
    QLabel* l = new QLabel("-");
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setFont(mono);
    l->setMinimumWidth(120);
    l->setStyleSheet(QStringLiteral("color:%1;%2").arg(kColorNeutral, kValueFontCss));
    return l;
  };
  auto makeHead = [](const QString& t) {
    QLabel* l = new QLabel(t);
    l->setStyleSheet("color:#9ecbff;font-weight:bold;font-size:17px;");
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return l;
  };

  // Poynt grid: rows B0/B1, columns A1.X A1.Y A2.X A2.Y.
  auto* pg = new QGridLayout();
  pg->addWidget(makeHead("A1 Poynt.X"), 0, 1);
  pg->addWidget(makeHead("A1 Poynt.Y"), 0, 2);
  pg->addWidget(makeHead("A1 Z (um)"),  0, 3);
  pg->addWidget(makeHead("A2 Poynt.X"), 0, 4);
  pg->addWidget(makeHead("A2 Poynt.Y"), 0, 5);
  pg->addWidget(makeHead("A2 Z (um)"),  0, 6);
  const char* beamNames[2] = {"B0 (Rx)", "B1 (Tx)"};
  for (int beam = 0; beam < 2; ++beam) {
    auto* rowHead = new QLabel(beamNames[beam]);
    rowHead->setStyleSheet("color:#ccc;font-weight:bold;font-size:18px;");
    pg->addWidget(rowHead, beam + 1, 0);
    // column order: A1.X, A1.Y, A1.Z, A2.X, A2.Y, A2.Z  ->  [loc][axis]
    m_valueLabels[0][beam][0] = makeValue(); pg->addWidget(m_valueLabels[0][beam][0], beam + 1, 1);
    m_valueLabels[0][beam][1] = makeValue(); pg->addWidget(m_valueLabels[0][beam][1], beam + 1, 2);
    m_valueLabels[0][beam][2] = makeValue(); pg->addWidget(m_valueLabels[0][beam][2], beam + 1, 3);
    m_valueLabels[1][beam][0] = makeValue(); pg->addWidget(m_valueLabels[1][beam][0], beam + 1, 4);
    m_valueLabels[1][beam][1] = makeValue(); pg->addWidget(m_valueLabels[1][beam][1], beam + 1, 5);
    m_valueLabels[1][beam][2] = makeValue(); pg->addWidget(m_valueLabels[1][beam][2], beam + 1, 6);
  }
  pv->addLayout(pg);

  // TX-RX delta grid: rows MOE-A1/MOE-A2, columns dx/dy.
  auto* txHeader = new QLabel("TX - RX delta  (um)");
  txHeader->setStyleSheet("color:#bbb;font-weight:bold;font-size:18px;margin-top:6px;");
  pv->addWidget(txHeader);
  auto* tg = new QGridLayout();
  tg->addWidget(makeHead("dx (um)"), 0, 1);
  tg->addWidget(makeHead("dy (um)"), 0, 2);
  const char* locNames[2] = {"MOE-A1", "MOE-A2"};
  for (int loc = 0; loc < 2; ++loc) {
    auto* rowHead = new QLabel(locNames[loc]);
    rowHead->setStyleSheet("color:#ccc;font-weight:bold;font-size:18px;");
    tg->addWidget(rowHead, loc + 1, 0);
    m_txRxLabels[loc][0] = makeValue(); tg->addWidget(m_txRxLabels[loc][0], loc + 1, 1);
    m_txRxLabels[loc][1] = makeValue(); tg->addWidget(m_txRxLabels[loc][1], loc + 1, 2);
  }
  pv->addLayout(tg);

  // Panel action buttons.
  auto* pbRow = new QHBoxLayout();
  m_saveFinalBtn  = new QPushButton("Save final measurement");
  m_openFolderBtn = new QPushButton("Open data folder");
  m_saveFinalBtn->setToolTip(
      "Export the latest A1 + A2 measurement to "
      "data/<product>/moe-screen/<MOE_ID>_final.json.");
  m_openFolderBtn->setToolTip("Open data/<product>/moe-screen in Windows Explorer.");
  pbRow->addWidget(m_saveFinalBtn);
  pbRow->addWidget(m_openFolderBtn);
  pbRow->addStretch(1);
  pv->addLayout(pbRow);

  root->addWidget(panel, 1);

  connect(m_saveFinalBtn,  &QPushButton::clicked, this,
          &MoeScreeningWidget::onSaveFinalMeasurement);
  connect(m_openFolderBtn, &QPushButton::clicked, this,
          &MoeScreeningWidget::onOpenDataFolder);

  // --- Bottom action row: capture-all across every station + report ---------
  auto* bottomRow = new QHBoxLayout();
  bottomRow->addStretch(1);
  m_captureAllBtn = new QPushButton("Capture all channels");
  m_captureAllBtn->setToolTip(
    "Run moe_screen_capture_all.lua in the background: measure screen-A1-IR, "
    "ch1..ch12 (interpolated between the taught screen-A1-IR / screen-A2-IR "
    "endpoints), and screen-A2-IR — one Rx/Tx pair each, saving a SWIR image "
    "package + Z-scan slices per position. Prompts for a MOE ID. Repeats for the "
    "count set beside it, generating + opening a report after each run. Stop "
    "breaks it.");
  m_captureAllBtn->setStyleSheet(kYellowStyle);
  m_captureAllBtn->setMinimumWidth(200);
  bottomRow->addWidget(m_captureAllBtn);

  bottomRow->addWidget(new QLabel("x"));
  m_captureAllIterSpin = new QSpinBox();
  m_captureAllIterSpin->setRange(1, 99);
  m_captureAllIterSpin->setValue(1);
  m_captureAllIterSpin->setToolTip(
    "How many times to run 'Capture all channels' in a row. After each run "
    "completes, its report is generated and opened automatically before the "
    "next run starts. Default 1.");
  m_captureAllIterSpin->setMinimumWidth(56);
  bottomRow->addWidget(m_captureAllIterSpin);

  m_generateReportBtn = new QPushButton("Generate / open report");
  m_generateReportBtn->setToolTip(
    "Build (or, if it already exists, just open) the HTML report for the latest "
    "capture-all run of the current MOE ID via make_moe_screen_report.py.");
  m_generateReportBtn->setStyleSheet(kGreenStyle);
  m_generateReportBtn->setMinimumWidth(200);
  bottomRow->addWidget(m_generateReportBtn);
  bottomRow->addStretch(1);
  root->addLayout(bottomRow);

  connect(m_captureAllBtn, &QPushButton::clicked, this,
          &MoeScreeningWidget::onCaptureAllChannels);
  connect(m_generateReportBtn, &QPushButton::clicked, this,
          &MoeScreeningWidget::onGenerateReport);
}

void MoeScreeningWidget::setBusy(bool busy, const QString& whatRunning) {
  m_busy = busy;
  QPushButton* gated[] = {
    m_gantrySafeBtn, m_measureBtn, m_measureStatsBtn, m_applyA1Btn, m_applyA2Btn,
    m_moveScreenerA1Btn, m_moveIrA1Btn, m_saveScreenerA1Btn, m_saveIrA1Btn,
    m_moveScreenerA2Btn, m_moveIrA2Btn, m_saveScreenerA2Btn, m_saveIrA2Btn,
    m_captureAllBtn,
  };
  for (QPushButton* b : gated) if (b) b->setEnabled(!busy);

  // Stop is inversely gated: it stays disabled here and is enabled explicitly by
  // launchScript once a ScriptWorker is running (so a bare move never arms it).
  if (m_stopBtn && !busy) m_stopBtn->setEnabled(false);

  if (m_busyLabel) {
    if (busy) {
      m_busyLabel->setText(QStringLiteral("BUSY: %1")
          .arg(whatRunning.isEmpty() ? "running..." : whatRunning));
      m_busyLabel->setStyleSheet("color:#fb8c00;font-weight:bold;");
    } else {
      m_busyLabel->setText("idle");
      m_busyLabel->setStyleSheet("color:#888;");
    }
  }
}

QString MoeScreeningWidget::resolveScriptPath(const QString& scriptFile) const {
  if (m_context && m_context->productPositionManager) {
    auto* ppm = m_context->productPositionManager;
    if (ppm->HasLoadedProduct()) {
      const QString productName = QString::fromStdString(ppm->GetCurrentProductName());
      const QString productCandidate =
        QStringLiteral("config/") + productName + QStringLiteral("/scripts/") + scriptFile;
      if (QFileInfo::exists(productCandidate)) return productCandidate;
    }
  }
  return QStringLiteral("scripts/") + scriptFile;
}

void MoeScreeningWidget::onMeasureMoe() {
  launchMoeMeasurement(1);
}

void MoeScreeningWidget::onMeasureStats() {
  const int repeats = m_repeatSpin ? m_repeatSpin->value() : 1;
  launchMoeMeasurement(repeats);
}

void MoeScreeningWidget::launchMoeMeasurement(int repeats) {
  if (repeats < 1) repeats = 1;
  if (m_busy) {
    appendLog("[busy] another job is already running", "#ef5350");
    return;
  }
  auto* ppm = m_context ? m_context->productPositionManager : nullptr;
  if (!ppm || !ppm->HasLoadedProduct()) {
    QMessageBox::warning(this, "Measure MOE",
        "No product loaded — cannot determine the data/<product>/moe-screen "
        "output folder. Load a product first.");
    return;
  }
  const QString product = QString::fromStdString(ppm->GetCurrentProductName());

  const QString prompt = (repeats > 1)
      ? QStringLiteral("Enter MOE ID for a %1x statistics run:").arg(repeats)
      : QStringLiteral("Enter MOE ID (not a DUT id):");
  bool ok = false;
  const QString moeId = QInputDialog::getText(
      this, "MOE ID", prompt,
      QLineEdit::Normal, QString(), &ok).trimmed();
  if (!ok) return;
  if (moeId.isEmpty()) {
    QMessageBox::warning(this, "Measure MOE", "MOE ID must not be empty.");
    return;
  }

  // Use the raw product name (matches watchDir() and the data/<product>
  // convention used by the other scripts); only the MOE ID is sanitized since
  // it becomes the CSV filename.
  const QString outDir = QStringLiteral("data/") + product + "/moe-screen";

  if (!m_context->scriptEngine) {
    appendLog("[error] script engine unavailable", "#ef5350");
    return;
  }
  m_context->scriptEngine->SetScriptGlobalString(
      "MOE_SCREEN_ID", sanitizeSegment(moeId).toStdString());
  m_context->scriptEngine->SetScriptGlobalString(
      "MOE_SCREEN_DIR", outDir.toStdString());
  m_context->scriptEngine->SetScriptGlobalString(
      "MOE_SCREEN_EXP_A1_US", std::to_string(m_expA1Spin->value()));
  m_context->scriptEngine->SetScriptGlobalString(
      "MOE_SCREEN_EXP_A2_US", std::to_string(m_expA2Spin->value()));
  m_context->scriptEngine->SetScriptGlobalString(
      "MOE_SCREEN_SLICES", std::to_string(m_scanSlices));
  m_context->scriptEngine->SetScriptGlobalString(
      "MOE_SCREEN_STEP_UM", std::to_string(m_scanStepUm));
  m_context->scriptEngine->SetScriptGlobalString(
      "MOE_SCREEN_REPEATS", std::to_string(repeats));

  const QString label = (repeats > 1)
      ? QStringLiteral("Measure MOE %1 (%2x stats)").arg(moeId).arg(repeats)
      : QStringLiteral("Measure MOE %1").arg(moeId);
  launchScript(QString::fromUtf8(kMoeScreenScript), label);
}

// ---------------------------------------------------------------------------
// Capture all channels (all stations) + report
// ---------------------------------------------------------------------------
void MoeScreeningWidget::onCaptureAllChannels() {
  if (m_busy) {
    appendLog("[busy] another job is already running", "#ef5350");
    return;
  }
  auto* ppm = m_context ? m_context->productPositionManager : nullptr;
  if (!ppm || !ppm->HasLoadedProduct()) {
    QMessageBox::warning(this, "Capture all channels",
        "No product loaded — cannot determine the data/<product>/moe-screen "
        "output folder. Load a product first.");
    return;
  }
  if (!m_context->scriptEngine) {
    appendLog("[error] script engine unavailable", "#ef5350");
    return;
  }
  const QString product = QString::fromStdString(ppm->GetCurrentProductName());

  // Prompt for the MOE ID (not a DUT id), same as Measure MOE.
  bool ok = false;
  const QString moeIdRaw = QInputDialog::getText(
      this, "MOE ID", "Enter MOE ID for the capture-all run:",
      QLineEdit::Normal, QString(), &ok).trimmed();
  if (!ok) return;
  if (moeIdRaw.isEmpty()) {
    QMessageBox::warning(this, "Capture all channels", "MOE ID must not be empty.");
    return;
  }
  const QString moeId = sanitizeSegment(moeIdRaw);

  // Confirm prerequisites (SWIR window streaming; the script measures each
  // station in turn) and the iteration count.
  const int iterations = m_captureAllIterSpin ? m_captureAllIterSpin->value() : 1;
  const auto answer = QMessageBox::question(
    this, QStringLiteral("Capture all channels"),
    QStringLiteral("Make sure screen-A1-IR and screen-A2-IR are taught, the SWIR "
                   "window is open + streaming, and the MOE is powered externally "
                   "before continuing.\n\nMeasure screen-A1-IR, ch1..ch12 "
                   "(interpolated between the two endpoints), and screen-A2-IR, "
                   "%1 time(s), generating + opening a report after each run?"
                   "\n\nProceed?")
        .arg(iterations),
    QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
  if (answer != QMessageBox::Ok) return;

  // Script globals (no DUT — identified by MOE ID; no power control).
  const QString outDir = QStringLiteral("data/") + product + "/moe-screen";
  auto* se = m_context->scriptEngine;
  se->SetScriptGlobalString("MOE_SCREEN_ID", moeId.toStdString());
  se->SetScriptGlobalString("MOE_SCREEN_DIR", outDir.toStdString());
  se->SetScriptGlobalString("MOE_SCREEN_EXP_A1_US", std::to_string(m_expA1Spin->value()));
  se->SetScriptGlobalString("MOE_SCREEN_EXP_A2_US", std::to_string(m_expA2Spin->value()));
  se->SetScriptGlobalString("MOE_SCREEN_SLICES", std::to_string(m_scanSlices));
  se->SetScriptGlobalString("MOE_SCREEN_STEP_UM", std::to_string(m_scanStepUm));

  // Arm the multi-run loop; onScriptCompleted drives report + next iteration.
  m_captureAllMoeId     = moeId;
  m_captureAllActive    = true;
  m_captureAllTotal     = iterations;
  m_captureAllRemaining = iterations;
  launchCaptureAllIteration();
}

void MoeScreeningWidget::launchCaptureAllIteration() {
  const int index = m_captureAllTotal - m_captureAllRemaining + 1;
  appendLog(QStringLiteral("[capture-all] iteration %1 of %2")
                .arg(index).arg(m_captureAllTotal), "#60a5fa");
  const QString label = m_captureAllTotal > 1
      ? QStringLiteral("Capture all channels (%1/%2)").arg(index).arg(m_captureAllTotal)
      : QStringLiteral("Capture all channels");
  if (!launchScript(QString::fromUtf8(kMoeCaptureAllScript), label)) {
    // Launch failed (engine unavailable / script missing) — abandon the loop.
    m_captureAllActive    = false;
    m_captureAllRemaining = 0;
  }
}

void MoeScreeningWidget::onGenerateReport() {
  if (m_reportProcess) {
    appendLog("[report] a report is already being generated", "#fb8c00");
    return;
  }
  auto* ppm = m_context ? m_context->productPositionManager : nullptr;
  if (!ppm || !ppm->HasLoadedProduct()) {
    QMessageBox::warning(this, "Generate report",
                         "No product loaded - cannot locate the data folder.");
    return;
  }
  const QString product = QString::fromStdString(ppm->GetCurrentProductName());

  // Prefer the MOE ID of the running/last capture-all; fall back to the panel's
  // currently-loaded MOE ID. Sanitize so the path matches the on-disk folder.
  QString moeId = m_captureAllMoeId.isEmpty() ? m_currentMoeId : m_captureAllMoeId;
  if (moeId.isEmpty()) {
    QMessageBox::warning(this, "Generate report",
        "No MOE ID known yet - run 'Capture all channels' first.");
    return;
  }
  moeId = sanitizeSegment(moeId);

  const QString appDir  = QCoreApplication::applicationDirPath();
  const QString dataDir = appDir + "/data/" + product + "/moe-screen";  // report CWD
  const QString moeDir  = dataDir + "/" + moeId;

  QDir md(moeDir);
  if (!md.exists()) {
    QMessageBox::warning(this, "Generate report",
        QStringLiteral("No capture folder for MOE '%1':\n%2").arg(moeId, moeDir));
    return;
  }
  // Latest run folder = most recently modified timestamp subdirectory.
  const QFileInfoList subs = md.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
  if (subs.isEmpty()) {
    QMessageBox::warning(this, "Generate report",
        QStringLiteral("No run folders found under:\n%1").arg(moeDir));
    return;
  }
  const QString runTag     = subs.first().fileName();
  const QString runDir     = moeDir + "/" + runTag;
  const QString reportPath = runDir + "/report_slices.html";

  // If the report already exists, just open it.
  if (QFileInfo::exists(reportPath)) {
    appendLog(QStringLiteral("[report] opening existing %1").arg(reportPath), "#60a5fa");
    QDesktopServices::openUrl(QUrl::fromLocalFile(reportPath));
    return;
  }

  // Otherwise locate make_moe_screen_report.py and build the report (script opens it).
  QString script;
  for (const QString& cand : {dataDir + "/" + QString::fromUtf8(kReportScript),
                              appDir + "/" + QString::fromUtf8(kReportScript),
                              appDir + "/tools/" + QString::fromUtf8(kReportScript)}) {
    if (QFileInfo::exists(cand)) { script = cand; break; }
  }
  if (script.isEmpty()) {
    QMessageBox::warning(this, "Generate report",
        QStringLiteral("%1 not found in the data folder or application folder.")
            .arg(QString::fromUtf8(kReportScript)));
    return;
  }

  m_reportProcess = new QProcess(this);
  m_reportProcess->setWorkingDirectory(dataDir);
  m_reportProcess->setProcessChannelMode(QProcess::MergedChannels);
  connect(m_reportProcess, &QProcess::readyReadStandardOutput, this, [this]() {
    const QString out = QString::fromLocal8Bit(m_reportProcess->readAllStandardOutput());
    for (const QString& line : out.split('\n', Qt::SkipEmptyParts))
      appendLog(QStringLiteral("[report] %1").arg(line.trimmed()), "#c9d1d9");
  });
  connect(m_reportProcess,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this](int code, QProcess::ExitStatus) {
            if (code == 0)
              appendLog("[report] done", "#22c55e");
            else
              appendLog(QStringLiteral("[report] %1 exited with code %2")
                            .arg(QString::fromUtf8(kReportScript)).arg(code), "#ef5350");
            if (m_generateReportBtn) m_generateReportBtn->setEnabled(true);
            m_reportProcess->deleteLater();
            m_reportProcess = nullptr;
          });
  connect(m_reportProcess, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError) {
            appendLog("[report] failed to start python (is it on PATH?)", "#ef5350");
          });

  // Absolute run-folder path so the script resolves regardless of CWD; --open
  // pops the report when finished.
  const QStringList args{script, QDir::toNativeSeparators(runDir), QStringLiteral("--open")};
  appendLog(QStringLiteral("[report] python %1 \"%2\\%3\" --open")
                .arg(QString::fromUtf8(kReportScript), moeId, runTag), "#60a5fa");
  if (m_generateReportBtn) m_generateReportBtn->setEnabled(false);
  m_reportProcess->start(QStringLiteral("python"), args);
}

bool MoeScreeningWidget::launchScript(const QString& scriptFile, const QString& busyLabel) {
  if (!m_context || !m_context->scriptEngine) {
    appendLog(QStringLiteral("[error] script engine unavailable"), "#ef5350");
    setBusy(false);
    return false;
  }
  if (m_busy && m_scriptWorker) {
    appendLog(QStringLiteral("[busy] another job is already running"), "#ef5350");
    return false;
  }

  const QString path = resolveScriptPath(scriptFile);
  if (!QFileInfo::exists(path)) {
    appendLog(QStringLiteral("[error] script not found at %1").arg(path), "#ef5350");
    setBusy(false);
    return false;
  }

  m_context->scriptEngine->ClearStopFlag();

  // Wire dialog callbacks so bobcat.prompt / bobcat.askInput pop a modal here
  // (the script runs on a worker thread, so each callback marshals to the UI
  // thread with BlockingQueuedConnection and returns the user's answer).
  if (m_context->sequenceEngine) {
    m_context->sequenceEngine->SetUserPromptCallback(
      [this](const std::string& title, const std::string& message) -> bool {
        bool result = false;
        auto show = [this, &title, &message, &result]() {
          result = SequencePromptDialog::ShowPrompt(
            QString::fromStdString(title), QString::fromStdString(message), this);
        };
        if (QThread::currentThread() == QApplication::instance()->thread()) show();
        else QMetaObject::invokeMethod(this, show, Qt::BlockingQueuedConnection);
        return result;
      });
    m_context->sequenceEngine->SetUserInputCallback(
      [this](const std::string& title, const std::string& prompt,
             const std::string& defaultValue) -> std::pair<bool, std::string> {
        std::pair<bool, std::string> result{false, ""};
        auto show = [this, &title, &prompt, &defaultValue, &result]() {
          bool okDlg = false;
          const QString text = QInputDialog::getText(
            this, QString::fromStdString(title), QString::fromStdString(prompt),
            QLineEdit::Normal, QString::fromStdString(defaultValue), &okDlg);
          result = std::make_pair(okDlg, text.toStdString());
        };
        if (QThread::currentThread() == QApplication::instance()->thread()) show();
        else QMetaObject::invokeMethod(this, show, Qt::BlockingQueuedConnection);
        return result;
      });
  }

  setBusy(true, busyLabel);
  appendLog(QStringLiteral("[run] %1").arg(path), "#60a5fa");

  m_scriptWorker = new ScriptWorker(
    m_context->scriptEngine, path.toStdString(), ScriptWorker::Mode::FILE, this);
  connect(m_scriptWorker, &ScriptWorker::executionStarted,
          this, &MoeScreeningWidget::onScriptStarted, Qt::QueuedConnection);
  connect(m_scriptWorker, &ScriptWorker::executionCompleted,
          this, &MoeScreeningWidget::onScriptCompleted, Qt::QueuedConnection);
  connect(m_scriptWorker, &ScriptWorker::scriptOutput,
          this, &MoeScreeningWidget::onScriptOutput, Qt::QueuedConnection);
  m_scriptWorker->start();
  if (m_stopBtn) m_stopBtn->setEnabled(true);  // arm Stop for the duration of the run
  return true;
}

void MoeScreeningWidget::onStopScript() {
  // Break the capture-all loop so no further iterations launch after the in-flight
  // script unwinds, regardless of how it reports completion.
  if (m_captureAllActive) {
    m_captureAllActive    = false;
    m_captureAllRemaining = 0;
    appendLog("[capture-all] stop requested — no further iterations", "#fb8c00");
  }
  if (!m_scriptWorker) return;
  appendLog("[stop] stop requested — aborting at the next script step...", "#fb8c00");
  m_scriptWorker->requestStop();   // sets the engine stop flag; DebugHook aborts the Lua VM
  if (m_stopBtn) m_stopBtn->setEnabled(false);
  if (m_busyLabel) {
    m_busyLabel->setText("BUSY: stopping...");
    m_busyLabel->setStyleSheet("color:#e53935;font-weight:bold;");
  }
}

void MoeScreeningWidget::onScriptStarted() {
  appendLog("[script] started", "#888");
}

void MoeScreeningWidget::onScriptCompleted(bool success, QString errorMessage) {
  if (success) {
    appendLog("[script] completed OK", "#4caf50");
  } else if (errorMessage.contains("stopped by user", Qt::CaseInsensitive)) {
    appendLog("[script] stopped by user", "#fb8c00");
  } else {
    appendLog(QStringLiteral("[script] FAILED: %1").arg(errorMessage), "#ef5350");
  }

  if (m_scriptWorker) {
    m_scriptWorker->deleteLater();
    m_scriptWorker = nullptr;
  }
  if (m_context && m_context->sequenceEngine) {
    m_context->sequenceEngine->SetUserPromptCallback(nullptr);
    m_context->sequenceEngine->SetUserInputCallback(nullptr);
  }

  // Capture-all multi-run loop: on a successful run, generate + open its report,
  // then launch the next iteration if any remain. A failed / stopped run ends the
  // loop. onStopScript() also clears m_captureAllActive so Stop breaks the chain.
  if (m_captureAllActive) {
    if (success) {
      onGenerateReport();          // targets the newest run folder just written
      --m_captureAllRemaining;
      if (m_captureAllRemaining > 0) {
        m_busy = false;            // launchScript() guards on m_busy; worker cleared
        launchCaptureAllIteration();
        return;                    // stay busy for the next iteration
      }
      appendLog(QStringLiteral("[capture-all] all %1 iteration(s) complete")
                    .arg(m_captureAllTotal), "#4caf50");
    } else {
      appendLog("[capture-all] run did not complete — loop stopped", "#fb8c00");
    }
    m_captureAllActive    = false;
    m_captureAllRemaining = 0;
  }

  setBusy(false);
  onTick();  // refresh the panel from the freshly written CSV
}

void MoeScreeningWidget::onScriptOutput(QString message) {
  appendLog(message);
}

void MoeScreeningWidget::applyExposure(int us) {
  if (m_busy) {
    appendLog("[busy] another job is already running", "#ef5350");
    return;
  }
  if (!m_context || !m_context->cameraManager) {
    appendLog("[error] camera manager unavailable", "#ef5350");
    return;
  }
  // Same effect as the run's camera.setExposure: disable auto-exposure and set
  // the manual exposure time on swir_camera.
  auto settings = m_context->cameraManager->GetExposureSettings(kSwirCamera);
  settings.auto_exposure = false;
  settings.exposure_time = static_cast<double>(us);
  if (m_context->cameraManager->ApplyExposureSettings(kSwirCamera, settings)) {
    appendLog(QStringLiteral("[camera] %1 exposure -> %2 us")
                  .arg(QString::fromUtf8(kSwirCamera)).arg(us), "#4caf50");
  } else {
    appendLog(QStringLiteral("[camera] failed to set %1 exposure to %2 us")
                  .arg(QString::fromUtf8(kSwirCamera)).arg(us), "#ef5350");
  }
}

void MoeScreeningWidget::refreshScanButtonText() {
  if (!m_scanSettingsBtn) return;
  m_scanSettingsBtn->setText(
      QStringLiteral("Fit scan\n%1 x %2um").arg(m_scanSlices).arg(m_scanStepUm));
}

void MoeScreeningWidget::openScanSettingsDialog() {
  QDialog dlg(this);
  dlg.setWindowTitle("Beam-fit Z-scan");

  auto* form = new QFormLayout();
  auto* countSpin = new QSpinBox(&dlg);
  countSpin->setRange(3, 99);
  countSpin->setValue(m_scanSlices);
  auto* stepSpin = new QSpinBox(&dlg);
  stepSpin->setRange(1, 500);
  stepSpin->setSuffix(" um");
  stepSpin->setValue(m_scanStepUm);
  auto* rangeLbl = new QLabel(&dlg);
  rangeLbl->setStyleSheet("color:#9ecbff;");

  // Live "centred range" preview, matching the script's centring math.
  auto updateRange = [countSpin, stepSpin, rangeLbl]() {
    const double half = (countSpin->value() - 1) * stepSpin->value() / 2.0;
    rangeLbl->setText(QString("%1 slices span %2 .. +%3 um around taught Z")
                          .arg(countSpin->value())
                          .arg(-half, 0, 'f', 1)
                          .arg(half, 0, 'f', 1));
  };
  updateRange();
  connect(countSpin, qOverload<int>(&QSpinBox::valueChanged), &dlg, updateRange);
  connect(stepSpin,  qOverload<int>(&QSpinBox::valueChanged), &dlg, updateRange);

  form->addRow("Number of steps (count):", countSpin);
  form->addRow("Step size:", stepSpin);
  form->addRow("", rangeLbl);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  auto* v = new QVBoxLayout(&dlg);
  v->addLayout(form);
  v->addWidget(buttons);

  if (dlg.exec() != QDialog::Accepted) return;

  m_scanSlices = countSpin->value();
  m_scanStepUm = stepSpin->value();
  refreshScanButtonText();
  saveExposures();  // persists exposures + scan geometry to the same file
  appendLog(QStringLiteral("[fit] Z-scan set to %1 slices x %2 um")
                .arg(m_scanSlices).arg(m_scanStepUm), "#4caf50");
}

QString MoeScreeningWidget::exposureConfigPath() const {
  if (!m_context || !m_context->productPositionManager) return QString();
  if (!m_context->productPositionManager->HasLoadedProduct()) return QString();
  const QString product = QString::fromStdString(
      m_context->productPositionManager->GetCurrentProductName());
  if (product.isEmpty()) return QString();
  return QCoreApplication::applicationDirPath() + "/config/" + product +
         "/moe_screen_exposure.json";
}

void MoeScreeningWidget::loadExposures() {
  const QString path = exposureConfigPath();
  if (path.isEmpty() || !m_expA1Spin || !m_expA2Spin) return;
  std::ifstream in(path.toStdString(), std::ios::binary);
  if (!in.is_open()) return;
  // Non-throwing parse (this codebase is exception-free).
  json j = json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return;

  auto readClamped = [](const json& obj, const char* key, int def) -> int {
    if (obj.contains(key) && obj.at(key).is_number()) {
      int v = obj.at(key).get<int>();
      if (v < 1) v = 1;
      if (v > kMaxExpUs) v = kMaxExpUs;
      return v;
    }
    return def;
  };
  const int a1 = readClamped(j, "a1_us", m_expA1Spin->value());
  const int a2 = readClamped(j, "a2_us", m_expA2Spin->value());

  // Block valueChanged so restoring values doesn't immediately re-save.
  { QSignalBlocker b1(m_expA1Spin); m_expA1Spin->setValue(a1); }
  { QSignalBlocker b2(m_expA2Spin); m_expA2Spin->setValue(a2); }

  // Beam-fit Z-scan geometry (clamped to the dialog's ranges).
  auto readRange = [](const json& obj, const char* key, int def, int lo, int hi) -> int {
    if (obj.contains(key) && obj.at(key).is_number()) {
      int v = obj.at(key).get<int>();
      return v < lo ? lo : (v > hi ? hi : v);
    }
    return def;
  };
  m_scanSlices = readRange(j, "scan_slices",  m_scanSlices, 3, 99);
  m_scanStepUm = readRange(j, "scan_step_um", m_scanStepUm, 1, 500);
  refreshScanButtonText();
}

void MoeScreeningWidget::saveExposures() {
  const QString path = exposureConfigPath();
  if (path.isEmpty() || !m_expA1Spin || !m_expA2Spin) return;
  QDir().mkpath(QFileInfo(path).absolutePath());
  json j;
  j["a1_us"] = m_expA1Spin->value();
  j["a2_us"] = m_expA2Spin->value();
  j["scan_slices"]  = m_scanSlices;
  j["scan_step_um"] = m_scanStepUm;
  std::ofstream o(path.toStdString(), std::ios::binary | std::ios::trunc);
  if (!o.is_open()) return;
  o << j.dump(2);
}

void MoeScreeningWidget::moveToNamedGantryPosition(const char* name) {
  if (m_busy) {
    appendLog("[busy] another job is already running", "#ef5350");
    return;
  }
  if (!m_context || !m_context->motionManager) {
    appendLog("[error] motion manager unavailable", "#ef5350");
    return;
  }

  PositionStruct target;
  if (!m_context->motionManager->GetNamedPosition("gantry-main", name, target)) {
    appendLog(QStringLiteral("[error] no saved gantry-main position '%1'.")
                  .arg(QString::fromUtf8(name)), "#ef5350");
    return;
  }

  const QString tag = QString::fromUtf8(name);
  setBusy(true, QStringLiteral("Move to %1").arg(tag));
  appendLog(QStringLiteral("[move] gantry-main -> %1 (X=%2 Y=%3 Z=%4 mm)")
                .arg(tag).arg(target.x, 0, 'f', 4)
                .arg(target.y, 0, 'f', 4).arg(target.z, 0, 'f', 4), "#60a5fa");

  auto* motionMgr = m_context->motionManager;
  m_moveFuture = std::async(std::launch::async,
    [this, motionMgr, target, tag]() {
      const bool ok = motionMgr->MoveToAbs6("gantry-main", target, true);
      QMetaObject::invokeMethod(this, [this, ok, tag]() {
        appendLog(ok ? QStringLiteral("[move] arrived at %1").arg(tag)
                     : QStringLiteral("[move] move to %1 FAILED").arg(tag),
                  ok ? "#4caf50" : "#ef5350");
        setBusy(false);
      }, Qt::QueuedConnection);
    });
}

void MoeScreeningWidget::saveCurrentToNamedGantryPosition(const char* name) {
  if (m_busy) {
    appendLog("[busy] another job is already running", "#ef5350");
    return;
  }
  if (!m_context || !m_context->motionManager) {
    appendLog("[error] motion manager unavailable", "#ef5350");
    return;
  }

  auto* motionMgr  = m_context->motionManager;
  auto* productMgr = m_context->productPositionManager;
  const QString tag = QString::fromUtf8(name);

  const PositionStruct cur = motionMgr->GetCurrentPosition("gantry-main");

  PositionStruct prev;
  const bool hadPrev = motionMgr->GetNamedPosition("gantry-main", name, prev);

  QString deltaText;
  if (hadPrev) {
    const double dx = cur.x - prev.x, dy = cur.y - prev.y, dz = cur.z - prev.z;
    const double du = cur.u - prev.u, dv = cur.v - prev.v, dw = cur.w - prev.w;
    deltaText = QStringLiteral(
      "<b>Current vs. saved '%1':</b><br><pre>"
      "          new           old           delta\n"
      "X    %2  %3  %4 mm\n"
      "Y    %5  %6  %7 mm\n"
      "Z    %8  %9  %10 mm\n"
      "U    %11  %12  %13 deg\n"
      "V    %14  %15  %16 deg\n"
      "W    %17  %18  %19 deg</pre>")
      .arg(tag)
      .arg(cur.x, 12, 'f', 4).arg(prev.x, 12, 'f', 4).arg(dx, 12, 'f', 4)
      .arg(cur.y, 12, 'f', 4).arg(prev.y, 12, 'f', 4).arg(dy, 12, 'f', 4)
      .arg(cur.z, 12, 'f', 4).arg(prev.z, 12, 'f', 4).arg(dz, 12, 'f', 4)
      .arg(cur.u, 12, 'f', 4).arg(prev.u, 12, 'f', 4).arg(du, 12, 'f', 4)
      .arg(cur.v, 12, 'f', 4).arg(prev.v, 12, 'f', 4).arg(dv, 12, 'f', 4)
      .arg(cur.w, 12, 'f', 4).arg(prev.w, 12, 'f', 4).arg(dw, 12, 'f', 4);
  } else {
    deltaText = QStringLiteral(
      "<b>No previous saved value for '%1'.</b><br><pre>"
      "X = %2 mm\nY = %3 mm\nZ = %4 mm\n"
      "U = %5 deg\nV = %6 deg\nW = %7 deg</pre>")
      .arg(tag).arg(cur.x, 0, 'f', 4).arg(cur.y, 0, 'f', 4).arg(cur.z, 0, 'f', 4)
      .arg(cur.u, 0, 'f', 4).arg(cur.v, 0, 'f', 4).arg(cur.w, 0, 'f', 4);
  }

  QMessageBox box(this);
  box.setWindowTitle(QStringLiteral("Save %1").arg(tag));
  box.setIcon(QMessageBox::Question);
  box.setTextFormat(Qt::RichText);
  box.setText(QStringLiteral("Overwrite gantry-main named position '<b>%1</b>' "
                             "with the current position?").arg(tag));
  box.setInformativeText(deltaText);
  box.setStandardButtons(QMessageBox::Save | QMessageBox::Cancel);
  box.setDefaultButton(QMessageBox::Cancel);
  if (box.exec() != QMessageBox::Save) {
    appendLog(QStringLiteral("[save %1] cancelled").arg(tag), "#888");
    return;
  }

  motionMgr->AddNamedPosition("gantry-main", name, cur);

  QString status;
  const char* color = "#888";
  if (productMgr && productMgr->HasLoadedProduct()) {
    productMgr->ImportFromMotionManager(*motionMgr);
    const bool saved = productMgr->SaveProduct();
    status = saved
      ? QStringLiteral("[save %1] saved gantry-main current position as '%1'.").arg(tag)
      : QStringLiteral("[save %1] updated in memory, but SaveProduct() FAILED.").arg(tag);
    color = saved ? "#4caf50" : "#ef5350";
  } else {
    status = QStringLiteral(
      "[save %1] no product loaded; named position updated in memory only "
      "(not written to disk).").arg(tag);
  }
  appendLog(status, color);
}

// ---------------------------------------------------------------------------
// Read-back panel
// ---------------------------------------------------------------------------

QString MoeScreeningWidget::watchDir() const {
  if (!m_context || !m_context->productPositionManager) return QString();
  if (!m_context->productPositionManager->HasLoadedProduct()) return QString();
  const QString product = QString::fromStdString(
      m_context->productPositionManager->GetCurrentProductName());
  if (product.isEmpty()) return QString();
  return QCoreApplication::applicationDirPath() + "/data/" + product + "/moe-screen";
}

void MoeScreeningWidget::onTick() {
  const QString dir = watchDir();

  if (dir != m_lastWatchDir) {   // product switch -> drop stale state
    m_lastWatchDir = dir;
    m_currentPath.clear();
    m_currentMtime = -1;
    m_currentMoeId.clear();
    m_haveData = false;
    clearPanel();
    loadExposures();  // restore this product's saved IR exposures
  }

  if (dir.isEmpty()) {
    m_panelStatus->setText("● idle  -  no product loaded");
    return;
  }

  fs::path latest;
  long long latestMtime = -1;
  std::error_code ec;
  fs::path base(dir.toStdString());
  if (fs::is_directory(base, ec)) {
    for (auto it = fs::directory_iterator(
             base, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      if (it->path().extension() != ".csv") continue;
      auto t = fs::last_write_time(it->path(), ec);
      if (ec) { ec.clear(); continue; }
      long long m = static_cast<long long>(t.time_since_epoch().count());
      if (m > latestMtime) { latestMtime = m; latest = it->path(); }
    }
  }

  m_panelStatus->setText(QString("● monitoring  -  %1").arg(dir));

  if (latestMtime < 0) {
    if (m_currentPath.isEmpty())
      m_panelHeader->setText("No CSV in moe-screen folder - waiting...");
    return;
  }

  const QString latestQ = QString::fromStdString(latest.string());
  if (latestQ != m_currentPath || latestMtime != m_currentMtime) {
    m_currentPath  = latestQ;
    m_currentMtime = latestMtime;
    reloadFromCsv(latestQ);
  }
}

void MoeScreeningWidget::reloadFromCsv(const QString& path) {
  std::ifstream in(path.toStdString(), std::ios::binary);
  if (!in.is_open()) return;

  std::string raw;
  std::getline(in, raw);
  if (raw.empty()) return;
  QString headerLine = QString::fromStdString(raw);
  if (headerLine.endsWith('\r')) headerLine.chop(1);
  const QStringList cols = splitCsvLine(headerLine);

  auto colIdx = [&cols](const char* name) -> int {
    return cols.indexOf(QString::fromUtf8(name));
  };
  const int iMoe  = colIdx("moe_id");
  const int iLoc  = colIdx("location");
  const int iB0x  = colIdx("b0_poynt_x_deg");
  const int iB0y  = colIdx("b0_poynt_y_deg");
  const int iB0z  = colIdx("b0_z0_um");
  const int iB1x  = colIdx("b1_poynt_x_deg");
  const int iB1y  = colIdx("b1_poynt_y_deg");
  const int iB1z  = colIdx("b1_z0_um");
  const int iDx   = colIdx("tx_rx_dx_um");
  const int iDy   = colIdx("tx_rx_dy_um");

  struct LocData {
    bool has = false;
    double b0x = 0, b0y = 0, b0z = 0, b1x = 0, b1y = 0, b1z = 0, dx = 0, dy = 0;
  } loc[2];
  QString moeId;

  auto num = [](const QStringList& f, int idx) -> double {
    if (idx < 0 || idx >= f.size()) return 0.0;
    return f.at(idx).toDouble();
  };

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    QString q = QString::fromStdString(line);
    if (q.endsWith('\r')) q.chop(1);
    if (q.trimmed().isEmpty()) continue;
    const QStringList f = splitCsvLine(q);
    if (iLoc < 0 || iLoc >= f.size()) continue;
    const QString locName = f.at(iLoc);
    int li = -1;
    if (locName.contains("A1")) li = 0;
    else if (locName.contains("A2")) li = 1;
    if (li < 0) continue;
    loc[li].has = true;
    loc[li].b0x = num(f, iB0x); loc[li].b0y = num(f, iB0y); loc[li].b0z = num(f, iB0z);
    loc[li].b1x = num(f, iB1x); loc[li].b1y = num(f, iB1y); loc[li].b1z = num(f, iB1z);
    loc[li].dx  = num(f, iDx);  loc[li].dy  = num(f, iDy);
    if (iMoe >= 0 && iMoe < f.size()) moeId = f.at(iMoe);
  }

  m_currentMoeId = moeId;
  m_haveData = loc[0].has || loc[1].has;

  auto colorFor = [](double v) -> QString {
    const double a = std::fabs(v);
    if (a < kPoyntGreenMax) return kColorOk;
    if (a < kPoyntRedMin)   return kColorWarn;
    return kColorBad;
  };
  auto setVal = [](QLabel* lbl, bool has, double v, int prec, const QString& color) {
    if (!lbl) return;
    if (has) {
      lbl->setText(QString::number(v, 'f', prec));
      lbl->setStyleSheet(QStringLiteral("color:%1;%2").arg(color, kValueFontCss));
    } else {
      lbl->setText("-");
      lbl->setStyleSheet(QStringLiteral("color:%1;%2").arg(kColorNeutral, kValueFontCss));
    }
  };

  for (int li = 0; li < 2; ++li) {
    const LocData& d = loc[li];
    setVal(m_valueLabels[li][0][0], d.has, d.b0x, 4, colorFor(d.b0x));
    setVal(m_valueLabels[li][0][1], d.has, d.b0y, 4, colorFor(d.b0y));
    setVal(m_valueLabels[li][0][2], d.has, d.b0z, 1, "#e0e0e0");   // Z (um), not pass/fail
    setVal(m_valueLabels[li][1][0], d.has, d.b1x, 4, colorFor(d.b1x));
    setVal(m_valueLabels[li][1][1], d.has, d.b1y, 4, colorFor(d.b1y));
    setVal(m_valueLabels[li][1][2], d.has, d.b1z, 1, "#e0e0e0");   // Z (um), not pass/fail
    setVal(m_txRxLabels[li][0], d.has, d.dx, 3, "#e0e0e0");
    setVal(m_txRxLabels[li][1], d.has, d.dy, 3, "#e0e0e0");
  }

  m_panelHeader->setText(
      QStringLiteral("Poynt X / Y  (deg)   -   MOE %1").arg(moeId.isEmpty() ? "?" : moeId));
}

void MoeScreeningWidget::clearPanel() {
  for (int li = 0; li < 2; ++li)
    for (int b = 0; b < 2; ++b)
      for (int a = 0; a < 3; ++a)
        if (m_valueLabels[li][b][a]) {
          m_valueLabels[li][b][a]->setText("-");
          m_valueLabels[li][b][a]->setStyleSheet(
              QStringLiteral("color:%1;%2").arg(kColorNeutral, kValueFontCss));
        }
  for (int li = 0; li < 2; ++li)
    for (int a = 0; a < 2; ++a)
      if (m_txRxLabels[li][a]) {
        m_txRxLabels[li][a]->setText("-");
        m_txRxLabels[li][a]->setStyleSheet(
            QStringLiteral("color:%1;%2").arg(kColorNeutral, kValueFontCss));
      }
  if (m_panelHeader) m_panelHeader->setText("Poynt X / Y  (deg)");
}

void MoeScreeningWidget::onSaveFinalMeasurement() {
  if (!m_haveData || m_currentPath.isEmpty()) {
    QMessageBox::warning(this, "Save final measurement",
                         "No MOE measurement loaded yet.");
    return;
  }
  const QString dir = watchDir();
  if (dir.isEmpty()) {
    QMessageBox::warning(this, "Save final measurement", "No product loaded.");
    return;
  }
  QDir().mkpath(dir);

  // Re-parse the current CSV into a JSON snapshot keyed by A1/A2.
  json out;
  out["moe_id"] = m_currentMoeId.toStdString();
  out["exported_at"] = QDateTime::currentDateTimeUtc()
                           .toString("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'").toStdString();
  out["source_csv"] =
      QFileInfo(m_currentPath).fileName().toStdString();

  auto locJson = [this](int li) {
    json j;
    j["b0"] = { {"poynt_x_deg", m_valueLabels[li][0][0]->text().toDouble()},
                {"poynt_y_deg", m_valueLabels[li][0][1]->text().toDouble()},
                {"z0_um",       m_valueLabels[li][0][2]->text().toDouble()} };
    j["b1"] = { {"poynt_x_deg", m_valueLabels[li][1][0]->text().toDouble()},
                {"poynt_y_deg", m_valueLabels[li][1][1]->text().toDouble()},
                {"z0_um",       m_valueLabels[li][1][2]->text().toDouble()} };
    j["tx_rx"] = { {"dx_um", m_txRxLabels[li][0]->text().toDouble()},
                   {"dy_um", m_txRxLabels[li][1]->text().toDouble()} };
    return j;
  };
  out["a1"] = locJson(0);
  out["a2"] = locJson(1);

  const QString id = m_currentMoeId.isEmpty() ? QStringLiteral("unknown")
                                              : sanitizeSegment(m_currentMoeId);
  const QString outPath = dir + "/" + id + "_final.json";
  std::ofstream o(outPath.toStdString(), std::ios::binary | std::ios::trunc);
  if (!o.is_open()) {
    QMessageBox::warning(this, "Save final measurement",
                         QString("Failed to write:\n%1").arg(outPath));
    return;
  }
  o << out.dump(2);
  o.close();

  appendLog(QStringLiteral("[final] saved -> %1").arg(outPath), "#4caf50");
  QMessageBox::information(this, "Save final measurement",
                           QString("Saved final measurement to:\n%1").arg(outPath));
}

void MoeScreeningWidget::onOpenDataFolder() {
  const QString dir = watchDir();
  if (dir.isEmpty()) {
    QMessageBox::warning(this, "Open data folder",
                         "No product loaded - data folder is unknown.");
    return;
  }
  QDir().mkpath(dir);
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MoeScreeningWidget::appendLog(const QString& line, const QString& color) {
  if (!m_log) return;
  if (color.isEmpty()) {
    m_log->appendPlainText(line);
  } else {
    m_log->appendHtml(QStringLiteral("<span style=\"color:%1;\">%2</span>")
                          .arg(color, line.toHtmlEscaped()));
  }
}

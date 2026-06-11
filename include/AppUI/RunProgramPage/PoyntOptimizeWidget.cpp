// PoyntOptimizeWidget.cpp - see PoyntOptimizeWidget.h for overview.

#include "PoyntOptimizeWidget.h"
#include "Bezel3dStyle.h"
#include "ApplicationContext.h"
#include "unified_motion_manager.h"
#include "product_position_manager.h"
#include "ScriptEngine.h"
#include "ScriptWorker.h"
#include "SequenceEngine.h"
#include "SequencePromptDialog.h"
#include "M4AASerialDataWidget.h"

#include <QMessageBox>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QFont>
#include <QFileInfo>
#include <QMetaObject>
#include <QInputDialog>
#include <QApplication>
#include <QSplitter>
#include <QThread>
#include <Qt>

#include <future>

namespace {
constexpr const char* kHexDevice           = "hex-right";
constexpr const char* kBaselineScript      = "CLAUDE_step1_baseline.lua";
constexpr const char* kAutomateScript      = "CLAUDE_step_all.lua";
constexpr const char* kDutInitScript       = "dut_init.lua";
constexpr const char* kDutCloseScript      = "dut_close.lua";
constexpr double      kTranslationStepUmIncrement = 0.5;     // um
constexpr double      kRotationStepMilliDegIncrement = 10.0; // millideg

QFont boldOf(const QLabel* src) {
  QFont f = src->font();
  f.setBold(true);
  return f;
}
}  // namespace

PoyntOptimizeWidget::PoyntOptimizeWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent), m_context(context) {
  setupUI();
  refreshTranslationStepLabel();
  refreshRotationStepLabel();
  setBusy(false);
}

PoyntOptimizeWidget::~PoyntOptimizeWidget() {
  // Ask any running script to stop. m_scriptWorker is parented to `this`, so
  // Qt will delete it after deleteLater(); we just signal it.
  if (m_context && m_context->scriptEngine && m_context->scriptEngine->IsRunning()) {
    m_context->scriptEngine->RequestStop();
  }
  if (m_scriptWorker) {
    m_scriptWorker->requestStop();
    m_scriptWorker->wait(2000);
  }
}

void PoyntOptimizeWidget::setupUI() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(10);

  auto* header = new QLabel(
    "<b>Poynt Optimize</b> &mdash; DUT lifecycle + automation scripts, "
    "plus a hex-right jog ladder that re-measures with "
    "<code>CLAUDE_step1_baseline.lua</code> after each move.");
  header->setWordWrap(true);
  root->addWidget(header);

  // Shared 3D bevel styles (see Bezel3dStyle.h). Green = DUT lifecycle / save,
  // yellow = automation / move-to, orange = single measurement, grey = step
  // nudges, blue = the X/Y/Z + U/V/W jog ladder, red = Stop.
  const QString kGreenStyle  = bezel3d("#66bb6a", "#43a047", "#81c784", "#2e7d32", "white");
  const QString kYellowStyle = bezel3d("#fff350", "#fbc02d", "#fff59d", "#f9a825", "#1a1a1a");
  const QString kOrangeStyle = bezel3d("#ffa726", "#f57c00", "#ffb74d", "#ef6c00", "white");
  const QString kGrayStyle   = bezel3d("#d4d9de", "#aeb4ba", "#e2e6ea", "#9aa0a6", "#1a1a1a");
  const QString kJogBlueStyle = bezel3d("#42a5f5", "#1e88e5", "#64b5f6", "#1565c0", "white");
  const QString kRedStyle    = bezel3d("#ef5350", "#e53935", "#f44336", "#c62828", "white", "#5a3a3a", "#aa9999");

  // --- Top action row -------------------------------------------------------
  auto* topRow = new QHBoxLayout();
  topRow->setSpacing(8);

  m_dutInitBtn   = new QPushButton("DUT Init");
  m_dutInitBtn->setToolTip("Run dut_init.lua in the background.");
  m_dutInitBtn->setStyleSheet(kGreenStyle);
  m_dutCloseBtn  = new QPushButton("DUT Close");
  m_dutCloseBtn->setToolTip("Run dut_close.lua in the background.");
  m_dutCloseBtn->setStyleSheet(kGreenStyle);
  m_automateBtn  = new QPushButton("Automate correction");
  m_automateBtn->setToolTip(
    "Run CLAUDE_step_all.lua in the background. Stop button breaks it.");
  m_automateBtn->setStyleSheet(kYellowStyle);
  m_singleBtn    = new QPushButton("Single measurement");
  m_singleBtn->setToolTip("Run CLAUDE_step1_baseline.lua in the background.");
  m_singleBtn->setStyleSheet(kOrangeStyle);
  m_stopBtn      = new QPushButton("Stop");
  m_stopBtn->setToolTip(
    "Request a stop on whatever script is running. Has no effect on a "
    "blocking motion move that has already been dispatched.");
  m_stopBtn->setStyleSheet(kRedStyle);

  for (QPushButton* b : {m_dutInitBtn, m_dutCloseBtn, m_automateBtn, m_singleBtn}) {
    b->setMinimumWidth(150);
    topRow->addWidget(b);
  }
  topRow->addSpacing(20);
  topRow->addWidget(m_stopBtn);
  topRow->addStretch(1);
  root->addLayout(topRow);

  connect(m_dutInitBtn,  &QPushButton::clicked, this, &PoyntOptimizeWidget::onDutInit);
  connect(m_dutCloseBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onDutClose);
  connect(m_automateBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onAutomate);
  connect(m_singleBtn,   &QPushButton::clicked, this, &PoyntOptimizeWidget::onSingleMeasurement);
  connect(m_stopBtn,     &QPushButton::clicked, this, &PoyntOptimizeWidget::onStop);

  m_busyLabel = new QLabel("idle");
  m_busyLabel->setStyleSheet("color: #888;");
  root->addWidget(m_busyLabel);

  // --- Jog groups (side by side) -------------------------------------------
  auto* jogRow = new QHBoxLayout();
  jogRow->setSpacing(12);

  // Translation group.
  m_translationGroup = new QGroupBox("translation (hex-right X/Y/Z, mm)");
  m_translationGroup->setStyleSheet(
    "QGroupBox { background-color: #1e2a1f; border: 1px solid #2e5232; border-radius: 4px; margin-top: 8px; padding-top: 8px; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
  {
    auto* g = new QGridLayout(m_translationGroup);
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(6);

    // Step display + +/- step buttons.
    m_translationStepLabel = new QLabel();
    {
      QFont f = m_translationStepLabel->font();
      f.setPointSizeF(f.pointSizeF() + 3.0);
      f.setBold(true);
      m_translationStepLabel->setFont(f);
      m_translationStepLabel->setStyleSheet("color: #ffeb3b;");
    }
    auto* stepUnit = new QLabel("um");
    g->addWidget(m_translationStepLabel, 0, 0);
    g->addWidget(stepUnit,               0, 1);

    m_translationStepPlusBtn  = new QPushButton(QStringLiteral("+%1").arg(kTranslationStepUmIncrement, 0, 'f', 1));
    m_translationStepMinusBtn = new QPushButton(QStringLiteral("-%1").arg(kTranslationStepUmIncrement, 0, 'f', 1));
    m_translationStepPlusBtn->setToolTip("Add 0.5 um to step.");
    m_translationStepMinusBtn->setToolTip("Subtract 0.5 um from step.");
    m_translationStepPlusBtn->setStyleSheet(kGrayStyle);
    m_translationStepMinusBtn->setStyleSheet(kGrayStyle);
    g->addWidget(m_translationStepPlusBtn,  1, 0);
    g->addWidget(m_translationStepMinusBtn, 1, 1);
    connect(m_translationStepPlusBtn,  &QPushButton::clicked, this, &PoyntOptimizeWidget::onTranslationStepInc);
    connect(m_translationStepMinusBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onTranslationStepDec);

    // +X / +Y / +Z (row 2), -X / -Y / -Z (row 3).
    m_xPlusBtn  = new QPushButton("+X");  m_yPlusBtn  = new QPushButton("+Y");  m_zPlusBtn  = new QPushButton("+Z");
    m_xMinusBtn = new QPushButton("-X");  m_yMinusBtn = new QPushButton("-Y");  m_zMinusBtn = new QPushButton("-Z");
    for (QPushButton* b : {m_xPlusBtn, m_yPlusBtn, m_zPlusBtn, m_xMinusBtn, m_yMinusBtn, m_zMinusBtn})
      b->setStyleSheet(kJogBlueStyle);
    g->addWidget(m_xPlusBtn,  2, 0);  g->addWidget(m_yPlusBtn,  2, 1);  g->addWidget(m_zPlusBtn,  2, 2);
    g->addWidget(m_xMinusBtn, 3, 0);  g->addWidget(m_yMinusBtn, 3, 1);  g->addWidget(m_zMinusBtn, 3, 2);
    connect(m_xPlusBtn,  &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogXPlus);
    connect(m_xMinusBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogXMinus);
    connect(m_yPlusBtn,  &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogYPlus);
    connect(m_yMinusBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogYMinus);
    connect(m_zPlusBtn,  &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogZPlus);
    connect(m_zMinusBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogZMinus);
  }
  jogRow->addWidget(m_translationGroup);

  // Rotation group.
  m_rotationGroup = new QGroupBox("rotation (hex-right U/V/W, deg)");
  m_rotationGroup->setStyleSheet(
    "QGroupBox { background-color: #1d2630; border: 1px solid #2c4762; border-radius: 4px; margin-top: 8px; padding-top: 8px; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
  {
    auto* g = new QGridLayout(m_rotationGroup);
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(6);

    m_rotationStepLabel = new QLabel();
    {
      QFont f = m_rotationStepLabel->font();
      f.setPointSizeF(f.pointSizeF() + 3.0);
      f.setBold(true);
      m_rotationStepLabel->setFont(f);
      m_rotationStepLabel->setStyleSheet("color: #ffeb3b;");
    }
    auto* stepUnit = new QLabel("millideg");
    g->addWidget(m_rotationStepLabel, 0, 0);
    g->addWidget(stepUnit,            0, 1);

    m_rotationStepPlusBtn  = new QPushButton(QStringLiteral("+%1").arg(kRotationStepMilliDegIncrement, 0, 'f', 0));
    m_rotationStepMinusBtn = new QPushButton(QStringLiteral("-%1").arg(kRotationStepMilliDegIncrement, 0, 'f', 0));
    m_rotationStepPlusBtn->setToolTip("Add 10 millideg to step.");
    m_rotationStepMinusBtn->setToolTip("Subtract 10 millideg from step.");
    m_rotationStepPlusBtn->setStyleSheet(kGrayStyle);
    m_rotationStepMinusBtn->setStyleSheet(kGrayStyle);
    g->addWidget(m_rotationStepPlusBtn,  1, 0);
    g->addWidget(m_rotationStepMinusBtn, 1, 1);
    connect(m_rotationStepPlusBtn,  &QPushButton::clicked, this, &PoyntOptimizeWidget::onRotationStepInc);
    connect(m_rotationStepMinusBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onRotationStepDec);

    m_uPlusBtn  = new QPushButton("+U");  m_vPlusBtn  = new QPushButton("+V");  m_wPlusBtn  = new QPushButton("+W");
    m_uMinusBtn = new QPushButton("-U");  m_vMinusBtn = new QPushButton("-V");  m_wMinusBtn = new QPushButton("-W");
    for (QPushButton* b : {m_uPlusBtn, m_vPlusBtn, m_wPlusBtn, m_uMinusBtn, m_vMinusBtn, m_wMinusBtn})
      b->setStyleSheet(kJogBlueStyle);
    g->addWidget(m_uPlusBtn,  2, 0);  g->addWidget(m_vPlusBtn,  2, 1);  g->addWidget(m_wPlusBtn,  2, 2);
    g->addWidget(m_uMinusBtn, 3, 0);  g->addWidget(m_vMinusBtn, 3, 1);  g->addWidget(m_wMinusBtn, 3, 2);
    connect(m_uPlusBtn,  &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogUPlus);
    connect(m_uMinusBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogUMinus);
    connect(m_vPlusBtn,  &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogVPlus);
    connect(m_vMinusBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogVMinus);
    connect(m_wPlusBtn,  &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogWPlus);
    connect(m_wMinusBtn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onJogWMinus);
  }
  jogRow->addWidget(m_rotationGroup);

  // Saved-position quick-move group.
  m_positionsGroup = new QGroupBox("positions (gantry-main)");
  m_positionsGroup->setStyleSheet(
    "QGroupBox { background-color: #2a2230; border: 1px solid #563f6a; border-radius: 4px; margin-top: 8px; padding-top: 8px; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }");
  {
    auto* g = new QGridLayout(m_positionsGroup);
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(6);

    m_moveToMoeA1Btn = new QPushButton("Move to MOE-A1");
    m_moveToMoeA1Btn->setMinimumWidth(150);
    m_moveToMoeA1Btn->setStyleSheet(kYellowStyle);
    m_moveToMoeA1Btn->setToolTip(
      "Move gantry-main to the saved 'MOE-A1' named position. "
      "Save it from the M4 Angle tab first.");
    g->addWidget(m_moveToMoeA1Btn, 0, 0);

    m_moveToMoeA2Btn = new QPushButton("Move to MOE-A2");
    m_moveToMoeA2Btn->setMinimumWidth(150);
    m_moveToMoeA2Btn->setStyleSheet(kYellowStyle);
    m_moveToMoeA2Btn->setToolTip(
      "Move gantry-main to the saved 'MOE-A2' named position. "
      "Save it from the M4 Angle tab first.");
    g->addWidget(m_moveToMoeA2Btn, 1, 0);

    m_saveMoeA1Btn = new QPushButton("Save MOE-A1");
    m_saveMoeA1Btn->setMinimumWidth(150);
    m_saveMoeA1Btn->setStyleSheet(kGreenStyle);
    m_saveMoeA1Btn->setToolTip(
      "Overwrite the saved 'MOE-A1' named position with the current gantry-main "
      "position. Shows a confirmation dialog with the delta vs. the existing "
      "saved value before writing.");
    g->addWidget(m_saveMoeA1Btn, 0, 1);

    m_saveMoeA2Btn = new QPushButton("Save MOE-A2");
    m_saveMoeA2Btn->setMinimumWidth(150);
    m_saveMoeA2Btn->setStyleSheet(kGreenStyle);
    m_saveMoeA2Btn->setToolTip(
      "Overwrite the saved 'MOE-A2' named position with the current gantry-main "
      "position. Shows a confirmation dialog with the delta vs. the existing "
      "saved value before writing.");
    g->addWidget(m_saveMoeA2Btn, 1, 1);

    connect(m_moveToMoeA1Btn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onMoveToMoeA1);
    connect(m_moveToMoeA2Btn, &QPushButton::clicked, this, &PoyntOptimizeWidget::onMoveToMoeA2);
    connect(m_saveMoeA1Btn,   &QPushButton::clicked, this, &PoyntOptimizeWidget::onSaveMoeA1);
    connect(m_saveMoeA2Btn,   &QPushButton::clicked, this, &PoyntOptimizeWidget::onSaveMoeA2);
  }
  jogRow->addWidget(m_positionsGroup);

  jogRow->addStretch(1);
  root->addLayout(jogRow);

  // --- Log + M4AA chart (vertical splitter so the operator can size them) -
  m_log = new QPlainTextEdit();
  m_log->setReadOnly(true);
  m_log->setMaximumBlockCount(2000);
  m_log->setStyleSheet(
    "QPlainTextEdit { background-color: #0d1117; color: #c9d1d9; "
    "font-family: 'Consolas', 'Courier New', monospace; font-size: 11pt; }");

  m_m4aaPanel = new M4AASerialDataWidget(m_context);

  auto* bottomSplit = new QSplitter(Qt::Vertical);
  bottomSplit->addWidget(m_log);
  bottomSplit->addWidget(m_m4aaPanel);
  bottomSplit->setStretchFactor(0, 1);
  bottomSplit->setStretchFactor(1, 2);  // give M4AA the bigger share by default
  bottomSplit->setChildrenCollapsible(false);
  root->addWidget(bottomSplit, 1);
}

void PoyntOptimizeWidget::refreshTranslationStepLabel() {
  m_translationStepLabel->setText(QString::number(m_translationStepUm, 'f', 1));
}

void PoyntOptimizeWidget::refreshRotationStepLabel() {
  m_rotationStepLabel->setText(QString::number(m_rotationStepMilliDeg, 'f', 0));
}

void PoyntOptimizeWidget::onTranslationStepInc() {
  if (m_busy) return;
  m_translationStepUm += kTranslationStepUmIncrement;
  refreshTranslationStepLabel();
}

void PoyntOptimizeWidget::onTranslationStepDec() {
  if (m_busy) return;
  m_translationStepUm -= kTranslationStepUmIncrement;
  if (m_translationStepUm < 0.0) m_translationStepUm = 0.0;
  refreshTranslationStepLabel();
}

void PoyntOptimizeWidget::onRotationStepInc() {
  if (m_busy) return;
  m_rotationStepMilliDeg += kRotationStepMilliDegIncrement;
  refreshRotationStepLabel();
}

void PoyntOptimizeWidget::onRotationStepDec() {
  if (m_busy) return;
  m_rotationStepMilliDeg -= kRotationStepMilliDegIncrement;
  if (m_rotationStepMilliDeg < 0.0) m_rotationStepMilliDeg = 0.0;
  refreshRotationStepLabel();
}

void PoyntOptimizeWidget::setBusy(bool busy, const QString& whatRunning) {
  m_busy = busy;
  // Disable every action except Stop while busy. The +/-step buttons are
  // also disabled because changing the step mid-flight would confuse the
  // operator about which step value the running jog used.
  QPushButton* gated[] = {
    m_dutInitBtn, m_dutCloseBtn, m_automateBtn, m_singleBtn,
    m_translationStepPlusBtn, m_translationStepMinusBtn,
    m_xPlusBtn, m_xMinusBtn, m_yPlusBtn, m_yMinusBtn, m_zPlusBtn, m_zMinusBtn,
    m_rotationStepPlusBtn, m_rotationStepMinusBtn,
    m_uPlusBtn, m_uMinusBtn, m_vPlusBtn, m_vMinusBtn, m_wPlusBtn, m_wMinusBtn,
    m_moveToMoeA1Btn, m_moveToMoeA2Btn,
    m_saveMoeA1Btn, m_saveMoeA2Btn,
  };
  for (QPushButton* b : gated) if (b) b->setEnabled(!busy);
  if (m_stopBtn) m_stopBtn->setEnabled(busy);

  if (m_busyLabel) {
    if (busy) {
      m_busyLabel->setText(QStringLiteral("BUSY: %1").arg(whatRunning.isEmpty() ? "running..." : whatRunning));
      m_busyLabel->setStyleSheet("color: #fb8c00; font-weight: bold;");
    } else {
      m_busyLabel->setText("idle");
      m_busyLabel->setStyleSheet("color: #888;");
    }
  }
}

QString PoyntOptimizeWidget::resolveScriptPath(const QString& scriptFile) const {
  // Same resolution as ScriptRunnerPage: prefer product scripts when a
  // product is loaded, otherwise fall back to the global scripts/ folder.
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

bool PoyntOptimizeWidget::launchScript(const QString& scriptFile, const QString& busyLabel) {
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

  // Clear the engine's stop flag from any prior Stop click.
  m_context->scriptEngine->ClearStopFlag();

  // Wire dialog callbacks so scripts that call bobcat.askConfirm /
  // bobcat.askInput pop a modal here (the script runs on a worker
  // thread, so each callback marshals to the UI thread with
  // BlockingQueuedConnection and returns the user's answer).
  if (m_context->sequenceEngine) {
    m_context->sequenceEngine->SetUserPromptCallback(
      [this](const std::string& title, const std::string& message) -> bool {
        bool result = false;
        auto show = [this, &title, &message, &result]() {
          result = SequencePromptDialog::ShowPrompt(
            QString::fromStdString(title),
            QString::fromStdString(message),
            this);
        };
        if (QThread::currentThread() == QApplication::instance()->thread()) {
          show();
        } else {
          QMetaObject::invokeMethod(this, show, Qt::BlockingQueuedConnection);
        }
        return result;
      });
    m_context->sequenceEngine->SetUserInputCallback(
      [this](const std::string& title, const std::string& prompt,
             const std::string& defaultValue) -> std::pair<bool, std::string> {
        std::pair<bool, std::string> result{false, ""};
        auto show = [this, &title, &prompt, &defaultValue, &result]() {
          bool ok = false;
          const QString text = QInputDialog::getText(
            this,
            QString::fromStdString(title),
            QString::fromStdString(prompt),
            QLineEdit::Normal,
            QString::fromStdString(defaultValue),
            &ok);
          result = std::make_pair(ok, text.toStdString());
        };
        if (QThread::currentThread() == QApplication::instance()->thread()) {
          show();
        } else {
          QMetaObject::invokeMethod(this, show, Qt::BlockingQueuedConnection);
        }
        return result;
      });
  }

  setBusy(true, busyLabel);
  appendLog(QStringLiteral("[run] %1").arg(path), "#60a5fa");

  m_scriptWorker = new ScriptWorker(
    m_context->scriptEngine, path.toStdString(),
    ScriptWorker::Mode::FILE, this);

  connect(m_scriptWorker, &ScriptWorker::executionStarted,
          this, &PoyntOptimizeWidget::onScriptStarted, Qt::QueuedConnection);
  connect(m_scriptWorker, &ScriptWorker::executionCompleted,
          this, &PoyntOptimizeWidget::onScriptCompleted, Qt::QueuedConnection);
  connect(m_scriptWorker, &ScriptWorker::scriptOutput,
          this, &PoyntOptimizeWidget::onScriptOutput, Qt::QueuedConnection);

  m_scriptWorker->start();
  return true;
}

void PoyntOptimizeWidget::onDutInit() {
  launchScript(QString::fromUtf8(kDutInitScript),  QStringLiteral("DUT Init"));
}
void PoyntOptimizeWidget::onDutClose() {
  launchScript(QString::fromUtf8(kDutCloseScript), QStringLiteral("DUT Close"));
}
void PoyntOptimizeWidget::onAutomate() {
  launchScript(QString::fromUtf8(kAutomateScript), QStringLiteral("Automate correction"));
}
void PoyntOptimizeWidget::onSingleMeasurement() {
  launchScript(QString::fromUtf8(kBaselineScript), QStringLiteral("Single measurement"));
}

void PoyntOptimizeWidget::onStop() {
  if (!m_context || !m_context->scriptEngine) return;
  appendLog("[stop] requested", "#fb8c00");
  m_context->scriptEngine->RequestStop();
  if (m_scriptWorker) m_scriptWorker->requestStop();
}

void PoyntOptimizeWidget::jogThenBaseline(const QString& axis,
                                          double signedDistance,
                                          const QString& busyLabel) {
  if (m_busy) {
    appendLog("[busy] another job is already running", "#ef5350");
    return;
  }
  if (!m_context || !m_context->motionManager) {
    appendLog("[error] motion manager unavailable", "#ef5350");
    return;
  }

  setBusy(true, busyLabel);
  appendLog(QStringLiteral("[jog] hex-right %1 %2%3")
              .arg(axis)
              .arg(signedDistance >= 0 ? "+" : "")
              .arg(signedDistance, 0, 'f', 6),
            "#60a5fa");

  auto* motionMgr = m_context->motionManager;
  const std::string device = kHexDevice;
  const std::string axisStd = axis.toStdString();

  m_moveFuture = std::async(std::launch::async,
    [this, motionMgr, device, axisStd, signedDistance]() {
      const bool ok = motionMgr->MoveRelative(device, axisStd, signedDistance, true);
      QMetaObject::invokeMethod(this, [this, ok]() {
        if (!ok) {
          appendLog(QStringLiteral("[jog] MoveRelative FAILED"), "#ef5350");
          setBusy(false);
          return;
        }
        appendLog(QStringLiteral("[jog] move complete, running baseline..."), "#4caf50");
        // launchScript flips setBusy back to busy with the baseline label;
        // we're still in the busy state so the chain is seamless from the UI.
        // Re-set busy so the label updates to the baseline phase.
        m_busy = false;  // launchScript() guards on this — clear before re-entering.
        launchScript(QString::fromUtf8(kBaselineScript),
                     QStringLiteral("post-jog baseline"));
      }, Qt::QueuedConnection);
    });
}

namespace {
// Translation step is shown in um; relative move expects mm.
inline double translationStepMm(double stepUm) { return stepUm * 1e-3; }
// Rotation step is shown in millideg; relative move expects deg.
inline double rotationStepDeg(double stepMilliDeg) { return stepMilliDeg * 1e-3; }
}

void PoyntOptimizeWidget::onJogXPlus()  { jogThenBaseline("X", +translationStepMm(m_translationStepUm), "X+ jog"); }
void PoyntOptimizeWidget::onJogXMinus() { jogThenBaseline("X", -translationStepMm(m_translationStepUm), "X- jog"); }
void PoyntOptimizeWidget::onJogYPlus()  { jogThenBaseline("Y", +translationStepMm(m_translationStepUm), "Y+ jog"); }
void PoyntOptimizeWidget::onJogYMinus() { jogThenBaseline("Y", -translationStepMm(m_translationStepUm), "Y- jog"); }
void PoyntOptimizeWidget::onJogZPlus()  { jogThenBaseline("Z", +translationStepMm(m_translationStepUm), "Z+ jog"); }
void PoyntOptimizeWidget::onJogZMinus() { jogThenBaseline("Z", -translationStepMm(m_translationStepUm), "Z- jog"); }

void PoyntOptimizeWidget::onJogUPlus()  { jogThenBaseline("U", +rotationStepDeg(m_rotationStepMilliDeg), "U+ jog"); }
void PoyntOptimizeWidget::onJogUMinus() { jogThenBaseline("U", -rotationStepDeg(m_rotationStepMilliDeg), "U- jog"); }
void PoyntOptimizeWidget::onJogVPlus()  { jogThenBaseline("V", +rotationStepDeg(m_rotationStepMilliDeg), "V+ jog"); }
void PoyntOptimizeWidget::onJogVMinus() { jogThenBaseline("V", -rotationStepDeg(m_rotationStepMilliDeg), "V- jog"); }
void PoyntOptimizeWidget::onJogWPlus()  { jogThenBaseline("W", +rotationStepDeg(m_rotationStepMilliDeg), "W+ jog"); }
void PoyntOptimizeWidget::onJogWMinus() { jogThenBaseline("W", -rotationStepDeg(m_rotationStepMilliDeg), "W- jog"); }

void PoyntOptimizeWidget::onMoveToMoeA1() { moveToNamedGantryPosition("MOE-A1"); }
void PoyntOptimizeWidget::onMoveToMoeA2() { moveToNamedGantryPosition("MOE-A2"); }

void PoyntOptimizeWidget::onSaveMoeA1() { saveCurrentToNamedGantryPosition("MOE-A1"); }
void PoyntOptimizeWidget::onSaveMoeA2() { saveCurrentToNamedGantryPosition("MOE-A2"); }

void PoyntOptimizeWidget::saveCurrentToNamedGantryPosition(const char* name) {
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

  // Look up the existing saved position so the dialog can show the delta.
  PositionStruct prev;
  const bool hadPrev = motionMgr->GetNamedPosition("gantry-main", name, prev);

  QString deltaText;
  if (hadPrev) {
    const double dx = cur.x - prev.x;
    const double dy = cur.y - prev.y;
    const double dz = cur.z - prev.z;
    const double du = cur.u - prev.u;
    const double dv = cur.v - prev.v;
    const double dw = cur.w - prev.w;
    deltaText = QStringLiteral(
      "<b>Current vs. saved '%1':</b><br>"
      "<pre>"
      "          new           old           delta\n"
      "X    %2  %3  %4 mm\n"
      "Y    %5  %6  %7 mm\n"
      "Z    %8  %9  %10 mm\n"
      "U    %11  %12  %13 deg\n"
      "V    %14  %15  %16 deg\n"
      "W    %17  %18  %19 deg"
      "</pre>")
      .arg(tag)
      .arg(cur.x, 12, 'f', 4).arg(prev.x, 12, 'f', 4).arg(dx, 12, 'f', 4)
      .arg(cur.y, 12, 'f', 4).arg(prev.y, 12, 'f', 4).arg(dy, 12, 'f', 4)
      .arg(cur.z, 12, 'f', 4).arg(prev.z, 12, 'f', 4).arg(dz, 12, 'f', 4)
      .arg(cur.u, 12, 'f', 4).arg(prev.u, 12, 'f', 4).arg(du, 12, 'f', 4)
      .arg(cur.v, 12, 'f', 4).arg(prev.v, 12, 'f', 4).arg(dv, 12, 'f', 4)
      .arg(cur.w, 12, 'f', 4).arg(prev.w, 12, 'f', 4).arg(dw, 12, 'f', 4);
  } else {
    deltaText = QStringLiteral(
      "<b>No previous saved value for '%1'.</b><br>"
      "<pre>"
      "X = %2 mm\n"
      "Y = %3 mm\n"
      "Z = %4 mm\n"
      "U = %5 deg\n"
      "V = %6 deg\n"
      "W = %7 deg"
      "</pre>")
      .arg(tag)
      .arg(cur.x, 0, 'f', 4)
      .arg(cur.y, 0, 'f', 4)
      .arg(cur.z, 0, 'f', 4)
      .arg(cur.u, 0, 'f', 4)
      .arg(cur.v, 0, 'f', 4)
      .arg(cur.w, 0, 'f', 4);
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

  // Persist using the same flow as M4 Angle's Save MOE-A{1,2}: update the
  // motion manager's in-memory map, import into the product manager, then
  // SaveProduct to flush positions.{product}.json.
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

void PoyntOptimizeWidget::moveToNamedGantryPosition(const char* name) {
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
    appendLog(QStringLiteral(
        "[error] no saved gantry-main position '%1'. "
        "Save it from the M4 Angle tab first.").arg(QString::fromUtf8(name)),
      "#ef5350");
    return;
  }

  const QString tag = QString::fromUtf8(name);
  setBusy(true, QStringLiteral("Move to %1").arg(tag));
  appendLog(QStringLiteral("[move] gantry-main -> %1 (X=%2 Y=%3 Z=%4 mm)")
              .arg(tag)
              .arg(target.x, 0, 'f', 4)
              .arg(target.y, 0, 'f', 4)
              .arg(target.z, 0, 'f', 4),
            "#60a5fa");

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

void PoyntOptimizeWidget::onScriptStarted() {
  appendLog("[script] started", "#888");
}

void PoyntOptimizeWidget::onScriptCompleted(bool success, QString errorMessage) {
  if (success) {
    appendLog("[script] completed OK", "#4caf50");
  } else {
    appendLog(QStringLiteral("[script] FAILED: %1").arg(errorMessage), "#ef5350");
  }
  if (m_scriptWorker) {
    m_scriptWorker->deleteLater();
    m_scriptWorker = nullptr;
  }
  // Unregister the dialog callbacks so other consumers (RecipeWorker,
  // ProgramWorker, ScriptRunnerPage) can install their own without
  // colliding with ours.
  if (m_context && m_context->sequenceEngine) {
    m_context->sequenceEngine->SetUserPromptCallback(nullptr);
    m_context->sequenceEngine->SetUserInputCallback(nullptr);
  }
  setBusy(false);
}

void PoyntOptimizeWidget::onScriptOutput(QString message) {
  appendLog(message);
}

void PoyntOptimizeWidget::appendLog(const QString& line, const QString& color) {
  if (!m_log) return;
  if (color.isEmpty()) {
    m_log->appendPlainText(line);
  } else {
    // Tinted line via inline HTML; appendHtml respects QPlainTextEdit's
    // setMaximumBlockCount setting.
    m_log->appendHtml(QStringLiteral("<span style=\"color:%1;\">%2</span>")
                        .arg(color, line.toHtmlEscaped()));
  }
}

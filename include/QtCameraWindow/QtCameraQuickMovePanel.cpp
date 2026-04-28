// QtCameraQuickMovePanel.cpp
#include "QtCameraQuickMovePanel.h"

#include "motions/unified_motion_manager.h"

#include <QAction>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>

namespace {

const char* kAssignedStyle =
    "QPushButton { background-color: #3e3e42; border: 1px solid #555; "
    "              color: #cccccc; padding: 6px; min-height: 28px; "
    "              border-radius: 3px; font-size: 11px; }"
    "QPushButton:hover { background-color: #505054; }"
    "QPushButton:pressed { background-color: #007acc; }";

const char* kUnassignedStyle =
    "QPushButton { background-color: #333337; border: 1px dashed #555; "
    "              color: #666666; padding: 6px; min-height: 28px; "
    "              border-radius: 3px; font-size: 11px; }"
    "QPushButton:hover { background-color: #3e3e42; }";

}  // namespace

QtCameraQuickMovePanel::QtCameraQuickMovePanel(
    UnifiedMotionManager* motionManager,
    std::string instanceId,
    QWidget* parent)
  : QGroupBox("Quick Move", parent)
  , m_motionManager(motionManager)
  , m_instanceId(std::move(instanceId))
{
  setStyleSheet(
      "QGroupBox { color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; "
      "            margin-top: 6px; padding-top: 4px; }"
      "QGroupBox::title { subcontrol-origin: margin; left: 8px; }"
  );

  setupUI();
  loadSettings();
}

void QtCameraQuickMovePanel::setupUI()
{
  auto* grid = new QGridLayout(this);
  grid->setSpacing(4);
  grid->setContentsMargins(6, 8, 6, 6);

  for (int i = 0; i < COUNT; ++i) {
    auto* btn = new QPushButton("---");
    btn->setStyleSheet(kUnassignedStyle);
    btn->setContextMenuPolicy(Qt::CustomContextMenu);

    const int row = i / COLS;
    const int col = i % COLS;
    grid->addWidget(btn, row, col);

    m_buttons[i] = btn;

    connect(btn, &QPushButton::clicked, this, [this, i]() {
      onClicked(i);
    });
    connect(btn, &QPushButton::customContextMenuRequested, this, [this, i]() {
      onRightClicked(i);
    });
  }
}

void QtCameraQuickMovePanel::onClicked(int index)
{
  if (index < 0 || index >= COUNT) return;
  const auto& data = m_data[index];
  if (!data.assigned) return;
  if (!m_motionManager) return;

  // Execute relative moves for each non-zero axis (non-blocking).
  if (data.relX != 0.0) {
    m_motionManager->MoveRelative("gantry-main", "X", data.relX, false);
  }
  if (data.relY != 0.0) {
    m_motionManager->MoveRelative("gantry-main", "Y", data.relY, false);
  }
  if (data.relZ != 0.0) {
    m_motionManager->MoveRelative("gantry-main", "Z", data.relZ, false);
  }
}

void QtCameraQuickMovePanel::onRightClicked(int index)
{
  if (index < 0 || index >= COUNT) return;

  QMenu menu(this);
  menu.setStyleSheet(
      "QMenu { background-color: #2d2d30; color: #cccccc; border: 1px solid #3e3e42; }"
      "QMenu::item:selected { background-color: #007acc; }"
  );

  auto* configAction = menu.addAction("Configure...");
  auto* clearAction  = menu.addAction("Clear");

  QAction* chosen = menu.exec(QCursor::pos());
  if (chosen == configAction) {
    showConfigDialog(index);
  } else if (chosen == clearAction) {
    m_data[index] = ButtonData{};
    updateButton(index);
    saveSettings();
  }
}

void QtCameraQuickMovePanel::showConfigDialog(int index)
{
  if (index < 0 || index >= COUNT) return;

  const auto& existing = m_data[index];

  QDialog dlg(this);
  dlg.setWindowTitle("Configure Quick Move Button");
  dlg.setFixedSize(280, 220);
  dlg.setStyleSheet(
      "QDialog { background-color: #2d2d30; color: #cccccc; }"
      "QLabel { color: #cccccc; }"
      "QLineEdit { background-color: #3e3e42; color: #cccccc; border: 1px solid #555; "
      "            padding: 2px 4px; }"
      "QDoubleSpinBox { background-color: #3e3e42; color: #cccccc; border: 1px solid #555; "
      "                 padding: 2px 4px; }"
  );

  auto* layout = new QFormLayout(&dlg);
  layout->setSpacing(8);
  layout->setContentsMargins(12, 12, 12, 12);

  auto* labelEdit = new QLineEdit(existing.label);
  labelEdit->setMaxLength(10);
  labelEdit->setPlaceholderText("10 chars max");
  layout->addRow("Label:", labelEdit);

  auto makeSpinBox = [&](double val) {
    auto* spin = new QDoubleSpinBox();
    spin->setRange(-999.0, 999.0);
    spin->setDecimals(4);
    spin->setSuffix(" mm");
    spin->setValue(val);
    return spin;
  };

  auto* xSpin = makeSpinBox(existing.relX);
  auto* ySpin = makeSpinBox(existing.relY);
  auto* zSpin = makeSpinBox(existing.relZ);

  layout->addRow("Rel X:", xSpin);
  layout->addRow("Rel Y:", ySpin);
  layout->addRow("Rel Z:", zSpin);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttons->setStyleSheet(
      "QPushButton { background-color: #3e3e42; color: #cccccc; border: 1px solid #555; "
      "              padding: 4px 16px; border-radius: 3px; }"
      "QPushButton:hover { background-color: #007acc; }"
  );
  layout->addRow(buttons);

  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() == QDialog::Accepted) {
    auto& data = m_data[index];
    data.assigned = true;
    data.label = labelEdit->text().left(10);
    data.relX  = xSpin->value();
    data.relY  = ySpin->value();
    data.relZ  = zSpin->value();

    if (data.label.isEmpty()) {
      data.label = QString("B%1").arg(index);
    }

    updateButton(index);
    saveSettings();
  }
}

void QtCameraQuickMovePanel::updateButton(int index)
{
  if (index < 0 || index >= COUNT) return;
  if (!m_buttons[index]) return;

  const auto& data = m_data[index];

  if (data.assigned) {
    m_buttons[index]->setText(data.label);
    m_buttons[index]->setStyleSheet(kAssignedStyle);
    m_buttons[index]->setToolTip(
        QString("X:%1  Y:%2  Z:%3 mm")
            .arg(data.relX, 0, 'f', 4)
            .arg(data.relY, 0, 'f', 4)
            .arg(data.relZ, 0, 'f', 4));
  } else {
    m_buttons[index]->setText("---");
    m_buttons[index]->setStyleSheet(kUnassignedStyle);
    m_buttons[index]->setToolTip("");
  }
}

void QtCameraQuickMovePanel::saveSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  for (int i = 0; i < COUNT; ++i) {
    const QString prefix = settingsKey(QString::number(i));
    const auto& d = m_data[i];
    settings.setValue(prefix + "/assigned", d.assigned);
    settings.setValue(prefix + "/label",    d.label);
    settings.setValue(prefix + "/relX",     d.relX);
    settings.setValue(prefix + "/relY",     d.relY);
    settings.setValue(prefix + "/relZ",     d.relZ);
  }
  settings.sync();
}

void QtCameraQuickMovePanel::loadSettings()
{
  QSettings settings("Fabrinet", "BobcatEngine");
  for (int i = 0; i < COUNT; ++i) {
    const QString prefix = settingsKey(QString::number(i));
    auto& d = m_data[i];
    d.assigned = settings.value(prefix + "/assigned", false).toBool();
    d.label    = settings.value(prefix + "/label", "").toString().left(10);
    d.relX     = settings.value(prefix + "/relX", 0.0).toDouble();
    d.relY     = settings.value(prefix + "/relY", 0.0).toDouble();
    d.relZ     = settings.value(prefix + "/relZ", 0.0).toDouble();
    updateButton(i);
  }
}

QString QtCameraQuickMovePanel::settingsKey(const QString& key) const
{
  // Settings key prefix is unchanged from the original QtCameraWindow
  // implementation so saved bindings survive the refactor.
  const QString prefix = m_instanceId.empty()
      ? QStringLiteral("QtCameraWindow")
      : QString::fromStdString("QtCameraWindow_" + m_instanceId);
  return prefix + "/QuickMove/" + key;
}

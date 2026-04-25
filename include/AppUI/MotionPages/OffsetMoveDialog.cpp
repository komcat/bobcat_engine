// OffsetMoveDialog.cpp
#include "OffsetMoveDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>

OffsetMoveDialog::OffsetMoveDialog(const QString& positionName,
  double baseX, double baseY, double baseZ,
  QWidget* parent)
  : QDialog(parent)
  , m_baseName(positionName)
  , m_baseX(baseX)
  , m_baseY(baseY)
  , m_baseZ(baseZ)
{
  setupUI();
  updatePreview();
}

void OffsetMoveDialog::setupUI()
{
  setWindowTitle("Move with Offset");
  setModal(true);
  setMinimumWidth(400);
  setMaximumHeight(600);

  // ✅ No dialog stylesheet - inherits dark theme from MainWindow

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(8);
  mainLayout->setContentsMargins(10, 10, 10, 10);

  // Title
  QLabel* titleLabel = new QLabel(QString("Move to: <b>%1</b> with offset").arg(m_baseName));
  titleLabel->setStyleSheet("font-size: 13px;");
  mainLayout->addWidget(titleLabel);

  // Offset inputs
  QGroupBox* offsetGroup = new QGroupBox("Offset (mm)");
  QFormLayout* offsetLayout = new QFormLayout(offsetGroup);
  offsetLayout->setVerticalSpacing(6);

  m_xOffset = new QDoubleSpinBox();
  m_xOffset->setRange(-100.0, 100.0);
  m_xOffset->setDecimals(2);
  m_xOffset->setSingleStep(0.5);
  m_xOffset->setValue(0.0);
  m_xOffset->setMinimumHeight(28);  // ✅ CHANGED from 32

  m_yOffset = new QDoubleSpinBox();
  m_yOffset->setRange(-100.0, 100.0);
  m_yOffset->setDecimals(2);
  m_yOffset->setSingleStep(0.5);
  m_yOffset->setValue(0.0);
  m_yOffset->setMinimumHeight(28);  // ✅ CHANGED from 32

  m_zOffset = new QDoubleSpinBox();
  m_zOffset->setRange(-100.0, 100.0);
  m_zOffset->setDecimals(2);
  m_zOffset->setSingleStep(0.5);
  m_zOffset->setValue(0.0);
  m_zOffset->setMinimumHeight(28);  // ✅ CHANGED from 32

  offsetLayout->addRow("X offset:", m_xOffset);
  offsetLayout->addRow("Y offset:", m_yOffset);
  offsetLayout->addRow("Z offset:", m_zOffset);

  mainLayout->addWidget(offsetGroup);

  // Quick preset buttons
  QGroupBox* presetGroup = new QGroupBox("Quick Presets");
  QHBoxLayout* presetLayout = new QHBoxLayout(presetGroup);

  QPushButton* upBtn = new QPushButton("↑ Y+1mm");
  QPushButton* downBtn = new QPushButton("↓ Y-1mm");
  QPushButton* resetBtn = new QPushButton("Reset");

  upBtn->setFixedHeight(28);  // ✅ CHANGED from 30
  downBtn->setFixedHeight(28);
  resetBtn->setFixedHeight(28);

  presetLayout->addWidget(upBtn);
  presetLayout->addWidget(downBtn);
  presetLayout->addWidget(resetBtn);

  mainLayout->addWidget(presetGroup);

  // Preview
  QGroupBox* previewGroup = new QGroupBox("Preview");
  QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
  previewLayout->setSpacing(4);

  m_previewLabel = new QLabel();
  m_previewLabel->setStyleSheet("font-family: monospace; font-size: 11px; padding: 6px;");
  m_previewLabel->setWordWrap(true);

  m_resultLabel = new QLabel();
  m_resultLabel->setStyleSheet("font-size: 10px; margin-top: 2px;");

  previewLayout->addWidget(m_previewLabel);
  previewLayout->addWidget(m_resultLabel);

  mainLayout->addWidget(previewGroup);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setMinimumHeight(32);
  m_cancelButton->setMinimumWidth(90);

  m_okButton = new QPushButton("Move");
  m_okButton->setMinimumHeight(32);
  m_okButton->setMinimumWidth(90);
  m_okButton->setDefault(true);

  buttonLayout->addWidget(m_cancelButton);
  buttonLayout->addWidget(m_okButton);

  mainLayout->addLayout(buttonLayout);

  // Connections
  connect(m_xOffset, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &OffsetMoveDialog::updatePreview);
  connect(m_yOffset, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &OffsetMoveDialog::updatePreview);
  connect(m_zOffset, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &OffsetMoveDialog::updatePreview);

  connect(upBtn, &QPushButton::clicked, [this]() {
    m_yOffset->setValue(1.0);
  });

  connect(downBtn, &QPushButton::clicked, [this]() {
    m_yOffset->setValue(-1.0);
  });

  connect(resetBtn, &QPushButton::clicked, [this]() {
    m_xOffset->setValue(0.0);
    m_yOffset->setValue(0.0);
    m_zOffset->setValue(0.0);
  });

  connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

void OffsetMoveDialog::updatePreview()
{
  double offsetX = m_xOffset->value();
  double offsetY = m_yOffset->value();
  double offsetZ = m_zOffset->value();

  // Build position name string
  QString posName = m_baseName;

  if (offsetX != 0.0) {
    posName += QString("+X%1").arg(offsetX, 0, 'f', 2);
  }
  if (offsetY != 0.0) {
    posName += QString("+Y%1").arg(offsetY, 0, 'f', 2);
  }
  if (offsetZ != 0.0) {
    posName += QString("+Z%1").arg(offsetZ, 0, 'f', 2);
  }

  m_previewLabel->setText(QString("Position: <b>%1</b>").arg(posName));

  // Show resulting coordinates
  double resultX = m_baseX + offsetX;
  double resultY = m_baseY + offsetY;
  double resultZ = m_baseZ + offsetZ;

  m_resultLabel->setText(
    QString("Result: X=%1, Y=%2, Z=%3 mm")
    .arg(resultX, 0, 'f', 4)
    .arg(resultY, 0, 'f', 4)
    .arg(resultZ, 0, 'f', 4)
  );
}

QString OffsetMoveDialog::getOffsetPositionName() const
{
  double offsetX = m_xOffset->value();
  double offsetY = m_yOffset->value();
  double offsetZ = m_zOffset->value();

  QString posName = m_baseName;

  if (offsetX != 0.0) {
    posName += QString("+X%1").arg(offsetX, 0, 'f', 2);
  }
  if (offsetY != 0.0) {
    posName += QString("+Y%1").arg(offsetY, 0, 'f', 2);
  }
  if (offsetZ != 0.0) {
    posName += QString("+Z%1").arg(offsetZ, 0, 'f', 2);
  }

  return posName;
}
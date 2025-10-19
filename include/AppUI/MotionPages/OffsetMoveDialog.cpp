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
  setMaximumHeight(600);  // ✅ ADD - Limit height

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(8);  // ✅ CHANGED from 12
  mainLayout->setContentsMargins(10, 10, 10, 10);  // ✅ ADD

  // Title
  QLabel* titleLabel = new QLabel(QString("Move to: <b>%1</b> with offset").arg(m_baseName));
  titleLabel->setStyleSheet("QLabel { font-size: 13px; color: #2c3e50; }");  // ✅ Smaller font
  mainLayout->addWidget(titleLabel);

  // ✅ REMOVE or make base group smaller - it's not essential
  // Skip showing base position to save space

  // Offset inputs
  QGroupBox* offsetGroup = new QGroupBox("Offset (mm)");
  QFormLayout* offsetLayout = new QFormLayout(offsetGroup);
  offsetLayout->setVerticalSpacing(6);  // ✅ ADD

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

  // Preview - more compact
  QGroupBox* previewGroup = new QGroupBox("Preview");
  QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
  previewLayout->setSpacing(4);  // ✅ ADD

  m_previewLabel = new QLabel();
  m_previewLabel->setStyleSheet(
    "QLabel {"
    "  color: #2980b9;"
    "  font-family: monospace;"
    "  font-size: 11px;"  // ✅ CHANGED from 12
    "  padding: 6px;"  // ✅ CHANGED from 8
    "  background-color: #ecf0f1;"
    "  border-radius: 4px;"
    "}"
  );
  m_previewLabel->setWordWrap(true);

  m_resultLabel = new QLabel();
  m_resultLabel->setStyleSheet("QLabel { color: #27ae60; font-size: 10px; margin-top: 2px; }");  // ✅ Smaller

  previewLayout->addWidget(m_previewLabel);
  previewLayout->addWidget(m_resultLabel);

  mainLayout->addWidget(previewGroup);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setMinimumHeight(32);  // ✅ CHANGED from 36
  m_cancelButton->setMinimumWidth(90);   // ✅ CHANGED from 100

  m_okButton = new QPushButton("Move");
  m_okButton->setMinimumHeight(32);
  m_okButton->setMinimumWidth(90);
  m_okButton->setDefault(true);

  buttonLayout->addWidget(m_cancelButton);
  buttonLayout->addWidget(m_okButton);

  mainLayout->addLayout(buttonLayout);

  // Styling
  setStyleSheet(
    "QGroupBox {"
    "  font-weight: bold;"
    "  border: 2px solid #bdc3c7;"
    "  border-radius: 5px;"
    "  margin-top: 10px;"
    "  padding-top: 10px;"
    "}"
    "QGroupBox::title {"
    "  subcontrol-origin: margin;"
    "  left: 10px;"
    "  padding: 0 5px;"
    "}"
    "QDoubleSpinBox {"
    "  padding: 4px;"
    "  border: 1px solid #bdc3c7;"
    "  border-radius: 3px;"
    "}"
    "QPushButton {"
    "  background-color: #3498db;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 4px;"
    "  padding: 6px 12px;"
    "  font-weight: bold;"
    "}"
    "QPushButton:hover {"
    "  background-color: #2980b9;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #21618c;"
    "}"
    "QPushButton#okButton {"
    "  background-color: #27ae60;"
    "}"
    "QPushButton#okButton:hover {"
    "  background-color: #229954;"
    "}"
  );

  m_okButton->setObjectName("okButton");

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
    .arg(resultX, 0, 'f', 3)
    .arg(resultY, 0, 'f', 3)
    .arg(resultZ, 0, 'f', 3)
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
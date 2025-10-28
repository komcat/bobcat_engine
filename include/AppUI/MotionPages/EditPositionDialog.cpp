// EditPositionDialog.cpp
#include "EditPositionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>

EditPositionDialog::EditPositionDialog(const QString& positionName,
  double x, double y, double z,
  double u, double v, double w,
  QWidget* parent)
  : QDialog(parent)
  , m_positionName(positionName)
  , m_x(x), m_y(y), m_z(z)
  , m_u(u), m_v(v), m_w(w)
{
  setupUI();
}

void EditPositionDialog::setupUI()
{
  setWindowTitle("Edit Position");
  setMinimumWidth(400);
  setMaximumWidth(500);

  // Set light background for the entire dialog
  setStyleSheet("QDialog { background-color: white; }");

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // Title
  QLabel* titleLabel = new QLabel("Edit Position");
  titleLabel->setStyleSheet(
    "QLabel {"
    "  font-size: 18px;"
    "  font-weight: bold;"
    "  color: #2c3e50;"
    "  margin-bottom: 5px;"
    "}"
  );
  mainLayout->addWidget(titleLabel);

  // Position name (read-only)
  QLabel* nameLabel = new QLabel(QString("Position: %1").arg(m_positionName));
  nameLabel->setStyleSheet(
    "QLabel {"
    "  font-size: 13px;"
    "  color: #7f8c8d;"
    "  margin-bottom: 10px;"
    "}"
  );
  mainLayout->addWidget(nameLabel);

  // Position Values
  QLabel* valuesLabel = new QLabel("Position Values");
  valuesLabel->setStyleSheet("font-weight: bold; color: #2c3e50; font-size: 13px; margin-top: 5px;");
  mainLayout->addWidget(valuesLabel);

  // Create compact grid for XYZ and UVW
  QGridLayout* gridLayout = new QGridLayout();
  gridLayout->setSpacing(8);
  gridLayout->setContentsMargins(0, 0, 0, 0);

  // Style for labels and spinboxes
  QString labelStyle = "QLabel { color: #34495e; font-weight: bold; font-size: 12px; }";
  QString spinBoxStyle =
    "QDoubleSpinBox {"
    "  padding: 6px;"
    "  border: 2px solid #bdc3c7;"
    "  background-color: white;"
    "  font-size: 12px;"
    "}"
    "QDoubleSpinBox:focus {"
    "  border: 2px solid #3498db;"
    "}"
    "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
    "  background-color: #ecf0f1;"
    "  border: none;"
    "  width: 16px;"
    "}"
    "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {"
    "  background-color: #bdc3c7;"
    "}";

  // Create spinboxes
  m_xSpin = createSpinBox(m_x);
  m_ySpin = createSpinBox(m_y);
  m_zSpin = createSpinBox(m_z);
  m_uSpin = createSpinBox(m_u);
  m_vSpin = createSpinBox(m_v);
  m_wSpin = createSpinBox(m_w);

  // Apply styles
  m_xSpin->setStyleSheet(spinBoxStyle);
  m_ySpin->setStyleSheet(spinBoxStyle);
  m_zSpin->setStyleSheet(spinBoxStyle);
  m_uSpin->setStyleSheet(spinBoxStyle);
  m_vSpin->setStyleSheet(spinBoxStyle);
  m_wSpin->setStyleSheet(spinBoxStyle);

  // Add to grid (2 columns: XYZ | UVW)
  QLabel* xLabel = new QLabel("X:");
  xLabel->setStyleSheet(labelStyle);
  gridLayout->addWidget(xLabel, 0, 0);
  gridLayout->addWidget(m_xSpin, 0, 1);

  QLabel* yLabel = new QLabel("Y:");
  yLabel->setStyleSheet(labelStyle);
  gridLayout->addWidget(yLabel, 1, 0);
  gridLayout->addWidget(m_ySpin, 1, 1);

  QLabel* zLabel = new QLabel("Z:");
  zLabel->setStyleSheet(labelStyle);
  gridLayout->addWidget(zLabel, 2, 0);
  gridLayout->addWidget(m_zSpin, 2, 1);

  QLabel* uLabel = new QLabel("U:");
  uLabel->setStyleSheet(labelStyle);
  gridLayout->addWidget(uLabel, 0, 2);
  gridLayout->addWidget(m_uSpin, 0, 3);

  QLabel* vLabel = new QLabel("V:");
  vLabel->setStyleSheet(labelStyle);
  gridLayout->addWidget(vLabel, 1, 2);
  gridLayout->addWidget(m_vSpin, 1, 3);

  QLabel* wLabel = new QLabel("W:");
  wLabel->setStyleSheet(labelStyle);
  gridLayout->addWidget(wLabel, 2, 2);
  gridLayout->addWidget(m_wSpin, 2, 3);

  gridLayout->setColumnStretch(1, 1);
  gridLayout->setColumnStretch(3, 1);

  mainLayout->addLayout(gridLayout);

  // Spacer
  mainLayout->addStretch();

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(10);

  QPushButton* cancelButton = new QPushButton("Cancel");
  cancelButton->setFixedHeight(36);
  cancelButton->setCursor(Qt::PointingHandCursor);
  cancelButton->setStyleSheet(
    "QPushButton {"
    "  background-color: #95a5a6;"
    "  color: white;"
    "  border: none;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "  padding: 8px 24px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #7f8c8d;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #6c7a89;"
    "}"
  );

  QPushButton* saveButton = new QPushButton("Save");
  saveButton->setFixedHeight(36);
  saveButton->setCursor(Qt::PointingHandCursor);
  saveButton->setStyleSheet(
    "QPushButton {"
    "  background-color: #3498db;"
    "  color: white;"
    "  border: none;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "  padding: 8px 24px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #2980b9;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #21618c;"
    "}"
  );

  buttonLayout->addStretch();
  buttonLayout->addWidget(cancelButton);
  buttonLayout->addWidget(saveButton);

  mainLayout->addLayout(buttonLayout);

  // Connections
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(saveButton, &QPushButton::clicked, this, &EditPositionDialog::onSaveClicked);

  // Set dialog background
  setStyleSheet(
    "QDialog {"
    "  background-color: #ecf0f1;"
    "}"
  );
}

QDoubleSpinBox* EditPositionDialog::createSpinBox(double value)
{
  QDoubleSpinBox* spinBox = new QDoubleSpinBox();
  spinBox->setRange(-10000.0, 10000.0);
  spinBox->setDecimals(6);
  spinBox->setValue(value);
  spinBox->setMinimumWidth(120);
  spinBox->setFixedHeight(32);
  return spinBox;
}

void EditPositionDialog::onSaveClicked()
{
  accept();
}

double EditPositionDialog::getX() const { return m_xSpin->value(); }
double EditPositionDialog::getY() const { return m_ySpin->value(); }
double EditPositionDialog::getZ() const { return m_zSpin->value(); }
double EditPositionDialog::getU() const { return m_uSpin->value(); }
double EditPositionDialog::getV() const { return m_vSpin->value(); }
double EditPositionDialog::getW() const { return m_wSpin->value(); }
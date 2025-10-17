// NewPositionDialog.cpp
#include "NewPositionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>

NewPositionDialog::NewPositionDialog(const QString& deviceName,
  double x, double y, double z,
  double u, double v, double w,
  QWidget* parent)
  : QDialog(parent)
  , m_deviceName(deviceName)
{
  setWindowTitle("Create New Position");
  setMinimumWidth(400);

  setupUI();

  // Set current values
  m_xSpin->setValue(x);
  m_ySpin->setValue(y);
  m_zSpin->setValue(z);
  m_uSpin->setValue(u);
  m_vSpin->setValue(v);
  m_wSpin->setValue(w);
}

void NewPositionDialog::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(15);

  // === Device Info ===
  QLabel* deviceLabel = new QLabel(QString("Device: <b>%1</b>").arg(m_deviceName));
  deviceLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 14px; }");
  mainLayout->addWidget(deviceLabel);

  // === Position Name ===
  QGroupBox* nameGroup = new QGroupBox("Position Name");
  QVBoxLayout* nameLayout = new QVBoxLayout(nameGroup);

  m_nameEdit = new QLineEdit();
  m_nameEdit->setPlaceholderText("Enter position name (e.g., 'approach_lens')");
  m_nameEdit->setMinimumHeight(35);
  nameLayout->addWidget(m_nameEdit);

  mainLayout->addWidget(nameGroup);

  // === Position Values ===
  QGroupBox* valuesGroup = new QGroupBox("Position Values");
  QFormLayout* formLayout = new QFormLayout(valuesGroup);
  formLayout->setSpacing(10);

  // Create spin boxes
  m_xSpin = new QDoubleSpinBox();
  m_ySpin = new QDoubleSpinBox();
  m_zSpin = new QDoubleSpinBox();
  m_uSpin = new QDoubleSpinBox();
  m_vSpin = new QDoubleSpinBox();
  m_wSpin = new QDoubleSpinBox();

  QList<QDoubleSpinBox*> spinBoxes = { m_xSpin, m_ySpin, m_zSpin,
                                      m_uSpin, m_vSpin, m_wSpin };

  for (auto* spin : spinBoxes) {
    spin->setDecimals(6);
    spin->setRange(-1000.0, 1000.0);
    spin->setSingleStep(0.1);
    spin->setMinimumHeight(30);
  }

  formLayout->addRow("X:", m_xSpin);
  formLayout->addRow("Y:", m_ySpin);
  formLayout->addRow("Z:", m_zSpin);
  formLayout->addRow("U:", m_uSpin);
  formLayout->addRow("V:", m_vSpin);
  formLayout->addRow("W:", m_wSpin);

  mainLayout->addWidget(valuesGroup);

  // === Error Label ===
  m_errorLabel = new QLabel();
  m_errorLabel->setStyleSheet("QLabel { color: #ff6b6b; font-weight: bold; }");
  m_errorLabel->setVisible(false);
  mainLayout->addWidget(m_errorLabel);

  // === Buttons ===
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setMinimumSize(100, 35);
  buttonLayout->addWidget(m_cancelButton);

  m_okButton = new QPushButton("Create");
  m_okButton->setMinimumSize(100, 35);
  m_okButton->setDefault(true);
  buttonLayout->addWidget(m_okButton);

  mainLayout->addLayout(buttonLayout);

  // === Styling ===
  setStyleSheet(
    "QDialog {"
    "  background-color: #1a1f2e;"
    "}"
    "QGroupBox {"
    "  background-color: #2d3e5f;"
    "  border: 2px solid #3d5a8c;"
    "  border-radius: 8px;"
    "  margin-top: 10px;"
    "  padding-top: 15px;"
    "  color: #ffffff;"
    "  font-weight: bold;"
    "}"
    "QGroupBox::title {"
    "  subcontrol-origin: margin;"
    "  left: 10px;"
    "  padding: 0 5px;"
    "}"
    "QLineEdit {"
    "  background-color: #1d2e4f;"
    "  color: white;"
    "  border: 2px solid #3d5a8c;"
    "  border-radius: 5px;"
    "  padding: 8px;"
    "  font-size: 13px;"
    "}"
    "QLineEdit:focus {"
    "  border-color: #5a8fd6;"
    "}"
    "QDoubleSpinBox {"
    "  background-color: #1d2e4f;"
    "  color: white;"
    "  border: 2px solid #3d5a8c;"
    "  border-radius: 5px;"
    "  padding: 5px;"
    "  font-size: 13px;"
    "}"
    "QDoubleSpinBox:focus {"
    "  border-color: #5a8fd6;"
    "}"
    "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
    "  background-color: #3d5a8c;"
    "  border: none;"
    "  width: 20px;"
    "}"
    "QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {"
    "  background-color: #4d6a9c;"
    "}"
    "QPushButton {"
    "  background-color: #3d5a8c;"
    "  color: white;"
    "  border: 2px solid #5a8fd6;"
    "  border-radius: 5px;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "  padding: 8px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #4d6a9c;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #2d4a7c;"
    "}"
    "QLabel {"
    "  color: #ffffff;"
    "}"
  );

  // === Connect Signals ===
  connect(m_okButton, &QPushButton::clicked, this, &NewPositionDialog::onOkClicked);
  connect(m_cancelButton, &QPushButton::clicked, this, &NewPositionDialog::onCancelClicked);
  connect(m_nameEdit, &QLineEdit::textChanged, this, &NewPositionDialog::validateInput);
}

void NewPositionDialog::onOkClicked()
{
  // Validate position name
  QString name = m_nameEdit->text().trimmed();

  if (name.isEmpty()) {
    m_errorLabel->setText("Error: Position name cannot be empty");
    m_errorLabel->setVisible(true);
    return;
  }

  // Check for invalid characters
  if (name.contains(' ')) {
    m_errorLabel->setText("Error: Position name cannot contain spaces (use underscore)");
    m_errorLabel->setVisible(true);
    return;
  }

  accept();
}

void NewPositionDialog::onCancelClicked()
{
  reject();
}

void NewPositionDialog::validateInput()
{
  m_errorLabel->setVisible(false);
}

QString NewPositionDialog::getPositionName() const
{
  return m_nameEdit->text().trimmed();
}

double NewPositionDialog::getX() const
{
  return m_xSpin->value();
}

double NewPositionDialog::getY() const
{
  return m_ySpin->value();
}

double NewPositionDialog::getZ() const
{
  return m_zSpin->value();
}

double NewPositionDialog::getU() const
{
  return m_uSpin->value();
}

double NewPositionDialog::getV() const
{
  return m_vSpin->value();
}

double NewPositionDialog::getW() const
{
  return m_wSpin->value();
}
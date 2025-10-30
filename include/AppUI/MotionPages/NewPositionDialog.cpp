// NewPositionDialog.cpp
#include "NewPositionDialog.h"
#include "AppUI/DarkTheme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>

NewPositionDialog::NewPositionDialog(const QString& deviceName,
  double x, double y, double z,
  double u, double v, double w,
  QWidget* parent)
  : QDialog(parent)
  , m_deviceName(deviceName)
  , m_x(x), m_y(y), m_z(z)
  , m_u(u), m_v(v), m_w(w)
{
  // ✅ Apply dark theme directly to this dialog
  this->setStyleSheet(DarkTheme::getStyleSheet());

  setupUI();
}

void NewPositionDialog::setupUI()
{
  setWindowTitle("Create New Position");
  setMinimumWidth(400);
  setMaximumWidth(500);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // Title
  QLabel* titleLabel = new QLabel("Create New Position");
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
  mainLayout->addWidget(titleLabel);

  // Device info
  QLabel* deviceLabel = new QLabel(QString("Device: %1").arg(m_deviceName));
  deviceLabel->setStyleSheet("font-size: 13px;");
  mainLayout->addWidget(deviceLabel);

  // Position Name
  QLabel* nameLabel = new QLabel("Position Name");
  nameLabel->setStyleSheet("font-weight: bold; font-size: 13px;");
  mainLayout->addWidget(nameLabel);

  m_nameEdit = new QLineEdit();
  m_nameEdit->setPlaceholderText("Enter position name...");
  mainLayout->addWidget(m_nameEdit);

  // Position Values
  QLabel* valuesLabel = new QLabel("Position Values");
  valuesLabel->setStyleSheet("font-weight: bold; font-size: 13px; margin-top: 5px;");
  mainLayout->addWidget(valuesLabel);

  // Create compact grid for XYZ and UVW
  QGridLayout* gridLayout = new QGridLayout();
  gridLayout->setSpacing(8);
  gridLayout->setContentsMargins(0, 0, 0, 0);

  // Create spinboxes
  m_xSpin = createSpinBox(m_x);
  m_ySpin = createSpinBox(m_y);
  m_zSpin = createSpinBox(m_z);
  m_uSpin = createSpinBox(m_u);
  m_vSpin = createSpinBox(m_v);
  m_wSpin = createSpinBox(m_w);

  // Add to grid (2 columns: XYZ | UVW)
  QLabel* xLabel = new QLabel("X:");
  xLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
  gridLayout->addWidget(xLabel, 0, 0);
  gridLayout->addWidget(m_xSpin, 0, 1);

  QLabel* yLabel = new QLabel("Y:");
  yLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
  gridLayout->addWidget(yLabel, 1, 0);
  gridLayout->addWidget(m_ySpin, 1, 1);

  QLabel* zLabel = new QLabel("Z:");
  zLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
  gridLayout->addWidget(zLabel, 2, 0);
  gridLayout->addWidget(m_zSpin, 2, 1);

  QLabel* uLabel = new QLabel("U:");
  uLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
  gridLayout->addWidget(uLabel, 0, 2);
  gridLayout->addWidget(m_uSpin, 0, 3);

  QLabel* vLabel = new QLabel("V:");
  vLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
  gridLayout->addWidget(vLabel, 1, 2);
  gridLayout->addWidget(m_vSpin, 1, 3);

  QLabel* wLabel = new QLabel("W:");
  wLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
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

  QPushButton* createButton = new QPushButton("Create");
  createButton->setFixedHeight(36);
  createButton->setCursor(Qt::PointingHandCursor);
  createButton->setDefault(true);

  buttonLayout->addStretch();
  buttonLayout->addWidget(cancelButton);
  buttonLayout->addWidget(createButton);

  mainLayout->addLayout(buttonLayout);

  // Connections
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(createButton, &QPushButton::clicked, this, &NewPositionDialog::onCreateClicked);
}

QDoubleSpinBox* NewPositionDialog::createSpinBox(double value)
{
  QDoubleSpinBox* spinBox = new QDoubleSpinBox();
  spinBox->setRange(-10000.0, 10000.0);
  spinBox->setDecimals(6);
  spinBox->setValue(value);
  spinBox->setMinimumWidth(120);
  spinBox->setFixedHeight(32);  // Compact height
  return spinBox;
}

void NewPositionDialog::onCreateClicked()
{
  if (m_nameEdit->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "Invalid Name",
      "Please enter a position name.");
    return;
  }

  accept();
}

QString NewPositionDialog::getPositionName() const { return m_nameEdit->text().trimmed(); }
double NewPositionDialog::getX() const { return m_xSpin->value(); }
double NewPositionDialog::getY() const { return m_ySpin->value(); }
double NewPositionDialog::getZ() const { return m_zSpin->value(); }
double NewPositionDialog::getU() const { return m_uSpin->value(); }
double NewPositionDialog::getV() const { return m_vSpin->value(); }
double NewPositionDialog::getW() const { return m_wSpin->value(); }
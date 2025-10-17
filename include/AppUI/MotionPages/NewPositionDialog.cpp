// NewPositionDialog.cpp
#include "NewPositionDialog.h"
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
  titleLabel->setStyleSheet(
    "QLabel {"
    "  font-size: 18px;"
    "  font-weight: bold;"
    "  color: #2c3e50;"
    "  margin-bottom: 5px;"
    "}"
  );
  mainLayout->addWidget(titleLabel);

  // Device info
  QLabel* deviceLabel = new QLabel(QString("Device: %1").arg(m_deviceName));
  deviceLabel->setStyleSheet(
    "QLabel {"
    "  font-size: 13px;"
    "  color: #7f8c8d;"
    "  margin-bottom: 10px;"
    "}"
  );
  mainLayout->addWidget(deviceLabel);

  // Position Name
  QLabel* nameLabel = new QLabel("Position Name");
  nameLabel->setStyleSheet("font-weight: bold; color: #2c3e50; font-size: 13px;");
  mainLayout->addWidget(nameLabel);

  m_nameEdit = new QLineEdit();
  m_nameEdit->setPlaceholderText("Enter position name...");
  m_nameEdit->setStyleSheet(
    "QLineEdit {"
    "  padding: 8px;"
    "  border: 2px solid #bdc3c7;"
    "  border-radius: 4px;"
    "  background-color: white;"
    "  font-size: 13px;"
    "}"
    "QLineEdit:focus {"
    "  border: 2px solid #3498db;"
    "}"
  );
  mainLayout->addWidget(m_nameEdit);

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
    "  border-radius: 4px;"
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
  // Left column - XYZ
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

  // Right column - UVW
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

  // Set column stretch to make both columns equal width
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
    "  border-radius: 4px;"
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

  QPushButton* createButton = new QPushButton("Create");
  createButton->setFixedHeight(36);
  createButton->setCursor(Qt::PointingHandCursor);
  createButton->setStyleSheet(
    "QPushButton {"
    "  background-color: #27ae60;"
    "  color: white;"
    "  border: none;"
    "  border-radius: 4px;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "  padding: 8px 24px;"
    "}"
    "QPushButton:hover {"
    "  background-color: #229954;"
    "}"
    "QPushButton:pressed {"
    "  background-color: #1e8449;"
    "}"
  );

  buttonLayout->addStretch();
  buttonLayout->addWidget(cancelButton);
  buttonLayout->addWidget(createButton);

  mainLayout->addLayout(buttonLayout);

  // Connections
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  connect(createButton, &QPushButton::clicked, this, &NewPositionDialog::onCreateClicked);

  // Set dialog background
  setStyleSheet(
    "QDialog {"
    "  background-color: #ecf0f1;"
    "}"
  );
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
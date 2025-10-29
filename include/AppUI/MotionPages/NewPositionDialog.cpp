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

  // ✅ No need to set dialog stylesheet - inherited from MainWindow

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // Title
  QLabel* titleLabel = new QLabel("Create New Position");
  titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");  // ✅ Only custom styling
  mainLayout->addWidget(titleLabel);

  // Device info
  QLabel* deviceLabel = new QLabel(QString("Device: %1").arg(m_deviceName));
  deviceLabel->setStyleSheet("font-size: 13px; color: #a0a0a0;");  // ✅ Only custom color
  mainLayout->addWidget(deviceLabel);

  // Position Name
  QLabel* nameLabel = new QLabel("Position Name");
  nameLabel->setStyleSheet("font-weight: bold; font-size: 13px;");  // ✅ Only custom styling
  mainLayout->addWidget(nameLabel);

  m_nameEdit = new QLineEdit();
  m_nameEdit->setPlaceholderText("Enter position name...");
  // ✅ No stylesheet needed - global theme handles it
  mainLayout->addWidget(m_nameEdit);

  // Position Values
  QLabel* valuesLabel = new QLabel("Position Values");
  valuesLabel->setStyleSheet("font-weight: bold; font-size: 13px; margin-top: 5px;");  // ✅ Only custom styling
  mainLayout->addWidget(valuesLabel);

  // Create compact grid for XYZ and UVW
  QGridLayout* gridLayout = new QGridLayout();
  gridLayout->setSpacing(8);
  gridLayout->setContentsMargins(0, 0, 0, 0);

  // ✅ Only override label font weight if needed
  QString labelStyle = "font-weight: bold; font-size: 12px;";

  // Create spinboxes - ✅ No stylesheet needed, global theme handles it
  m_xSpin = createSpinBox(m_x);
  m_ySpin = createSpinBox(m_y);
  m_zSpin = createSpinBox(m_z);
  m_uSpin = createSpinBox(m_u);
  m_vSpin = createSpinBox(m_v);
  m_wSpin = createSpinBox(m_w);

  // Add to grid (2 columns: XYZ | UVW)
  // Left column - XYZ
  QLabel* xLabel = new QLabel("X:");
  xLabel->setStyleSheet(labelStyle);  // ✅ Only custom font styling
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

  // ✅ Use object name to target buttons for color override
  QPushButton* cancelButton = new QPushButton("Cancel");
  cancelButton->setObjectName("cancelButton");  // ✅ For gray color
  cancelButton->setFixedHeight(36);
  cancelButton->setCursor(Qt::PointingHandCursor);
  cancelButton->setStyleSheet(
    "QPushButton#cancelButton {"
    "  background-color: #555555;"
    "  border-color: #3f3f46;"
    "  font-size: 13px;"
    "  padding: 8px 24px;"
    "}"
    "QPushButton#cancelButton:hover {"
    "  background-color: #666666;"
    "}"
  );

  QPushButton* createButton = new QPushButton("Create");
  createButton->setObjectName("createButton");  // ✅ For green color
  createButton->setFixedHeight(36);
  createButton->setCursor(Qt::PointingHandCursor);
  createButton->setDefault(true);
  createButton->setStyleSheet(
    "QPushButton#createButton {"
    "  background-color: #27ae60;"
    "  border-color: #229954;"
    "  font-size: 13px;"
    "  padding: 8px 24px;"
    "}"
    "QPushButton#createButton:hover {"
    "  background-color: #2ecc71;"
    "}"
  );

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
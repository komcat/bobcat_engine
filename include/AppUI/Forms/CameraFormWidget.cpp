// CameraFormWidget.cpp
#include "CameraFormWidget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>

CameraFormWidget::CameraFormWidget(QWidget* parent)
  : QWidget(parent)
  , m_scrollArea(nullptr)
  , m_formContent(nullptr)
{
  setupUI();
}

void CameraFormWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Title
  QLabel* title = new QLabel("Camera Configuration");
  title->setStyleSheet("QLabel { color: white; font-size: 20px; font-weight: bold; padding: 10px; }");
  mainLayout->addWidget(title);

  // Scroll area
  m_scrollArea = new QScrollArea();
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setStyleSheet("QScrollArea { border: none; background-color: #2b2b2b; }");

  m_formContent = new QWidget();
  m_scrollArea->setWidget(m_formContent);

  mainLayout->addWidget(m_scrollArea);
}

void CameraFormWidget::clear()
{
  m_fields.clear();

  if (m_formContent->layout()) {
    QLayoutItem* item;
    while ((item = m_formContent->layout()->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
    delete m_formContent->layout();
  }
}

void CameraFormWidget::addCameraGroup(const QJsonObject& camera)
{
  QVBoxLayout* formLayout = qobject_cast<QVBoxLayout*>(m_formContent->layout());
  if (!formLayout) return;

  int index = static_cast<int>(m_fields.size());

  QString formStyle =
    "QLineEdit, QSpinBox, QComboBox { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
    "padding: 5px; font-size: 14px; } "
    "QCheckBox { color: white; font-size: 14px; }";

  QGroupBox* groupBox = new QGroupBox(QString("Camera #%1").arg(index + 1));
  groupBox->setStyleSheet(
    "QGroupBox { color: white; font-size: 16px; font-weight: bold; "
    "border: 2px solid #555555; border-radius: 5px; margin-top: 10px; padding-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");

  QVBoxLayout* groupLayout = new QVBoxLayout();

  QFormLayout* cameraForm = new QFormLayout();
  cameraForm->setSpacing(10);
  cameraForm->setLabelAlignment(Qt::AlignRight);

  CameraFields fields;

  // ID
  fields.id = new QLineEdit(camera["id"].toString());
  fields.id->setStyleSheet(formStyle);
  cameraForm->addRow(new QLabel("<span style='color: white;'>ID:</span>"), fields.id);

  // Display Name
  fields.displayName = new QLineEdit(camera["display_name"].toString());
  fields.displayName->setStyleSheet(formStyle);
  cameraForm->addRow(new QLabel("<span style='color: white;'>Display Name:</span>"), fields.displayName);

  // Enabled
  fields.enabled = new QCheckBox("Enabled");
  fields.enabled->setChecked(camera.isEmpty() ? true : camera["enabled"].toBool());
  fields.enabled->setStyleSheet("QCheckBox { color: white; }");
  cameraForm->addRow("", fields.enabled);

  // Camera Type
  fields.cameraType = new QComboBox();
  fields.cameraType->addItems({ "IDS", "Basler", "Generic" });
  fields.cameraType->setCurrentText(camera.isEmpty() ? "IDS" : camera["camera_type"].toString());
  fields.cameraType->setStyleSheet(formStyle);
  cameraForm->addRow(new QLabel("<span style='color: white;'>Camera Type:</span>"), fields.cameraType);

  // Connection Type
  fields.connectionType = new QComboBox();
  fields.connectionType->addItems({ "index", "serial", "ip" });
  fields.connectionType->setCurrentText(camera.isEmpty() ? "index" : camera["connection_type"].toString());
  fields.connectionType->setStyleSheet(formStyle);
  cameraForm->addRow(new QLabel("<span style='color: white;'>Connection Type:</span>"), fields.connectionType);

  // Port
  fields.port = new QSpinBox();
  fields.port->setRange(0, 99);
  fields.port->setValue(camera.isEmpty() ? 0 : camera["port"].toInt());
  fields.port->setStyleSheet(formStyle);
  cameraForm->addRow(new QLabel("<span style='color: white;'>Port/Index:</span>"), fields.port);

  groupLayout->addLayout(cameraForm);

  // Remove button
  QPushButton* removeBtn = new QPushButton("🗑 Remove Camera");
  removeBtn->setStyleSheet(
    "QPushButton { background-color: #cc0000; color: white; border: none; "
    "padding: 8px; font-size: 12px; } "
    "QPushButton:hover { background-color: #ee0000; }");
  connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
    onRemoveCameraClicked(index);
  });
  groupLayout->addWidget(removeBtn);

  groupBox->setLayout(groupLayout);

  m_fields.push_back(fields);

  // Insert before the stretch and add button
  int insertPos = formLayout->count() - 2;  // Before stretch and add button
  formLayout->insertWidget(insertPos, groupBox);
}

void CameraFormWidget::rebuildForm()
{
  // Collect current data
  QJsonDocument currentData = collectToJson();

  // Reload form
  loadFromJson(currentData);
}

void CameraFormWidget::loadFromJson(const QJsonDocument& config)
{
  clear();

  QVBoxLayout* formLayout = new QVBoxLayout(m_formContent);
  formLayout->setSpacing(20);

  QJsonArray cameras = config.object()["cameras"].toArray();

  // Add each camera
  for (int i = 0; i < cameras.size(); ++i) {
    addCameraGroup(cameras[i].toObject());
  }

  // Add Camera button
  QPushButton* addBtn = new QPushButton("➕ Add Camera");
  addBtn->setStyleSheet(
    "QPushButton { background-color: #0066cc; color: white; border: none; "
    "padding: 10px; font-size: 14px; font-weight: bold; } "
    "QPushButton:hover { background-color: #0077ee; }");
  connect(addBtn, &QPushButton::clicked, this, &CameraFormWidget::onAddCameraClicked);
  formLayout->addWidget(addBtn);

  formLayout->addStretch();
}

void CameraFormWidget::onAddCameraClicked()
{
  QJsonObject newCamera;
  newCamera["id"] = QString("camera_%1").arg(m_fields.size() + 1);
  newCamera["display_name"] = QString("Camera %1").arg(m_fields.size() + 1);
  newCamera["enabled"] = true;
  newCamera["camera_type"] = "IDS";
  newCamera["connection_type"] = "index";
  newCamera["port"] = 0;

  addCameraGroup(newCamera);
}

void CameraFormWidget::onRemoveCameraClicked(int index)
{
  if (index < 0 || index >= static_cast<int>(m_fields.size())) return;

  // Must keep at least one camera
  if (m_fields.size() <= 1) {
    return;
  }

  // Remove from fields
  m_fields.erase(m_fields.begin() + index);

  // Rebuild the form to update indices
  rebuildForm();
}

QJsonDocument CameraFormWidget::collectToJson() const
{
  QJsonArray camerasArray;

  for (const auto& fields : m_fields) {
    QJsonObject camera;
    camera["id"] = fields.id->text();
    camera["display_name"] = fields.displayName->text();
    camera["enabled"] = fields.enabled->isChecked();
    camera["camera_type"] = fields.cameraType->currentText();
    camera["connection_type"] = fields.connectionType->currentText();
    camera["port"] = fields.port->value();
    camerasArray.append(camera);
  }

  QJsonObject root;
  root["cameras"] = camerasArray;
  return QJsonDocument(root);
}
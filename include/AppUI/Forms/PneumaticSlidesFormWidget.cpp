// PneumaticSlidesFormWidget.cpp
#include "PneumaticSlidesFormWidget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>

PneumaticSlidesFormWidget::PneumaticSlidesFormWidget(QWidget* parent)
  : QWidget(parent)
  , m_scrollArea(nullptr)
  , m_formContent(nullptr)
{
  // Default device names (fallback if not set)
  m_availableDevices = { "IOTop", "IOBottom" };
  setupUI();
}

void PneumaticSlidesFormWidget::setAvailableDevices(const QStringList& deviceNames)
{
  m_availableDevices = deviceNames;
  std::cout << "[PneumaticSlidesFormWidget] Loaded " << deviceNames.size()
    << " IO devices" << std::endl;
}

void PneumaticSlidesFormWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Title
  QLabel* title = new QLabel("Pneumatic Slides Configuration");
  title->setStyleSheet("QLabel { color: #2c3e50; font-size: 20px; font-weight: bold; padding: 10px; background-color: #ecf0f1; }");
  mainLayout->addWidget(title);

  // Scroll area
  m_scrollArea = new QScrollArea();
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setStyleSheet("QScrollArea { border: none; background-color: #f5f6fa; }");

  m_formContent = new QWidget();
  m_formContent->setStyleSheet("QWidget { background-color: #f5f6fa; }");
  m_scrollArea->setWidget(m_formContent);

  mainLayout->addWidget(m_scrollArea);
}

void PneumaticSlidesFormWidget::clear()
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

QComboBox* PneumaticSlidesFormWidget::createDeviceComboBox()
{
  QComboBox* combo = new QComboBox();
  combo->setEditable(true);  // Allow custom device names
  combo->addItems({ "IOTop", "IOBottom", "IOLeft", "IORight" });
  combo->setStyleSheet(
    "QComboBox { "
    "  background-color: #3a3a3a; "
    "  color: white; "
    "  border: 1px solid #555555; "
    "  padding: 5px; "
    "  padding-right: 25px; "
    "  font-size: 14px; "
    "} "
    "QComboBox::drop-down { "
    "  subcontrol-origin: padding; "
    "  subcontrol-position: top right; "
    "  width: 20px; "
    "  border-left: 1px solid #555555; "
    "  background-color: #4a4a4a; "
    "} "
    "QComboBox::down-arrow { "
    "  width: 0; "
    "  height: 0; "
    "  border-left: 5px solid transparent; "
    "  border-right: 5px solid transparent; "
    "  border-top: 5px solid white; "
    "} "
    "QComboBox:hover { "
    "  border: 1px solid #0066cc; "
    "} "
    "QComboBox::drop-down:hover { "
    "  background-color: #5a5a5a; "
    "}");
  return combo;
}

void PneumaticSlidesFormWidget::addSlideGroup(const QJsonObject& slide)
{
  QVBoxLayout* formLayout = qobject_cast<QVBoxLayout*>(m_formContent->layout());
  if (!formLayout) return;

  int index = static_cast<int>(m_fields.size());

  QString formStyle =
    "QLineEdit, QSpinBox { background-color: white; color: #2c3e50; border: 1px solid #bdc3c7; "
    "padding: 5px; font-size: 14px; }";

  QGroupBox* groupBox = new QGroupBox(QString("Pneumatic Slide #%1").arg(index + 1));
  groupBox->setStyleSheet(
    "QGroupBox { color: #2c3e50; font-size: 16px; font-weight: bold; "
    "background-color: white; "
    "border: 2px solid #bdc3c7; border-radius: 5px; margin-top: 10px; padding-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");

  QVBoxLayout* groupLayout = new QVBoxLayout();

  QFormLayout* slideForm = new QFormLayout();
  slideForm->setSpacing(10);
  slideForm->setLabelAlignment(Qt::AlignRight);

  PneumaticSlideFields fields;

  // ===== NAME =====
  fields.name = new QLineEdit(slide["name"].toString());
  fields.name->setStyleSheet(formStyle);
  slideForm->addRow(new QLabel("<span style='color: #2c3e50; font-weight: bold;'>Name:</span>"), fields.name);

  // ===== OUTPUT =====
  QLabel* outputHeader = new QLabel("<span style='color: #3498db; font-size: 15px; font-weight: bold;'>── Output ──</span>");
  slideForm->addRow(outputHeader);

  fields.outputDevice = createDeviceComboBox();
  if (!slide.isEmpty()) {
    fields.outputDevice->setCurrentText(slide["output"].toObject()["deviceName"].toString());
  }
  slideForm->addRow(new QLabel("<span style='color: #2c3e50;'>Device:</span>"), fields.outputDevice);

  fields.outputPin = new QLineEdit(slide.isEmpty() ? "" : slide["output"].toObject()["pinName"].toString());
  fields.outputPin->setStyleSheet(formStyle);
  slideForm->addRow(new QLabel("<span style='color: #2c3e50;'>Pin:</span>"), fields.outputPin);

  // ===== EXTENDED SENSOR =====
  QLabel* extendedHeader = new QLabel("<span style='color: #27ae60; font-size: 15px; font-weight: bold;'>── Extended Sensor ──</span>");
  slideForm->addRow(extendedHeader);

  fields.extendedDevice = createDeviceComboBox();
  if (!slide.isEmpty()) {
    fields.extendedDevice->setCurrentText(slide["extendedInput"].toObject()["deviceName"].toString());
  }
  slideForm->addRow(new QLabel("<span style='color: #2c3e50;'>Device:</span>"), fields.extendedDevice);

  fields.extendedPin = new QLineEdit(slide.isEmpty() ? "" : slide["extendedInput"].toObject()["pinName"].toString());
  fields.extendedPin->setStyleSheet(formStyle);
  slideForm->addRow(new QLabel("<span style='color: #2c3e50;'>Pin:</span>"), fields.extendedPin);

  // ===== RETRACTED SENSOR =====
  QLabel* retractedHeader = new QLabel("<span style='color: #e67e22; font-size: 15px; font-weight: bold;'>── Retracted Sensor ──</span>");
  slideForm->addRow(retractedHeader);

  fields.retractedDevice = createDeviceComboBox();
  if (!slide.isEmpty()) {
    fields.retractedDevice->setCurrentText(slide["retractedInput"].toObject()["deviceName"].toString());
  }
  slideForm->addRow(new QLabel("<span style='color: #2c3e50;'>Device:</span>"), fields.retractedDevice);

  fields.retractedPin = new QLineEdit(slide.isEmpty() ? "" : slide["retractedInput"].toObject()["pinName"].toString());
  fields.retractedPin->setStyleSheet(formStyle);
  slideForm->addRow(new QLabel("<span style='color: #2c3e50;'>Pin:</span>"), fields.retractedPin);

  // ===== TIMEOUT =====
  QLabel* timeoutHeader = new QLabel("<span style='color: #7f8c8d; font-size: 15px; font-weight: bold;'>── Timeout ──</span>");
  slideForm->addRow(timeoutHeader);

  fields.timeout = new QSpinBox();
  fields.timeout->setRange(100, 60000);
  fields.timeout->setSingleStep(100);
  fields.timeout->setSuffix(" ms");
  fields.timeout->setValue(slide.isEmpty() ? 5000 : slide["timeoutMs"].toInt());
  fields.timeout->setStyleSheet(formStyle);
  slideForm->addRow(new QLabel("<span style='color: #2c3e50;'>Timeout:</span>"), fields.timeout);

  groupLayout->addLayout(slideForm);

  // Remove button
  QPushButton* removeBtn = new QPushButton("🗑 Remove Slide");
  removeBtn->setStyleSheet(
    "QPushButton { background-color: #e74c3c; color: white; border: none; "
    "padding: 8px; font-size: 12px; border-radius: 4px; } "
    "QPushButton:hover { background-color: #c0392b; }");
  connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
    onRemoveSlideClicked(index);
  });
  groupLayout->addWidget(removeBtn);

  groupBox->setLayout(groupLayout);

  m_fields.push_back(fields);

  // Insert before the stretch and add button
  int insertPos = formLayout->count() - 2;  // Before stretch and add button
  formLayout->insertWidget(insertPos, groupBox);
}

void PneumaticSlidesFormWidget::rebuildForm()
{
  // Collect current data
  QJsonDocument currentData = collectToJson();

  // Reload form
  loadFromJson(currentData);
}

void PneumaticSlidesFormWidget::loadFromJson(const QJsonDocument& config)
{
  clear();

  QVBoxLayout* formLayout = new QVBoxLayout(m_formContent);
  formLayout->setSpacing(20);

  QJsonArray slides = config.object()["pneumaticSlides"].toArray();

  // Add each slide
  for (int i = 0; i < slides.size(); ++i) {
    addSlideGroup(slides[i].toObject());
  }

  // Add Slide button
  QPushButton* addBtn = new QPushButton("➕ Add Pneumatic Slide");
  addBtn->setStyleSheet(
    "QPushButton { background-color: #3498db; color: white; border: none; "
    "padding: 10px; font-size: 14px; font-weight: bold; border-radius: 4px; } "
    "QPushButton:hover { background-color: #2980b9; }");
  connect(addBtn, &QPushButton::clicked, this, &PneumaticSlidesFormWidget::onAddSlideClicked);
  formLayout->addWidget(addBtn);

  formLayout->addStretch();
}

void PneumaticSlidesFormWidget::onAddSlideClicked()
{
  QJsonObject newSlide;
  newSlide["name"] = QString("New_Slide_%1").arg(m_fields.size() + 1);

  QJsonObject output;
  output["deviceName"] = "IOBottom";
  output["pinName"] = "";
  newSlide["output"] = output;

  QJsonObject extendedInput;
  extendedInput["deviceName"] = "IOTop";
  extendedInput["pinName"] = "";
  newSlide["extendedInput"] = extendedInput;

  QJsonObject retractedInput;
  retractedInput["deviceName"] = "IOTop";
  retractedInput["pinName"] = "";
  newSlide["retractedInput"] = retractedInput;

  newSlide["timeoutMs"] = 5000;

  addSlideGroup(newSlide);
}

void PneumaticSlidesFormWidget::onRemoveSlideClicked(int index)
{
  if (index < 0 || index >= static_cast<int>(m_fields.size())) return;

  // Must keep at least one slide
  if (m_fields.size() <= 1) {
    return;
  }

  // Remove from fields
  m_fields.erase(m_fields.begin() + index);

  // Rebuild the form to update indices
  rebuildForm();
}

QJsonDocument PneumaticSlidesFormWidget::collectToJson() const
{
  QJsonArray slidesArray;

  for (const auto& fields : m_fields) {
    QJsonObject slide;
    slide["name"] = fields.name->text();

    QJsonObject output;
    output["deviceName"] = fields.outputDevice->currentText();
    output["pinName"] = fields.outputPin->text();
    slide["output"] = output;

    QJsonObject extendedInput;
    extendedInput["deviceName"] = fields.extendedDevice->currentText();
    extendedInput["pinName"] = fields.extendedPin->text();
    slide["extendedInput"] = extendedInput;

    QJsonObject retractedInput;
    retractedInput["deviceName"] = fields.retractedDevice->currentText();
    retractedInput["pinName"] = fields.retractedPin->text();
    slide["retractedInput"] = retractedInput;

    slide["timeoutMs"] = fields.timeout->value();

    slidesArray.append(slide);
  }

  QJsonObject root;
  root["pneumaticSlides"] = slidesArray;
  return QJsonDocument(root);
}
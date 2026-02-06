// ConfigEditorPage.cpp
#include "ConfigEditorPage.h"
#include "TcpClientFormWidget.h"
#include "CameraFormWidget.h"
#include "MotorDeviceFormWidget.h"
#include "SavedPositionsFormWidget.h"
#include "PneumaticSlidesFormWidget.h"
#include "IODevicesFormWidget.h"
#include "PowerSupplyFormWidget.h"
#include "TECDevicesFormWidget.h"
#include "ApplicationContext.h"
#include "LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>

ConfigEditorPage::ConfigEditorPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_tcpClientForm(nullptr)
  , m_cameraForm(nullptr)
  , m_motorDeviceForm(nullptr)
  , m_savedPositionsForm(nullptr)
  , m_pneumaticSlidesForm(nullptr)
  , m_ioDevicesForm(nullptr)
  , m_powerSupplyForm(nullptr)
  , m_tecDevicesForm(nullptr)
  , m_emptyForm(nullptr)
{
  setupUI();
  loadConfigList();
}

void ConfigEditorPage::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Top bar with back button and save button
  QWidget* topBar = new QWidget();
  topBar->setStyleSheet("QWidget { background-color: #333333; }");
  topBar->setFixedHeight(60);

  QHBoxLayout* topBarLayout = new QHBoxLayout(topBar);
  topBarLayout->setContentsMargins(10, 10, 10, 10);

  m_backButton = new QPushButton("← Back to Main Menu");
  m_backButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #656565, stop:1 #555555); "
    "  color: white; "
    "  padding: 10px 20px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #757575, stop:1 #656565); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #454545; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 12px; "
    "  padding-left: 22px; "
    "}");
  connect(m_backButton, &QPushButton::clicked, this, &ConfigEditorPage::onBackClicked);

  topBarLayout->addWidget(m_backButton);
  topBarLayout->addStretch();

  // Reload button
  m_reloadButton = new QPushButton("↻ Reload");
  m_reloadButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #656565, stop:1 #555555); "
    "  color: white; "
    "  padding: 10px 20px; "
    "  font-size: 14px; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #757575, stop:1 #656565); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #454545; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 12px; "
    "  padding-left: 22px; "
    "}");
  connect(m_reloadButton, &QPushButton::clicked, this, &ConfigEditorPage::onReloadClicked);
  topBarLayout->addWidget(m_reloadButton);

  // Save button
  m_saveButton = new QPushButton("💾 Save");
  m_saveButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0077ee, stop:1 #0066cc); "
    "  color: white; "
    "  padding: 10px 20px; "
    "  font-size: 14px; "
    "  font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0088ff, stop:1 #0077ee); "
    "  border-top: 2px solid rgba(255, 255, 255, 0.6); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.6); "
    "} "
    "QPushButton:pressed { "
    "  background-color: #0055aa; "
    "  border-top: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-left: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-bottom: 2px solid rgba(255, 255, 255, 0.3); "
    "  border-right: 2px solid rgba(255, 255, 255, 0.3); "
    "  padding-top: 12px; "
    "  padding-left: 22px; "
    "}");
  connect(m_saveButton, &QPushButton::clicked, this, &ConfigEditorPage::onSaveClicked);
  topBarLayout->addWidget(m_saveButton);

  mainLayout->addWidget(topBar);

  // Content area with splitter (20% left, 80% right)
  QSplitter* splitter = new QSplitter(Qt::Horizontal);
  splitter->setStyleSheet("QSplitter::handle { background-color: #555555; }");

  // LEFT PANEL: Config file list (20%)
  QWidget* leftPanel = new QWidget();
  leftPanel->setStyleSheet("QWidget { background-color: #2b2b2b; }");
  QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(10, 10, 10, 10);

  QLabel* listTitle = new QLabel("Config Files");
  listTitle->setStyleSheet("QLabel { color: white; font-size: 16px; font-weight: bold; }");
  leftLayout->addWidget(listTitle);

  m_configList = new QListWidget();
  m_configList->setStyleSheet(
    "QListWidget { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
    "font-size: 14px; } "
    "QListWidget::item:selected { background-color: #0066cc; }");
  connect(m_configList, &QListWidget::itemClicked, this, &ConfigEditorPage::onConfigSelected);
  leftLayout->addWidget(m_configList);

  // RIGHT PANEL: Form stack (80%)
  QWidget* rightPanel = new QWidget();
  rightPanel->setStyleSheet("QWidget { background-color: #2b2b2b; }");
  QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(10, 10, 10, 10);

  m_formStack = new QStackedWidget();

  // Create forms
  createEmptyForm();

  m_tcpClientForm = new TcpClientFormWidget();
  m_formStack->addWidget(m_tcpClientForm);

  m_cameraForm = new CameraFormWidget();
  m_formStack->addWidget(m_cameraForm);

  m_motorDeviceForm = new MotorDeviceFormWidget();
  m_formStack->addWidget(m_motorDeviceForm);

  m_savedPositionsForm = new SavedPositionsFormWidget();
  m_formStack->addWidget(m_savedPositionsForm);

  m_pneumaticSlidesForm = new PneumaticSlidesFormWidget();
  m_formStack->addWidget(m_pneumaticSlidesForm);

  m_ioDevicesForm = new IODevicesFormWidget();
  m_formStack->addWidget(m_ioDevicesForm);

  m_powerSupplyForm = new PowerSupplyFormWidget();
  m_formStack->addWidget(m_powerSupplyForm);

  m_tecDevicesForm = new TECDevicesFormWidget();
  m_formStack->addWidget(m_tecDevicesForm);


  rightLayout->addWidget(m_formStack);

  // Add panels to splitter
  splitter->addWidget(leftPanel);
  splitter->addWidget(rightPanel);
  splitter->setStretchFactor(0, 20);  // 20% for left
  splitter->setStretchFactor(1, 80);  // 80% for right

  mainLayout->addWidget(splitter);
}

void ConfigEditorPage::createEmptyForm()
{
  m_emptyForm = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(m_emptyForm);
  layout->setAlignment(Qt::AlignCenter);

  QLabel* label = new QLabel("Select a config file to edit");
  label->setStyleSheet("QLabel { color: #888888; font-size: 18px; }");
  label->setAlignment(Qt::AlignCenter);
  layout->addWidget(label);

  m_formStack->addWidget(m_emptyForm);
  m_formStack->setCurrentWidget(m_emptyForm);
}

void ConfigEditorPage::loadConfigList()
{
  m_configList->clear();

  QDir configDir("config");
  if (!configDir.exists()) {
    m_context->logManager->Warning("Config directory does not exist", "ConfigEditor");
    return;
  }

  QStringList filters;
  filters << "*.local.json";  // Only show .local.json files
  QFileInfoList files = configDir.entryInfoList(filters, QDir::Files);

  for (const QFileInfo& fileInfo : files) {
    m_configList->addItem(fileInfo.fileName());
  }

  m_context->logManager->Info("Loaded config file list", "ConfigEditor");
}

void ConfigEditorPage::onConfigSelected(QListWidgetItem* item)
{
  if (!item) return;

  QString filename = item->text();
  m_currentConfigFile = filename;

  loadConfigFile(filename);
}

void ConfigEditorPage::loadConfigFile(const QString& filename)
{
  QString filepath = "config/" + filename;
  QFile file(filepath);

  if (!file.open(QIODevice::ReadOnly)) {
    m_context->logManager->Error("Failed to open config file: " + filename.toStdString(), "ConfigEditor");
    QMessageBox::warning(this, "Error", "Failed to open file: " + filename);
    return;
  }

  QByteArray fileData = file.readAll();
  file.close();

  QJsonParseError parseError;
  m_currentConfig = QJsonDocument::fromJson(fileData, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    m_context->logManager->Error("Failed to parse JSON: " + parseError.errorString().toStdString(), "ConfigEditor");
    QMessageBox::warning(this, "Error", "Invalid JSON: " + parseError.errorString());
    return;
  }

  // Determine config type and show appropriate form
  if (filename.contains("tcp_client")) {
    m_tcpClientForm->loadFromJson(m_currentConfig);
    m_formStack->setCurrentWidget(m_tcpClientForm);
  }
  else if (filename.contains("camera")) {
    m_cameraForm->loadFromJson(m_currentConfig);
    m_formStack->setCurrentWidget(m_cameraForm);
  }
  else if (filename.contains("motor_device")) {
    m_motorDeviceForm->loadFromJson(m_currentConfig);
    m_formStack->setCurrentWidget(m_motorDeviceForm);
  }
  else if (filename.contains("saved_position")) {
    m_savedPositionsForm->loadFromJson(m_currentConfig);
    m_formStack->setCurrentWidget(m_savedPositionsForm);
  }
  else if (filename.contains("pneumatic_slides")) {
    m_pneumaticSlidesForm->loadFromJson(m_currentConfig);
    m_formStack->setCurrentWidget(m_pneumaticSlidesForm);
  }
  else if (filename.contains("io_devices")) {
    m_ioDevicesForm->loadFromJson(m_currentConfig);
    m_formStack->setCurrentWidget(m_ioDevicesForm);
  }
  else if (filename.contains("power_supply")) {
    m_powerSupplyForm->loadFromJson(m_currentConfig);
    m_formStack->setCurrentWidget(m_powerSupplyForm);
  }
  else if (filename.contains("tec_devices")) {
    m_tecDevicesForm->loadFromJson(m_currentConfig);
    m_formStack->setCurrentWidget(m_tecDevicesForm);
  }
  else {
    m_formStack->setCurrentWidget(m_emptyForm);
  }


  m_context->logManager->Info("Loaded config: " + filename.toStdString(), "ConfigEditor");
}

void ConfigEditorPage::onSaveClicked()
{
  if (m_currentConfigFile.isEmpty()) {
    QMessageBox::information(this, "Info", "No config file selected");
    return;
  }

  saveConfigFile(m_currentConfigFile);
}

void ConfigEditorPage::saveConfigFile(const QString& filename)
{
  // Collect data from form
  QJsonDocument docToSave;

  if (filename.contains("tcp_client")) {
    docToSave = m_tcpClientForm->collectToJson();
  }
  else if (filename.contains("camera")) {
    docToSave = m_cameraForm->collectToJson();
  }
  else if (filename.contains("motor_device")) {
    docToSave = m_motorDeviceForm->collectToJson();
  }
  else if (filename.contains("saved_position")) {
    docToSave = m_savedPositionsForm->collectToJson();
  }
  else if (filename.contains("pneumatic_slides")) {
    docToSave = m_pneumaticSlidesForm->collectToJson();
  }
  else if (filename.contains("io_devices")) {
    docToSave = m_ioDevicesForm->collectToJson();
  }
  else if (filename.contains("power_supply")) {
    docToSave = m_powerSupplyForm->collectToJson();
  }
  else if (filename.contains("tec_devices")) {
    docToSave = m_tecDevicesForm->collectToJson();
  }
  else {
    QMessageBox::warning(this, "Error", "Unknown config type");
    return;
  }

  // Write to file
  QString filepath = "config/" + filename;
  QFile file(filepath);

  if (!file.open(QIODevice::WriteOnly)) {
    m_context->logManager->Error("Failed to save config file: " + filename.toStdString(), "ConfigEditor");
    QMessageBox::critical(this, "Error", "Failed to save file: " + filename);
    return;
  }

  file.write(docToSave.toJson(QJsonDocument::Indented));
  file.close();

  m_context->logManager->Info("Saved config: " + filename.toStdString(), "ConfigEditor");
  QMessageBox::information(this, "Success", "Configuration saved successfully!");
}

void ConfigEditorPage::onReloadClicked()
{
  if (m_currentConfigFile.isEmpty()) {
    QMessageBox::information(this, "Info", "No config file selected");
    return;
  }

  loadConfigFile(m_currentConfigFile);
  m_context->logManager->Info("Reloaded: " + m_currentConfigFile.toStdString(), "ConfigEditor");
}

void ConfigEditorPage::onBackClicked()
{
  emit backToMainMenu();
}
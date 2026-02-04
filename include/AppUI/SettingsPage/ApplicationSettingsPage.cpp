// ApplicationSettingsPage.cpp
#include "ApplicationSettingsPage.h"
#include "ApplicationContext.h"
#include "settings/ApplicationSettings.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QFont>
#include <iostream>

// Section descriptions for the UI
const std::map<std::string, std::string> ApplicationSettingsPage::s_sectionDescriptions = {
  {"ChainExecution", "Settings for chain execution (auto-running dependent recipes)"},
  {"Scanning", "Settings for scanning operations"},
  {"UI", "User interface settings"}
};

// Key descriptions for tooltips
const std::map<std::string, std::map<std::string, std::string>> ApplicationSettingsPage::s_keyDescriptions = {
  {"ChainExecution", {
    {"AutoContinueSeconds", "Seconds to wait before auto-continuing to next recipe (1-60)"}
  }},
  {"Scanning", {
    {"DefaultScanTimeoutMs", "Default timeout for scan operations in milliseconds"}
  }},
  {"UI", {
    {"MaxLogLines", "Maximum number of lines to keep in execution log (100-1000)"}
  }}
};

ApplicationSettingsPage::ApplicationSettingsPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_modified(false)
{
  setupUI();
  loadSettings();
}

void ApplicationSettingsPage::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // === Top Bar ===
  QHBoxLayout* topBar = new QHBoxLayout();

  m_backButton = new QPushButton("< Back");
  m_backButton->setFixedWidth(100);
  m_backButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3f3f46; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 8px 16px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #52525b; "
    "}"
  );
  connect(m_backButton, &QPushButton::clicked, this, &ApplicationSettingsPage::onBackClicked);

  m_titleLabel = new QLabel("Application Settings");
  QFont titleFont = m_titleLabel->font();
  titleFont.setPointSize(18);
  titleFont.setBold(true);
  m_titleLabel->setFont(titleFont);
  m_titleLabel->setStyleSheet("color: white;");

  m_statusLabel = new QLabel("");
  m_statusLabel->setStyleSheet("color: #f59e0b; font-weight: bold;");

  topBar->addWidget(m_backButton);
  topBar->addSpacing(20);
  topBar->addWidget(m_titleLabel);
  topBar->addStretch();
  topBar->addWidget(m_statusLabel);

  mainLayout->addLayout(topBar);

  // === File Path Info ===
  m_filePathLabel = new QLabel("Config file: config/system/applicationSettings.ini");
  m_filePathLabel->setStyleSheet("color: #888888; font-size: 11px;");
  mainLayout->addWidget(m_filePathLabel);

  // === Settings Scroll Area ===
  m_scrollArea = new QScrollArea();
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setStyleSheet(
    "QScrollArea { "
    "  background-color: #27272a; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 8px; "
    "}"
  );

  m_settingsContainer = new QWidget();
  m_settingsContainer->setStyleSheet("background-color: #27272a;");
  m_scrollArea->setWidget(m_settingsContainer);

  mainLayout->addWidget(m_scrollArea, 1);

  // === Button Bar ===
  QHBoxLayout* buttonBar = new QHBoxLayout();

  m_resetButton = new QPushButton("Reset to Defaults");
  m_resetButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #7f1d1d; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 10px 20px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #991b1b; "
    "}"
  );
  connect(m_resetButton, &QPushButton::clicked, this, &ApplicationSettingsPage::onResetToDefaults);

  m_reloadButton = new QPushButton("Reload");
  m_reloadButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #3f3f46; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 10px 20px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #52525b; "
    "}"
  );
  connect(m_reloadButton, &QPushButton::clicked, this, &ApplicationSettingsPage::onReloadClicked);

  m_saveButton = new QPushButton("Save Changes");
  m_saveButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #166534; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 10px 20px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #15803d; "
    "} "
    "QPushButton:disabled { "
    "  background-color: #3f3f46; "
    "  color: #71717a; "
    "}"
  );
  m_saveButton->setEnabled(false);
  connect(m_saveButton, &QPushButton::clicked, this, &ApplicationSettingsPage::onSaveClicked);

  buttonBar->addWidget(m_resetButton);
  buttonBar->addStretch();
  buttonBar->addWidget(m_reloadButton);
  buttonBar->addWidget(m_saveButton);

  mainLayout->addLayout(buttonBar);
}

void ApplicationSettingsPage::loadSettings()
{
  createSettingsForm();
  m_modified = false;
  updateModifiedIndicator();
}

void ApplicationSettingsPage::createSettingsForm()
{
  // Clear existing widgets
  if (m_settingsContainer->layout()) {
    QLayoutItem* item;
    while ((item = m_settingsContainer->layout()->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
    delete m_settingsContainer->layout();
  }
  m_settingEdits.clear();

  QVBoxLayout* containerLayout = new QVBoxLayout(m_settingsContainer);
  containerLayout->setContentsMargins(20, 20, 20, 20);
  containerLayout->setSpacing(20);

  // Get settings from context
  ApplicationSettings* settings = m_context ? m_context->applicationSettings : nullptr;

  // Define sections and their keys with default values
  struct SettingDef {
    std::string key;
    std::string defaultValue;
    std::string description;
  };

  std::map<std::string, std::vector<SettingDef>> sections = {
    {"ChainExecution", {
      {"AutoContinueSeconds", "3", "Seconds before auto-continuing to next recipe in chain execution"}
    }},
    {"Scanning", {
      {"DefaultScanTimeoutMs", "60000", "Default timeout for scan operations (milliseconds)"}
    }},
    {"UI", {
      {"MaxLogLines", "200", "Maximum lines to keep in execution log"}
    }}
  };

  // Create UI for each section
  for (const auto& [sectionName, keys] : sections) {
    // Section group box
    QGroupBox* sectionGroup = new QGroupBox(QString::fromStdString(sectionName));
    sectionGroup->setStyleSheet(
      "QGroupBox { "
      "  font-weight: bold; "
      "  font-size: 14px; "
      "  color: #60a5fa; "
      "  border: 1px solid #3f3f46; "
      "  border-radius: 6px; "
      "  margin-top: 10px; "
      "  padding-top: 10px; "
      "} "
      "QGroupBox::title { "
      "  subcontrol-origin: margin; "
      "  left: 10px; "
      "  padding: 0 5px; "
      "}"
    );

    QFormLayout* formLayout = new QFormLayout(sectionGroup);
    formLayout->setContentsMargins(15, 20, 15, 15);
    formLayout->setSpacing(12);
    formLayout->setLabelAlignment(Qt::AlignRight);

    // Section description
    auto descIt = s_sectionDescriptions.find(sectionName);
    if (descIt != s_sectionDescriptions.end()) {
      QLabel* descLabel = new QLabel(QString::fromStdString(descIt->second));
      descLabel->setStyleSheet("color: #a1a1aa; font-size: 11px; font-weight: normal;");
      descLabel->setWordWrap(true);
      formLayout->addRow(descLabel);
    }

    // Create fields for each key
    for (const auto& settingDef : keys) {
      QLabel* keyLabel = new QLabel(QString::fromStdString(settingDef.key) + ":");
      keyLabel->setStyleSheet("color: white; font-weight: normal;");

      QLineEdit* valueEdit = new QLineEdit();
      valueEdit->setStyleSheet(
        "QLineEdit { "
        "  background-color: #3f3f46; "
        "  color: white; "
        "  border: 1px solid #52525b; "
        "  border-radius: 4px; "
        "  padding: 6px 10px; "
        "} "
        "QLineEdit:focus { "
        "  border-color: #3b82f6; "
        "}"
      );
      valueEdit->setMinimumWidth(200);
      valueEdit->setMaximumWidth(300);

      // Get current value from settings
      std::string currentValue = settingDef.defaultValue;
      if (settings) {
        currentValue = settings->GetString(sectionName, settingDef.key, settingDef.defaultValue);
      }
      valueEdit->setText(QString::fromStdString(currentValue));

      // Set tooltip
      valueEdit->setToolTip(QString::fromStdString(settingDef.description));

      // Connect change signal
      connect(valueEdit, &QLineEdit::textChanged, this, &ApplicationSettingsPage::onSettingChanged);

      // Add description label below the edit
      QWidget* fieldWidget = new QWidget();
      QVBoxLayout* fieldLayout = new QVBoxLayout(fieldWidget);
      fieldLayout->setContentsMargins(0, 0, 0, 0);
      fieldLayout->setSpacing(2);
      fieldLayout->addWidget(valueEdit);

      QLabel* fieldDescLabel = new QLabel(QString::fromStdString(settingDef.description));
      fieldDescLabel->setStyleSheet("color: #71717a; font-size: 10px; font-weight: normal;");
      fieldDescLabel->setWordWrap(true);
      fieldLayout->addWidget(fieldDescLabel);

      formLayout->addRow(keyLabel, fieldWidget);

      // Store reference
      m_settingEdits[sectionName][settingDef.key] = valueEdit;
    }

    containerLayout->addWidget(sectionGroup);
  }

  containerLayout->addStretch();

  // Update file path label
  if (settings && settings->IsLoaded()) {
    m_filePathLabel->setText(QString("Loaded from: %1").arg(QString::fromStdString(settings->GetLoadedFilePath())));
  }
}

void ApplicationSettingsPage::saveSettings()
{
  // Save to local config file
  QString localPath = "config/system/applicationSettings.local.ini";

  QFile file(localPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::critical(this, "Save Error",
      QString("Failed to save settings to:\n%1").arg(localPath));
    return;
  }

  QTextStream out(&file);
  out << "; Application Settings (Local Override)\n";
  out << "; This file overrides applicationSettings.ini\n";
  out << "; Delete this file to restore defaults\n\n";

  // Write each section
  for (const auto& [sectionName, keys] : m_settingEdits) {
    out << "[" << QString::fromStdString(sectionName) << "]\n";

    for (const auto& [keyName, edit] : keys) {
      out << QString::fromStdString(keyName) << "=" << edit->text() << "\n";
    }
    out << "\n";
  }

  file.close();

  // Reload settings in ApplicationSettings
  if (m_context && m_context->applicationSettings) {
    m_context->applicationSettings->LoadFromFile(localPath.toStdString());
  }

  m_modified = false;
  updateModifiedIndicator();

  m_filePathLabel->setText(QString("Saved to: %1").arg(localPath));

  std::cout << "[ApplicationSettingsPage] Settings saved to: " << localPath.toStdString() << std::endl;
}

void ApplicationSettingsPage::onBackClicked()
{
  if (m_modified) {
    QMessageBox::StandardButton reply = QMessageBox::question(
      this, "Unsaved Changes",
      "You have unsaved changes. Do you want to save before leaving?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
    );

    if (reply == QMessageBox::Save) {
      saveSettings();
    } else if (reply == QMessageBox::Cancel) {
      return;
    }
  }

  emit backToMainMenu();
}

void ApplicationSettingsPage::onSaveClicked()
{
  saveSettings();
  QMessageBox::information(this, "Settings Saved",
    "Settings have been saved.\nChanges will take effect immediately.");
}

void ApplicationSettingsPage::onReloadClicked()
{
  if (m_modified) {
    QMessageBox::StandardButton reply = QMessageBox::question(
      this, "Unsaved Changes",
      "Reloading will discard unsaved changes. Continue?",
      QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::No) {
      return;
    }
  }

  // Reload settings from file
  if (m_context && m_context->applicationSettings) {
    QString localPath = "config/system/applicationSettings.local.ini";
    QString defaultPath = "config/system/applicationSettings.ini";

    if (QFile::exists(localPath)) {
      m_context->applicationSettings->LoadFromFile(localPath.toStdString());
    } else {
      m_context->applicationSettings->LoadFromFile(defaultPath.toStdString());
    }
  }

  loadSettings();
}

void ApplicationSettingsPage::onResetToDefaults()
{
  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Reset to Defaults",
    "This will delete the local settings file and restore all settings to their default values.\n\nContinue?",
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply == QMessageBox::No) {
    return;
  }

  // Delete local config file
  QString localPath = "config/system/applicationSettings.local.ini";
  if (QFile::exists(localPath)) {
    QFile::remove(localPath);
    std::cout << "[ApplicationSettingsPage] Deleted local config: " << localPath.toStdString() << std::endl;
  }

  // Reload from default
  if (m_context && m_context->applicationSettings) {
    QString defaultPath = "config/system/applicationSettings.ini";
    m_context->applicationSettings->LoadFromFile(defaultPath.toStdString());
  }

  loadSettings();

  QMessageBox::information(this, "Defaults Restored",
    "Settings have been reset to default values.");
}

void ApplicationSettingsPage::onSettingChanged()
{
  m_modified = true;
  updateModifiedIndicator();
}

void ApplicationSettingsPage::updateModifiedIndicator()
{
  m_saveButton->setEnabled(m_modified);

  if (m_modified) {
    m_statusLabel->setText("* Unsaved changes");
  } else {
    m_statusLabel->setText("");
  }
}

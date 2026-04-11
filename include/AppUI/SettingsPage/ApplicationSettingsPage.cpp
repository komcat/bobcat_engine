// ApplicationSettingsPage.cpp
#include "ApplicationSettingsPage.h"
#include "ConfigBackupManager.h"
#include "ApplicationContext.h"
#include "settings/ApplicationSettings.h"
#include "camera/CameraManager.h"
#include "data/DataStore.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QFont>
#include <QHeaderView>
#include <QFrame>
#include <QSettings>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <iostream>

// QSettings keys (same organization as GdiCameraWindowManager)
static const QString QSETTINGS_ORG = "Fabrinet";
static const QString QSETTINGS_APP = "BobcatEngine";

ApplicationSettingsPage::ApplicationSettingsPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_backupManager(new ConfigBackupManager(context))
  , m_modified(false)
{
  initializeSettingDefinitions();
  setupUI();
  loadSettings();
}

ApplicationSettingsPage::~ApplicationSettingsPage()
{
  delete m_backupManager;
}

void ApplicationSettingsPage::initializeSettingDefinitions()
{
  // Initialize section descriptions
  s_sectionDescriptions = {
    {"ChainExecution", "Settings for chain execution (auto-running dependent recipes)"},
    {"Scanning", "Settings for scanning operations"},
    {"UI", "User interface settings"},
    {"CameraMonitor", "GDI Camera monitor window settings"},
    {"ChartOverlay", "Chart overlay display settings"},
    {"Vision", "Vision processing settings"},
    {"Tools", "Tool overlay settings"},
    {"BeamServer", "Beam analyser server connection settings (command + stream ports)"},
    {"Logging", "Log verbosity settings. Disable debug logs for cleaner operator output."},
    {"Backup", "Configuration backup and restore settings"}
  };

  // Get dynamic options
  auto cameras = getAvailableCameras();
  auto channels = getAvailableChannels();
  auto profiles = getAvailableVisionProfiles();

  // Initialize setting definitions
  m_settingDefs = {
    // === Chain Execution ===
    {"ChainExecution", "AutoContinueSeconds", "3",
     "Seconds to wait before auto-continuing to next recipe in chain execution",
     "int", 1, 60, {}},

    // === Scanning ===
    {"Scanning", "DefaultScanTimeoutMs", "60000",
     "Default timeout for scan operations (milliseconds)",
     "int", 1000, 600000, {}},

    // === UI ===
    {"UI", "MaxLogLines", "200",
     "Maximum number of lines to keep in execution log",
     "int", 100, 10000, {}},

    // === Camera Monitor ===
    {"CameraMonitor", "ViewMode", "single",
     "Camera view mode: single camera or dual (split) view",
     "combo", 0, 0, {{"single", "Single View"}, {"dual", "Dual View (Split)"}}},

    {"CameraMonitor", "PrimaryCamera", "",
     "Primary camera to display (single view or left side in dual view)",
     "combo", 0, 0, cameras},

    {"CameraMonitor", "SecondaryCamera", "",
     "Secondary camera to display (right side in dual view)",
     "combo", 0, 0, cameras},

    // === Chart Overlay ===
    {"ChartOverlay", "Enabled", "false",
     "Enable chart overlay on camera view",
     "bool", 0, 0, {}},

    {"ChartOverlay", "LastChannel", "",
     "Last used data channel for chart overlay",
     "combo", 0, 0, channels},

    // === Vision ===
    {"Vision", "Enabled", "false",
     "Enable vision processing on camera view",
     "bool", 0, 0, {}},

    {"Vision", "LastProfile", "",
     "Last used vision profile",
     "combo", 0, 0, profiles},

    // === Tools ===
    {"Tools", "CrosshairEnabled", "false",
     "Show crosshair overlay on camera view",
     "bool", 0, 0, {}},

    {"Tools", "ClickToMoveEnabled", "false",
     "Enable click-to-move functionality on camera view",
     "bool", 0, 0, {}},

    // === Beam Server ===
    {"BeamServer", "Host", "127.0.0.1",
     "IP address of the beam analyser server",
     "string", 0, 0, {}},

    {"BeamServer", "CommandPort", "5555",
     "TCP port for beam commands (DETECT, FIT, RESET, SAVE)",
     "int", 1, 65535, {}},

    {"BeamServer", "StreamPort", "5567",
     "TCP port for beam streaming (STREAM_START, STREAM_FRAME, STREAM_STOP)",
     "int", 1, 65535, {}},

    {"BeamServer", "UmPerPxX", "0.25",
     "Pixel scale X in micrometers per pixel",
     "string", 0, 0, {}},

    {"BeamServer", "UmPerPxY", "0.25",
     "Pixel scale Y in micrometers per pixel",
     "string", 0, 0, {}},

    {"BeamServer", "Rotate180", "false",
     "Rotate image 180 degrees before beam detection",
     "bool", 0, 0, {}},

    {"BeamServer", "StreamIntervalMs", "100",
     "Target interval between stream frames in milliseconds (100 = 10Hz)",
     "int", 50, 1000, {}},

    // === Logging ===
    {"Logging", "DebugEnabled", "false",
     "Show verbose DEBUG-level log messages. When disabled (default), the log shows only "
     "operator-meaningful events: recipe steps, hardware state changes, and errors. When enabled, "
     "shows all developer debug traces (internal state, parameter echoes, wait cycles, handler "
     "registration). Requires application restart to take effect.",
     "bool", 0, 0, {}},

    // === Backup ===
    {"Backup", "DestinationPath", "",
     "Folder path where config backups are stored. Leave empty to use default (config_backups/ next to executable).",
     "string", 0, 0, {}}
  };
}

std::vector<std::pair<std::string, std::string>> ApplicationSettingsPage::getAvailableCameras()
{
  std::vector<std::pair<std::string, std::string>> cameras;
  cameras.push_back({"", "(Auto - First Available)"});

  if (m_context && m_context->cameraManager) {
    auto ids = m_context->cameraManager->GetCameraIds();
    for (const auto& id : ids) {
      cameras.push_back({id, id});
    }
  }

  return cameras;
}

std::vector<std::pair<std::string, std::string>> ApplicationSettingsPage::getAvailableChannels()
{
  std::vector<std::pair<std::string, std::string>> channels;
  channels.push_back({"", "(None)"});

  if (m_context && m_context->dataStore) {
    auto names = m_context->dataStore->GetAllChannelNames();
    for (const auto& name : names) {
      channels.push_back({name, name});
    }
  }

  // Add some common channel names if DataStore is empty
  if (channels.size() == 1) {
    channels.push_back({"SMU1_Current", "SMU1_Current"});
    channels.push_back({"SMU2_Current", "SMU2_Current"});
    channels.push_back({"Temperature", "Temperature"});
  }

  return channels;
}

std::vector<std::pair<std::string, std::string>> ApplicationSettingsPage::getAvailableVisionProfiles()
{
  std::vector<std::pair<std::string, std::string>> profiles;
  profiles.push_back({"", "(None)"});

  // Scan vision profiles directory
  QDir profileDir("config/vision_profiles");
  if (profileDir.exists()) {
    QStringList filters;
    filters << "*.json";
    QFileInfoList files = profileDir.entryInfoList(filters, QDir::Files);
    for (const auto& fileInfo : files) {
      std::string path = fileInfo.filePath().toStdString();
      std::string name = fileInfo.baseName().toStdString();
      profiles.push_back({path, name});
    }
  }

  return profiles;
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
  m_filePathLabel = new QLabel("Settings stored in: Windows Registry (HKCU\\Software\\Fabrinet\\BobcatEngine)");
  m_filePathLabel->setStyleSheet("color: #888888; font-size: 11px;");
  mainLayout->addWidget(m_filePathLabel);

  // === Main Content: Splitter with Tree and Property Panel ===
  m_splitter = new QSplitter(Qt::Horizontal);
  m_splitter->setStyleSheet(
    "QSplitter::handle { "
    "  background-color: #3f3f46; "
    "  width: 2px; "
    "}"
  );

  setupTreeView();
  setupPropertyPanel();

  m_splitter->addWidget(m_treeWidget);

  // Create stacked widget for property/section panels
  QStackedWidget* rightStack = new QStackedWidget();
  rightStack->addWidget(m_propertyPanel);
  rightStack->addWidget(m_sectionPanel);
  rightStack->setCurrentWidget(m_propertyPanel);
  m_splitter->addWidget(rightStack);

  m_splitter->setSizes({280, 500});
  m_splitter->setStretchFactor(0, 0);
  m_splitter->setStretchFactor(1, 1);

  mainLayout->addWidget(m_splitter, 1);

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

  QPushButton* openConfigButton = new QPushButton("Open Config Folder");
  openConfigButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #0e639c; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 10px 20px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1177bb; "
    "}"
  );
  connect(openConfigButton, &QPushButton::clicked, this, [this]() {
    QString configPath = QCoreApplication::applicationDirPath() + "/config";
    QDir configDir(configPath);
    if (configDir.exists()) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(configPath));
    } else {
      QMessageBox::warning(this, "Config Folder Not Found",
        "Config folder not found at:\n" + configPath);
    }
  });

  m_backupButton = new QPushButton("Backup Config");
  m_backupButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #1e40af; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 10px 20px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #1e3a8a; "
    "}"
  );
  connect(m_backupButton, &QPushButton::clicked, this, [this]() {
    if (m_backupManager->showBackupDialog(this)) {
      // Update the in-memory setting value so the tree reflects the saved path
      m_currentValues["Backup"]["DestinationPath"] =
        m_backupManager->getBackupBasePath().string();
    }
  });

  m_restoreButton = new QPushButton("Restore Config");
  m_restoreButton->setStyleSheet(
    "QPushButton { "
    "  background-color: #78350f; "
    "  color: white; "
    "  border: none; "
    "  border-radius: 4px; "
    "  padding: 10px 20px; "
    "  font-weight: bold; "
    "} "
    "QPushButton:hover { "
    "  background-color: #92400e; "
    "}"
  );
  connect(m_restoreButton, &QPushButton::clicked, this, [this]() {
    m_backupManager->showRestoreDialog(this);
  });

  buttonBar->addWidget(m_resetButton);
  buttonBar->addWidget(openConfigButton);
  buttonBar->addWidget(m_backupButton);
  buttonBar->addWidget(m_restoreButton);
  buttonBar->addStretch();
  buttonBar->addWidget(m_reloadButton);
  buttonBar->addWidget(m_saveButton);

  mainLayout->addLayout(buttonBar);
}

void ApplicationSettingsPage::setupTreeView()
{
  m_treeWidget = new QTreeWidget();
  m_treeWidget->setHeaderHidden(true);
  m_treeWidget->setIndentation(20);
  m_treeWidget->setAnimated(true);
  m_treeWidget->setStyleSheet(
    "QTreeWidget { "
    "  background-color: #27272a; "
    "  border: 1px solid #3f3f46; "
    "  border-radius: 8px; "
    "  color: white; "
    "  font-size: 12px; "
    "} "
    "QTreeWidget::item { "
    "  padding: 6px 8px; "
    "  border-radius: 4px; "
    "} "
    "QTreeWidget::item:hover { "
    "  background-color: #3f3f46; "
    "} "
    "QTreeWidget::item:selected { "
    "  background-color: #3b82f6; "
    "  color: white; "
    "} "
    "QTreeWidget::branch:has-children:closed { "
    "  image: none; "
    "  border-image: none; "
    "} "
    "QTreeWidget::branch:has-children:open { "
    "  image: none; "
    "  border-image: none; "
    "}"
  );

  connect(m_treeWidget, &QTreeWidget::itemSelectionChanged,
          this, &ApplicationSettingsPage::onTreeItemSelected);
}

void ApplicationSettingsPage::setupPropertyPanel()
{
  // === Property Panel (for individual settings) ===
  m_propertyPanel = new QWidget();
  m_propertyPanel->setStyleSheet("background-color: #27272a; border-radius: 8px;");

  QVBoxLayout* propLayout = new QVBoxLayout(m_propertyPanel);
  propLayout->setContentsMargins(20, 20, 20, 20);
  propLayout->setSpacing(15);

  // Title
  m_propTitleLabel = new QLabel("Select a setting");
  QFont titleFont = m_propTitleLabel->font();
  titleFont.setPointSize(16);
  titleFont.setBold(true);
  m_propTitleLabel->setFont(titleFont);
  m_propTitleLabel->setStyleSheet("color: #60a5fa;");
  propLayout->addWidget(m_propTitleLabel);

  // Section badge
  m_propSectionLabel = new QLabel("");
  m_propSectionLabel->setStyleSheet(
    "color: #a1a1aa; "
    "background-color: #3f3f46; "
    "padding: 4px 10px; "
    "border-radius: 4px; "
    "font-size: 11px;"
  );
  m_propSectionLabel->setFixedHeight(24);
  m_propSectionLabel->hide();
  propLayout->addWidget(m_propSectionLabel);

  // Separator
  QFrame* sep1 = new QFrame();
  sep1->setFrameShape(QFrame::HLine);
  sep1->setStyleSheet("background-color: #3f3f46;");
  sep1->setFixedHeight(1);
  propLayout->addWidget(sep1);

  // Value editor container
  m_propEditorContainer = new QWidget();
  QVBoxLayout* editorLayout = new QVBoxLayout(m_propEditorContainer);
  editorLayout->setContentsMargins(0, 0, 0, 0);
  editorLayout->setSpacing(8);

  QLabel* valueLabel = new QLabel("Value");
  valueLabel->setStyleSheet("color: #a1a1aa; font-size: 11px; font-weight: bold;");
  editorLayout->addWidget(valueLabel);

  // Stacked widget for different editor types
  m_propValueStack = new QStackedWidget();

  // String editor (QLineEdit) - index 0
  m_propValueEdit = new QLineEdit();
  m_propValueEdit->setStyleSheet(
    "QLineEdit { "
    "  background-color: #3f3f46; "
    "  color: white; "
    "  border: 2px solid #52525b; "
    "  border-radius: 6px; "
    "  padding: 10px 14px; "
    "  font-size: 14px; "
    "} "
    "QLineEdit:focus { "
    "  border-color: #3b82f6; "
    "}"
  );
  connect(m_propValueEdit, &QLineEdit::textChanged, this, &ApplicationSettingsPage::onSettingChanged);
  m_propValueStack->addWidget(m_propValueEdit);

  // Integer editor (QSpinBox) - index 1
  m_propValueSpin = new QSpinBox();
  m_propValueSpin->setStyleSheet(
    "QSpinBox { "
    "  background-color: #3f3f46; "
    "  color: white; "
    "  border: 2px solid #52525b; "
    "  border-radius: 6px; "
    "  padding: 10px 14px; "
    "  font-size: 14px; "
    "} "
    "QSpinBox:focus { "
    "  border-color: #3b82f6; "
    "} "
    "QSpinBox::up-button, QSpinBox::down-button { "
    "  background-color: #52525b; "
    "  border: none; "
    "  width: 20px; "
    "} "
    "QSpinBox::up-button:hover, QSpinBox::down-button:hover { "
    "  background-color: #71717a; "
    "}"
  );
  m_propValueSpin->setMinimum(0);
  m_propValueSpin->setMaximum(1000000);
  connect(m_propValueSpin, QOverload<int>::of(&QSpinBox::valueChanged),
          this, &ApplicationSettingsPage::onSettingChanged);
  m_propValueStack->addWidget(m_propValueSpin);

  // Boolean editor (QCheckBox) - index 2
  m_propValueCheck = new QCheckBox("Enabled");
  m_propValueCheck->setStyleSheet(
    "QCheckBox { "
    "  color: white; "
    "  font-size: 14px; "
    "  spacing: 10px; "
    "} "
    "QCheckBox::indicator { "
    "  width: 24px; "
    "  height: 24px; "
    "  border: 2px solid #52525b; "
    "  border-radius: 4px; "
    "  background-color: #3f3f46; "
    "} "
    "QCheckBox::indicator:checked { "
    "  background-color: #3b82f6; "
    "  border-color: #3b82f6; "
    "} "
    "QCheckBox::indicator:hover { "
    "  border-color: #71717a; "
    "}"
  );
  connect(m_propValueCheck, &QCheckBox::checkStateChanged, this, &ApplicationSettingsPage::onSettingChanged);
  m_propValueStack->addWidget(m_propValueCheck);

  // Combo editor (QComboBox) - index 3
  m_propValueCombo = new QComboBox();
  m_propValueCombo->setStyleSheet(
    "QComboBox { "
    "  background-color: #3f3f46; "
    "  color: white; "
    "  border: 2px solid #52525b; "
    "  border-radius: 6px; "
    "  padding: 10px 14px; "
    "  font-size: 14px; "
    "  min-width: 200px; "
    "} "
    "QComboBox:focus { "
    "  border-color: #3b82f6; "
    "} "
    "QComboBox::drop-down { "
    "  border: none; "
    "  width: 30px; "
    "} "
    "QComboBox::down-arrow { "
    "  image: none; "
    "  border-left: 5px solid transparent; "
    "  border-right: 5px solid transparent; "
    "  border-top: 6px solid white; "
    "  margin-right: 10px; "
    "} "
    "QComboBox QAbstractItemView { "
    "  background-color: #3f3f46; "
    "  color: white; "
    "  selection-background-color: #3b82f6; "
    "  border: 1px solid #52525b; "
    "  border-radius: 4px; "
    "}"
  );
  connect(m_propValueCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ApplicationSettingsPage::onSettingChanged);
  m_propValueStack->addWidget(m_propValueCombo);

  editorLayout->addWidget(m_propValueStack);

  // Type info
  m_propTypeLabel = new QLabel("");
  m_propTypeLabel->setStyleSheet("color: #71717a; font-size: 11px;");
  editorLayout->addWidget(m_propTypeLabel);

  // Range info
  m_propRangeLabel = new QLabel("");
  m_propRangeLabel->setStyleSheet("color: #71717a; font-size: 11px;");
  editorLayout->addWidget(m_propRangeLabel);

  m_propEditorContainer->hide();
  propLayout->addWidget(m_propEditorContainer);

  // Description
  QLabel* descHeader = new QLabel("Description");
  descHeader->setStyleSheet("color: #a1a1aa; font-size: 11px; font-weight: bold; margin-top: 10px;");
  propLayout->addWidget(descHeader);

  m_propDescLabel = new QLabel("Select a setting from the tree to view and edit its value.");
  m_propDescLabel->setStyleSheet("color: #d4d4d8; font-size: 12px; line-height: 1.5;");
  m_propDescLabel->setWordWrap(true);
  propLayout->addWidget(m_propDescLabel);

  propLayout->addStretch();

  // === Section Panel (for section headers) ===
  m_sectionPanel = new QWidget();
  m_sectionPanel->setStyleSheet("background-color: #27272a; border-radius: 8px;");

  QVBoxLayout* sectionLayout = new QVBoxLayout(m_sectionPanel);
  sectionLayout->setContentsMargins(20, 20, 20, 20);
  sectionLayout->setSpacing(15);

  m_sectionTitleLabel = new QLabel("Section");
  QFont secTitleFont = m_sectionTitleLabel->font();
  secTitleFont.setPointSize(16);
  secTitleFont.setBold(true);
  m_sectionTitleLabel->setFont(secTitleFont);
  m_sectionTitleLabel->setStyleSheet("color: #60a5fa;");
  sectionLayout->addWidget(m_sectionTitleLabel);

  QFrame* sep2 = new QFrame();
  sep2->setFrameShape(QFrame::HLine);
  sep2->setStyleSheet("background-color: #3f3f46;");
  sep2->setFixedHeight(1);
  sectionLayout->addWidget(sep2);

  m_sectionDescLabel = new QLabel("");
  m_sectionDescLabel->setStyleSheet("color: #d4d4d8; font-size: 12px;");
  m_sectionDescLabel->setWordWrap(true);
  sectionLayout->addWidget(m_sectionDescLabel);

  m_sectionCountLabel = new QLabel("");
  m_sectionCountLabel->setStyleSheet("color: #71717a; font-size: 11px; margin-top: 10px;");
  sectionLayout->addWidget(m_sectionCountLabel);

  sectionLayout->addStretch();
}

void ApplicationSettingsPage::loadSettings()
{
  loadFromQSettings();
  populateTree();
  clearPropertyPanel();

  m_modified = false;
  updateModifiedIndicator();
}

void ApplicationSettingsPage::loadFromQSettings()
{
  QSettings settings(QSETTINGS_ORG, QSETTINGS_APP);

  m_currentValues.clear();
  for (const auto& def : m_settingDefs) {
    QString key = QString::fromStdString(def.section + "/" + def.key);
    std::string value = def.defaultValue;

    if (settings.contains(key)) {
      value = settings.value(key).toString().toStdString();
    }

    m_currentValues[def.section][def.key] = value;
  }

  std::cout << "[ApplicationSettingsPage] Loaded settings from QSettings" << std::endl;
}

void ApplicationSettingsPage::populateTree()
{
  m_treeWidget->clear();

  // Group settings by section
  std::map<std::string, std::vector<const SettingDefinition*>> sectionSettings;
  for (const auto& def : m_settingDefs) {
    sectionSettings[def.section].push_back(&def);
  }

  // Define section order
  std::vector<std::string> sectionOrder = {
    "CameraMonitor", "ChartOverlay", "Vision", "Tools",
    "BeamServer", "ChainExecution", "Scanning", "UI", "Logging", "Backup"
  };

  // Create tree items in order
  for (const auto& sectionName : sectionOrder) {
    auto it = sectionSettings.find(sectionName);
    if (it == sectionSettings.end()) continue;

    const auto& settings = it->second;

    QTreeWidgetItem* sectionItem = new QTreeWidgetItem(m_treeWidget);
    sectionItem->setText(0, QString::fromStdString(sectionName));
    sectionItem->setData(0, Qt::UserRole, QString("section"));
    sectionItem->setData(0, Qt::UserRole + 1, QString::fromStdString(sectionName));

    QFont sectionFont = sectionItem->font(0);
    sectionFont.setBold(true);
    sectionItem->setFont(0, sectionFont);

    for (const auto* def : settings) {
      QTreeWidgetItem* keyItem = new QTreeWidgetItem(sectionItem);
      keyItem->setText(0, QString::fromStdString(def->key));
      keyItem->setData(0, Qt::UserRole, QString("setting"));
      keyItem->setData(0, Qt::UserRole + 1, QString::fromStdString(def->section));
      keyItem->setData(0, Qt::UserRole + 2, QString::fromStdString(def->key));
    }

    sectionItem->setExpanded(true);
  }
}

void ApplicationSettingsPage::onTreeItemSelected()
{
  QList<QTreeWidgetItem*> selected = m_treeWidget->selectedItems();
  if (selected.isEmpty()) {
    clearPropertyPanel();
    return;
  }

  QTreeWidgetItem* item = selected.first();
  QString itemType = item->data(0, Qt::UserRole).toString();

  if (itemType == "section") {
    QString sectionName = item->data(0, Qt::UserRole + 1).toString();
    showSectionPanel(sectionName.toStdString());
  } else if (itemType == "setting") {
    QString section = item->data(0, Qt::UserRole + 1).toString();
    QString key = item->data(0, Qt::UserRole + 2).toString();

    // Find the setting definition
    for (auto& def : m_settingDefs) {
      if (def.section == section.toStdString() && def.key == key.toStdString()) {
        showPropertyPanel(def);
        return;
      }
    }
  }
}

void ApplicationSettingsPage::showPropertyPanel(const SettingDefinition& setting)
{
  // Switch to property panel
  QStackedWidget* stack = qobject_cast<QStackedWidget*>(m_splitter->widget(1));
  if (stack) {
    stack->setCurrentWidget(m_propertyPanel);
  }

  m_currentSetting = const_cast<SettingDefinition*>(&setting);

  m_propTitleLabel->setText(QString::fromStdString(setting.key));
  m_propSectionLabel->setText(QString::fromStdString(setting.section));
  m_propSectionLabel->show();
  m_propDescLabel->setText(QString::fromStdString(setting.description));
  m_propEditorContainer->show();

  // Get current value
  std::string currentValue = m_currentValues[setting.section][setting.key];

  // Setup editor based on type
  if (setting.type == "int") {
    m_propValueStack->setCurrentIndex(1);  // SpinBox
    m_propValueSpin->blockSignals(true);
    m_propValueSpin->setMinimum(setting.minValue);
    m_propValueSpin->setMaximum(setting.maxValue);
    try {
      m_propValueSpin->setValue(std::stoi(currentValue));
    } catch (...) {
      m_propValueSpin->setValue(setting.minValue);
    }
    m_propValueSpin->blockSignals(false);
    m_propTypeLabel->setText("Type: Integer");
    m_propRangeLabel->setText(QString("Range: %1 - %2").arg(setting.minValue).arg(setting.maxValue));
    m_propRangeLabel->show();

  } else if (setting.type == "bool") {
    m_propValueStack->setCurrentIndex(2);  // CheckBox
    m_propValueCheck->blockSignals(true);
    m_propValueCheck->setChecked(currentValue == "true" || currentValue == "1");
    m_propValueCheck->blockSignals(false);
    m_propTypeLabel->setText("Type: Boolean");
    m_propRangeLabel->hide();

  } else if (setting.type == "combo") {
    m_propValueStack->setCurrentIndex(3);  // ComboBox
    m_propValueCombo->blockSignals(true);
    m_propValueCombo->clear();

    int selectedIndex = 0;
    for (size_t i = 0; i < setting.comboOptions.size(); ++i) {
      const auto& opt = setting.comboOptions[i];
      m_propValueCombo->addItem(QString::fromStdString(opt.second),
                                 QString::fromStdString(opt.first));
      if (opt.first == currentValue) {
        selectedIndex = static_cast<int>(i);
      }
    }
    m_propValueCombo->setCurrentIndex(selectedIndex);
    m_propValueCombo->blockSignals(false);
    m_propTypeLabel->setText("Type: Selection");
    m_propRangeLabel->hide();

  } else {
    // String type
    m_propValueStack->setCurrentIndex(0);  // LineEdit
    m_propValueEdit->blockSignals(true);
    m_propValueEdit->setText(QString::fromStdString(currentValue));
    m_propValueEdit->blockSignals(false);
    m_propTypeLabel->setText("Type: String");
    m_propRangeLabel->hide();
  }
}

void ApplicationSettingsPage::showSectionPanel(const std::string& sectionName)
{
  // Switch to section panel
  QStackedWidget* stack = qobject_cast<QStackedWidget*>(m_splitter->widget(1));
  if (stack) {
    stack->setCurrentWidget(m_sectionPanel);
  }

  m_currentSetting = nullptr;

  m_sectionTitleLabel->setText(QString::fromStdString(sectionName));

  auto descIt = s_sectionDescriptions.find(sectionName);
  if (descIt != s_sectionDescriptions.end()) {
    m_sectionDescLabel->setText(QString::fromStdString(descIt->second));
  } else {
    m_sectionDescLabel->setText("No description available.");
  }

  // Count settings in this section
  int count = 0;
  for (const auto& def : m_settingDefs) {
    if (def.section == sectionName) count++;
  }
  m_sectionCountLabel->setText(QString("%1 setting(s) in this section").arg(count));
}

void ApplicationSettingsPage::clearPropertyPanel()
{
  m_currentSetting = nullptr;
  m_propTitleLabel->setText("Select a setting");
  m_propSectionLabel->hide();
  m_propDescLabel->setText("Select a setting from the tree to view and edit its value.");
  m_propEditorContainer->hide();

  QStackedWidget* stack = qobject_cast<QStackedWidget*>(m_splitter->widget(1));
  if (stack) {
    stack->setCurrentWidget(m_propertyPanel);
  }
}

std::string ApplicationSettingsPage::getCurrentValue(const std::string& section,
                                                      const std::string& key,
                                                      const std::string& defaultVal)
{
  auto secIt = m_currentValues.find(section);
  if (secIt != m_currentValues.end()) {
    auto keyIt = secIt->second.find(key);
    if (keyIt != secIt->second.end()) {
      return keyIt->second;
    }
  }
  return defaultVal;
}

void ApplicationSettingsPage::onSettingChanged()
{
  if (!m_currentSetting) return;

  // Update stored value based on editor type
  if (m_currentSetting->type == "int") {
    m_currentValues[m_currentSetting->section][m_currentSetting->key] =
      std::to_string(m_propValueSpin->value());
  } else if (m_currentSetting->type == "bool") {
    m_currentValues[m_currentSetting->section][m_currentSetting->key] =
      m_propValueCheck->isChecked() ? "true" : "false";
  } else if (m_currentSetting->type == "combo") {
    int idx = m_propValueCombo->currentIndex();
    if (idx >= 0) {
      m_currentValues[m_currentSetting->section][m_currentSetting->key] =
        m_propValueCombo->currentData().toString().toStdString();
    }
  } else {
    m_currentValues[m_currentSetting->section][m_currentSetting->key] =
      m_propValueEdit->text().toStdString();
  }

  m_modified = true;
  updateModifiedIndicator();
}

void ApplicationSettingsPage::saveSettings()
{
  saveToQSettings();

  // Sync values into the in-memory ApplicationSettings so other code sees changes immediately
  if (m_context && m_context->applicationSettings) {
    for (const auto& def : m_settingDefs) {
      std::string value = m_currentValues[def.section][def.key];
      m_context->applicationSettings->Set(def.section, def.key, value);
    }
  }

  m_modified = false;
  updateModifiedIndicator();

  emit settingsChanged();

  std::cout << "[ApplicationSettingsPage] Settings saved to QSettings" << std::endl;
}

void ApplicationSettingsPage::saveToQSettings()
{
  QSettings settings(QSETTINGS_ORG, QSETTINGS_APP);

  for (const auto& def : m_settingDefs) {
    QString key = QString::fromStdString(def.section + "/" + def.key);
    std::string value = m_currentValues[def.section][def.key];
    settings.setValue(key, QString::fromStdString(value));
  }

  settings.sync();
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
    "Settings have been saved.\nSome changes may require restarting the camera monitor.");
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

  // Refresh dynamic options (cameras, channels, profiles)
  initializeSettingDefinitions();
  loadSettings();
}

void ApplicationSettingsPage::onResetToDefaults()
{
  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Reset to Defaults",
    "This will reset all settings to their default values.\n\nContinue?",
    QMessageBox::Yes | QMessageBox::No
  );

  if (reply == QMessageBox::No) {
    return;
  }

  // Reset all values to defaults
  for (const auto& def : m_settingDefs) {
    m_currentValues[def.section][def.key] = def.defaultValue;
  }

  // Save the defaults
  saveSettings();

  // Reload UI
  populateTree();
  clearPropertyPanel();

  QMessageBox::information(this, "Defaults Restored",
    "All settings have been reset to default values.");
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


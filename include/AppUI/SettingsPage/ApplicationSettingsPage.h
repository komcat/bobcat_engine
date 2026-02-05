// ApplicationSettingsPage.h - Application settings editor page
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <map>
#include <string>
#include <vector>
#include <functional>

struct ApplicationContext;

// Setting definition for the property panel
struct SettingDefinition {
  std::string section;
  std::string key;
  std::string defaultValue;
  std::string description;
  std::string type;  // "int", "string", "bool", "combo"
  int minValue = 0;
  int maxValue = 100000;
  std::vector<std::pair<std::string, std::string>> comboOptions; // value, display text
};

/**
 * @brief Application Settings editor page
 *
 * Displays and allows editing of application settings using a
 * Tree View + Property Panel layout.
 * Changes are saved to QSettings and applicationSettings.local.ini
 */
class ApplicationSettingsPage : public QWidget
{
  Q_OBJECT

public:
  explicit ApplicationSettingsPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~ApplicationSettingsPage() = default;

signals:
  void backToMainMenu();
  void settingsChanged();  // Emitted when settings are saved

private slots:
  void onBackClicked();
  void onSaveClicked();
  void onReloadClicked();
  void onResetToDefaults();
  void onSettingChanged();
  void onTreeItemSelected();

private:
  void setupUI();
  void setupTreeView();
  void setupPropertyPanel();
  void initializeSettingDefinitions();
  void loadSettings();
  void saveSettings();
  void saveToQSettings();
  void loadFromQSettings();
  void populateTree();
  void showPropertyPanel(const SettingDefinition& setting);
  void showSectionPanel(const std::string& sectionName);
  void clearPropertyPanel();
  void updateModifiedIndicator();
  std::string getCurrentValue(const std::string& section, const std::string& key, const std::string& defaultVal);

  // Helper to get camera IDs for combo box
  std::vector<std::pair<std::string, std::string>> getAvailableCameras();
  std::vector<std::pair<std::string, std::string>> getAvailableChannels();
  std::vector<std::pair<std::string, std::string>> getAvailableVisionProfiles();

  ApplicationContext* m_context;

  // UI Components - Top bar
  QPushButton* m_backButton;
  QPushButton* m_saveButton;
  QPushButton* m_reloadButton;
  QPushButton* m_resetButton;
  QLabel* m_titleLabel;
  QLabel* m_statusLabel;
  QLabel* m_filePathLabel;

  // Main layout
  QSplitter* m_splitter;
  QTreeWidget* m_treeWidget;

  // Property panel
  QWidget* m_propertyPanel;
  QLabel* m_propTitleLabel;
  QLabel* m_propSectionLabel;
  QLabel* m_propTypeLabel;
  QLabel* m_propRangeLabel;
  QLabel* m_propDescLabel;
  QLineEdit* m_propValueEdit;
  QSpinBox* m_propValueSpin;
  QCheckBox* m_propValueCheck;
  QComboBox* m_propValueCombo;
  QStackedWidget* m_propValueStack;
  QWidget* m_propEditorContainer;

  // Section panel (shown when section is selected)
  QWidget* m_sectionPanel;
  QLabel* m_sectionTitleLabel;
  QLabel* m_sectionDescLabel;
  QLabel* m_sectionCountLabel;

  // Settings definitions
  std::vector<SettingDefinition> m_settingDefs;
  std::map<std::string, std::string> s_sectionDescriptions;

  // Current values storage: section -> (key -> value)
  std::map<std::string, std::map<std::string, std::string>> m_currentValues;

  // Currently selected setting
  SettingDefinition* m_currentSetting = nullptr;

  // Track if settings have been modified
  bool m_modified;
};

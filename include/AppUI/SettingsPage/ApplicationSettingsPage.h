// ApplicationSettingsPage.h - Application settings editor page
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QScrollArea>
#include <map>
#include <string>

struct ApplicationContext;

/**
 * @brief Application Settings editor page
 *
 * Displays and allows editing of application settings from INI file.
 * Changes are saved to applicationSettings.local.ini to preserve defaults.
 */
class ApplicationSettingsPage : public QWidget
{
  Q_OBJECT

public:
  explicit ApplicationSettingsPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~ApplicationSettingsPage() = default;

signals:
  void backToMainMenu();

private slots:
  void onBackClicked();
  void onSaveClicked();
  void onReloadClicked();
  void onResetToDefaults();
  void onSettingChanged();

private:
  void setupUI();
  void loadSettings();
  void saveSettings();
  void createSettingsForm();
  void updateModifiedIndicator();

  ApplicationContext* m_context;

  // UI Components
  QPushButton* m_backButton;
  QPushButton* m_saveButton;
  QPushButton* m_reloadButton;
  QPushButton* m_resetButton;
  QLabel* m_titleLabel;
  QLabel* m_statusLabel;
  QLabel* m_filePathLabel;

  QScrollArea* m_scrollArea;
  QWidget* m_settingsContainer;

  // Settings storage: section -> (key -> QLineEdit*)
  std::map<std::string, std::map<std::string, QLineEdit*>> m_settingEdits;

  // Track if settings have been modified
  bool m_modified;

  // Section descriptions for UI
  static const std::map<std::string, std::string> s_sectionDescriptions;
  static const std::map<std::string, std::map<std::string, std::string>> s_keyDescriptions;
};

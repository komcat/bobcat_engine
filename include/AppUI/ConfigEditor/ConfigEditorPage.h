// ConfigEditorPage.h - Config editor with left panel list + right panel forms
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QString>
#include <QJsonDocument>

struct ApplicationContext;
class TcpClientFormWidget;
class CameraFormWidget;
class MotorDeviceFormWidget;
class SavedPositionsFormWidget;
class PneumaticSlidesFormWidget;  // ← Add this
class IODevicesFormWidget;  // ← Add this

/**
 * @brief Config file editor page
 * Left panel (20%): List of config files
 * Right panel (80%): Form editor for selected config
 */
class ConfigEditorPage : public QWidget
{
  Q_OBJECT

public:
  explicit ConfigEditorPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~ConfigEditorPage() = default;

signals:
  void backToMainMenu();

private slots:
  void onBackClicked();
  void onConfigSelected(QListWidgetItem* item);
  void onSaveClicked();
  void onReloadClicked();

private:
  void setupUI();
  void loadConfigList();
  void loadConfigFile(const QString& filename);
  void saveConfigFile(const QString& filename);

  ApplicationContext* m_context;

  // UI Components
  QPushButton* m_backButton;
  QPushButton* m_saveButton;
  QPushButton* m_reloadButton;

  QListWidget* m_configList;      // Left panel: config file list
  QStackedWidget* m_formStack;    // Right panel: form editors

  // Form widgets (one for each config type)
  TcpClientFormWidget* m_tcpClientForm;
  CameraFormWidget* m_cameraForm;
  MotorDeviceFormWidget* m_motorDeviceForm;
  SavedPositionsFormWidget* m_savedPositionsForm;
  PneumaticSlidesFormWidget* m_pneumaticSlidesForm;  // ← Add this
  IODevicesFormWidget* m_ioDevicesForm;  // ← Add this


  QWidget* m_emptyForm;

  QString m_currentConfigFile;
  QJsonDocument m_currentConfig;

  // Helper methods
  void createEmptyForm();
};
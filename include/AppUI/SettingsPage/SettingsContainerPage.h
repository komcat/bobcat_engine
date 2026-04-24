// SettingsContainerPage.h - Container with sidebar navigation over settings sub-pages
#pragma once

#include <QWidget>

class QPushButton;
class QStackedWidget;

struct ApplicationContext;
class ApplicationSettingsPage;
class IoWatchSettingsWidget;

/**
 * @brief Settings page container with left sidebar (mirrors HardwareInstrumentPage).
 *
 * Sub-pages:
 *   - App Settings (wraps existing ApplicationSettingsPage)
 *   - IO Watch (IoWatchSettingsWidget with hot reload)
 *
 * Exposes appSettingsPage() so MainWindow can keep its signal wiring
 * (settingsChanged → theme / camera window reload).
 */
class SettingsContainerPage : public QWidget
{
  Q_OBJECT

public:
  explicit SettingsContainerPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~SettingsContainerPage() override = default;

  ApplicationSettingsPage* appSettingsPage() const { return m_appSettingsPage; }

signals:
  void backToMainMenu();

private slots:
  void showAppSettings();
  void showIoWatch();

private:
  void setupUI();
  void createSidebar();
  void createContentArea();
  QPushButton* createSidebarButton(const QString& text);

  ApplicationContext* m_context;

  QWidget* m_sidebar;
  QStackedWidget* m_contentStack;

  QPushButton* m_backButton;
  QPushButton* m_appSettingsButton;
  QPushButton* m_ioWatchButton;
  QPushButton* m_currentButton;

  ApplicationSettingsPage* m_appSettingsPage;
  IoWatchSettingsWidget* m_ioWatchWidget;
};

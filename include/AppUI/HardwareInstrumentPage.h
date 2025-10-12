// HardwareInstrumentPage.h - Hardware page with sidebar + content
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

struct ApplicationContext;  // Forward declaration
class MultiCameraView;

/**
 * @brief Hardware & Instrument page
 * Left sidebar (10%) + right content area (90%)
 */
class HardwareInstrumentPage : public QWidget
{
  Q_OBJECT

public:
  explicit HardwareInstrumentPage(ApplicationContext* context, QWidget* parent = nullptr);
  ~HardwareInstrumentPage() = default;

signals:
  void backToMainMenu();

private slots:
  void showCamera();
  void showPIHexapod();
  void showACS();

private:
  void setupUI();
  void createSidebar();
  void createContentArea();
  QPushButton* createSidebarButton(const QString& text);

  ApplicationContext* m_context;  // ✅ NEW - Store context

  // UI Components
  QWidget* m_sidebar;
  QStackedWidget* m_contentStack;

  // Sidebar buttons
  QPushButton* m_backButton;
  QPushButton* m_cameraButton;
  QPushButton* m_piButton;
  QPushButton* m_acsButton;

  // Content pages
  MultiCameraView* m_cameraView;

  // Track current selection
  QPushButton* m_currentButton;
};
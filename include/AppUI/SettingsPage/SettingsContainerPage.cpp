// SettingsContainerPage.cpp
#include "SettingsContainerPage.h"
#include "ApplicationSettingsPage.h"
#include "IoWatchSettingsWidget.h"
#include "PneumaticSettingsWidget.h"
#include "IoDevicesSettingsWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>

SettingsContainerPage::SettingsContainerPage(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_sidebar(nullptr)
  , m_contentStack(nullptr)
  , m_backButton(nullptr)
  , m_appSettingsButton(nullptr)
  , m_ioWatchButton(nullptr)
  , m_pneumaticsButton(nullptr)
  , m_ioDevicesButton(nullptr)
  , m_currentButton(nullptr)
  , m_appSettingsPage(nullptr)
  , m_ioWatchWidget(nullptr)
  , m_pneumaticWidget(nullptr)
  , m_ioDevicesWidget(nullptr)
{
  setupUI();
}

void SettingsContainerPage::setupUI()
{
  QHBoxLayout* mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  createSidebar();
  mainLayout->addWidget(m_sidebar);

  createContentArea();
  mainLayout->addWidget(m_contentStack, 9);

  showAppSettings();
}

void SettingsContainerPage::createSidebar()
{
  m_sidebar = new QWidget();
  m_sidebar->setStyleSheet("QWidget { background-color: #333333; }");
  m_sidebar->setMinimumWidth(100);
  m_sidebar->setMaximumWidth(250);

  QVBoxLayout* sidebarLayout = new QVBoxLayout(m_sidebar);
  sidebarLayout->setContentsMargins(5, 10, 5, 10);
  sidebarLayout->setSpacing(10);

  m_backButton = new QPushButton("← Back");
  m_backButton->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #656565, stop:1 #555555); "
    "  color: white; padding: 10px; font-size: 14px; font-weight: bold; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-left: 2px solid rgba(255, 255, 255, 0.5); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.5); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.5); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #757575, stop:1 #656565); "
    "} "
    "QPushButton:pressed { background-color: #454545; }");
  connect(m_backButton, &QPushButton::clicked, this, &SettingsContainerPage::backToMainMenu);
  sidebarLayout->addWidget(m_backButton);

  QLabel* sep = new QLabel();
  sep->setFixedHeight(2);
  sep->setStyleSheet("QLabel { background-color: #555555; }");
  sidebarLayout->addWidget(sep);

  m_appSettingsButton = createSidebarButton("App Settings");
  connect(m_appSettingsButton, &QPushButton::clicked, this, &SettingsContainerPage::showAppSettings);
  sidebarLayout->addWidget(m_appSettingsButton);

  m_ioWatchButton = createSidebarButton("IO Watch");
  connect(m_ioWatchButton, &QPushButton::clicked, this, &SettingsContainerPage::showIoWatch);
  sidebarLayout->addWidget(m_ioWatchButton);

  m_pneumaticsButton = createSidebarButton("Pneumatics");
  connect(m_pneumaticsButton, &QPushButton::clicked, this, &SettingsContainerPage::showPneumatics);
  sidebarLayout->addWidget(m_pneumaticsButton);

  m_ioDevicesButton = createSidebarButton("IO Devices");
  connect(m_ioDevicesButton, &QPushButton::clicked, this, &SettingsContainerPage::showIoDevices);
  sidebarLayout->addWidget(m_ioDevicesButton);

  sidebarLayout->addStretch();
}

void SettingsContainerPage::createContentArea()
{
  m_contentStack = new QStackedWidget();

  // Index 0: App Settings
  m_appSettingsPage = new ApplicationSettingsPage(m_context);
  // Forward the sub-page's back signal through the container.
  connect(m_appSettingsPage, &ApplicationSettingsPage::backToMainMenu,
          this, &SettingsContainerPage::backToMainMenu);
  m_contentStack->addWidget(m_appSettingsPage);

  // Index 1: IO Watch
  m_ioWatchWidget = new IoWatchSettingsWidget(m_context);
  m_contentStack->addWidget(m_ioWatchWidget);

  // Index 2: Pneumatics
  m_pneumaticWidget = new PneumaticSettingsWidget(m_context);
  m_contentStack->addWidget(m_pneumaticWidget);

  // Index 3: IO Devices
  m_ioDevicesWidget = new IoDevicesSettingsWidget(m_context);
  m_contentStack->addWidget(m_ioDevicesWidget);
}

QPushButton* SettingsContainerPage::createSidebarButton(const QString& text)
{
  QPushButton* button = new QPushButton(text);
  button->setCheckable(true);
  button->setStyleSheet(
    "QPushButton { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4a4a, stop:1 #3a3a3a); "
    "  color: white; border-left: 3px solid transparent; "
    "  padding: 15px 10px; font-size: 14px; text-align: left; "
    "  border-top: 2px solid rgba(255, 255, 255, 0.4); "
    "  border-right: 2px solid rgba(0, 0, 0, 0.4); "
    "  border-bottom: 2px solid rgba(0, 0, 0, 0.4); "
    "} "
    "QPushButton:hover { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #4a4a4a); "
    "} "
    "QPushButton:pressed { background-color: #2a2a2a; } "
    "QPushButton:checked { "
    "  background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3b3b3b, stop:1 #2b2b2b); "
    "  border-left-color: #00aaff; font-weight: bold; "
    "}");
  return button;
}

void SettingsContainerPage::showAppSettings()
{
  m_contentStack->setCurrentIndex(0);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_appSettingsButton->setChecked(true);
  m_currentButton = m_appSettingsButton;
}

void SettingsContainerPage::showIoWatch()
{
  m_contentStack->setCurrentIndex(1);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_ioWatchButton->setChecked(true);
  m_currentButton = m_ioWatchButton;
}

void SettingsContainerPage::showPneumatics()
{
  m_contentStack->setCurrentIndex(2);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_pneumaticsButton->setChecked(true);
  m_currentButton = m_pneumaticsButton;
}

void SettingsContainerPage::showIoDevices()
{
  m_contentStack->setCurrentIndex(3);
  if (m_currentButton) m_currentButton->setChecked(false);
  m_ioDevicesButton->setChecked(true);
  m_currentButton = m_ioDevicesButton;
}

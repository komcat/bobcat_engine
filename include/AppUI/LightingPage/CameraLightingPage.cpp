// CameraLightingPage.cpp - Combined Camera and Lighting page implementation
#include "CameraLightingPage.h"
#include "MultiCameraView.h"
#include "LightingControlWidget.h"
#include "ApplicationContext.h"
#include <QVBoxLayout>
#include <iostream>

CameraLightingPage::CameraLightingPage(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_tabWidget(nullptr)
    , m_cameraView(nullptr)
    , m_lightingWidget(nullptr)
{
    setupUI();
}

void CameraLightingPage::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Create tab widget
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane {"
        "  border: 1px solid #444;"
        "  background: #2b2b2b;"
        "}"
        "QTabBar::tab {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4a4a, stop:1 #3a3a3a);"
        "  color: #ccc;"
        "  padding: 10px 25px;"
        "  margin-right: 2px;"
        "  border-top-left-radius: 4px;"
        "  border-top-right-radius: 4px;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "}"
        "QTabBar::tab:selected {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a3a3a, stop:1 #2b2b2b);"
        "  color: #00aaff;"
        "  border-bottom: 2px solid #00aaff;"
        "}"
        "QTabBar::tab:hover:!selected {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5a5a5a, stop:1 #4a4a4a);"
        "  color: white;"
        "}"
    );

    // Tab 1: Camera view (existing MultiCameraView)
    m_cameraView = new MultiCameraView(m_context);
    m_tabWidget->addTab(m_cameraView, "Camera");

    // Tab 2: Lighting control
    m_lightingWidget = new LightingControlWidget(m_context);
    m_tabWidget->addTab(m_lightingWidget, "Lighting");

    mainLayout->addWidget(m_tabWidget);

    std::cout << "[CameraLightingPage] Created with Camera and Lighting tabs" << std::endl;
}

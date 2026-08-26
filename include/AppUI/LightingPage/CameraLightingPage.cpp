// CameraLightingPage.cpp - Combined Camera and Lighting page implementation
#include "CameraLightingPage.h"
#include "MultiCameraView.h"
#include "LightingControlWidget.h"
#include "ApplicationContext.h"
#include <QVBoxLayout>
#include <chrono>
#include <iostream>

namespace {

// Scoped stopwatch, same pattern as RecipePage/MotionPage/RunProgramPage.
// This page is the default panel of F4, so it is the whole of that page's
// first-open cost. Measure in a RELEASE build - MSVC debug inflates Qt widget
// construction and ranks these wrong.
class PhaseTimer
{
public:
    explicit PhaseTimer(const char* label)
        : m_label(label)
        , m_start(std::chrono::steady_clock::now())
    {
    }

    ~PhaseTimer()
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_start).count();
        std::cout << "[CameraLightingPage][timing] " << m_label << ": " << ms << " ms" << std::endl;
    }

    PhaseTimer(const PhaseTimer&) = delete;
    PhaseTimer& operator=(const PhaseTimer&) = delete;

private:
    const char* m_label;
    std::chrono::steady_clock::time_point m_start;
};

}  // namespace

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
    {
        PhaseTimer t("  MultiCameraView");
        m_cameraView = new MultiCameraView(m_context);
        m_tabWidget->addTab(m_cameraView, "Camera");
    }

    // Tab 2: Lighting control - shell only. LightingControlWidget builds a
    // channel UI and spawns a LightingParameterWorker thread, neither of which
    // is worth paying for on the tab that is not selected. ensureLightingWidget()
    // fills it in on first selection.
    m_lightingTab = new QWidget();
    QVBoxLayout* lightingTabLayout = new QVBoxLayout(m_lightingTab);
    lightingTabLayout->setContentsMargins(0, 0, 0, 0);
    m_tabWidget->addTab(m_lightingTab, "Lighting");

    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) {
            ensureLightingWidget();
        }
    });

    mainLayout->addWidget(m_tabWidget);

    std::cout << "[CameraLightingPage] Created with Camera and Lighting tabs" << std::endl;
}

void CameraLightingPage::ensureLightingWidget()
{
    if (m_lightingWidget || !m_lightingTab) {
        return;
    }

    QVBoxLayout* lightingTabLayout = qobject_cast<QVBoxLayout*>(m_lightingTab->layout());
    if (!lightingTabLayout) {
        return;
    }

    PhaseTimer t("ensureLightingWidget");

    m_lightingWidget = new LightingControlWidget(m_context);
    lightingTabLayout->addWidget(m_lightingWidget);
}

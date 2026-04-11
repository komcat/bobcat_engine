// PluginConfigStepWidget.cpp
// Plugin parameter configuration + image view for vision wizard
#include "PluginConfigStepWidget.h"
#include "VisionImageWidget.h"
#include "VisionWizardState.h"
#include <QHBoxLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <iostream>

PluginConfigStepWidget::PluginConfigStepWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_state(nullptr)
    , m_pluginScrollArea(nullptr)
    , m_pluginParamsLayout(nullptr)
    , m_currentPluginConfig(nullptr)
    , m_applyParamsBtn(nullptr)
    , m_imagePlaceholder(nullptr)
    , m_imagePlaceholderLayout(nullptr)
{
    setupUI();

    // Live-execute debounce timer (single-shot, restarted on every parameter
    // change). Keeps the wizard from re-executing on every keystroke while a
    // user holds the spinbox up/down arrow.
    m_liveExecuteTimer = new QTimer(this);
    m_liveExecuteTimer->setSingleShot(true);
    m_liveExecuteTimer->setInterval(300);
    connect(m_liveExecuteTimer, &QTimer::timeout,
            this, &PluginConfigStepWidget::liveExecuteRequested);
}

void PluginConfigStepWidget::setupUI()
{
    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Left: plugin config (scrollable, fixed 350px) ─────────────────────
    QScrollArea* configScrollArea = new QScrollArea(this);
    configScrollArea->setWidgetResizable(true);
    configScrollArea->setFixedWidth(350);
    configScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    configScrollArea->setFrameShape(QFrame::NoFrame);

    // Compact cascading style for plugin config widgets
    // max-width on inputs prevents clipping; they resize to fit the container
    configScrollArea->setStyleSheet(
        "QGroupBox { color: #CCCCCC; border: 1px solid #444; border-radius: 3px; "
        "margin-top: 8px; padding: 4px 4px 2px 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; font-weight: bold; font-size: 11px; }"
        "QLabel { font-size: 11px; }"
        "QSpinBox, QDoubleSpinBox, QComboBox, QLineEdit { padding: 1px; max-width: 105px; }"
    );

    m_pluginScrollArea = configScrollArea;

    QWidget* scrollContent = new QWidget();
    m_pluginParamsLayout = new QVBoxLayout(scrollContent);
    m_pluginParamsLayout->setContentsMargins(4, 4, 4, 4);
    m_pluginParamsLayout->setSpacing(4);

    // Placeholder label when no plugin config loaded
    QLabel* noPluginLabel = new QLabel("Select a plugin using the\n\"Select Plugin...\" button\nin the left panel");
    noPluginLabel->setStyleSheet("color: #808080; font-style: italic;");
    noPluginLabel->setAlignment(Qt::AlignCenter);
    noPluginLabel->setWordWrap(true);
    m_pluginParamsLayout->addWidget(noPluginLabel);

    m_pluginParamsLayout->addStretch();

    // Apply + Test Execute buttons
    m_applyParamsBtn = new QPushButton("Apply Parameters", scrollContent);
    m_applyParamsBtn->setToolTip("Apply current parameter settings");
    m_applyParamsBtn->setStyleSheet(
        "QPushButton { background-color: #1565C0; color: white; font-weight: bold; padding: 6px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #1976D2; }"
        "QPushButton:pressed { background-color: #0D47A1; }"
    );
    m_pluginParamsLayout->addWidget(m_applyParamsBtn);

    auto* testBtn = new QPushButton("Test Execute", scrollContent);
    testBtn->setToolTip("Apply parameters and execute the vision profile");
    testBtn->setStyleSheet(
        "QPushButton { background-color: #2E7D32; color: white; font-weight: bold; padding: 6px; border-radius: 3px; }"
        "QPushButton:hover { background-color: #388E3C; }"
        "QPushButton:pressed { background-color: #1B5E20; }"
    );
    m_pluginParamsLayout->addWidget(testBtn);
    connect(testBtn, &QPushButton::clicked, this, &PluginConfigStepWidget::applyParamsClicked);

    m_pluginScrollArea->setWidget(scrollContent);

    connect(m_applyParamsBtn, &QPushButton::clicked,
            this, &PluginConfigStepWidget::applyParamsClicked);

    rootLayout->addWidget(configScrollArea);

    // ── Right: image widget placeholder (flex) ────────────────────────────
    m_imagePlaceholder = new QWidget(this);
    m_imagePlaceholderLayout = new QVBoxLayout(m_imagePlaceholder);
    m_imagePlaceholderLayout->setContentsMargins(0, 0, 0, 0);
    m_imagePlaceholderLayout->setSpacing(0);

    rootLayout->addWidget(m_imagePlaceholder, 1);
}

// ── Public API ──────────────────────────────────────────────────────────────

void PluginConfigStepWidget::setState(VisionWizardState* state)
{
    m_state = state;
}

void PluginConfigStepWidget::embedImageWidget(VisionImageWidget* imgWidget)
{
    if (!imgWidget || !m_imagePlaceholderLayout) return;

    // Remove any existing children (hint labels, etc.)
    while (m_imagePlaceholderLayout->count() > 0) {
        QLayoutItem* item = m_imagePlaceholderLayout->takeAt(0);
        if (item->widget() && item->widget() != imgWidget) {
            item->widget()->hide();
        }
        delete item;
    }

    imgWidget->setParent(m_imagePlaceholder);
    m_imagePlaceholderLayout->addWidget(imgWidget);
    imgWidget->show();
}

void PluginConfigStepWidget::removeImageWidget()
{
    if (!m_imagePlaceholderLayout) return;
    for (int i = m_imagePlaceholderLayout->count() - 1; i >= 0; --i) {
        QLayoutItem* item = m_imagePlaceholderLayout->takeAt(i);
        if (item->widget()) {
            item->widget()->setParent(nullptr);
            item->widget()->hide();
        }
        delete item;
    }
}

void PluginConfigStepWidget::setPluginConfigWidget(QWidget* configWidget)
{
    clearPluginConfigWidget();

    if (!configWidget) return;

    // Remove placeholder (everything before stretch + button = index 0)
    while (m_pluginParamsLayout->count() > 2) {
        QLayoutItem* item = m_pluginParamsLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    m_pluginParamsLayout->insertWidget(0, configWidget);
    m_currentPluginConfig = configWidget;

    // Wire every spinbox/slider in the new plugin widget to the live-execute
    // debounce timer so the user can tune parameters in real time.
    wireLiveExecution(configWidget);
}

void PluginConfigStepWidget::wireLiveExecution(QWidget* configWidget)
{
    if (!configWidget || !m_liveExecuteTimer) return;

    auto restartTimer = [this]() {
        if (m_liveExecuteTimer) m_liveExecuteTimer->start();
    };

    // Opt-out marker: any widget that sets property "noLiveExecute" = true
    // will be skipped (e.g. cosmetic spin boxes that should not re-execute).
    auto isOptedOut = [](QWidget* w) {
        return w && w->property("noLiveExecute").toBool();
    };

    // Connections are added AFTER the plugin's own valueChanged → m_member
    // lambdas, so when the user changes a value Qt fires those FIRST (updating
    // the plugin's internal members), then our handler runs and starts the
    // debounce timer. By the time executePlugin() runs, the plugin already
    // sees the new parameter values.
    int wired = 0;
    const auto intSpins = configWidget->findChildren<QSpinBox*>();
    for (QSpinBox* sp : intSpins) {
        if (isOptedOut(sp)) continue;
        connect(sp, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [restartTimer](int) { restartTimer(); });
        ++wired;
    }

    const auto dblSpins = configWidget->findChildren<QDoubleSpinBox*>();
    for (QDoubleSpinBox* sp : dblSpins) {
        if (isOptedOut(sp)) continue;
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [restartTimer](double) { restartTimer(); });
        ++wired;
    }

    const auto sliders = configWidget->findChildren<QSlider*>();
    for (QSlider* sl : sliders) {
        if (isOptedOut(sl)) continue;
        connect(sl, &QSlider::valueChanged,
                this, [restartTimer](int) { restartTimer(); });
        ++wired;
    }

    std::cout << "[PluginConfigStepWidget] Wired " << wired
              << " parameter widgets for live execution" << std::endl;
}

void PluginConfigStepWidget::clearPluginConfigWidget()
{
    if (m_liveExecuteTimer && m_liveExecuteTimer->isActive()) {
        m_liveExecuteTimer->stop();
    }

    if (m_currentPluginConfig) {
        m_pluginParamsLayout->removeWidget(m_currentPluginConfig);
        m_currentPluginConfig->deleteLater();
        m_currentPluginConfig = nullptr;
    }

    // Restore placeholder if only stretch + button remain
    if (m_pluginParamsLayout->count() <= 2) {
        QLabel* noPluginLabel = new QLabel("Select a plugin using the\n\"Select Plugin...\" button\nin the left panel");
        noPluginLabel->setStyleSheet("color: #808080; font-style: italic;");
        noPluginLabel->setAlignment(Qt::AlignCenter);
        noPluginLabel->setWordWrap(true);
        m_pluginParamsLayout->insertWidget(0, noPluginLabel);
    }
}

// LightingControlWidget.cpp - Lighting channel control widget implementation
#include "LightingControlWidget.h"
#include "ApplicationContext.h"
#include "lighting/LightingManager.h"
#include "lighting/LightingTypes.h"
#include "lighting/LightingParameterWorker.h"
#include <QScrollArea>
#include <QFrame>
#include <iostream>
#include <iomanip>

// ============================================================================
// LightingChannelWidget Implementation
// ============================================================================

LightingChannelWidget::LightingChannelWidget(int channelNumber,
                                              const QString& channelName,
                                              const QString& color,
                                              QWidget* parent)
    : QWidget(parent)
    , m_channelNumber(channelNumber)
    , m_channelName(channelName)
    , m_color(color)
    , m_isOn(false)
    , m_isConnected(false)
    , m_isUpdating(false)
    , m_currentIntensity(0)
{
    setupUI();
}

void LightingChannelWidget::setupUI()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 5, 10, 5);
    mainLayout->setSpacing(10);

    // Channel number label (L1, L2, etc.)
    m_channelLabel = new QLabel(QString("L%1").arg(m_channelNumber + 1));
    m_channelLabel->setFixedWidth(30);
    m_channelLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #aaa;");
    mainLayout->addWidget(m_channelLabel);

    // Channel name label with color indicator
    QWidget* nameContainer = new QWidget();
    QHBoxLayout* nameLayout = new QHBoxLayout(nameContainer);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(5);

    // Color indicator square
    if (!m_color.isEmpty()) {
        QLabel* colorIndicator = new QLabel();
        colorIndicator->setFixedSize(16, 16);
        QColor c = getColorFromString(m_color);
        colorIndicator->setStyleSheet(
            QString("background-color: %1; border: 1px solid #555; border-radius: 2px;")
                .arg(c.name()));
        nameLayout->addWidget(colorIndicator);
    }

    m_nameLabel = new QLabel(m_channelName);
    m_nameLabel->setFixedWidth(100);
    m_nameLabel->setStyleSheet("font-size: 13px; color: white;");
    nameLayout->addWidget(m_nameLabel);
    nameLayout->addStretch();

    nameContainer->setFixedWidth(130);
    mainLayout->addWidget(nameContainer);

    // Decrement button (-2.5%)
    m_decrementButton = new QPushButton("-");
    m_decrementButton->setFixedSize(32, 28);
    m_decrementButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #444;"
        "  color: white;"
        "  font-weight: bold;"
        "  font-size: 16px;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover { background-color: #555; }"
        "QPushButton:pressed { background-color: #333; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }"
    );
    m_decrementButton->setToolTip("-2.5%");
    connect(m_decrementButton, &QPushButton::clicked, this, &LightingChannelWidget::onDecrementClicked);
    mainLayout->addWidget(m_decrementButton);

    // Percent input field
    m_percentInput = new QLineEdit("0.0");
    m_percentInput->setFixedWidth(60);
    m_percentInput->setAlignment(Qt::AlignCenter);
    m_percentInput->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2a2a2a;"
        "  color: #00aaff;"
        "  border: 1px solid #555;"
        "  border-radius: 3px;"
        "  padding: 4px;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "}"
        "QLineEdit:focus { border-color: #00aaff; }"
        "QLineEdit:disabled { background-color: #1a1a1a; color: #555; }"
    );
    connect(m_percentInput, &QLineEdit::editingFinished, this, &LightingChannelWidget::onPercentInputFinished);
    mainLayout->addWidget(m_percentInput);

    // Percent symbol label
    QLabel* percentSymbol = new QLabel("%");
    percentSymbol->setStyleSheet("font-size: 12px; color: #888;");
    mainLayout->addWidget(percentSymbol);

    // Increment button (+2.5%)
    m_incrementButton = new QPushButton("+");
    m_incrementButton->setFixedSize(32, 28);
    m_incrementButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #444;"
        "  color: white;"
        "  font-weight: bold;"
        "  font-size: 16px;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover { background-color: #555; }"
        "QPushButton:pressed { background-color: #333; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }"
    );
    m_incrementButton->setToolTip("+2.5%");
    connect(m_incrementButton, &QPushButton::clicked, this, &LightingChannelWidget::onIncrementClicked);
    mainLayout->addWidget(m_incrementButton);

    // Apply button
    m_applyButton = new QPushButton("Apply");
    m_applyButton->setFixedSize(60, 28);
    m_applyButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #0066aa;"
        "  color: white;"
        "  font-weight: bold;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover { background-color: #0088cc; }"
        "QPushButton:pressed { background-color: #005599; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }"
    );
    connect(m_applyButton, &QPushButton::clicked, this, &LightingChannelWidget::onApplyClicked);
    mainLayout->addWidget(m_applyButton);

    mainLayout->addSpacing(10);

    // Intensity value display (raw 0-255)
    m_intensityValue = new QLabel("(0)");
    m_intensityValue->setFixedWidth(40);
    m_intensityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_intensityValue->setStyleSheet("font-size: 11px; color: #666;");
    mainLayout->addWidget(m_intensityValue);

    mainLayout->addSpacing(10);

    // Toggle button
    m_toggleButton = new QPushButton("OFF");
    m_toggleButton->setFixedSize(70, 28);
    m_toggleButton->setCheckable(true);
    updateButtonState();
    connect(m_toggleButton, &QPushButton::clicked, this, &LightingChannelWidget::onToggleClicked);
    mainLayout->addWidget(m_toggleButton);

    mainLayout->addStretch();

    setEnabled(false);  // Disabled until connected
}

QColor LightingChannelWidget::getColorFromString(const QString& colorStr)
{
    QString lower = colorStr.toLower();
    if (lower == "red") return QColor(255, 60, 60);
    if (lower == "green") return QColor(60, 255, 60);
    if (lower == "blue") return QColor(60, 100, 255);
    if (lower == "white") return QColor(255, 255, 255);
    if (lower == "yellow") return QColor(255, 255, 60);
    if (lower == "cyan") return QColor(60, 255, 255);
    if (lower == "magenta") return QColor(255, 60, 255);
    if (lower == "orange") return QColor(255, 165, 0);
    return QColor(128, 128, 128);  // Default gray
}

void LightingChannelWidget::setIntensity(int intensity)
{
    m_isUpdating = true;
    m_currentIntensity = intensity;
    m_intensityValue->setText(QString("(%1)").arg(intensity));
    double percent = Lighting::IntensityToPercent(intensity);
    m_percentInput->setText(QString::number(percent, 'f', 1));
    m_isUpdating = false;
}

void LightingChannelWidget::setOn(bool on)
{
    m_isOn = on;
    m_isUpdating = true;
    m_toggleButton->setChecked(on);
    m_isUpdating = false;
    updateButtonState();
}

void LightingChannelWidget::setConnected(bool connected)
{
    m_isConnected = connected;
    setEnabled(connected);
}

void LightingChannelWidget::onApplyClicked()
{
    if (m_isUpdating) return;

    bool ok;
    double percent = m_percentInput->text().toDouble(&ok);

    if (!ok) {
        // Invalid input - restore current value
        double currentPercent = Lighting::IntensityToPercent(m_currentIntensity);
        m_percentInput->setText(QString::number(currentPercent, 'f', 1));
        return;
    }

    // Clamp to valid range
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;

    // Convert to intensity and send update
    int intensity = Lighting::PercentToIntensity(percent);
    sendIntensityUpdate(intensity);
}

void LightingChannelWidget::onIncrementClicked()
{
    if (m_isUpdating) return;

    // Increment by 2.5%
    double currentPercent = Lighting::IntensityToPercent(m_currentIntensity);
    double newPercent = currentPercent + 2.5;
    if (newPercent > 100.0) newPercent = 100.0;

    int intensity = Lighting::PercentToIntensity(newPercent);

    // Update UI immediately
    m_isUpdating = true;
    m_percentInput->setText(QString::number(newPercent, 'f', 1));
    m_intensityValue->setText(QString("(%1)").arg(intensity));
    m_isUpdating = false;

    sendIntensityUpdate(intensity);
}

void LightingChannelWidget::onDecrementClicked()
{
    if (m_isUpdating) return;

    // Decrement by 2.5%
    double currentPercent = Lighting::IntensityToPercent(m_currentIntensity);
    double newPercent = currentPercent - 2.5;
    if (newPercent < 0.0) newPercent = 0.0;

    int intensity = Lighting::PercentToIntensity(newPercent);

    // Update UI immediately
    m_isUpdating = true;
    m_percentInput->setText(QString::number(newPercent, 'f', 1));
    m_intensityValue->setText(QString("(%1)").arg(intensity));
    m_isUpdating = false;

    sendIntensityUpdate(intensity);
}

void LightingChannelWidget::sendIntensityUpdate(int intensity)
{
    m_currentIntensity = intensity;
    emit intensityChanged(m_channelNumber, intensity);
}

void LightingChannelWidget::onPercentInputFinished()
{
    if (m_isUpdating) return;

    bool ok;
    double percent = m_percentInput->text().toDouble(&ok);

    if (!ok) {
        // Invalid input - restore current value
        double currentPercent = Lighting::IntensityToPercent(m_currentIntensity);
        m_percentInput->setText(QString::number(currentPercent, 'f', 1));
        return;
    }

    // Clamp to valid range
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;

    // Convert to intensity and update UI
    int intensity = Lighting::PercentToIntensity(percent);

    m_isUpdating = true;
    m_intensityValue->setText(QString("(%1)").arg(intensity));
    // Update text field with clamped/normalized value
    m_percentInput->setText(QString::number(Lighting::IntensityToPercent(intensity), 'f', 1));
    m_isUpdating = false;

    // Note: Don't send update here - user needs to click Apply
    // This just validates and normalizes the input
}

void LightingChannelWidget::onToggleClicked()
{
    if (m_isUpdating) return;

    m_isOn = m_toggleButton->isChecked();
    updateButtonState();
    emit toggleChanged(m_channelNumber, m_isOn);
}

void LightingChannelWidget::updateButtonState()
{
    if (m_isOn) {
        m_toggleButton->setText("ON");
        m_toggleButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #00aa00;"
            "  color: white;"
            "  font-weight: bold;"
            "  border: none;"
            "  border-radius: 4px;"
            "}"
            "QPushButton:hover { background-color: #00cc00; }"
        );
    } else {
        m_toggleButton->setText("OFF");
        m_toggleButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #555;"
            "  color: #aaa;"
            "  font-weight: bold;"
            "  border: none;"
            "  border-radius: 4px;"
            "}"
            "QPushButton:hover { background-color: #666; }"
        );
    }
}

// ============================================================================
// LightingControlWidget Implementation
// ============================================================================

LightingControlWidget::LightingControlWidget(ApplicationContext* context, QWidget* parent)
    : QWidget(parent)
    , m_context(context)
    , m_lightingManager(nullptr)
    , m_isConnected(false)
    , m_parameterWorker(nullptr)
    , m_connectionPending(false)
{
    if (m_context) {
        m_lightingManager = m_context->lightingManager;
    }

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &LightingControlWidget::onPollTimer);

    // Get the first controller ID BEFORE setupUI so channel widgets use correct config
    if (m_lightingManager && m_lightingManager->GetControllerCount() > 0) {
        auto ids = m_lightingManager->GetControllerIds();
        if (!ids.empty()) {
            m_currentDeviceId = ids[0];
        }
    }

    setupUI();
    updateStatus();

    // Initialize parameter worker
    if (m_lightingManager) {
        m_parameterWorker = new LightingParameterWorker(m_lightingManager, nullptr);

        // Connect worker signals (use Qt::QueuedConnection for cross-thread safety)
        connect(m_parameterWorker, &LightingParameterWorker::connectionSuccess,
                this, &LightingControlWidget::onWorkerConnectionSuccess, Qt::QueuedConnection);
        connect(m_parameterWorker, &LightingParameterWorker::connectionFailed,
                this, &LightingControlWidget::onWorkerConnectionFailed, Qt::QueuedConnection);
        connect(m_parameterWorker, &LightingParameterWorker::disconnectionComplete,
                this, &LightingControlWidget::onWorkerDisconnectionComplete, Qt::QueuedConnection);
        connect(m_parameterWorker, &LightingParameterWorker::statusUpdated,
                this, &LightingControlWidget::onWorkerStatusUpdated, Qt::QueuedConnection);
        connect(m_parameterWorker, &LightingParameterWorker::allOffComplete,
                this, &LightingControlWidget::onWorkerAllOffComplete, Qt::QueuedConnection);
        connect(m_parameterWorker, &LightingParameterWorker::commandError,
                this, &LightingControlWidget::onWorkerCommandError, Qt::QueuedConnection);

        // Set current device on worker
        if (!m_currentDeviceId.empty()) {
            m_parameterWorker->setCurrentDevice(m_currentDeviceId);
        }

        // Start the worker thread
        m_parameterWorker->start();
    }
}

LightingControlWidget::~LightingControlWidget()
{
    m_pollTimer->stop();

    // Stop the parameter worker thread
    if (m_parameterWorker) {
        m_parameterWorker->stop();
        delete m_parameterWorker;
        m_parameterWorker = nullptr;
    }
}

void LightingControlWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateStatus();

    // Enable worker polling when visible
    if (m_parameterWorker) {
        m_parameterWorker->setStatusPollingEnabled(true);
        // Request initial status refresh
        if (m_isConnected) {
            m_parameterWorker->requestRefreshStatus();
        }
    }

    m_pollTimer->start(500);  // Poll every 500ms for connection status
}

void LightingControlWidget::hideEvent(QHideEvent* event)
{
    m_pollTimer->stop();

    // Disable worker polling when hidden
    if (m_parameterWorker) {
        m_parameterWorker->setStatusPollingEnabled(false);
    }

    QWidget::hideEvent(event);
}

void LightingControlWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(15);

    createHeaderSection();
    createPresetsSection();
    createChannelWidgets();
}

void LightingControlWidget::createHeaderSection()
{
    QGroupBox* headerGroup = new QGroupBox("Lighting Controller");
    headerGroup->setStyleSheet(
        "QGroupBox {"
        "  font-weight: bold;"
        "  font-size: 14px;"
        "  color: white;"
        "  border: 1px solid #555;"
        "  border-radius: 5px;"
        "  margin-top: 10px;"
        "  padding-top: 10px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 10px;"
        "  padding: 0 5px;"
        "}"
    );

    QVBoxLayout* headerLayout = new QVBoxLayout(headerGroup);
    headerLayout->setSpacing(10);

    // Device info row
    QHBoxLayout* infoRow = new QHBoxLayout();
    m_deviceInfo = new QLabel("No device configured");
    m_deviceInfo->setStyleSheet("color: #aaa; font-size: 13px;");
    infoRow->addWidget(m_deviceInfo);

    m_connectionStatus = new QLabel("Disconnected");
    m_connectionStatus->setStyleSheet(
        "color: #ff6666; font-weight: bold; font-size: 13px;"
    );
    infoRow->addWidget(m_connectionStatus);
    infoRow->addStretch();
    headerLayout->addLayout(infoRow);

    // Button row
    QHBoxLayout* buttonRow = new QHBoxLayout();

    m_connectButton = new QPushButton("Connect");
    m_connectButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #00aa00;"
        "  color: white;"
        "  padding: 8px 20px;"
        "  font-weight: bold;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover { background-color: #00cc00; }"
        "QPushButton:disabled { background-color: #444; color: #888; }"
    );
    connect(m_connectButton, &QPushButton::clicked, this, &LightingControlWidget::onConnectClicked);
    buttonRow->addWidget(m_connectButton);

    m_disconnectButton = new QPushButton("Disconnect");
    m_disconnectButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #aa4444;"
        "  color: white;"
        "  padding: 8px 20px;"
        "  font-weight: bold;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover { background-color: #cc5555; }"
        "QPushButton:disabled { background-color: #444; color: #888; }"
    );
    connect(m_disconnectButton, &QPushButton::clicked, this, &LightingControlWidget::onDisconnectClicked);
    buttonRow->addWidget(m_disconnectButton);

    buttonRow->addSpacing(20);

    m_allOffButton = new QPushButton("All OFF");
    m_allOffButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #aa6600;"
        "  color: white;"
        "  padding: 8px 20px;"
        "  font-weight: bold;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover { background-color: #cc7700; }"
        "QPushButton:disabled { background-color: #444; color: #888; }"
    );
    connect(m_allOffButton, &QPushButton::clicked, this, &LightingControlWidget::onAllOffClicked);
    buttonRow->addWidget(m_allOffButton);

    m_refreshButton = new QPushButton("Refresh");
    m_refreshButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #0066aa;"
        "  color: white;"
        "  padding: 8px 20px;"
        "  font-weight: bold;"
        "  border: none;"
        "  border-radius: 4px;"
        "}"
        "QPushButton:hover { background-color: #0088cc; }"
        "QPushButton:disabled { background-color: #444; color: #888; }"
    );
    connect(m_refreshButton, &QPushButton::clicked, this, &LightingControlWidget::onRefreshClicked);
    buttonRow->addWidget(m_refreshButton);

    buttonRow->addStretch();
    headerLayout->addLayout(buttonRow);

    QVBoxLayout* parentLayout = qobject_cast<QVBoxLayout*>(layout());
    if (parentLayout) {
        parentLayout->addWidget(headerGroup);
    }
}

void LightingControlWidget::createPresetsSection()
{
    QGroupBox* presetsGroup = new QGroupBox("Presets");
    presetsGroup->setStyleSheet(
        "QGroupBox {"
        "  font-weight: bold;"
        "  font-size: 14px;"
        "  color: white;"
        "  border: 1px solid #555;"
        "  border-radius: 5px;"
        "  margin-top: 10px;"
        "  padding-top: 10px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 10px;"
        "  padding: 0 5px;"
        "}"
    );

    QVBoxLayout* presetsLayout = new QVBoxLayout(presetsGroup);
    presetsLayout->setSpacing(10);

    // Row 1: Intensity presets (0% - 100% in 10% increments)
    QLabel* intensityLabel = new QLabel("Intensity Presets:");
    intensityLabel->setStyleSheet("color: #aaa; font-size: 12px; font-weight: normal;");
    presetsLayout->addWidget(intensityLabel);

    QHBoxLayout* intensityRow = new QHBoxLayout();
    intensityRow->setSpacing(5);

    QString presetBtnStyle =
        "QPushButton {"
        "  background-color: #3a3a3a;"
        "  color: white;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  border: 1px solid #555;"
        "  border-radius: 3px;"
        "  padding: 4px 8px;"
        "  min-width: 35px;"
        "}"
        "QPushButton:hover { background-color: #4a4a4a; border-color: #00aaff; }"
        "QPushButton:pressed { background-color: #2a2a2a; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }";

    for (int percent = 0; percent <= 100; percent += 10) {
        QPushButton* btn = new QPushButton(QString("%1%").arg(percent));
        btn->setStyleSheet(presetBtnStyle);
        btn->setProperty("presetPercent", percent);
        connect(btn, &QPushButton::clicked, this, [this, percent]() {
            onPresetClicked(percent);
        });
        intensityRow->addWidget(btn);
    }
    intensityRow->addStretch();
    presetsLayout->addLayout(intensityRow);

    // Row 2: Color presets (Collimate, Red, Green, Blue)
    QLabel* colorLabel = new QLabel("Color Presets:");
    colorLabel->setStyleSheet("color: #aaa; font-size: 12px; font-weight: normal;");
    presetsLayout->addWidget(colorLabel);

    QHBoxLayout* colorRow = new QHBoxLayout();
    colorRow->setSpacing(8);

    // Collimate Light preset
    QPushButton* collimateBtn = new QPushButton("Collimate");
    collimateBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #4a4a4a;"
        "  color: white;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  border: 1px solid #888;"
        "  border-radius: 4px;"
        "  padding: 6px 15px;"
        "}"
        "QPushButton:hover { background-color: #5a5a5a; border-color: #aaa; }"
        "QPushButton:pressed { background-color: #3a3a3a; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }"
    );
    connect(collimateBtn, &QPushButton::clicked, this, &LightingControlWidget::onCollimatePreset);
    colorRow->addWidget(collimateBtn);

    // Red preset
    QPushButton* redBtn = new QPushButton("Red");
    redBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #8a2a2a;"
        "  color: white;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  border: 1px solid #aa4444;"
        "  border-radius: 4px;"
        "  padding: 6px 15px;"
        "}"
        "QPushButton:hover { background-color: #aa3a3a; }"
        "QPushButton:pressed { background-color: #6a1a1a; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }"
    );
    connect(redBtn, &QPushButton::clicked, this, &LightingControlWidget::onRedPreset);
    colorRow->addWidget(redBtn);

    // Green preset
    QPushButton* greenBtn = new QPushButton("Green");
    greenBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #2a6a2a;"
        "  color: white;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  border: 1px solid #44aa44;"
        "  border-radius: 4px;"
        "  padding: 6px 15px;"
        "}"
        "QPushButton:hover { background-color: #3a8a3a; }"
        "QPushButton:pressed { background-color: #1a4a1a; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }"
    );
    connect(greenBtn, &QPushButton::clicked, this, &LightingControlWidget::onGreenPreset);
    colorRow->addWidget(greenBtn);

    // Blue preset
    QPushButton* blueBtn = new QPushButton("Blue");
    blueBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #2a4a8a;"
        "  color: white;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  border: 1px solid #4466aa;"
        "  border-radius: 4px;"
        "  padding: 6px 15px;"
        "}"
        "QPushButton:hover { background-color: #3a5aaa; }"
        "QPushButton:pressed { background-color: #1a3a6a; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }"
    );
    connect(blueBtn, &QPushButton::clicked, this, &LightingControlWidget::onBluePreset);
    colorRow->addWidget(blueBtn);

    colorRow->addStretch();
    presetsLayout->addLayout(colorRow);

    QVBoxLayout* parentLayout = qobject_cast<QVBoxLayout*>(layout());
    if (parentLayout) {
        parentLayout->addWidget(presetsGroup);
    }
}

void LightingControlWidget::createChannelWidgets()
{
    QGroupBox* channelsGroup = new QGroupBox("Channels");
    channelsGroup->setStyleSheet(
        "QGroupBox {"
        "  font-weight: bold;"
        "  font-size: 14px;"
        "  color: white;"
        "  border: 1px solid #555;"
        "  border-radius: 5px;"
        "  margin-top: 10px;"
        "  padding-top: 10px;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 10px;"
        "  padding: 0 5px;"
        "}"
    );

    QVBoxLayout* groupLayout = new QVBoxLayout(channelsGroup);

    // Scroll area for channels
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; }");

    m_channelsContainer = new QWidget();
    m_channelsContainer->setStyleSheet("background: transparent;");
    m_channelsLayout = new QVBoxLayout(m_channelsContainer);
    m_channelsLayout->setContentsMargins(0, 0, 0, 0);
    m_channelsLayout->setSpacing(5);

    // Create channel widgets from config (only enabled channels)
    if (m_lightingManager && !m_currentDeviceId.empty()) {
        auto channels = m_lightingManager->GetChannelConfigs(m_currentDeviceId);

        // Filter to only enabled channels
        std::vector<Lighting::ChannelConfig> enabledChannels;
        for (const auto& ch : channels) {
            if (ch.enabled) {
                enabledChannels.push_back(ch);
            }
        }

        for (size_t i = 0; i < enabledChannels.size(); ++i) {
            const auto& ch = enabledChannels[i];
            auto* widget = new LightingChannelWidget(
                ch.channelNumber,
                QString::fromStdString(ch.name),
                QString::fromStdString(ch.color),
                this
            );
            // Store color as property for preset functionality
            widget->setProperty("channelColor", QString::fromStdString(ch.color));
            connect(widget, &LightingChannelWidget::intensityChanged,
                    this, &LightingControlWidget::onChannelIntensityChanged);
            connect(widget, &LightingChannelWidget::toggleChanged,
                    this, &LightingControlWidget::onChannelToggleChanged);

            m_channelWidgets.push_back(widget);
            m_channelsLayout->addWidget(widget);

            // Add separator line between channels (not after last one)
            if (i < enabledChannels.size() - 1) {
                QFrame* line = new QFrame();
                line->setFrameShape(QFrame::HLine);
                line->setStyleSheet("background-color: #3a3a3a;");
                line->setFixedHeight(1);
                m_channelsLayout->addWidget(line);
            }
        }
    } else {
        // Default 8 channels if no config
        for (int i = 0; i < 8; ++i) {
            auto* widget = new LightingChannelWidget(
                i,
                QString("L%1").arg(i + 1),
                "",
                this
            );
            connect(widget, &LightingChannelWidget::intensityChanged,
                    this, &LightingControlWidget::onChannelIntensityChanged);
            connect(widget, &LightingChannelWidget::toggleChanged,
                    this, &LightingControlWidget::onChannelToggleChanged);

            m_channelWidgets.push_back(widget);
            m_channelsLayout->addWidget(widget);

            if (i < 7) {
                QFrame* line = new QFrame();
                line->setFrameShape(QFrame::HLine);
                line->setStyleSheet("background-color: #3a3a3a;");
                line->setFixedHeight(1);
                m_channelsLayout->addWidget(line);
            }
        }
    }

    m_channelsLayout->addStretch();
    scrollArea->setWidget(m_channelsContainer);
    groupLayout->addWidget(scrollArea);

    QVBoxLayout* parentLayout = qobject_cast<QVBoxLayout*>(layout());
    if (parentLayout) {
        parentLayout->addWidget(channelsGroup, 1);
    }
}

void LightingControlWidget::updateStatus()
{
    if (!m_lightingManager || m_currentDeviceId.empty()) {
        m_deviceInfo->setText("No lighting controller configured");
        m_connectionStatus->setText("N/A");
        m_connectionStatus->setStyleSheet("color: #888; font-weight: bold; font-size: 13px;");
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(false);
        m_allOffButton->setEnabled(false);
        m_refreshButton->setEnabled(false);
        return;
    }

    const auto* config = m_lightingManager->GetControllerConfig(m_currentDeviceId);
    if (config) {
        m_deviceInfo->setText(QString("%1 (%2:%3)")
            .arg(QString::fromStdString(config->deviceName))
            .arg(QString::fromStdString(config->ipAddress))
            .arg(config->port));
    }

    m_isConnected = m_lightingManager->IsConnected(m_currentDeviceId);

    if (m_isConnected) {
        m_connectionStatus->setText("Connected");
        m_connectionStatus->setStyleSheet(
            "color: #66ff66; font-weight: bold; font-size: 13px;"
        );
        m_connectButton->setEnabled(false);
        m_disconnectButton->setEnabled(true);
        m_allOffButton->setEnabled(true);
        m_refreshButton->setEnabled(true);
    } else {
        m_connectionStatus->setText("Disconnected");
        m_connectionStatus->setStyleSheet(
            "color: #ff6666; font-weight: bold; font-size: 13px;"
        );
        m_connectButton->setEnabled(true);
        m_disconnectButton->setEnabled(false);
        m_allOffButton->setEnabled(false);
        m_refreshButton->setEnabled(false);
    }

    // Enable/disable channel widgets
    for (auto* widget : m_channelWidgets) {
        widget->setConnected(m_isConnected);
    }
}

void LightingControlWidget::updateChannelWidgets()
{
    if (!m_lightingManager || m_currentDeviceId.empty() || !m_isConnected) {
        return;
    }

    auto status = m_lightingManager->GetControllerStatus(m_currentDeviceId);
    for (const auto& chStatus : status.channelStatuses) {
        if (chStatus.channelNumber < static_cast<int>(m_channelWidgets.size())) {
            auto* widget = m_channelWidgets[chStatus.channelNumber];
            widget->setIntensity(chStatus.intensity);
            widget->setOn(chStatus.isOn);
        }
    }
}

void LightingControlWidget::onPollTimer()
{
    updateStatus();
    if (m_isConnected) {
        updateChannelWidgets();
    }
}

void LightingControlWidget::onChannelIntensityChanged(int channel, int intensity)
{
    if (!m_lightingManager || m_currentDeviceId.empty() || !m_isConnected) {
        return;
    }

    // Use parameter worker for non-blocking operation
    if (m_parameterWorker) {
        m_parameterWorker->requestSetIntensity(channel, intensity);
    }
}

void LightingControlWidget::onChannelToggleChanged(int channel, bool on)
{
    if (!m_lightingManager || m_currentDeviceId.empty() || !m_isConnected) {
        return;
    }

    // Use parameter worker for non-blocking operation
    if (m_parameterWorker) {
        m_parameterWorker->requestSetOn(channel, on);
    }
}

void LightingControlWidget::onConnectClicked()
{
    if (!m_lightingManager || m_currentDeviceId.empty()) {
        return;
    }

    if (m_connectionPending) {
        return;  // Already connecting
    }

    std::cout << "[LightingControlWidget] Connecting to " << m_currentDeviceId << std::endl;

    // Disable buttons and show connecting status
    m_connectionPending = true;
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(false);
    m_connectionStatus->setText("Connecting...");
    m_connectionStatus->setStyleSheet("color: #ffaa00; font-weight: bold; font-size: 13px;");

    // Use parameter worker for non-blocking operation
    if (m_parameterWorker) {
        m_parameterWorker->requestConnect(QString::fromStdString(m_currentDeviceId));
    }
}

void LightingControlWidget::onDisconnectClicked()
{
    if (!m_lightingManager || m_currentDeviceId.empty()) {
        return;
    }

    if (m_connectionPending) {
        return;  // Operation in progress
    }

    std::cout << "[LightingControlWidget] Disconnecting from " << m_currentDeviceId << std::endl;

    // Disable buttons and show disconnecting status
    m_connectionPending = true;
    m_connectButton->setEnabled(false);
    m_disconnectButton->setEnabled(false);
    m_connectionStatus->setText("Disconnecting...");
    m_connectionStatus->setStyleSheet("color: #ffaa00; font-weight: bold; font-size: 13px;");

    // Use parameter worker for non-blocking operation
    if (m_parameterWorker) {
        m_parameterWorker->requestDisconnect(QString::fromStdString(m_currentDeviceId));
    }
}

void LightingControlWidget::onAllOffClicked()
{
    if (!m_lightingManager || m_currentDeviceId.empty() || !m_isConnected) {
        return;
    }

    std::cout << "[LightingControlWidget] Turning all channels OFF" << std::endl;

    // Use parameter worker for non-blocking operation
    if (m_parameterWorker) {
        m_parameterWorker->requestAllOff();
    }
}

void LightingControlWidget::onRefreshClicked()
{
    updateStatus();
    // Use parameter worker for non-blocking status refresh
    if (m_parameterWorker && m_isConnected) {
        m_parameterWorker->requestRefreshStatus();
    }
}

// ============================================================================
// Preset Implementations
// ============================================================================

void LightingControlWidget::onPresetClicked(int percent)
{
    if (!m_isConnected) return;

    int intensity = Lighting::PercentToIntensity(static_cast<double>(percent));
    setAllChannelsIntensity(intensity);

    std::cout << "[LightingControlWidget] Applied preset: " << percent << "% (" << intensity << ")" << std::endl;
}

void LightingControlWidget::onCollimatePreset()
{
    // Collimate light - typically white/neutral, set all channels to same value
    setColorPreset("white", 50);  // 50% intensity for all channels
    std::cout << "[LightingControlWidget] Applied Collimate preset (50% all)" << std::endl;
}

void LightingControlWidget::onRedPreset()
{
    setColorPreset("red", 100);  // Full red
    std::cout << "[LightingControlWidget] Applied Red preset" << std::endl;
}

void LightingControlWidget::onGreenPreset()
{
    setColorPreset("green", 100);  // Full green
    std::cout << "[LightingControlWidget] Applied Green preset" << std::endl;
}

void LightingControlWidget::onBluePreset()
{
    setColorPreset("blue", 100);  // Full blue
    std::cout << "[LightingControlWidget] Applied Blue preset" << std::endl;
}

void LightingControlWidget::setAllChannelsIntensity(int intensity)
{
    if (!m_parameterWorker || !m_isConnected) return;

    for (auto* widget : m_channelWidgets) {
        int channel = widget->channelNumber();
        m_parameterWorker->requestSetIntensity(channel, intensity);
        widget->setIntensity(intensity);
    }
}

void LightingControlWidget::setColorPreset(const QString& color, int percentIntensity)
{
    if (!m_parameterWorker || !m_isConnected) return;

    QString targetColor = color.toLower();
    int intensity = Lighting::PercentToIntensity(static_cast<double>(percentIntensity));

    for (auto* widget : m_channelWidgets) {
        int channel = widget->channelNumber();
        QString channelColor = widget->property("channelColor").toString().toLower();

        // If no color property, try to get from channel name
        if (channelColor.isEmpty()) {
            QString name = widget->channelName().toLower();
            if (name.contains("red")) channelColor = "red";
            else if (name.contains("green")) channelColor = "green";
            else if (name.contains("blue")) channelColor = "blue";
            else if (name.contains("white")) channelColor = "white";
        }

        int targetIntensity = 0;

        if (targetColor == "white") {
            // White/Collimate - set all channels to same intensity
            targetIntensity = intensity;
        } else if (channelColor == targetColor) {
            // This channel matches the target color - full intensity
            targetIntensity = intensity;
        } else {
            // Different color - turn off
            targetIntensity = 0;
        }

        m_parameterWorker->requestSetIntensity(channel, targetIntensity);
        widget->setIntensity(targetIntensity);
    }
}

// ============================================================================
// Worker Callback Implementations
// ============================================================================

void LightingControlWidget::onWorkerConnectionSuccess(QString deviceId)
{
    std::cout << "[LightingControlWidget] Connection success: " << deviceId.toStdString() << std::endl;

    m_connectionPending = false;

    if (deviceId.toStdString() == m_currentDeviceId) {
        updateStatus();
        // Request status refresh to get current channel states
        if (m_parameterWorker) {
            m_parameterWorker->requestRefreshStatus();
        }
    }
}

void LightingControlWidget::onWorkerConnectionFailed(QString deviceId, QString error)
{
    std::cerr << "[LightingControlWidget] Connection failed: " << deviceId.toStdString()
              << " - " << error.toStdString() << std::endl;

    m_connectionPending = false;

    if (deviceId.toStdString() == m_currentDeviceId) {
        m_connectionStatus->setText("Connection Failed");
        m_connectionStatus->setStyleSheet("color: #ff6666; font-weight: bold; font-size: 13px;");
        updateStatus();
    }
}

void LightingControlWidget::onWorkerDisconnectionComplete(QString deviceId)
{
    std::cout << "[LightingControlWidget] Disconnection complete: " << deviceId.toStdString() << std::endl;

    m_connectionPending = false;

    if (deviceId.toStdString() == m_currentDeviceId) {
        updateStatus();
    }
}

void LightingControlWidget::onWorkerStatusUpdated(QString deviceId, std::vector<int> intensities, std::vector<bool> onStates)
{
    if (deviceId.toStdString() != m_currentDeviceId) {
        return;
    }

    // Update channel widgets with new status
    for (size_t i = 0; i < m_channelWidgets.size() && i < intensities.size(); ++i) {
        auto* widget = m_channelWidgets[i];
        int channelNum = widget->channelNumber();

        // Find matching channel in status arrays
        if (channelNum < static_cast<int>(intensities.size())) {
            widget->setIntensity(intensities[channelNum]);
        }
        if (channelNum < static_cast<int>(onStates.size())) {
            widget->setOn(onStates[channelNum]);
        }
    }
}

void LightingControlWidget::onWorkerAllOffComplete()
{
    std::cout << "[LightingControlWidget] All OFF complete" << std::endl;

    // Request status refresh to update UI
    if (m_parameterWorker) {
        m_parameterWorker->requestRefreshStatus();
    }
}

void LightingControlWidget::onWorkerCommandError(QString operation, QString error)
{
    std::cerr << "[LightingControlWidget] Command error - " << operation.toStdString()
              << ": " << error.toStdString() << std::endl;
}

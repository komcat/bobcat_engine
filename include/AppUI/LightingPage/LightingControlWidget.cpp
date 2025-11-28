// LightingControlWidget.cpp - Lighting channel control widget implementation
#include "LightingControlWidget.h"
#include "ApplicationContext.h"
#include "lighting/LightingManager.h"
#include "lighting/LightingTypes.h"
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
{
    setupUI();
}

void LightingChannelWidget::setupUI()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(10, 5, 10, 5);
    mainLayout->setSpacing(15);

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

    // Intensity slider
    m_intensitySlider = new QSlider(Qt::Horizontal);
    m_intensitySlider->setRange(0, 255);
    m_intensitySlider->setValue(0);
    m_intensitySlider->setMinimumWidth(200);
    m_intensitySlider->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  background: #3a3a3a;"
        "  height: 8px;"
        "  border-radius: 4px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: #00aaff;"
        "  width: 18px;"
        "  margin: -5px 0;"
        "  border-radius: 9px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0066aa, stop:1 #00aaff);"
        "  border-radius: 4px;"
        "}"
        "QSlider::groove:horizontal:disabled { background: #2a2a2a; }"
        "QSlider::handle:horizontal:disabled { background: #555; }"
    );
    connect(m_intensitySlider, &QSlider::valueChanged, this, &LightingChannelWidget::onSliderChanged);
    mainLayout->addWidget(m_intensitySlider, 1);

    // Intensity value display
    m_intensityValue = new QLabel("0");
    m_intensityValue->setFixedWidth(35);
    m_intensityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_intensityValue->setStyleSheet("font-size: 13px; color: #00aaff; font-weight: bold;");
    mainLayout->addWidget(m_intensityValue);

    // Percent display
    m_percentLabel = new QLabel("(0%)");
    m_percentLabel->setFixedWidth(55);
    m_percentLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_percentLabel->setStyleSheet("font-size: 12px; color: #888;");
    mainLayout->addWidget(m_percentLabel);

    // Toggle button
    m_toggleButton = new QPushButton("OFF");
    m_toggleButton->setFixedSize(70, 30);
    m_toggleButton->setCheckable(true);
    updateButtonState();
    connect(m_toggleButton, &QPushButton::clicked, this, &LightingChannelWidget::onToggleClicked);
    mainLayout->addWidget(m_toggleButton);

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
    m_intensitySlider->setValue(intensity);
    m_intensityValue->setText(QString::number(intensity));
    double percent = Lighting::IntensityToPercent(intensity);
    m_percentLabel->setText(QString("(%1%)").arg(QString::number(percent, 'f', 1)));
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

void LightingChannelWidget::onSliderChanged(int value)
{
    if (m_isUpdating) return;

    m_intensityValue->setText(QString::number(value));
    double percent = Lighting::IntensityToPercent(value);
    m_percentLabel->setText(QString("(%1%)").arg(QString::number(percent, 'f', 1)));

    emit intensityChanged(m_channelNumber, value);
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
}

LightingControlWidget::~LightingControlWidget()
{
    m_pollTimer->stop();
}

void LightingControlWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateStatus();
    updateChannelWidgets();
    m_pollTimer->start(500);  // Poll every 500ms
}

void LightingControlWidget::hideEvent(QHideEvent* event)
{
    m_pollTimer->stop();
    QWidget::hideEvent(event);
}

void LightingControlWidget::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(15);

    createHeaderSection();
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

    auto result = m_lightingManager->SetIntensity(m_currentDeviceId, channel, intensity);
    if (result != Lighting::LightingResult::Success) {
        std::cerr << "[LightingControlWidget] Failed to set intensity for channel "
                  << channel << ": " << Lighting::LightingResultToString(result) << std::endl;
    }
}

void LightingControlWidget::onChannelToggleChanged(int channel, bool on)
{
    if (!m_lightingManager || m_currentDeviceId.empty() || !m_isConnected) {
        return;
    }

    auto result = m_lightingManager->SetOn(m_currentDeviceId, channel, on);
    if (result != Lighting::LightingResult::Success) {
        std::cerr << "[LightingControlWidget] Failed to toggle channel "
                  << channel << ": " << Lighting::LightingResultToString(result) << std::endl;
    }
}

void LightingControlWidget::onConnectClicked()
{
    if (!m_lightingManager || m_currentDeviceId.empty()) {
        return;
    }

    std::cout << "[LightingControlWidget] Connecting to " << m_currentDeviceId << std::endl;
    bool success = m_lightingManager->Connect(m_currentDeviceId);
    if (success) {
        std::cout << "[LightingControlWidget] Connected successfully" << std::endl;
    } else {
        std::cerr << "[LightingControlWidget] Connection failed" << std::endl;
    }

    updateStatus();
    updateChannelWidgets();
}

void LightingControlWidget::onDisconnectClicked()
{
    if (!m_lightingManager || m_currentDeviceId.empty()) {
        return;
    }

    std::cout << "[LightingControlWidget] Disconnecting from " << m_currentDeviceId << std::endl;
    m_lightingManager->Disconnect(m_currentDeviceId);
    updateStatus();
}

void LightingControlWidget::onAllOffClicked()
{
    if (!m_lightingManager || m_currentDeviceId.empty() || !m_isConnected) {
        return;
    }

    std::cout << "[LightingControlWidget] Turning all channels OFF" << std::endl;
    m_lightingManager->SetAllOff(m_currentDeviceId);
    updateChannelWidgets();
}

void LightingControlWidget::onRefreshClicked()
{
    updateStatus();
    updateChannelWidgets();
}

// LightingControlWidget.cpp - Lighting channel control widget implementation
#include "LightingControlWidget.h"
#include "ApplicationContext.h"
#include "lighting/LightingManager.h"
#include "lighting/LightingTypes.h"
#include "lighting/LightingParameterWorker.h"
#include "safety/SafetyWatchManager.h"
#include <QScrollArea>
#include <QGridLayout>
#include <QFrame>
#include <QDateTime>
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
    , m_lastUserChangeTime(0)
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

void LightingChannelWidget::setIntensity(int intensity, bool fromPoll)
{
    // Skip poll updates during suppression window after user input
    if (fromPoll) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastUserChangeTime < POLL_SUPPRESS_MS) {
            return;
        }
    }

    m_isUpdating = true;
    m_currentIntensity = intensity;
    m_intensityValue->setText(QString("(%1)").arg(intensity));
    double percent = Lighting::IntensityToPercent(intensity);
    m_percentInput->setText(QString::number(percent, 'f', 1));
    m_isUpdating = false;
}

void LightingChannelWidget::setOn(bool on, bool fromPoll)
{
    // Skip poll updates during suppression window after user input
    if (fromPoll) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastUserChangeTime < POLL_SUPPRESS_MS) {
            return;
        }
    }

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
    m_lastUserChangeTime = QDateTime::currentMSecsSinceEpoch();
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
    m_lastUserChangeTime = QDateTime::currentMSecsSinceEpoch();
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

    // Resume polling when E-Stop is cleared (if page is visible)
    if (m_context && m_context->safetyWatchManager) {
        connect(m_context->safetyWatchManager, &SafetyWatchManager::safetyCleared,
                this, [this]() {
            if (isVisible() && m_pollTimer && !m_pollTimer->isActive()) {
                if (m_parameterWorker) {
                    m_parameterWorker->setStatusPollingEnabled(true);
                }
                m_pollTimer->start(500);
            }
        });
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

    // Don't start polling during E-Stop - hardware may be unresponsive
    if (m_context && m_context->safetyWatchManager && m_context->safetyWatchManager->IsCriticalFlagRaised()) {
        return;
    }

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
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Header spans full width
    mainLayout->addWidget(createHeaderSection());

    // Two-column layout: Presets (left) | Channels (right)
    QHBoxLayout* columnsLayout = new QHBoxLayout();
    columnsLayout->setSpacing(10);

    // Left column: Presets (top-aligned)
    QVBoxLayout* leftColumn = new QVBoxLayout();
    leftColumn->addWidget(createPresetsSection());
    leftColumn->addStretch();
    columnsLayout->addLayout(leftColumn, 4);

    // Right column: Channels (fills height)
    columnsLayout->addWidget(createChannelWidgets(), 6);

    mainLayout->addLayout(columnsLayout, 1);
}

QWidget* LightingControlWidget::createHeaderSection()
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

    return headerGroup;
}

QWidget* LightingControlWidget::createPresetsSection()
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

    m_colorButtons.clear();

    // Helper to get button style colors from a color key
    auto getButtonColors = [](const QString& colorKey,
                              QString& bg, QString& hover, QString& pressed, QString& border) {
        if (colorKey.contains("red")) {
            bg = "#8a2a2a"; hover = "#aa3a3a"; pressed = "#6a1a1a"; border = "#aa4444";
        } else if (colorKey.contains("green")) {
            bg = "#2a6a2a"; hover = "#3a8a3a"; pressed = "#1a4a1a"; border = "#44aa44";
        } else if (colorKey.contains("blue")) {
            bg = "#2a4a8a"; hover = "#3a5aaa"; pressed = "#1a3a6a"; border = "#4466aa";
        } else if (colorKey.contains("white") || colorKey.contains("collimate")) {
            bg = "#5a5a5a"; hover = "#6a6a6a"; pressed = "#4a4a4a"; border = "#999";
        } else {
            bg = "#4a4a4a"; hover = "#5a5a5a"; pressed = "#3a3a3a"; border = "#888";
        }
    };

    // Build color buttons grouped by physical light group
    if (m_lightingManager && !m_currentDeviceId.empty()) {
        auto groups = m_lightingManager->GetChannelGroups(m_currentDeviceId);

        for (const auto& groupName : groups) {
            auto groupChannels = m_lightingManager->GetChannelsByGroup(m_currentDeviceId, groupName);
            if (groupChannels.empty()) continue;

            // Group label
            QString groupLabel = QString::fromStdString(groupName);
            QLabel* label = new QLabel(groupLabel + ":");
            label->setStyleSheet("color: #ccc; font-size: 12px; font-weight: bold;");
            presetsLayout->addWidget(label);

            // Color buttons for this group
            QHBoxLayout* colorRow = new QHBoxLayout();
            colorRow->setSpacing(8);

            // Collect unique colors within this group
            std::vector<std::pair<QString, QString>> uniqueColors; // {colorKey, displayName}
            for (const auto& ch : groupChannels) {
                QString colorKey = QString::fromStdString(ch.color).toLower().trimmed();
                if (colorKey.isEmpty()) {
                    colorKey = QString::fromStdString(ch.name).toLower().trimmed();
                }

                bool dup = false;
                for (const auto& uc : uniqueColors) {
                    if (uc.first == colorKey) { dup = true; break; }
                }
                if (!dup) {
                    uniqueColors.push_back({colorKey, QString::fromStdString(ch.name)});
                }
            }

            for (const auto& [colorKey, displayName] : uniqueColors) {
                QString bg, hover, pressed, border;
                getButtonColors(colorKey, bg, hover, pressed, border);

                QPushButton* btn = new QPushButton(displayName);
                btn->setCheckable(true);
                btn->setProperty("colorName", colorKey);
                btn->setProperty("groupName", QString::fromStdString(groupName));
                btn->setStyleSheet(
                    QString(
                        "QPushButton {"
                        "  background-color: %1;"
                        "  color: white;"
                        "  font-size: 12px;"
                        "  font-weight: bold;"
                        "  border: 1px solid %4;"
                        "  border-radius: 4px;"
                        "  padding: 6px 15px;"
                        "}"
                        "QPushButton:hover { background-color: %2; }"
                        "QPushButton:pressed { background-color: %3; }"
                        "QPushButton:checked {"
                        "  border: 2px solid #00aaff;"
                        "  background-color: %2;"
                        "}"
                        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }"
                    ).arg(bg, hover, pressed, border)
                );

                QString grp = QString::fromStdString(groupName);
                connect(btn, &QPushButton::clicked, this, [this, grp, colorKey]() {
                    onColorSelected(grp, colorKey);
                });
                colorRow->addWidget(btn);
                m_colorButtons.push_back(btn);
            }

            colorRow->addStretch();
            presetsLayout->addLayout(colorRow);
        }
    }

    // Intensity presets in 2-column grid (apply to active color in active group)
    QLabel* intensityLabel = new QLabel("Intensity (applies to active color):");
    intensityLabel->setStyleSheet("color: #aaa; font-size: 12px; font-weight: normal;");
    presetsLayout->addWidget(intensityLabel);

    QGridLayout* intensityGrid = new QGridLayout();
    intensityGrid->setSpacing(5);

    QString presetBtnStyle =
        "QPushButton {"
        "  background-color: #3a3a3a;"
        "  color: white;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "  border: 1px solid #555;"
        "  border-radius: 3px;"
        "  padding: 6px 4px;"
        "}"
        "QPushButton:hover { background-color: #4a4a4a; border-color: #00aaff; }"
        "QPushButton:pressed { background-color: #2a2a2a; }"
        "QPushButton:disabled { background-color: #2a2a2a; color: #555; }";

    int idx = 0;
    for (int percent = 0; percent <= 100; percent += 10) {
        QPushButton* btn = new QPushButton(QString("%1%").arg(percent));
        btn->setStyleSheet(presetBtnStyle);
        connect(btn, &QPushButton::clicked, this, [this, percent]() {
            onPresetClicked(percent);
        });
        intensityGrid->addWidget(btn, idx / 2, idx % 2);
        idx++;
    }
    presetsLayout->addLayout(intensityGrid);

    return presetsGroup;
}

QWidget* LightingControlWidget::createChannelWidgets()
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

    // Create channel widgets grouped by physical light group
    if (m_lightingManager && !m_currentDeviceId.empty()) {
        auto groups = m_lightingManager->GetChannelGroups(m_currentDeviceId);

        for (size_t gi = 0; gi < groups.size(); ++gi) {
            const auto& groupName = groups[gi];
            auto groupChannels = m_lightingManager->GetChannelsByGroup(m_currentDeviceId, groupName);
            if (groupChannels.empty()) continue;

            // Group header label
            QLabel* groupHeader = new QLabel(QString::fromStdString(groupName));
            groupHeader->setStyleSheet(
                "font-weight: bold; font-size: 13px; color: #00aaff;"
                "padding: 6px 5px 2px 5px;"
            );
            m_channelsLayout->addWidget(groupHeader);

            // Channel widgets for this group
            for (size_t ci = 0; ci < groupChannels.size(); ++ci) {
                const auto& ch = groupChannels[ci];
                auto* widget = new LightingChannelWidget(
                    ch.channelNumber,
                    QString::fromStdString(ch.name),
                    QString::fromStdString(ch.color),
                    this
                );
                widget->setProperty("channelColor", QString::fromStdString(ch.color));
                widget->setProperty("channelGroup", QString::fromStdString(groupName));
                connect(widget, &LightingChannelWidget::intensityChanged,
                        this, &LightingControlWidget::onChannelIntensityChanged);
                connect(widget, &LightingChannelWidget::toggleChanged,
                        this, &LightingControlWidget::onChannelToggleChanged);

                m_channelWidgets.push_back(widget);
                m_channelsLayout->addWidget(widget);

                // Separator between channels within group (not after last)
                if (ci < groupChannels.size() - 1) {
                    QFrame* line = new QFrame();
                    line->setFrameShape(QFrame::HLine);
                    line->setStyleSheet("background-color: #3a3a3a;");
                    line->setFixedHeight(1);
                    m_channelsLayout->addWidget(line);
                }
            }

            // Thicker separator between groups (not after last group)
            if (gi < groups.size() - 1) {
                QFrame* groupSep = new QFrame();
                groupSep->setFrameShape(QFrame::HLine);
                groupSep->setStyleSheet("background-color: #555;");
                groupSep->setFixedHeight(2);
                m_channelsLayout->addWidget(groupSep);
            }
        }
    } else {
        // Default 8 channels if no config (single default group)
        for (int i = 0; i < 8; ++i) {
            auto* widget = new LightingChannelWidget(
                i,
                QString("L%1").arg(i + 1),
                "",
                this
            );
            widget->setProperty("channelGroup", "default");
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

    return channelsGroup;
}

void LightingControlWidget::updateStatus()
{
    // The poll timer calls this every 500 ms. Diff against the cached visual
    // state so we don't re-run setStyleSheet (full QSS recompute) and a fan
    // of setEnabled calls on every tick. setText is also no-op'd when the
    // string hasn't changed.
    const int newStatus =
        (!m_lightingManager || m_currentDeviceId.empty()) ? 0
        : (m_lightingManager->IsConnected(m_currentDeviceId) ? 1 : 2);

    if (newStatus == 0) {
        if (m_lastRenderedStatus != 0) {
            m_deviceInfo->setText("No lighting controller configured");
            m_connectionStatus->setText("N/A");
            m_connectionStatus->setStyleSheet("color: #888; font-weight: bold; font-size: 13px;");
            m_connectButton->setEnabled(false);
            m_disconnectButton->setEnabled(false);
            m_allOffButton->setEnabled(false);
            m_refreshButton->setEnabled(false);
            m_lastRenderedStatus = 0;
            m_lastDeviceInfoText.clear();
        }
        return;
    }

    const auto* config = m_lightingManager->GetControllerConfig(m_currentDeviceId);
    if (config) {
        QString infoText = QString("%1 (%2:%3)")
            .arg(QString::fromStdString(config->deviceName))
            .arg(QString::fromStdString(config->ipAddress))
            .arg(config->port);
        if (infoText != m_lastDeviceInfoText) {
            m_deviceInfo->setText(infoText);
            m_lastDeviceInfoText = infoText;
        }
    }

    m_isConnected = (newStatus == 1);

    if (newStatus != m_lastRenderedStatus) {
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
        // Enable/disable channel widgets only on actual transitions —
        // setConnected unconditionally calls setEnabled(), which Qt no-ops
        // when unchanged, but we save the iteration overhead here too.
        for (auto* widget : m_channelWidgets) {
            widget->setConnected(m_isConnected);
        }
        m_lastRenderedStatus = newStatus;
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
    // Skip hardware access during E-Stop
    if (m_context && m_context->safetyWatchManager && m_context->safetyWatchManager->IsCriticalFlagRaised()) {
        m_pollTimer->stop();
        if (m_parameterWorker) {
            m_parameterWorker->setStatusPollingEnabled(false);
        }
        return;
    }

    // Only update connection status UI (non-blocking config/flag checks)
    // Channel widget updates are handled by the worker's statusUpdated signal -> onWorkerStatusUpdated
    updateStatus();
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
// Preset & Color Selector Implementations
// ============================================================================

QString LightingControlWidget::getChannelColor(LightingChannelWidget* widget) const
{
    QString color = widget->property("channelColor").toString().toLower().trimmed();
    if (!color.isEmpty()) {
        return color;
    }
    return widget->channelName().toLower().trimmed();
}

QString LightingControlWidget::getChannelGroup(LightingChannelWidget* widget) const
{
    return widget->property("channelGroup").toString();
}

void LightingControlWidget::onColorSelected(const QString& group, const QString& color)
{
    if (!m_isConnected) return;

    // Toggle: clicking the already-active group+color deselects it
    if (m_activeGroup == group && m_activeColor == color) {
        m_activeGroup.clear();
        m_activeColor.clear();
    } else {
        m_activeGroup = group;
        m_activeColor = color;
    }

    // Update button checked states (only one can be active across all groups)
    for (auto* btn : m_colorButtons) {
        bool match = (btn->property("groupName").toString() == m_activeGroup &&
                      btn->property("colorName").toString() == m_activeColor);
        btn->setChecked(match);
    }

    // If no color selected, do nothing further (just deselect)
    if (m_activeColor.isEmpty()) {
        std::cout << "[LightingControlWidget] Color deselected" << std::endl;
        return;
    }

    // Activate: within the selected group, turn on matching color, turn off others
    activateColor(m_activeGroup, m_activeColor);

    std::cout << "[LightingControlWidget] Active: " << m_activeGroup.toStdString()
              << "." << m_activeColor.toStdString() << std::endl;
}

void LightingControlWidget::activateColor(const QString& group, const QString& color)
{
    if (!m_parameterWorker || !m_isConnected) return;

    for (auto* widget : m_channelWidgets) {
        int channel = widget->channelNumber();
        QString chGroup = getChannelGroup(widget);
        QString chColor = getChannelColor(widget);

        // Only affect channels in the selected group
        if (chGroup != group) continue;

        if (chColor == color) {
            // Matching channel: turn ON, if intensity is 0 set to 50%
            if (widget->currentIntensity() <= 0) {
                int defaultIntensity = Lighting::PercentToIntensity(50.0);
                m_parameterWorker->requestSetIntensity(channel, defaultIntensity);
                widget->setIntensity(defaultIntensity);
            }
            m_parameterWorker->requestSetOn(channel, true);
            widget->setOn(true);
        } else {
            // Non-matching channel in same group: turn OFF
            m_parameterWorker->requestSetIntensity(channel, 0);
            m_parameterWorker->requestSetOn(channel, false);
            widget->setIntensity(0);
            widget->setOn(false);
        }
    }
}

void LightingControlWidget::onPresetClicked(int percent)
{
    if (!m_isConnected) return;

    int intensity = Lighting::PercentToIntensity(static_cast<double>(percent));
    applyIntensityToActiveColor(intensity);

    std::cout << "[LightingControlWidget] Intensity " << percent << "% applied to "
              << (m_activeColor.isEmpty() ? "all" :
                  m_activeGroup.toStdString() + "." + m_activeColor.toStdString()) << std::endl;
}

void LightingControlWidget::applyIntensityToActiveColor(int intensity)
{
    if (!m_parameterWorker || !m_isConnected) return;

    for (auto* widget : m_channelWidgets) {
        int channel = widget->channelNumber();
        QString chColor = getChannelColor(widget);
        QString chGroup = getChannelGroup(widget);

        bool match = false;
        if (m_activeColor.isEmpty()) {
            // No selection: apply to all channels
            match = true;
        } else {
            // Apply only to matching group + color
            match = (chGroup == m_activeGroup && chColor == m_activeColor);
        }

        if (match) {
            m_parameterWorker->requestSetIntensity(channel, intensity);
            widget->setIntensity(intensity);
        }
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

    // Update channel widgets with new status (fromPoll=true to respect suppression)
    for (size_t i = 0; i < m_channelWidgets.size() && i < intensities.size(); ++i) {
        auto* widget = m_channelWidgets[i];
        int channelNum = widget->channelNumber();

        if (channelNum < static_cast<int>(intensities.size())) {
            widget->setIntensity(intensities[channelNum], true);
        }
        if (channelNum < static_cast<int>(onStates.size())) {
            widget->setOn(onStates[channelNum], true);
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

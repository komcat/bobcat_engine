// LightingControlWidget.h - Lighting channel control widget
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QScrollArea>
#include <vector>
#include <map>
#include <string>

struct ApplicationContext;
class LightingParameterWorker;

namespace Lighting {
class LightingManager;
}

/**
 * @brief Widget for a single lighting channel
 */
class LightingChannelWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LightingChannelWidget(int channelNumber,
                                    const QString& channelName,
                                    const QString& color,
                                    QWidget* parent = nullptr);

    void setIntensity(int intensity, bool fromPoll = false);
    void setOn(bool on, bool fromPoll = false);
    void setConnected(bool connected);

    int channelNumber() const { return m_channelNumber; }
    QString channelName() const { return m_channelName; }
    int currentIntensity() const { return m_currentIntensity; }

signals:
    void intensityChanged(int channel, int intensity);
    void toggleChanged(int channel, bool on);

private slots:
    void onApplyClicked();
    void onIncrementClicked();
    void onDecrementClicked();
    void onToggleClicked();
    void onPercentInputFinished();

private:
    void setupUI();
    void updateButtonState();
    QColor getColorFromString(const QString& colorStr);
    void sendIntensityUpdate(int intensity);

    int m_channelNumber;
    QString m_channelName;
    QString m_color;

    QLabel* m_nameLabel;
    QLabel* m_channelLabel;
    QLabel* m_intensityValue;
    QLineEdit* m_percentInput;
    QPushButton* m_applyButton;
    QPushButton* m_incrementButton;
    QPushButton* m_decrementButton;
    QPushButton* m_toggleButton;

    bool m_isOn;
    bool m_isConnected;
    bool m_isUpdating;  // Prevent feedback loops
    int m_currentIntensity;  // Current intensity value (0-255)

    // Suppress poll updates briefly after user input to avoid race condition
    qint64 m_lastUserChangeTime;  // msecsSinceEpoch of last user-initiated change
    static constexpr int POLL_SUPPRESS_MS = 1000;  // Ignore polls for 1s after user change
};

/**
 * @brief Full lighting control page with all channels
 */
class LightingControlWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LightingControlWidget(ApplicationContext* context, QWidget* parent = nullptr);
    ~LightingControlWidget();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onPollTimer();
    void onChannelIntensityChanged(int channel, int intensity);
    void onChannelToggleChanged(int channel, bool on);
    void onConnectClicked();
    void onDisconnectClicked();
    void onAllOffClicked();
    void onRefreshClicked();

    // Preset slots
    void onPresetClicked(int percent);
    void onColorSelected(const QString& group, const QString& color);

    // Worker callbacks
    void onWorkerConnectionSuccess(QString deviceId);
    void onWorkerConnectionFailed(QString deviceId, QString error);
    void onWorkerDisconnectionComplete(QString deviceId);
    void onWorkerStatusUpdated(QString deviceId, std::vector<int> intensities, std::vector<bool> onStates);
    void onWorkerAllOffComplete();
    void onWorkerCommandError(QString operation, QString error);

private:
    void setupUI();
    QWidget* createHeaderSection();
    QWidget* createPresetsSection();
    QWidget* createChannelWidgets();
    void updateStatus();
    void updateChannelWidgets();
    void applyIntensityToActiveColor(int intensity);
    void activateColor(const QString& group, const QString& color);
    QString getChannelColor(LightingChannelWidget* widget) const;
    QString getChannelGroup(LightingChannelWidget* widget) const;

    ApplicationContext* m_context;
    Lighting::LightingManager* m_lightingManager;

    // UI Components
    QLabel* m_connectionStatus;
    QLabel* m_deviceInfo;
    QPushButton* m_connectButton;
    QPushButton* m_disconnectButton;
    QPushButton* m_allOffButton;
    QPushButton* m_refreshButton;

    QWidget* m_channelsContainer;
    QVBoxLayout* m_channelsLayout;
    std::vector<LightingChannelWidget*> m_channelWidgets;

    // Polling
    QTimer* m_pollTimer;

    // State
    std::string m_currentDeviceId;
    bool m_isConnected;

    // Parameter worker for non-blocking operations
    LightingParameterWorker* m_parameterWorker;
    bool m_connectionPending;

    // Active color selection (per group)
    QString m_activeGroup;  // Currently selected group ("light1", etc.), empty = none
    QString m_activeColor;  // Currently selected color within group ("red", etc.), empty = none
    std::vector<QPushButton*> m_colorButtons;  // All color buttons across groups
};

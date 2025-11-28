// LightingControlWidget.h - Lighting channel control widget
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QScrollArea>
#include <vector>
#include <string>

struct ApplicationContext;

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

    void setIntensity(int intensity);
    void setOn(bool on);
    void setConnected(bool connected);

    int channelNumber() const { return m_channelNumber; }
    QString channelName() const { return m_channelName; }

signals:
    void intensityChanged(int channel, int intensity);
    void toggleChanged(int channel, bool on);

private slots:
    void onSliderChanged(int value);
    void onToggleClicked();

private:
    void setupUI();
    void updateButtonState();
    QColor getColorFromString(const QString& colorStr);

    int m_channelNumber;
    QString m_channelName;
    QString m_color;

    QLabel* m_nameLabel;
    QLabel* m_channelLabel;
    QSlider* m_intensitySlider;
    QLabel* m_intensityValue;
    QLabel* m_percentLabel;
    QPushButton* m_toggleButton;

    bool m_isOn;
    bool m_isConnected;
    bool m_isUpdating;  // Prevent feedback loops
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

private:
    void setupUI();
    void createHeaderSection();
    void createChannelWidgets();
    void updateStatus();
    void updateChannelWidgets();

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
};

#pragma once

#include "PicAdjustData.h"

#include <QWidget>
#include <array>
#include <future>
#include <string>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QRadioButton;
struct ApplicationContext;

// "PIC adjust" tab body. Captures S1/S2 (reference line) and P1..P4 (PIC die
// corners) from gantry-main, persists them to a JSON file separate from the
// motion positions file, and shows derived U/V/W angles (degrees) and the
// reference deltas. Move-To buttons drive gantry-main with all-axis velocity
// temporarily set to 10 mm/s, then restored.
class PicAdjustWidget : public QWidget {
    Q_OBJECT
public:
    explicit PicAdjustWidget(ApplicationContext* context, QWidget* parent = nullptr);
    ~PicAdjustWidget() override;

private slots:
    void onCaptureClicked(int slot);
    void onMoveToClicked(int slot);
    void onSaveClicked();
    void onLoadClicked();
    void onRecomputeClicked();
    void onApplyClicked(char axis);   // 'U' / 'V' / 'W'
    void onApplyDistanceClicked();    // move hex Z toward target gap
    void onSettingsChanged();          // sync UI state -> m_settings and persist
    void onJogClicked(const QString& axis, int sign);  // axis = "X"|"Y"|"Z", sign = +/-1
    void onMoveHexDownClicked();   // hex-right Z -150 µm quick step

private:
    void buildUI();
    void refreshPointLabel(int slot);
    void refreshResults();
    void appendLog(const QString& s);

    // Async move helpers
    void startMoveToAsync(int slot);
    void startApplyAsync(char axis, double deltaDeg);
    void startDistanceMoveAsync(double deltaMm);
    void startJogAsync(const QString& axis, double distMm);
    void setRowEnabled(bool enabled);
    void persistAll();                 // writes points + settings to file
    void refreshDistanceLabel();       // update current gap label

    ApplicationContext* m_context = nullptr;
    std::string         m_deviceName = "gantry-main";
    double              m_safeVelocity = 10.0;     // mm/s during managed move

    PicAdjustSet        m_set{};
    PicAdjustSettings   m_settings{};
    std::string         m_filePath;
    bool                m_loadingSettings = false;  // guard against signal-loops while applying loaded values

    // Per-row widgets (size kPicPointCount)
    std::array<QLabel*,      kPicPointCount> m_posLabels{};
    std::array<QPushButton*, kPicPointCount> m_captureBtns{};
    std::array<QPushButton*, kPicPointCount> m_moveBtns{};

    // Results panel
    QLabel* m_uRefLabel    = nullptr;
    QLabel* m_wRefLabel    = nullptr;
    QLabel* m_uPicLabel    = nullptr;
    QLabel* m_wPicLabel    = nullptr;
    QLabel* m_wRollLLabel  = nullptr;   // cross-check from P1->P3
    QLabel* m_vPitchTLabel = nullptr;
    QLabel* m_vPitchBLabel = nullptr;
    QLabel* m_deltaULabel  = nullptr;
    QLabel* m_deltaWLabel  = nullptr;
    QLabel* m_vAvgLabel    = nullptr;

    QPlainTextEdit* m_logEdit = nullptr;

    // Apply-correction widgets
    QComboBox*   m_targetDeviceCombo = nullptr;
    QCheckBox*   m_invertU = nullptr;
    QCheckBox*   m_invertV = nullptr;
    QCheckBox*   m_invertW = nullptr;
    QPushButton* m_applyUBtn = nullptr;
    QPushButton* m_applyVBtn = nullptr;
    QPushButton* m_applyWBtn = nullptr;
    double       m_hexSafeVelocity = 0.2;  // mm/s during managed hex move
    QCheckBox*   m_lockSigns = nullptr;     // top-of-content lock toggle

    // Distance-adjustment widgets
    QLabel*         m_distanceCurrentLabel = nullptr;
    QDoubleSpinBox* m_distanceTargetSpin   = nullptr;   // target gap (µm)
    QCheckBox*      m_invertDistance       = nullptr;
    QPushButton*    m_applyDistanceBtn     = nullptr;

    // Manual jog (gantry-main) widgets — right column
    QRadioButton*   m_jogStep2  = nullptr;
    QRadioButton*   m_jogStep5  = nullptr;
    QRadioButton*   m_jogStep10 = nullptr;
    QPushButton*    m_jogXmBtn  = nullptr;
    QPushButton*    m_jogXpBtn  = nullptr;
    QPushButton*    m_jogYmBtn  = nullptr;
    QPushButton*    m_jogYpBtn  = nullptr;
    QPushButton*    m_jogZmBtn  = nullptr;
    QPushButton*    m_jogZpBtn  = nullptr;

    std::future<void> m_moveFuture;
    bool m_moveInFlight = false;
};

// MultiDeviceRunAlignmentWidget.h - Run alignment with gantry + hex device support
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>
#include <QTextEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <future>
#include <vector>
#include <string>
#include "AccuracyTrainerData.h"
#include "CoordinateTransformer.h"
#include "TransformationMatrixLoader.h"

struct ApplicationContext;

/**
 * @brief Multi-device run alignment mode:
 * 1. Move to each reference point, user adjusts, compute correction transform
 * 2. Register hex device end-effector positions via 3-corner capture
 * 3. Move to corrected bonding positions using gantry-main or hex devices
 *    (hex moves use gantry-to-hex coordinate transform via 3x3 matrices)
 */
class MultiDeviceRunAlignmentWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MultiDeviceRunAlignmentWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetTrainingSet(TrainingSet* ts);

private slots:
    void onStartAlignment();
    void onCaptureActualRef();
    void onCaptureHexCorner();
    void onSkipHexCorner3();
    void onConfirmHexRegistration();
    void onRedoHexRegistration();
    void onMoveToNextBonding();
    void onMoveToPrevBonding();
    void onFinish();
    void updatePositionDisplay();

private:
    // Hex device registration data (private, not persisted)
    struct HexDeviceRegistration {
        std::string deviceName;       // e.g. "hex-left"
        // 3 corners captured in gantry-main coordinates
        double cornerTL_X = 0.0, cornerTL_Y = 0.0;
        double cornerBR_X = 0.0, cornerBR_Y = 0.0;
        double cornerTR_X = 0.0, cornerTR_Y = 0.0;
        bool hasThirdCorner = false;
        // Computed center in gantry-main coordinates
        double centerGantryX = 0.0, centerGantryY = 0.0, centerGantryZ = 0.0;
        // Hex device's own position at registration time (read from hex device)
        double hexPosX = 0.0, hexPosY = 0.0, hexPosZ = 0.0;
        bool valid = false;
    };

    enum AlignState {
        Ready,
        MoveToRef,              // Auto-moving to a taught ref
        AdjustRef,              // Waiting for user to adjust
        Computing,              // Computing correction transform
        RegisterHex_Corner1,    // Jog gantry to hex tip TL corner, capture
        RegisterHex_Corner2,    // Jog gantry to hex tip BR corner, capture
        RegisterHex_Corner3,    // Jog gantry to hex tip TR corner (optional)
        RegisterHex_Summary,    // Show computed center, confirm
        MoveToBonding,          // Move with pre-assigned device
        Done                    // Summary of corrections
    };

    void setupUI();
    void setState(AlignState state);
    void moveToCurrentRef();
    void showManualAdjust(int refIdx, size_t refCount, const std::string& refName);
    void autoCaptureRef(int refIdx, const std::string& device);
    bool runVisionDetectCenter(const std::string& profilePath, const std::string& device);
    void computeTransform();
    void startHexRegistration();
    void beginNextHexCornerCapture();
    void computeHexCenter();
    void autoRegisterSinglePoint();
    void applyDeviceAssignments();
    void startBondingSequence();
    void showBondingPointAndAutoMove();
    void moveToCurrentBonding();
    void appendLog(const QString& text);

    ApplicationContext* m_context;
    TrainingSet* m_trainingSet = nullptr;
    TransformationMatrixLoader m_matrixLoader;

    AlignState m_state = Ready;
    int m_currentRefIndex = 0;
    int m_currentBondIndex = 0;

    // Actual reference positions (after user adjustment)
    std::vector<ReferencePoint> m_actualRefs;

    // Corrected bonding positions
    struct CorrectedBond {
        std::string name;
        double correctedX = 0.0;
        double correctedY = 0.0;
        double correctedZ = 0.0;
        double deltaX = 0.0;
        double deltaY = 0.0;
        std::string assignedDevice;   // "gantry-main" or hex device name
        int assignedHexIndex = -1;    // -1 = gantry, 0..N = index into m_hexRegistrations
    };
    std::vector<CorrectedBond> m_correctedBonds;

    // Live frame computed from actual refs
    double m_liveOriginX = 0.0, m_liveOriginY = 0.0;
    double m_liveXAxisX = 1.0, m_liveXAxisY = 0.0;
    double m_liveYAxisX = 0.0, m_liveYAxisY = 1.0;

    // Hex registration
    std::vector<std::string> m_hexDevicesToRegister;
    std::vector<HexDeviceRegistration> m_hexRegistrations;
    int m_currentHexRegIndex = 0;
    HexDeviceRegistration m_pendingHexReg;

    // UI - Page 0: Ready
    QStackedWidget* m_wizardStack;
    QLabel* m_readyLabel;
    QCheckBox* m_hexLeftCheck;
    QCheckBox* m_hexRightCheck;
    QPushButton* m_startBtn;

    // UI - Page 1: Ref adjustment
    QLabel* m_refInstructionLabel;
    QLabel* m_refPositionLabel;
    QPushButton* m_captureRefBtn;
    QPushButton* m_cancelRefBtn;

    // UI - Page 2: Hex registration corners
    QLabel* m_hexInstructionLabel;
    QLabel* m_hexPositionLabel;
    QPushButton* m_captureHexCornerBtn;
    QPushButton* m_skipHexCornerBtn;
    QPushButton* m_cancelHexBtn;

    // UI - Page 3: Hex registration summary
    QLabel* m_hexSummaryLabel;
    QPushButton* m_confirmHexBtn;
    QPushButton* m_redoHexBtn;

    // UI - Page 4: Bonding (auto-moves with pre-assigned device)
    QLabel* m_bondInstructionLabel;
    QLabel* m_bondPositionLabel;
    QPushButton* m_prevBondBtn;
    QPushButton* m_nextBondBtn;

    // UI - Page 5: Done
    QLabel* m_doneLabel;
    QPushButton* m_finishBtn;

    // Shared log
    QTextEdit* m_logView;

    QTimer* m_positionTimer;

    // Background motion thread
    std::future<void> m_moveFuture;
};

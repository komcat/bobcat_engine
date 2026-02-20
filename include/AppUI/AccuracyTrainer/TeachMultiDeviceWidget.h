// TeachMultiDeviceWidget.h - Wizard for teaching hex device positions for auto-navigate
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QTimer>
#include <QComboBox>
#include <future>
#include <vector>
#include <string>
#include "AccuracyTrainerData.h"
#include "MotionTypes.h"

struct ApplicationContext;

/**
 * @brief Step-by-step wizard for teaching hex device positions.
 *
 * Teaches both the hex device working position (6-axis) and the gantry
 * viewing position so that during Run Multi-Device Alignment, both devices
 * auto-navigate to the taught positions before corner capture.
 *
 * Flow per device: Idle -> PositionHex -> PositionGantry -> Summary -> Idle
 */
class TeachMultiDeviceWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TeachMultiDeviceWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetTrainingSet(TrainingSet* ts);

signals:
    void hexPositionsUpdated(std::vector<HexDeviceTeachData> positions);
    void deviceAssignmentsChanged();

private slots:
    void onTeach();
    void onReteachSelected();
    void onRemoveSelected();
    void onMoveToPosition();
    void onCaptureHexPosition();
    void onCaptureGantryPosition();
    void onDetectCenter();
    void onConfirm();
    void onRedo();
    void onCancel();
    void updatePositionDisplay();
    void onAssignmentComboChanged();

private:
    enum WizardState { Idle, PositionHex, PositionGantry, Summary };

    void setupUI();
    void setState(WizardState state);
    void refreshList();
    void rebuildAssignmentUI();
    void refreshVisionProfiles();
    QString getVisionDirectory() const;

    ApplicationContext* m_context;
    TrainingSet* m_trainingSet = nullptr;

    WizardState m_state = Idle;
    HexDeviceTeachData m_pendingTeach;

    // UI - Page 0: Idle (list of taught positions)
    QListWidget* m_teachList;
    QComboBox* m_hexDeviceCombo;
    QPushButton* m_teachBtn;
    QPushButton* m_reteachBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_moveToBtn;

    // Async move
    std::future<void> m_moveFuture;

    // UI - Page 1: Position hex device
    QStackedWidget* m_wizardStack;
    QLabel* m_hexInstructionLabel;
    QLabel* m_hexPositionLabel;
    QPushButton* m_captureHexBtn;
    QPushButton* m_cancelHexBtn;

    // UI - Page 2: Position gantry
    QLabel* m_gantryInstructionLabel;
    QLabel* m_gantryPositionLabel;
    QPushButton* m_captureGantryBtn;
    QPushButton* m_cancelGantryBtn;

    // Vision detect on gantry page
    QComboBox* m_visionProfileCombo;
    QPushButton* m_refreshProfilesBtn;
    QPushButton* m_detectCenterBtn;
    std::future<void> m_detectFuture;

    // UI - Page 3: Summary
    QLabel* m_summaryLabel;
    QPushButton* m_confirmBtn;
    QPushButton* m_redoBtn;

    // UI - Assignment section (on Idle page)
    QWidget* m_assignContainer;
    std::vector<QComboBox*> m_assignCombos;

    // Position polling
    QTimer* m_positionTimer;
};

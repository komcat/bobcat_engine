// PlacementCorrectionPage.h - Main page for Placement Correction Trainer
//
// Standalone trainer UI (accessed via F8 shortcut in MainWindow).
// Left panel: training set list + management. Right panel: teach/retrace widgets.
//
// The retrace functionality is also available as a recipe step via
// PlacementCorrectionProcesses.cpp ("Placement Correction Retrace" plugin).
// That plugin reuses PlacementRetraceWidget embedded in RunProgramPage.
// See PlacementCorrectionData.h header for the full module overview.
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QProgressBar>
#include <future>
#include "PlacementCorrectionData.h"
#include "PlacementCorrectionStore.h"

struct ApplicationContext;
class TeachPlacementLineWidget;
class PlacementResultWidget;
class PlacementRetraceWidget;
class ExposureAdjustWidget;
class ImageGalleryWidget;
class SummaryDiagramWidget;
class RetraceLogViewer;

/**
 * @brief Main Placement Correction Trainer page: left nav (30%) + right content (70%)
 *
 * Teaches reference and target edge lines using gantry-main crosshair,
 * computes master vector and angular delta, calculates corrected position
 * for the target position on the selected hexapod device.
 */
class PlacementCorrectionPage : public QWidget
{
    Q_OBJECT

public:
    explicit PlacementCorrectionPage(ApplicationContext* context, QWidget* parent = nullptr);

    void setTopBarVisible(bool visible);

protected:
    void showEvent(QShowEvent* event) override;

signals:
    void backToMainMenu();

private slots:
    void onNavItemChanged(int currentRow);
    void onNewSet();
    void onLoadSet();
    void onDeleteSet();
    void onSaveSet();
    void onSaveAsSet();
    void onTargetDeviceChanged(int index);
    void onMasterPositionChanged(int index);
    void onTargetPositionChanged(int index);
    void onNormalPositionChanged(int index);
    void onMoveMaster();
    void onMoveTarget();
    void onMoveNormal();
    void onStopMove();

private:
    // Background mover: lowers system velocity, runs MoveToAbs to the named
    // position, then restores velocity. Disables `btn` while running.
    void startSafeMoveAsync(const std::string& device,
                            const std::string& posName,
                            QPushButton* btn);

    void setupUI();
    void ensureContentBuilt(int navRow);
    void updateCurrentSet();
    void setSetLoaded(bool loaded);
    void updateStoreBasePath();
    void populateHexDevices();
    void populateNamedPositions();
    void updateMasterFromPosition();
    void updateTargetFromPosition();

    ApplicationContext* m_context;
    PlacementCorrectionStore m_store;
    PlacementCorrectionSet m_currentSet;
    bool m_hasLoadedSet = false;

    // Left panel
    QListWidget* m_navList;
    QLabel* m_setLabel;
    QPushButton* m_newBtn;
    QPushButton* m_loadBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_saveBtn;
    QPushButton* m_saveAsBtn = nullptr;
    QComboBox* m_targetDeviceCombo;
    QComboBox* m_masterPositionCombo;
    QLabel* m_masterUVWLabel;
    QPushButton* m_masterMoveBtn = nullptr;
    QComboBox* m_targetPositionCombo;
    QLabel* m_targetUVWLabel;
    QPushButton* m_targetMoveBtn = nullptr;
    QComboBox* m_normalPositionCombo = nullptr;
    QPushButton* m_normalMoveBtn = nullptr;
    QComboBox* m_cameraCombo;
    QLabel* m_statusLabel;

    // Move-in-progress widgets (shown during async safe-move)
    QWidget* m_moveStatusWidget = nullptr;
    QProgressBar* m_moveProgressBar = nullptr;
    QPushButton* m_moveStopBtn = nullptr;
    std::string m_moveActiveDevice;     // device currently being moved (for EmergencyStop)

    // Right panel
    QStackedWidget* m_contentStack;
    QWidget* m_welcomeWidget;
    TeachPlacementLineWidget* m_teachRefLineWidget;
    TeachPlacementLineWidget* m_teachTgtLineWidget;
    TeachPlacementLineWidget* m_teachNrmLineWidget = nullptr;
    PlacementResultWidget* m_resultWidget;
    PlacementRetraceWidget* m_retraceWidget;
    ExposureAdjustWidget* m_exposureAdjustWidget;
    ImageGalleryWidget* m_imageGalleryWidget;
    SummaryDiagramWidget* m_summaryDiagramWidget;
    RetraceLogViewer* m_retraceLogViewer = nullptr;

    // Top bar container
    QWidget* m_topBarWidget;

    // Background mover future (kept alive for thread lifetime; per MISRA we
    // avoid std::thread::detach in favor of std::future).
    std::future<void> m_moveFuture;
};

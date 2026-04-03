// PlacementCorrectionPage.h - Main page for Placement Correction Trainer
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include "PlacementCorrectionData.h"
#include "PlacementCorrectionStore.h"

struct ApplicationContext;
class TeachPlacementLineWidget;
class PlacementResultWidget;
class PlacementRetraceWidget;
class ExposureAdjustWidget;
class ImageGalleryWidget;
class SummaryDiagramWidget;

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
    void onTargetDeviceChanged(int index);
    void onMasterPositionChanged(int index);
    void onTargetPositionChanged(int index);

private:
    void setupUI();
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
    QComboBox* m_targetDeviceCombo;
    QComboBox* m_masterPositionCombo;
    QLabel* m_masterUVWLabel;
    QComboBox* m_targetPositionCombo;
    QLabel* m_targetUVWLabel;
    QComboBox* m_cameraCombo;
    QLabel* m_statusLabel;

    // Right panel
    QStackedWidget* m_contentStack;
    QWidget* m_welcomeWidget;
    TeachPlacementLineWidget* m_teachRefLineWidget;
    TeachPlacementLineWidget* m_teachTgtLineWidget;
    PlacementResultWidget* m_resultWidget;
    PlacementRetraceWidget* m_retraceWidget;
    ExposureAdjustWidget* m_exposureAdjustWidget;
    ImageGalleryWidget* m_imageGalleryWidget;
    SummaryDiagramWidget* m_summaryDiagramWidget;

    // Top bar container
    QWidget* m_topBarWidget;
};

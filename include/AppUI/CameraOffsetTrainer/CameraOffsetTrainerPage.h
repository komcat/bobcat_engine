// CameraOffsetTrainerPage.h - Main page with left nav + right stacked widget
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include "CameraOffsetData.h"
#include "CameraOffsetStore.h"

struct ApplicationContext;
class CalibrateOffsetWidget;
class TestOffsetWidget;

/**
 * @brief Main Camera Offset Trainer page: left nav (30%) + right content (70%)
 *
 * Allows the user to calibrate the XYZ offset between a low-magnification
 * camera and a high-magnification camera, both mounted on the same gantry device.
 */
class CameraOffsetTrainerPage : public QWidget
{
    Q_OBJECT

public:
    explicit CameraOffsetTrainerPage(ApplicationContext* context, QWidget* parent = nullptr);

    /** Hide/show the built-in top bar (back button + title).
     *  Call setTopBarVisible(false) when embedding inside TrainerContainerPage. */
    void setTopBarVisible(bool visible);

signals:
    void backToMainMenu();

private slots:
    void onNavItemChanged(int currentRow);
    void onNewCalibrationSet();
    void onLoadCalibrationSet();
    void onDeleteCalibrationSet();
    void onSaveCalibrationSet();

private:
    void setupUI();
    void updateCurrentCalibrationSet();
    void setCalibrationSetLoaded(bool loaded);
    void updateStoreBasePath();

    ApplicationContext* m_context;
    CameraOffsetStore m_store;
    CameraOffsetCalibrationSet m_currentSet;
    bool m_hasLoadedSet = false;

    // Left panel
    QListWidget* m_navList;
    QLabel* m_calibrationSetLabel;
    QPushButton* m_newBtn;
    QPushButton* m_loadBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_saveBtn;
    QComboBox* m_lowMagCameraCombo;
    QComboBox* m_highMagCameraCombo;
    QComboBox* m_pneumaticSlideCombo;
    QLabel* m_statusLabel;

    // Right panel
    QStackedWidget* m_contentStack;
    QWidget* m_welcomeWidget;
    CalibrateOffsetWidget* m_calibrateWidget;
    TestOffsetWidget* m_testWidget;

    // Top bar container (back button + title) - can be hidden when embedded
    QWidget* m_topBarWidget;
};

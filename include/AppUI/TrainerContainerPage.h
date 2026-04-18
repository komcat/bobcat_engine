// TrainerContainerPage.h - Container with side panel navigation for trainer tools
#pragma once

#include <QWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QVector>

struct ApplicationContext;
class AccuracyTrainerPage;
class CameraOffsetTrainerPage;
class PlanarAdjustmentPage;
class ZOffsetPage;
class YawTrainerPage;
class PlacementCorrectionPage;
class EdgeTransferPage;

/**
 * @brief Container page with a narrow side panel for switching between trainer tools.
 *
 * Layout:
 *   [Top bar: Back button + "Trainer" title]
 *   [Side panel (60px) | Content area (QStackedWidget)]
 *
 * Side panel buttons:
 *   0: "Pos\nAccuracy" -> AccuracyTrainerPage
 *   1: "Camera\nOffset" -> CameraOffsetTrainerPage
 *   2: "Planar\nAdjust" -> PlanarAdjustmentPage
 *   3: "Z\nOffset"      -> ZOffsetPage
 *   4: "Yaw\nTrainer" -> YawTrainerPage
 *   5: "Place\nCorrect" -> PlacementCorrectionPage
 *   6: "Edge\nTransfer" -> EdgeTransferPage
 */
class TrainerContainerPage : public QWidget
{
    Q_OBJECT

public:
    explicit TrainerContainerPage(ApplicationContext* context, QWidget* parent = nullptr);

signals:
    void backToMainMenu();

private slots:
    void onSideButtonClicked(int index);

private:
    void setupUI();
    void selectButton(int index);

    ApplicationContext* m_context;
    QStackedWidget* m_contentStack;
    QVector<QPushButton*> m_sideButtons;
    AccuracyTrainerPage* m_accuracyTrainerPage;
    CameraOffsetTrainerPage* m_cameraOffsetPage;
    PlanarAdjustmentPage* m_planarAdjustmentPage;
    ZOffsetPage* m_zOffsetPage;
    YawTrainerPage* m_yawTrainerPage;
    PlacementCorrectionPage* m_placementCorrectionPage;
    EdgeTransferPage* m_edgeTransferPage;
    int m_activeIndex;
};

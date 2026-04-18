// EdgeTransferPage.h - Main page for Edge Transfer Trainer
//
// Workflow:
//   1. Create or load a training set (product-relative JSON).
//   2. Teach reference edge (P1, P2) from gantry-main.
//   3. Teach target edge   (P3, P4) from gantry-main on the chosen device.
//   4. Configure target device + UVW role mapping + per-axis sign reverse.
//   5. View computed metrics / deltas / suggested corrections.
//
// V1 is display-only: no motion corrections are issued, but sets are
// persisted as JSON via EdgeTransferStore.
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include "EdgeTransferData.h"
#include "EdgeTransferStore.h"

struct ApplicationContext;
class EdgeTeachWidget;
class EdgeResultWidget;
class EdgeImageGalleryWidget;
class EdgeSummaryDiagramWidget;
class EdgeRetraceWidget;

class EdgeTransferPage : public QWidget
{
    Q_OBJECT

public:
    explicit EdgeTransferPage(ApplicationContext* context, QWidget* parent = nullptr);

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
    void onCameraChanged(int index);

private:
    void setupUI();
    void setSetLoaded(bool loaded);
    void updateStoreBasePath();
    void updateStatus();
    void populateHexDevices();
    void populateCameras();

    ApplicationContext* m_context;
    EdgeTransferStore m_store;
    EdgeTransferSet m_currentSet;
    bool m_hasLoadedSet = false;
    std::string m_sourceFilename;   // disk stem of currently loaded set

    // Left panel — training set management
    QLabel* m_setLabel;
    QPushButton* m_newBtn;
    QPushButton* m_loadBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_saveBtn;
    QPushButton* m_saveAsBtn;

    // Left panel — Device / Camera tabs
    QComboBox* m_targetDeviceCombo = nullptr;
    QComboBox* m_cameraCombo = nullptr;

    // Left panel — workflow
    QListWidget* m_navList;
    QLabel* m_statusLabel;

    // Right panel
    QStackedWidget* m_contentStack;
    QWidget* m_welcomeWidget;
    EdgeTeachWidget* m_teachRefWidget;
    EdgeTeachWidget* m_teachTgtWidget;
    EdgeResultWidget* m_resultWidget;
    EdgeImageGalleryWidget* m_galleryWidget = nullptr;
    EdgeSummaryDiagramWidget* m_summaryWidget = nullptr;
    EdgeRetraceWidget* m_retraceWidget = nullptr;

    QWidget* m_topBarWidget;
};

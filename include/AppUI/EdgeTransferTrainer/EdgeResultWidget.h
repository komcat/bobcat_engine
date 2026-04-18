// EdgeResultWidget.h - Displays step-by-step calculations for Edge Transfer.
#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <future>
#include <vector>
#include <utility>
#include "EdgeTransferData.h"

struct ApplicationContext;

/**
 * @brief Result view for the Edge Transfer trainer.
 *
 * Shows per-edge metrics, the axis-role mapping (U/V/W -> None/Roll/Pitch/Yaw),
 * the per-metric deltas (yaw, vertical tilt), and the resulting correction
 * values for each mapped axis. V1 is display-only (no motion issued).
 */
class EdgeResultWidget : public QWidget
{
    Q_OBJECT

public:
    explicit EdgeResultWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void SetData(const EdgeTransferSet& set);

    // Called by the page to disable / re-enable all Apply buttons while a
    // gantry nav move is in progress (initiated from the summary diagram).
    void SetApplyButtonsLocked(bool locked);

signals:
    void configChanged(const EdgeTransferConfig& cfg);
    void recalculateRequested();
    // Fires when a correction-apply starts (true) / ends (false). The page
    // relays this to the summary diagram so its nav buttons disable.
    void applyBusyChanged(bool busy);

private slots:
    void onAnyConfigChanged();
    void onApplySelected();    // applies only axes where Enable is checked
    void onApplyU();
    void onApplyV();
    void onApplyW();

private:
    void setupUI();
    void refreshDisplay();
    static void populateRoleCombo(QComboBox* cb);

    // Sequential apply on a background thread. Each non-zero (axis, delta) is
    // moved with SetSystemVelocity(0.1) -> MoveRelative -> restore velocity at
    // the very end. `actionName` is shown to the user on failure / completion.
    void applyCorrectionsAsync(std::vector<std::pair<std::string, double>> axisDeltas,
                               const QString& actionName);

    std::future<void> m_applyFuture;
    bool m_applyInProgress = false;
    bool m_externallyLocked = false;

    ApplicationContext* m_context;
    EdgeTransferSet m_set;

    QComboBox* m_uRoleCombo;
    QComboBox* m_vRoleCombo;
    QComboBox* m_wRoleCombo;
    QCheckBox* m_reverseUCheck;
    QCheckBox* m_reverseVCheck;
    QCheckBox* m_reverseWCheck;
    QCheckBox* m_enableUCheck;
    QCheckBox* m_enableVCheck;
    QCheckBox* m_enableWCheck;
    QPushButton* m_applyUBtn;
    QPushButton* m_applyVBtn;
    QPushButton* m_applyWBtn;
    QPushButton* m_recalcBtn;
    QPushButton* m_applySelectedBtn;
    QLabel* m_applyStatusLabel;

    QLabel* m_refMetricsLabel;
    QLabel* m_tgtMetricsLabel;
    QLabel* m_deltaLabel;
    QLabel* m_correctionLabel;
    QLabel* m_stepLabel;   // narrative of the calculation steps
};

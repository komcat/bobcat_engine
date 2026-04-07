// CaptureROIStepWidget.h
// Step 2: Image capture & ROI configuration for vision wizard
#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>

struct ApplicationContext;
struct VisionWizardState;
class VisionImageWidget;

class CaptureROIStepWidget : public QWidget {
    Q_OBJECT

public:
    explicit CaptureROIStepWidget(ApplicationContext* context, QWidget* parent = nullptr);

    void setState(VisionWizardState* state);
    void embedImageWidget(VisionImageWidget* imgWidget);
    void removeImageWidget();

    // ROI management
    void setROI(int startX, int startY, int endX, int endY);
    void setROIEnabled(bool enabled);
    [[nodiscard]] bool isROIEnabled() const;

    // Search box management
    void setSearchBox(int startX, int startY, int endX, int endY);
    void setSearchBoxEnabled(bool enabled);
    [[nodiscard]] bool isSearchBoxEnabled() const;
    void getSearchBox(int& startX, int& startY, int& endX, int& endY) const;

signals:
    void captureClicked();
    void clearClicked();
    void roiChanged(int startX, int startY, int endX, int endY);
    void roiEnabledChanged(bool enabled);
    void searchBoxChanged(int startX, int startY, int endX, int endY);
    void searchBoxEnabledChanged(bool enabled);
    void crosshairToggled(bool enabled);

private slots:
    void onROIValueChanged();
    void onROIEnabledChanged(Qt::CheckState state);
    void onSearchBoxValueChanged();
    void onSearchBoxEnabledChanged(Qt::CheckState state);

private:
    void setupUI();
    void setupCaptureSection(QVBoxLayout* parentLayout);
    void setupROISection(QVBoxLayout* parentLayout);
    void setupSearchBoxSection(QVBoxLayout* parentLayout);
    void updateROISizeLabel();
    void updateSearchBoxSizeLabel();

    ApplicationContext* m_context;
    VisionWizardState*  m_state = nullptr;

    // Image widget area (right side)
    QWidget*     m_imagePlaceholder;
    QVBoxLayout* m_imagePlaceholderLayout;

    // Capture controls
    QPushButton* m_captureBtn;
    QPushButton* m_clearBtn;
    QCheckBox*   m_crosshairToggle;

    // ROI controls
    QCheckBox* m_roiEnabledCheck;
    QSpinBox*  m_roiStartXSpin;
    QSpinBox*  m_roiStartYSpin;
    QSpinBox*  m_roiEndXSpin;
    QSpinBox*  m_roiEndYSpin;
    QLabel*    m_roiSizeLabel;

    // Search box controls
    QCheckBox* m_searchBoxEnabledCheck;
    QSpinBox*  m_searchBoxStartXSpin;
    QSpinBox*  m_searchBoxStartYSpin;
    QSpinBox*  m_searchBoxEndXSpin;
    QSpinBox*  m_searchBoxEndYSpin;
    QLabel*    m_searchBoxSizeLabel;
};

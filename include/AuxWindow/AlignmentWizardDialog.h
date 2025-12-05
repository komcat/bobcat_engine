// AlignmentWizardDialog.h
// Wizard for teaching F1/F2 reference points and aligning markers to new parts
#pragma once

#include <QDialog>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QRadioButton>
#include <QCheckBox>
#include <QGroupBox>
#include <QTimer>
#include <QImage>
#include <functional>

class UnifiedMotionManager;

namespace WorldOverlay {
  struct Layer;
  struct LocalCoordinateSystem;
}

/**
 * @brief Step-by-step wizard for teaching local coordinate system alignment
 *
 * Workflow:
 * Step 1: Select mode (Teach Master / Re-align to New Part)
 * Step 2: Move to F1 and capture
 * Step 3: Move to F2 and capture
 * Step 4: Preview transform and apply
 */
class AlignmentWizardDialog : public QDialog
{
  Q_OBJECT

public:
  enum class Mode {
    TeachMaster,    // First time: teaching F1/F2 on master part
    RealignPart     // Subsequent: re-teaching F1/F2 for new part alignment
  };

  explicit AlignmentWizardDialog(WorldOverlay::Layer* layer,
                                  UnifiedMotionManager* motionManager,
                                  QWidget* parent = nullptr);
  ~AlignmentWizardDialog();

  // Get the result mode
  Mode getMode() const { return m_mode; }

  // Set callback to get current camera frame for reference image capture
  void setFrameProvider(std::function<QImage()> provider) { m_frameProvider = provider; }

  // Set the overlay directory for saving reference images
  void setOverlayDirectory(const QString& dir) { m_overlayDir = dir; }

signals:
  void alignmentCompleted();
  void previewRequested(double rotation, double translateX, double translateY, double scale);

private slots:
  void onNextClicked();
  void onBackClicked();
  void onCaptureF1Clicked();
  void onCaptureF2Clicked();
  void onMoveToMasterF1Clicked();
  void onMoveToMasterF2Clicked();
  void onApplyClicked();
  void onPreviewToggled(bool checked);
  void updatePositionDisplay();

private:
  void setupUI();
  void setupStep1_ModeSelection();
  void setupStep2_CaptureF1();
  void setupStep3_CaptureF2();
  void setupStep4_Preview();

  void goToStep(int step);
  void updateNavigationButtons();
  void computePreviewTransform();

  // Layer being aligned
  WorldOverlay::Layer* m_layer;
  UnifiedMotionManager* m_motionManager;

  // Current wizard state
  Mode m_mode = Mode::TeachMaster;
  int m_currentStep = 0;

  // Captured positions
  double m_f1X = 0, m_f1Y = 0, m_f1Z = 0;
  double m_f2X = 0, m_f2Y = 0, m_f2Z = 0;
  bool m_f1Captured = false;
  bool m_f2Captured = false;

  // Reference images
  QImage m_f1RefImage;
  QImage m_f2RefImage;
  QString m_f1RefImageFilename;
  QString m_f2RefImageFilename;

  // Frame provider callback and overlay directory
  std::function<QImage()> m_frameProvider;
  QString m_overlayDir;

  // Preview transform
  double m_previewRotation = 0;
  double m_previewTranslateX = 0;
  double m_previewTranslateY = 0;
  double m_previewScale = 1.0;

  // UI Elements
  QStackedWidget* m_stackedWidget;
  QPushButton* m_backButton;
  QPushButton* m_nextButton;
  QPushButton* m_cancelButton;

  // Step 1: Mode selection
  QRadioButton* m_teachMasterRadio;
  QRadioButton* m_realignPartRadio;
  QLabel* m_modeDescriptionLabel;

  // Step 2: F1 capture
  QLabel* m_f1PositionLabel;
  QLabel* m_f1CurrentPosLabel;
  QPushButton* m_captureF1Button;
  QPushButton* m_moveToMasterF1Button;  // Move to master F1 position (realign mode)
  QLabel* m_f1StatusLabel;
  QLabel* m_f1RefImageLabel;        // Thumbnail of master F1 reference (in realign mode)
  QLabel* m_f1CapturedImageLabel;   // Thumbnail of captured F1 image (in teach mode)

  // Step 3: F2 capture
  QLabel* m_f2PositionLabel;
  QLabel* m_f2CurrentPosLabel;
  QPushButton* m_captureF2Button;
  QPushButton* m_moveToMasterF2Button;  // Move to master F2 position (realign mode)
  QLabel* m_f2StatusLabel;
  QLabel* m_f2RefImageLabel;        // Thumbnail of master F2 reference (in realign mode)
  QLabel* m_f2CapturedImageLabel;   // Thumbnail of captured F2 image (in teach mode)
  QRadioButton* m_xAxisRadio;
  QRadioButton* m_yAxisRadio;

  // Step 4: Preview & Apply
  QLabel* m_rotationLabel;
  QLabel* m_translationLabel;
  QLabel* m_scaleLabel;
  QCheckBox* m_previewCheckbox;
  QCheckBox* m_enableScaleCheckbox;
  QPushButton* m_applyButton;

  // Timer for position updates
  QTimer* m_positionTimer;
};

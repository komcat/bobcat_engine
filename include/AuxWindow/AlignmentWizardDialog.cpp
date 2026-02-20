// AlignmentWizardDialog.cpp
#include "AlignmentWizardDialog.h"
#include "WorldOverlay.h"
#include "unified_motion_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QDir>
#include <QPixmap>
#include <iostream>
#include <cmath>

AlignmentWizardDialog::AlignmentWizardDialog(WorldOverlay::Layer* layer,
                                              UnifiedMotionManager* motionManager,
                                              QWidget* parent)
  : QDialog(parent)
  , m_layer(layer)
  , m_motionManager(motionManager)
{
  setWindowTitle("Alignment Wizard - " + layer->name);
  setMinimumSize(500, 400);
  setModal(false);  // Non-modal so user can move gantry while wizard is open
  setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

  setupUI();

  // Start position update timer
  m_positionTimer = new QTimer(this);
  connect(m_positionTimer, &QTimer::timeout, this, &AlignmentWizardDialog::updatePositionDisplay);
  m_positionTimer->start(100);  // 10 Hz

  // Initialize based on whether master has been taught
  if (m_layer->localSystem.masterTaught) {
    // Master already taught, default to realign mode
    m_realignPartRadio->setChecked(true);
    m_mode = Mode::RealignPart;
  } else {
    // First time, must teach master
    m_teachMasterRadio->setChecked(true);
    m_realignPartRadio->setEnabled(false);
    m_mode = Mode::TeachMaster;
  }

  goToStep(0);
}

AlignmentWizardDialog::~AlignmentWizardDialog()
{
  m_positionTimer->stop();
}

void AlignmentWizardDialog::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(16, 16, 16, 16);
  mainLayout->setSpacing(12);

  // Stacked widget for wizard steps
  m_stackedWidget = new QStackedWidget();
  mainLayout->addWidget(m_stackedWidget, 1);

  // Setup each step
  setupStep1_ModeSelection();
  setupStep2_CaptureF1();
  setupStep3_CaptureF2();
  setupStep4_Preview();

  // Navigation buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();

  m_backButton = new QPushButton("< Back");
  m_backButton->setStyleSheet(R"(
    QPushButton {
      background-color: #3d3d3d;
      color: white;
      padding: 8px 20px;
      border: 1px solid #555;
      border-radius: 4px;
    }
    QPushButton:hover { background-color: #4d4d4d; }
    QPushButton:disabled { background-color: #2d2d2d; color: #666; }
  )");
  connect(m_backButton, &QPushButton::clicked, this, &AlignmentWizardDialog::onBackClicked);
  buttonLayout->addWidget(m_backButton);

  buttonLayout->addStretch();

  m_cancelButton = new QPushButton("Cancel");
  m_cancelButton->setStyleSheet(R"(
    QPushButton {
      background-color: #3d3d3d;
      color: white;
      padding: 8px 20px;
      border: 1px solid #555;
      border-radius: 4px;
    }
    QPushButton:hover { background-color: #4d4d4d; }
  )");
  connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
  buttonLayout->addWidget(m_cancelButton);

  m_nextButton = new QPushButton("Next >");
  m_nextButton->setStyleSheet(R"(
    QPushButton {
      background-color: #0078d4;
      color: white;
      padding: 8px 20px;
      border: none;
      border-radius: 4px;
    }
    QPushButton:hover { background-color: #1084d8; }
    QPushButton:disabled { background-color: #555; color: #999; }
  )");
  connect(m_nextButton, &QPushButton::clicked, this, &AlignmentWizardDialog::onNextClicked);
  buttonLayout->addWidget(m_nextButton);

  mainLayout->addLayout(buttonLayout);

  // Apply dark theme
  setStyleSheet(R"(
    QDialog { background-color: #1e1e1e; color: #ffffff; }
    QLabel { color: #ffffff; }
    QGroupBox {
      color: #a0a0a0;
      border: 1px solid #444;
      border-radius: 4px;
      margin-top: 12px;
      padding-top: 8px;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      padding: 0 4px;
    }
    QRadioButton { color: #ffffff; }
    QCheckBox { color: #ffffff; }
  )");
}

void AlignmentWizardDialog::setupStep1_ModeSelection()
{
  QWidget* step1 = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(step1);
  layout->setSpacing(16);

  QLabel* titleLabel = new QLabel("Step 1: Select Alignment Mode");
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #00aaff;");
  layout->addWidget(titleLabel);

  QGroupBox* modeGroup = new QGroupBox("Alignment Mode");
  QVBoxLayout* modeLayout = new QVBoxLayout(modeGroup);

  m_teachMasterRadio = new QRadioButton("Teach Master (First Time Setup)");
  m_teachMasterRadio->setStyleSheet("font-size: 13px;");
  modeLayout->addWidget(m_teachMasterRadio);

  QLabel* teachDesc = new QLabel("   Use this when setting up markers on the master/golden part.\n"
                                  "   F1 and F2 positions will be saved as the reference.");
  teachDesc->setStyleSheet("color: #888; font-size: 11px; margin-left: 20px;");
  modeLayout->addWidget(teachDesc);

  modeLayout->addSpacing(12);

  m_realignPartRadio = new QRadioButton("Re-align to New Part");
  m_realignPartRadio->setStyleSheet("font-size: 13px;");
  modeLayout->addWidget(m_realignPartRadio);

  QLabel* realignDesc = new QLabel("   Use this when changing to a new part.\n"
                                    "   Re-teach F1 and F2 to compute the transform offset.");
  realignDesc->setStyleSheet("color: #888; font-size: 11px; margin-left: 20px;");
  modeLayout->addWidget(realignDesc);

  layout->addWidget(modeGroup);

  // Description label
  m_modeDescriptionLabel = new QLabel();
  m_modeDescriptionLabel->setWordWrap(true);
  m_modeDescriptionLabel->setStyleSheet("color: #ffcc00; font-size: 12px; padding: 8px; "
                                         "background-color: #2d2d00; border-radius: 4px;");
  layout->addWidget(m_modeDescriptionLabel);

  layout->addStretch();

  // Connect mode radio buttons
  connect(m_teachMasterRadio, &QRadioButton::toggled, [this](bool checked) {
    if (checked) {
      m_mode = Mode::TeachMaster;
      m_modeDescriptionLabel->setText("You will teach F1 (origin) and F2 (axis point) on the master part. "
                                       "All current marker positions will be saved as template positions.");
    }
  });
  connect(m_realignPartRadio, &QRadioButton::toggled, [this](bool checked) {
    if (checked) {
      m_mode = Mode::RealignPart;
      m_modeDescriptionLabel->setText("You will re-teach F1 and F2 on the new part. "
                                       "The system will compute rotation and translation to align markers.");
    }
  });

  m_stackedWidget->addWidget(step1);
}

void AlignmentWizardDialog::setupStep2_CaptureF1()
{
  QWidget* step2 = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(step2);
  layout->setSpacing(12);

  QLabel* titleLabel = new QLabel("Step 2: Capture F1 (Origin Point)");
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #00aaff;");
  layout->addWidget(titleLabel);

  QLabel* instructionLabel = new QLabel("Move the gantry to the F1 reference point (origin of local coordinate system), "
                                         "then click 'Capture F1'.");
  instructionLabel->setWordWrap(true);
  instructionLabel->setStyleSheet("color: #aaa; font-size: 12px;");
  layout->addWidget(instructionLabel);

  // Reference image display (shown in realign mode)
  QGroupBox* refGroup = new QGroupBox("Master F1 Reference");
  QHBoxLayout* refLayout = new QHBoxLayout(refGroup);

  m_f1RefImageLabel = new QLabel();
  m_f1RefImageLabel->setFixedSize(160, 120);
  m_f1RefImageLabel->setStyleSheet("QLabel { background-color: #2d2d2d; border: 1px solid #444; }");
  m_f1RefImageLabel->setAlignment(Qt::AlignCenter);
  m_f1RefImageLabel->setText("No reference image");
  refLayout->addWidget(m_f1RefImageLabel);

  QVBoxLayout* refInfoLayout = new QVBoxLayout();
  m_f1PositionLabel = new QLabel();
  m_f1PositionLabel->setStyleSheet("color: #888; font-size: 11px;");
  refInfoLayout->addWidget(m_f1PositionLabel);

  // Move to Master F1 button (only shown in realign mode)
  m_moveToMasterF1Button = new QPushButton("Move to Master F1");
  m_moveToMasterF1Button->setStyleSheet(R"(
    QPushButton {
      background-color: #0078d4;
      color: white;
      padding: 8px 16px;
      border: none;
      border-radius: 4px;
      font-size: 12px;
    }
    QPushButton:hover { background-color: #1084d8; }
  )");
  m_moveToMasterF1Button->setToolTip("Move gantry to the master F1 position");
  connect(m_moveToMasterF1Button, &QPushButton::clicked, this, &AlignmentWizardDialog::onMoveToMasterF1Clicked);
  refInfoLayout->addWidget(m_moveToMasterF1Button);

  refInfoLayout->addStretch();
  refLayout->addLayout(refInfoLayout);
  refLayout->addStretch();

  layout->addWidget(refGroup);

  // Current position display
  QGroupBox* posGroup = new QGroupBox("Current Gantry Position");
  QVBoxLayout* posLayout = new QVBoxLayout(posGroup);

  m_f1CurrentPosLabel = new QLabel("X: ---.--- Y: ---.--- Z: ---.---");
  m_f1CurrentPosLabel->setStyleSheet("font-family: monospace; font-size: 14px; color: #00ff00; padding: 8px;");
  posLayout->addWidget(m_f1CurrentPosLabel);

  layout->addWidget(posGroup);

  // Capture button and captured image row
  QHBoxLayout* captureRow = new QHBoxLayout();

  m_captureF1Button = new QPushButton("Capture F1 Position");
  m_captureF1Button->setStyleSheet(R"(
    QPushButton {
      background-color: #107c10;
      color: white;
      padding: 12px 24px;
      border: none;
      border-radius: 4px;
      font-size: 14px;
    }
    QPushButton:hover { background-color: #148c14; }
  )");
  connect(m_captureF1Button, &QPushButton::clicked, this, &AlignmentWizardDialog::onCaptureF1Clicked);
  captureRow->addWidget(m_captureF1Button);

  // Captured image thumbnail (shown after capture)
  m_f1CapturedImageLabel = new QLabel();
  m_f1CapturedImageLabel->setFixedSize(120, 90);
  m_f1CapturedImageLabel->setStyleSheet("QLabel { background-color: #2d2d2d; border: 1px solid #444; }");
  m_f1CapturedImageLabel->setAlignment(Qt::AlignCenter);
  m_f1CapturedImageLabel->setVisible(false);
  captureRow->addWidget(m_f1CapturedImageLabel);

  captureRow->addStretch();
  layout->addLayout(captureRow);

  // Status/captured position
  m_f1StatusLabel = new QLabel("F1 not captured yet");
  m_f1StatusLabel->setStyleSheet("color: #ff6600; font-size: 12px;");
  layout->addWidget(m_f1StatusLabel);

  layout->addStretch();

  m_stackedWidget->addWidget(step2);
}

void AlignmentWizardDialog::setupStep3_CaptureF2()
{
  QWidget* step3 = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(step3);
  layout->setSpacing(12);

  QLabel* titleLabel = new QLabel("Step 3: Capture F2 (Axis Point)");
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #00aaff;");
  layout->addWidget(titleLabel);

  QLabel* instructionLabel = new QLabel("Move the gantry to the F2 reference point. "
                                         "The line from F1 to F2 defines one axis of the local coordinate system.");
  instructionLabel->setWordWrap(true);
  instructionLabel->setStyleSheet("color: #aaa; font-size: 12px;");
  layout->addWidget(instructionLabel);

  // Reference image display (shown in realign mode)
  QGroupBox* refGroup = new QGroupBox("Master F2 Reference");
  QHBoxLayout* refLayout = new QHBoxLayout(refGroup);

  m_f2RefImageLabel = new QLabel();
  m_f2RefImageLabel->setFixedSize(160, 120);
  m_f2RefImageLabel->setStyleSheet("QLabel { background-color: #2d2d2d; border: 1px solid #444; }");
  m_f2RefImageLabel->setAlignment(Qt::AlignCenter);
  m_f2RefImageLabel->setText("No reference image");
  refLayout->addWidget(m_f2RefImageLabel);

  QVBoxLayout* refInfoLayout = new QVBoxLayout();
  m_f2PositionLabel = new QLabel();
  m_f2PositionLabel->setStyleSheet("color: #888; font-size: 11px;");
  refInfoLayout->addWidget(m_f2PositionLabel);

  // Move to Master F2 button (only shown in realign mode)
  m_moveToMasterF2Button = new QPushButton("Move to Master F2");
  m_moveToMasterF2Button->setStyleSheet(R"(
    QPushButton {
      background-color: #0078d4;
      color: white;
      padding: 8px 16px;
      border: none;
      border-radius: 4px;
      font-size: 12px;
    }
    QPushButton:hover { background-color: #1084d8; }
  )");
  m_moveToMasterF2Button->setToolTip("Move gantry to the master F2 position");
  connect(m_moveToMasterF2Button, &QPushButton::clicked, this, &AlignmentWizardDialog::onMoveToMasterF2Clicked);
  refInfoLayout->addWidget(m_moveToMasterF2Button);

  refInfoLayout->addStretch();
  refLayout->addLayout(refInfoLayout);
  refLayout->addStretch();

  layout->addWidget(refGroup);

  // Axis selection
  QGroupBox* axisGroup = new QGroupBox("F1 -> F2 Defines Which Axis?");
  QHBoxLayout* axisLayout = new QHBoxLayout(axisGroup);

  m_xAxisRadio = new QRadioButton("X-Axis");
  m_xAxisRadio->setChecked(true);
  axisLayout->addWidget(m_xAxisRadio);

  m_yAxisRadio = new QRadioButton("Y-Axis");
  axisLayout->addWidget(m_yAxisRadio);

  axisLayout->addStretch();
  layout->addWidget(axisGroup);

  // Current position display
  QGroupBox* posGroup = new QGroupBox("Current Gantry Position");
  QVBoxLayout* posLayout = new QVBoxLayout(posGroup);

  m_f2CurrentPosLabel = new QLabel("X: ---.--- Y: ---.--- Z: ---.---");
  m_f2CurrentPosLabel->setStyleSheet("font-family: monospace; font-size: 14px; color: #00ff00; padding: 8px;");
  posLayout->addWidget(m_f2CurrentPosLabel);

  layout->addWidget(posGroup);

  // Capture button and captured image row
  QHBoxLayout* captureRow = new QHBoxLayout();

  m_captureF2Button = new QPushButton("Capture F2 Position");
  m_captureF2Button->setStyleSheet(R"(
    QPushButton {
      background-color: #107c10;
      color: white;
      padding: 12px 24px;
      border: none;
      border-radius: 4px;
      font-size: 14px;
    }
    QPushButton:hover { background-color: #148c14; }
  )");
  connect(m_captureF2Button, &QPushButton::clicked, this, &AlignmentWizardDialog::onCaptureF2Clicked);
  captureRow->addWidget(m_captureF2Button);

  // Captured image thumbnail (shown after capture)
  m_f2CapturedImageLabel = new QLabel();
  m_f2CapturedImageLabel->setFixedSize(120, 90);
  m_f2CapturedImageLabel->setStyleSheet("QLabel { background-color: #2d2d2d; border: 1px solid #444; }");
  m_f2CapturedImageLabel->setAlignment(Qt::AlignCenter);
  m_f2CapturedImageLabel->setVisible(false);
  captureRow->addWidget(m_f2CapturedImageLabel);

  captureRow->addStretch();
  layout->addLayout(captureRow);

  // Status
  m_f2StatusLabel = new QLabel("F2 not captured yet");
  m_f2StatusLabel->setStyleSheet("color: #ff6600; font-size: 12px;");
  layout->addWidget(m_f2StatusLabel);

  layout->addStretch();

  m_stackedWidget->addWidget(step3);
}

void AlignmentWizardDialog::setupStep4_Preview()
{
  QWidget* step4 = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(step4);
  layout->setSpacing(16);

  QLabel* titleLabel = new QLabel("Step 4: Preview & Apply Transform");
  titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #00aaff;");
  layout->addWidget(titleLabel);

  // Transform summary
  QGroupBox* transformGroup = new QGroupBox("Computed Transform");
  QGridLayout* transformLayout = new QGridLayout(transformGroup);

  transformLayout->addWidget(new QLabel("Rotation:"), 0, 0);
  m_rotationLabel = new QLabel("0.00°");
  m_rotationLabel->setStyleSheet("font-family: monospace; font-size: 14px; color: #00ff00;");
  transformLayout->addWidget(m_rotationLabel, 0, 1);

  transformLayout->addWidget(new QLabel("Translation:"), 1, 0);
  m_translationLabel = new QLabel("X: 0.000 mm, Y: 0.000 mm");
  m_translationLabel->setStyleSheet("font-family: monospace; font-size: 14px; color: #00ff00;");
  transformLayout->addWidget(m_translationLabel, 1, 1);

  transformLayout->addWidget(new QLabel("Scale:"), 2, 0);
  m_scaleLabel = new QLabel("1.000");
  m_scaleLabel->setStyleSheet("font-family: monospace; font-size: 14px; color: #00ff00;");
  transformLayout->addWidget(m_scaleLabel, 2, 1);

  layout->addWidget(transformGroup);

  // Options
  QGroupBox* optionsGroup = new QGroupBox("Options");
  QVBoxLayout* optionsLayout = new QVBoxLayout(optionsGroup);

  m_previewCheckbox = new QCheckBox("Live Preview (show transformed markers)");
  m_previewCheckbox->setChecked(true);
  connect(m_previewCheckbox, &QCheckBox::toggled, this, &AlignmentWizardDialog::onPreviewToggled);
  optionsLayout->addWidget(m_previewCheckbox);

  m_enableScaleCheckbox = new QCheckBox("Enable Scaling (usually disabled)");
  m_enableScaleCheckbox->setChecked(false);
  optionsLayout->addWidget(m_enableScaleCheckbox);

  layout->addWidget(optionsGroup);

  // Apply button
  m_applyButton = new QPushButton("Apply Transform");
  m_applyButton->setStyleSheet(R"(
    QPushButton {
      background-color: #0078d4;
      color: white;
      padding: 12px 24px;
      border: none;
      border-radius: 4px;
      font-size: 14px;
    }
    QPushButton:hover { background-color: #1084d8; }
  )");
  connect(m_applyButton, &QPushButton::clicked, this, &AlignmentWizardDialog::onApplyClicked);
  layout->addWidget(m_applyButton);

  layout->addStretch();

  m_stackedWidget->addWidget(step4);
}

void AlignmentWizardDialog::goToStep(int step)
{
  m_currentStep = step;
  m_stackedWidget->setCurrentIndex(step);

  // Update step-specific UI
  if (step == 1) {
    // Step 2: Show master F1 reference in realign mode
    if (m_mode == Mode::RealignPart && m_layer->localSystem.masterTaught) {
      m_f1PositionLabel->setText(QString("Master F1: X=%1, Y=%2, Z=%3")
        .arg(m_layer->localSystem.masterF1X, 0, 'f', 3)
        .arg(m_layer->localSystem.masterF1Y, 0, 'f', 3)
        .arg(m_layer->localSystem.masterF1Z, 0, 'f', 3));
      m_f1PositionLabel->setVisible(true);
      m_f1RefImageLabel->parentWidget()->setVisible(true);
      m_moveToMasterF1Button->setVisible(true);  // Show move button in realign mode

      // Load and display master F1 reference image
      if (!m_layer->localSystem.masterF1RefImage.isEmpty() && !m_overlayDir.isEmpty()) {
        QString imagePath = m_overlayDir + "/" + m_layer->localSystem.masterF1RefImage;
        QImage refImage(imagePath);
        if (!refImage.isNull()) {
          QPixmap thumbnail = QPixmap::fromImage(refImage.scaled(160, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
          m_f1RefImageLabel->setPixmap(thumbnail);
          std::cout << "[AlignmentWizard] Loaded F1 reference: " << imagePath.toStdString() << std::endl;
        } else {
          m_f1RefImageLabel->setText("Image not found:\n" + m_layer->localSystem.masterF1RefImage);
        }
      } else {
        m_f1RefImageLabel->setText("No reference image");
      }
    } else {
      // Hide reference group in teach mode
      m_f1RefImageLabel->parentWidget()->setVisible(false);
      m_f1PositionLabel->setVisible(false);
      m_moveToMasterF1Button->setVisible(false);  // Hide move button in teach mode
    }
  } else if (step == 2) {
    // Step 3: Show master F2 reference in realign mode
    if (m_mode == Mode::RealignPart && m_layer->localSystem.masterTaught) {
      m_f2PositionLabel->setText(QString("Master F2: X=%1, Y=%2, Z=%3")
        .arg(m_layer->localSystem.masterF2X, 0, 'f', 3)
        .arg(m_layer->localSystem.masterF2Y, 0, 'f', 3)
        .arg(m_layer->localSystem.masterF2Z, 0, 'f', 3));
      m_f2PositionLabel->setVisible(true);
      m_f2RefImageLabel->parentWidget()->setVisible(true);
      m_moveToMasterF2Button->setVisible(true);  // Show move button in realign mode

      // Load and display master F2 reference image
      if (!m_layer->localSystem.masterF2RefImage.isEmpty() && !m_overlayDir.isEmpty()) {
        QString imagePath = m_overlayDir + "/" + m_layer->localSystem.masterF2RefImage;
        QImage refImage(imagePath);
        if (!refImage.isNull()) {
          QPixmap thumbnail = QPixmap::fromImage(refImage.scaled(160, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
          m_f2RefImageLabel->setPixmap(thumbnail);
          std::cout << "[AlignmentWizard] Loaded F2 reference: " << imagePath.toStdString() << std::endl;
        } else {
          m_f2RefImageLabel->setText("Image not found:\n" + m_layer->localSystem.masterF2RefImage);
        }
      } else {
        m_f2RefImageLabel->setText("No reference image");
      }
    } else {
      // Hide reference group in teach mode
      m_f2RefImageLabel->parentWidget()->setVisible(false);
      m_f2PositionLabel->setVisible(false);
      m_moveToMasterF2Button->setVisible(false);  // Hide move button in teach mode
    }
  } else if (step == 3) {
    // Step 4: Compute and display preview transform
    computePreviewTransform();
    if (m_previewCheckbox->isChecked()) {
      onPreviewToggled(true);
    }
  }

  updateNavigationButtons();
}

void AlignmentWizardDialog::updateNavigationButtons()
{
  m_backButton->setEnabled(m_currentStep > 0);

  if (m_currentStep == 3) {
    // Last step - hide next button
    m_nextButton->setVisible(false);
  } else {
    m_nextButton->setVisible(true);
    m_nextButton->setText("Next >");

    // Enable/disable based on step completion
    switch (m_currentStep) {
      case 0:  // Mode selection - always can proceed
        m_nextButton->setEnabled(true);
        break;
      case 1:  // F1 capture - need F1 captured
        m_nextButton->setEnabled(m_f1Captured);
        break;
      case 2:  // F2 capture - need F2 captured
        m_nextButton->setEnabled(m_f2Captured);
        break;
    }
  }
}

void AlignmentWizardDialog::onNextClicked()
{
  if (m_currentStep < 3) {
    goToStep(m_currentStep + 1);
  }
}

void AlignmentWizardDialog::onBackClicked()
{
  if (m_currentStep > 0) {
    goToStep(m_currentStep - 1);
  }
}

void AlignmentWizardDialog::onCaptureF1Clicked()
{
  if (!m_motionManager) {
    QMessageBox::warning(this, "Error", "Motion manager not available");
    return;
  }

  PositionStruct pos = m_motionManager->GetCurrentPosition("gantry-main");
  m_f1X = pos.x;
  m_f1Y = pos.y;
  m_f1Z = pos.z;
  m_f1Captured = true;

  m_f1StatusLabel->setText(QString("F1 Captured: X=%1, Y=%2, Z=%3")
    .arg(m_f1X, 0, 'f', 3)
    .arg(m_f1Y, 0, 'f', 3)
    .arg(m_f1Z, 0, 'f', 3));
  m_f1StatusLabel->setStyleSheet("color: #00ff00; font-size: 12px;");

  // Capture reference image if in teach master mode and frame provider is available
  if (m_mode == Mode::TeachMaster && m_frameProvider) {
    m_f1RefImage = m_frameProvider();
    if (!m_f1RefImage.isNull()) {
      // Generate unique filename
      m_f1RefImageFilename = WorldOverlay::LocalCoordinateSystem::generateRefImageFilename("F1");

      // Show thumbnail
      QPixmap thumbnail = QPixmap::fromImage(m_f1RefImage.scaled(120, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      m_f1CapturedImageLabel->setPixmap(thumbnail);
      m_f1CapturedImageLabel->setVisible(true);

      std::cout << "[AlignmentWizard] Captured F1 reference image: " << m_f1RefImageFilename.toStdString() << std::endl;
    }
  }

  std::cout << "[AlignmentWizard] Captured F1: X=" << m_f1X
            << " Y=" << m_f1Y << " Z=" << m_f1Z << std::endl;

  updateNavigationButtons();
}

void AlignmentWizardDialog::onCaptureF2Clicked()
{
  if (!m_motionManager) {
    QMessageBox::warning(this, "Error", "Motion manager not available");
    return;
  }

  PositionStruct pos = m_motionManager->GetCurrentPosition("gantry-main");
  m_f2X = pos.x;
  m_f2Y = pos.y;
  m_f2Z = pos.z;
  m_f2Captured = true;

  m_f2StatusLabel->setText(QString("F2 Captured: X=%1, Y=%2, Z=%3")
    .arg(m_f2X, 0, 'f', 3)
    .arg(m_f2Y, 0, 'f', 3)
    .arg(m_f2Z, 0, 'f', 3));
  m_f2StatusLabel->setStyleSheet("color: #00ff00; font-size: 12px;");

  // Capture reference image if in teach master mode and frame provider is available
  if (m_mode == Mode::TeachMaster && m_frameProvider) {
    m_f2RefImage = m_frameProvider();
    if (!m_f2RefImage.isNull()) {
      // Generate unique filename
      m_f2RefImageFilename = WorldOverlay::LocalCoordinateSystem::generateRefImageFilename("F2");

      // Show thumbnail
      QPixmap thumbnail = QPixmap::fromImage(m_f2RefImage.scaled(120, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      m_f2CapturedImageLabel->setPixmap(thumbnail);
      m_f2CapturedImageLabel->setVisible(true);

      std::cout << "[AlignmentWizard] Captured F2 reference image: " << m_f2RefImageFilename.toStdString() << std::endl;
    }
  }

  std::cout << "[AlignmentWizard] Captured F2: X=" << m_f2X
            << " Y=" << m_f2Y << " Z=" << m_f2Z << std::endl;

  updateNavigationButtons();
}

void AlignmentWizardDialog::computePreviewTransform()
{
  if (!m_f1Captured || !m_f2Captured) {
    return;
  }

  if (m_mode == Mode::TeachMaster) {
    // Teaching master: no transform, just store positions
    m_previewRotation = 0;
    m_previewTranslateX = 0;
    m_previewTranslateY = 0;
    m_previewScale = 1.0;

    m_rotationLabel->setText("0.00° (Master - no transform)");
    m_translationLabel->setText("X: 0.000 mm, Y: 0.000 mm");
    m_scaleLabel->setText("1.000");
  } else {
    // Realign mode: compute transform from master to current
    WorldOverlay::LocalCoordinateSystem tempSystem = m_layer->localSystem;

    // Set current F1/F2
    tempSystem.currentF1X = m_f1X;
    tempSystem.currentF1Y = m_f1Y;
    tempSystem.currentF1Z = m_f1Z;
    tempSystem.currentF2X = m_f2X;
    tempSystem.currentF2Y = m_f2Y;
    tempSystem.currentF2Z = m_f2Z;
    tempSystem.currentTaught = true;
    tempSystem.f1f2IsXAxis = m_xAxisRadio->isChecked();

    // Compute transform
    tempSystem.computeTransform();

    m_previewRotation = tempSystem.rotation;
    m_previewTranslateX = tempSystem.translateX;
    m_previewTranslateY = tempSystem.translateY;
    m_previewScale = m_enableScaleCheckbox->isChecked() ? tempSystem.scale : 1.0;

    // Update labels
    m_rotationLabel->setText(QString("%1°").arg(tempSystem.getRotationDegrees(), 0, 'f', 2));
    m_translationLabel->setText(QString("X: %1 mm, Y: %2 mm")
      .arg(m_previewTranslateX, 0, 'f', 3)
      .arg(m_previewTranslateY, 0, 'f', 3));
    m_scaleLabel->setText(QString("%1").arg(m_previewScale, 0, 'f', 3));
  }
}

void AlignmentWizardDialog::onPreviewToggled(bool checked)
{
  if (checked) {
    emit previewRequested(m_previewRotation, m_previewTranslateX, m_previewTranslateY, m_previewScale);
  } else {
    // Reset preview (rotation=0 means no transform)
    emit previewRequested(0, 0, 0, 1.0);
  }
}

void AlignmentWizardDialog::onApplyClicked()
{
  if (!m_f1Captured || !m_f2Captured) {
    QMessageBox::warning(this, "Error", "Please capture both F1 and F2 positions first.");
    return;
  }

  if (m_mode == Mode::TeachMaster) {
    // Save F1/F2 as master positions
    m_layer->localSystem.masterF1X = m_f1X;
    m_layer->localSystem.masterF1Y = m_f1Y;
    m_layer->localSystem.masterF1Z = m_f1Z;
    m_layer->localSystem.masterF2X = m_f2X;
    m_layer->localSystem.masterF2Y = m_f2Y;
    m_layer->localSystem.masterF2Z = m_f2Z;
    m_layer->localSystem.masterTaught = true;
    m_layer->localSystem.f1f2IsXAxis = m_xAxisRadio->isChecked();

    // Save reference images if captured
    if (!m_f1RefImage.isNull() && !m_f1RefImageFilename.isEmpty() && !m_overlayDir.isEmpty()) {
      // Ensure overlay directory exists
      QDir dir(m_overlayDir);
      if (!dir.exists()) {
        dir.mkpath(".");
      }

      QString f1ImagePath = m_overlayDir + "/" + m_f1RefImageFilename;
      if (m_f1RefImage.save(f1ImagePath)) {
        m_layer->localSystem.masterF1RefImage = m_f1RefImageFilename;
        std::cout << "[AlignmentWizard] Saved F1 reference image: " << f1ImagePath.toStdString() << std::endl;
      } else {
        std::cerr << "[AlignmentWizard] Failed to save F1 reference image" << std::endl;
      }
    }

    if (!m_f2RefImage.isNull() && !m_f2RefImageFilename.isEmpty() && !m_overlayDir.isEmpty()) {
      QString f2ImagePath = m_overlayDir + "/" + m_f2RefImageFilename;
      if (m_f2RefImage.save(f2ImagePath)) {
        m_layer->localSystem.masterF2RefImage = m_f2RefImageFilename;
        std::cout << "[AlignmentWizard] Saved F2 reference image: " << f2ImagePath.toStdString() << std::endl;
      } else {
        std::cerr << "[AlignmentWizard] Failed to save F2 reference image" << std::endl;
      }
    }

    // Also set as current (master = current initially)
    m_layer->localSystem.currentF1X = m_f1X;
    m_layer->localSystem.currentF1Y = m_f1Y;
    m_layer->localSystem.currentF1Z = m_f1Z;
    m_layer->localSystem.currentF2X = m_f2X;
    m_layer->localSystem.currentF2Y = m_f2Y;
    m_layer->localSystem.currentF2Z = m_f2Z;
    m_layer->localSystem.currentTaught = true;

    // Initialize all marker templates from current world positions
    for (auto& marker : m_layer->markers) {
      marker.initializeTemplate();
    }

    // No transform needed for master
    m_layer->localSystem.rotation = 0;
    m_layer->localSystem.translateX = 0;
    m_layer->localSystem.translateY = 0;
    m_layer->localSystem.scale = 1.0;
    m_layer->localSystem.transformValid = false;  // No transform on master

    std::cout << "[AlignmentWizard] Master taught: F1(" << m_f1X << ", " << m_f1Y << ") "
              << "F2(" << m_f2X << ", " << m_f2Y << ")" << std::endl;
  } else {
    // Update current F1/F2 for realignment
    m_layer->localSystem.currentF1X = m_f1X;
    m_layer->localSystem.currentF1Y = m_f1Y;
    m_layer->localSystem.currentF1Z = m_f1Z;
    m_layer->localSystem.currentF2X = m_f2X;
    m_layer->localSystem.currentF2Y = m_f2Y;
    m_layer->localSystem.currentF2Z = m_f2Z;
    m_layer->localSystem.currentTaught = true;
    m_layer->localSystem.f1f2IsXAxis = m_xAxisRadio->isChecked();

    // Compute and apply transform
    m_layer->localSystem.computeTransform();

    // Override scale if disabled
    if (!m_enableScaleCheckbox->isChecked()) {
      m_layer->localSystem.scale = 1.0;
    }

    // Apply transform to all markers
    m_layer->applyTransformToMarkers();

    std::cout << "[AlignmentWizard] Realigned: rotation=" << m_layer->localSystem.getRotationDegrees()
              << "° translate=(" << m_layer->localSystem.translateX << ", "
              << m_layer->localSystem.translateY << ") scale=" << m_layer->localSystem.scale << std::endl;
  }

  emit alignmentCompleted();
  accept();
}

void AlignmentWizardDialog::updatePositionDisplay()
{
  if (!m_motionManager) {
    return;
  }

  PositionStruct pos = m_motionManager->GetCurrentPosition("gantry-main");
  QString posText = QString("X: %1  Y: %2  Z: %3")
    .arg(pos.x, 8, 'f', 3)
    .arg(pos.y, 8, 'f', 3)
    .arg(pos.z, 8, 'f', 3);

  // Update both position labels
  if (m_currentStep == 1) {
    m_f1CurrentPosLabel->setText(posText);
  } else if (m_currentStep == 2) {
    m_f2CurrentPosLabel->setText(posText);
  }
}

void AlignmentWizardDialog::onMoveToMasterF1Clicked()
{
  if (!m_motionManager) {
    QMessageBox::warning(this, "Error", "Motion manager not available");
    return;
  }

  if (!m_layer->localSystem.masterTaught) {
    QMessageBox::warning(this, "Error", "Master F1 position has not been taught yet.");
    return;
  }

  // Ask user for confirmation
  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Move to Master F1",
    QString("Move gantry to master F1 position?\n\n"
            "X: %1\nY: %2\nZ: %3")
      .arg(m_layer->localSystem.masterF1X, 0, 'f', 3)
      .arg(m_layer->localSystem.masterF1Y, 0, 'f', 3)
      .arg(m_layer->localSystem.masterF1Z, 0, 'f', 3),
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::Yes);

  if (reply != QMessageBox::Yes) {
    return;
  }

  // Move to master F1 position (background thread)
  double f1x = m_layer->localSystem.masterF1X;
  double f1y = m_layer->localSystem.masterF1Y;
  double f1z = m_layer->localSystem.masterF1Z;

  std::cout << "[AlignmentWizard] Moving to Master F1: X=" << f1x
            << " Y=" << f1y << " Z=" << f1z << std::endl;

  if (m_moveToMasterF1Button) m_moveToMasterF1Button->setEnabled(false);

  m_moveFuture = std::async(std::launch::async, [this, f1x, f1y, f1z]() {
    bool ok = m_motionManager->MoveToAbs("gantry-main", f1x, f1y, f1z);
    QMetaObject::invokeMethod(this, [this, ok]() {
      if (m_moveToMasterF1Button) m_moveToMasterF1Button->setEnabled(true);
      if (!ok) {
        QMessageBox::warning(this, "Error", "Failed to move to Master F1 position");
      }
    }, Qt::QueuedConnection);
  });
}

void AlignmentWizardDialog::onMoveToMasterF2Clicked()
{
  if (!m_motionManager) {
    QMessageBox::warning(this, "Error", "Motion manager not available");
    return;
  }

  if (!m_layer->localSystem.masterTaught) {
    QMessageBox::warning(this, "Error", "Master F2 position has not been taught yet.");
    return;
  }

  // Ask user for confirmation
  QMessageBox::StandardButton reply = QMessageBox::question(
    this, "Move to Master F2",
    QString("Move gantry to master F2 position?\n\n"
            "X: %1\nY: %2\nZ: %3")
      .arg(m_layer->localSystem.masterF2X, 0, 'f', 3)
      .arg(m_layer->localSystem.masterF2Y, 0, 'f', 3)
      .arg(m_layer->localSystem.masterF2Z, 0, 'f', 3),
    QMessageBox::Yes | QMessageBox::No,
    QMessageBox::Yes);

  if (reply != QMessageBox::Yes) {
    return;
  }

  // Move to master F2 position (background thread)
  double f2x = m_layer->localSystem.masterF2X;
  double f2y = m_layer->localSystem.masterF2Y;
  double f2z = m_layer->localSystem.masterF2Z;

  std::cout << "[AlignmentWizard] Moving to Master F2: X=" << f2x
            << " Y=" << f2y << " Z=" << f2z << std::endl;

  if (m_moveToMasterF2Button) m_moveToMasterF2Button->setEnabled(false);

  m_moveFuture = std::async(std::launch::async, [this, f2x, f2y, f2z]() {
    bool ok = m_motionManager->MoveToAbs("gantry-main", f2x, f2y, f2z);
    QMetaObject::invokeMethod(this, [this, ok]() {
      if (m_moveToMasterF2Button) m_moveToMasterF2Button->setEnabled(true);
      if (!ok) {
        QMessageBox::warning(this, "Error", "Failed to move to Master F2 position");
      }
    }, Qt::QueuedConnection);
  });
}

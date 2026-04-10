// VisionManagerPage.h
// Wizard-style vision profile configuration page
#pragma once

#include <QWidget>
#include <QSplitter>
#include <QStackedWidget>
#include <QPushButton>
#include <QTimer>
#include <memory>
#include <functional>
#include "VisionWizardState.h"

struct ApplicationContext;
class VisionNavWidget;
class CameraLightingStepWidget;
class CaptureROIStepWidget;
class PluginConfigStepWidget;
class ExecuteTuneStepWidget;
class VisionImageWidget;
class LiveVideoSubscriber;
class VisionProcessingWorker;
struct VisionResult;

class VisionManagerPage : public QWidget {
    Q_OBJECT

public:
    explicit VisionManagerPage(ApplicationContext* context, QWidget* parent = nullptr);
    ~VisionManagerPage();

    // Template selection mode for plugins (e.g., PatternMatching teach template)
    void startTemplateSelectionMode(std::function<void(int, int, int, int)> onConfirm,
                                   std::function<void()> onCancel = nullptr);
    void stopTemplateSelectionMode();
    void onTemplateSelectionConfirmed();
    void onTemplateSelectionCancelled();
    VisionImageWidget* getImageWidget() { return m_imageWidget; }

signals:
    void backToMainMenu();

private slots:
    // Navigation
    void onNavItemChanged(int step);

    // Profile management
    void onNewProfile();
    void onLoadProfile();
    void onSaveProfile();
    void onSaveAsProfile();
    void onDeleteProfile();
    void onProfileLoadRequested(const QString& filePath);
    void onProfileDeleted(const QString& filePath);
    void onProfileRenamed(const QString& oldPath, const QString& newPath);

    // Image mode
    void onImageModeChanged(bool isLive);
    void onLiveTimerTick();

    // Step callbacks
    void onCameraSelected(const QString& cameraId);
    void onCameraSettingsChanged();
    void onLightingSettingsChanged();
    void onCaptureClicked();
    void onClearClicked();
    void onROIChanged(int startX, int startY, int endX, int endY);
    void onROIEnabledChanged(bool enabled);
    void onSearchBoxChanged(int startX, int startY, int endX, int endY);
    void onSearchBoxEnabledChanged(bool enabled);
    void onPluginSelected(const QString& pluginName);
    void onExecuteRequested();
    void onStopRequested();
    void onReliabilityTestRequested();

    // Template selection
    void onTemplateSelectionDragged(int startX, int startY, int endX, int endY);

    // Worker thread callbacks
    void onProcessingStarted(const QString& pluginName);
    void onProcessingProgress(int percent, const QString& message);
    void onProcessingFinished(const VisionResult& result, const QString& pluginName);
    void onProcessingError(const QString& errorMessage);
    void onProcessingCancelled();

private:
    void setupUI();
    void connectSignals();
    void registerPlugins();
    void updatePluginList();
    void loadPluginConfig(const QString& pluginName);
    void updatePatternMatchingSourceImage();

    // Profile/Recipe save/load helpers
    void saveProfileToPath(const QString& filePath);
    void saveRecipeToPath(const QString& filePath);
    void loadProfileFromFile(const QString& filePath);
    void loadVisionRecipeFile();
    void saveVisionRecipeFile();

    // Worker thread management
    void setupWorker();
    void setUIProcessingState(bool isProcessing);

    // Image widget reparenting
    void reparentImageWidget(int targetStep);

    // State management
    void refreshCautionIcons();
    void updateVisionDirectory();
    void refreshProfileList();

    ApplicationContext* m_context;
    VisionWizardState m_state;

    // Layout
    QSplitter* m_mainSplitter;
    QPushButton* m_backButton;

    // Left navigation
    VisionNavWidget* m_navWidget;

    // Right content area
    QStackedWidget* m_contentStack;
    QWidget* m_welcomeWidget;                   // index 0
    CameraLightingStepWidget* m_cameraStep;     // index 1
    CaptureROIStepWidget* m_captureStep;        // index 2
    PluginConfigStepWidget* m_pluginStep;        // index 3
    ExecuteTuneStepWidget* m_executeStep;        // index 4

    // Shared image widget (reparented between steps)
    VisionImageWidget* m_imageWidget;

    // Camera frame subscriber
    std::shared_ptr<LiveVideoSubscriber> m_frameSubscriber;

    // Vision processing worker
    VisionProcessingWorker* m_worker = nullptr;

    // Live mode timer
    QTimer* m_liveTimer = nullptr;

    // Current step index
    int m_currentStep = -1;
};

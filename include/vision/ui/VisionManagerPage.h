// VisionManagerPage.h
#pragma once

#include <QWidget>
#include <QSplitter>
#include <QPushButton>
#include <memory>
#include <functional>

struct ApplicationContext;
class VisionToolbarWidget;
class VisionLeftPanelWidget;
class VisionImageWidget;
class VisionRightPanelWidget;
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
    void onTemplateSelectionConfirmed();  // Public method to trigger confirmation
    void onTemplateSelectionCancelled();  // Public method to trigger cancellation
    VisionImageWidget* getImageWidget() { return m_imageWidget; }

signals:
    void backToMainMenu();

private slots:
    // Toolbar actions (mockup - just logging for now)
    void onNewRecipe();
    void onLoadRecipe();       // Load vision profile (single plugin settings)
    void onSaveRecipe();       // Save vision profile
    void onSaveAsRecipe();     // Save vision profile as
    void onRunRecipe();        // Run loaded recipe (multi-step)
    void onStopRecipe();

    // Left panel actions
    void onAddToNode();
    void onPluginSelected(const QString& pluginName);

    // Recipe management (multi-step processing)
    void loadVisionRecipeFile();   // Load recipe JSON with multiple steps
    void saveVisionRecipeFile();   // Save recipe JSON with multiple steps

    // Image widget actions
    void onCaptureImage();
    void onClearImage();
    void onROIDragged(int startX, int startY, int endX, int endY);

    // Right panel actions
    void onApplyParams();
    void onCameraSelected(const QString& cameraId);
    void onCameraSettingsChanged();
    void onCalibrationChanged(double xMmPerPx, double yMmPerPx);
    void onROIChanged(int startX, int startY, int endX, int endY);
    void onROIEnabledChanged(bool enabled);

    // Search Box actions
    void onSearchBoxDragged(int startX, int startY, int endX, int endY);
    void onSearchBoxChanged(int startX, int startY, int endX, int endY);
    void onSearchBoxEnabledChanged(bool enabled);

    // Template Selection actions
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

    // Profile/Recipe save helpers
    void saveProfileToPath(const QString& filePath);
    void saveRecipeToPath(const QString& filePath);
    void updateProfileDisplay();

    // Worker thread management
    void setupWorker();
    void setUIProcessingState(bool isProcessing);

    ApplicationContext* m_context;

    // Child widgets
    VisionToolbarWidget* m_toolbar;
    VisionLeftPanelWidget* m_leftPanel;
    VisionImageWidget* m_imageWidget;
    VisionRightPanelWidget* m_rightPanel;

    QSplitter* m_mainSplitter;
    QPushButton* m_backButton;

    // Currently selected plugin name
    QString m_selectedPluginName;

    // Camera frame subscriber
    std::shared_ptr<LiveVideoSubscriber> m_frameSubscriber;
    QString m_selectedCameraId;

    // Current profile/recipe state
    QString m_currentProfilePath;
    QString m_currentRecipePath;
    bool m_profileModified = false;

    // Vision processing worker
    VisionProcessingWorker* m_worker = nullptr;

    // Template selection mode callbacks
    std::function<void(int, int, int, int)> m_templateSelectionOnConfirm;
    std::function<void()> m_templateSelectionOnCancel;
    bool m_templateSelectionMode = false;
};

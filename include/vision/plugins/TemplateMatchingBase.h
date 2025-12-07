// TemplateMatchingBase.h
// Common base class for template-based pattern matching plugins
#pragma once

#ifdef HAS_OPENCV

#include "../IVisionPlugin.h"
#include "../VisionResult.h"
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <opencv2/core.hpp>
#include <functional>
#include <string>

/**
 * @brief Base class providing common template management functionality
 *
 * This class provides:
 * - Template image storage
 * - Template selection from ROI
 * - Anchor point management
 * - Save/Load template files
 * - Template preview generation
 * - Auto-save to profile directory
 * - Template file path management
 * - Common UI widgets for template section
 */
class TemplateMatchingBase : public VisionPluginBase {
public:
    TemplateMatchingBase(const std::string& name, const std::string& category, const std::string& description);
    virtual ~TemplateMatchingBase();

    // Template teaching interface
    virtual bool teachFromImage(const cv::Mat& templateImage) = 0;  // Must be implemented by derived class
    virtual bool hasTemplate() const { return !m_template.empty(); }
    virtual void clearTemplate();
    cv::Mat getTemplateImage() const { return m_template.clone(); }
    cv::Mat getTemplatePreview(int maxSize = 100) const;

    // Template file management
    bool saveTemplateToFile(const std::string& filePath);
    bool loadTemplateFromFile(const std::string& filePath);
    std::string getTemplateFilePath() const { return m_templateFilePath; }
    void setTemplateFilePath(const std::string& path) { m_templateFilePath = path; }

    // Directory management for auto-save
    void setTemplateDirectory(const std::string& dir) { m_templateDirectory = dir; }
    std::string getTemplateDirectory() const { return m_templateDirectory; }
    void setProfileName(const std::string& name) { m_profileName = name; }
    std::string getProfileName() const { return m_profileName; }

    // Source image for teaching
    void setSourceImage(const cv::Mat& sourceImage) { m_sourceImage = sourceImage.clone(); }

    // Vision manager page reference (for template selection mode)
    void setVisionManagerPage(void* visionPage) { m_visionManagerPage = visionPage; }

    // Anchor point accessors
    double getAnchorOffsetX() const { return m_anchorOffsetX; }
    double getAnchorOffsetY() const { return m_anchorOffsetY; }
    bool isUsingCustomAnchor() const { return m_useCustomAnchor; }
    void setAnchorOffset(double x, double y);

protected:
    // Create the common template UI section (call from derived class's createConfigWidget)
    QGroupBox* createTemplateGroupWidget(QWidget* parent);

    // Update UI preview (call after teaching template)
    void updatePreviewLabel();

    // Show anchor point dialog
    void showAnchorPointDialog();

    // Template selection mode handling
    void startTemplateSelection();
    void onTemplateSelectionComplete(int startX, int startY, int endX, int endY);
    void onTemplateSelectionCancelled();

    // Called after successful template teaching (override to show custom dialog)
    virtual void onTemplateTeachingComplete();

    // Get template file suffix for auto-save (override to customize)
    virtual std::string getTemplateFileSuffix() const { return "_template.png"; }

    // Helper to get common template parameters as JSON
    nlohmann::json getTemplateParameters() const;

    // Helper to set common template parameters from JSON
    void setTemplateParameters(const nlohmann::json& params);

    // Template data
    cv::Mat m_template;                    // The taught template image (original)
    std::string m_templateFilePath;        // Path to saved template file
    std::string m_templateDirectory;       // Directory for auto-saving templates
    std::string m_profileName;             // Profile name for auto-generating template filename

    // Anchor point (offset from center of template)
    double m_anchorOffsetX = 0.0;          // X offset from template center
    double m_anchorOffsetY = 0.0;          // Y offset from template center
    bool m_useCustomAnchor = false;        // Whether to use custom anchor point

    // Source image for teaching
    cv::Mat m_sourceImage;
    void* m_visionManagerPage = nullptr;   // VisionManagerPage reference (for template selection)
    bool m_inTemplateSelectionMode = false; // Track if currently selecting template

    // Common template UI widgets (managed by this class)
    QWidget* m_templateConfigWidget = nullptr;  // Parent widget for template section
    QLabel* m_previewLabel = nullptr;
    QLabel* m_templateInfoLabel = nullptr;
    QLabel* m_templatePathLabel = nullptr;
    QPushButton* m_teachBtn = nullptr;
    QPushButton* m_saveTemplateBtn = nullptr;
    QPushButton* m_loadTemplateBtn = nullptr;
    QPushButton* m_clearTemplateBtn = nullptr;
    QPushButton* m_setAnchorBtn = nullptr;

private:
    // Reset widget pointers when destroyed
    void resetTemplateWidgetPointers();
};

// Custom widget for anchor point selection (shared by all template matching plugins)
class TemplateAnchorPointWidget : public QLabel {
    Q_OBJECT
public:
    TemplateAnchorPointWidget(const QPixmap& templateImage, double initialOffsetX, double initialOffsetY, QWidget* parent = nullptr);

    double getAnchorOffsetX() const { return m_anchorOffsetX; }
    double getAnchorOffsetY() const { return m_anchorOffsetY; }

    std::function<void(double, double)> onAnchorChanged;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void updateAnchorFromMouse(const QPoint& pos);
    void updateDisplay();

    QPixmap m_templatePixmap;
    double m_anchorOffsetX;
    double m_anchorOffsetY;
};

#endif // HAS_OPENCV

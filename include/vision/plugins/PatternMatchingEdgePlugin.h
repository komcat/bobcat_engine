// PatternMatchingEdgePlugin.h
#pragma once

#ifdef HAS_OPENCV

#include "../IVisionPlugin.h"
#include "../VisionResult.h"
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>

/**
 * @brief Edge-based pattern matching plugin
 *
 * Converts both template and search image to edge maps using Canny edge detection,
 * then performs template matching on the edge images for lighting-invariant matching.
 */
class PatternMatchingEdgePlugin : public VisionPluginBase {
public:
    PatternMatchingEdgePlugin();

    // IVisionPlugin interface
    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;
    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

    // Template teaching interface
    bool teachFromImage(const cv::Mat& templateImage);
    bool hasTemplate() const { return !m_template.empty(); }
    void clearTemplate();
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

    // Special method to set the source image for teaching from ROI
    void setSourceImage(const cv::Mat& sourceImage) { m_sourceImage = sourceImage.clone(); }

    // Set vision manager page reference (for template selection mode)
    void setVisionManagerPage(void* visionPage) { m_visionManagerPage = visionPage; }

private:
    // Update UI preview
    void updatePreviewLabel();
    void showAnchorPointDialog();

    // Template data
    cv::Mat m_template;                    // The taught template image (original, not edge)
    cv::Mat m_templateEdge;                // Edge-detected template for matching
    cv::Point2d m_templateOrigin;          // Reference point in template (center by default)
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

    // Algorithm parameters
    double m_matchThreshold = 0.7;         // Minimum match score (0-1)
    bool m_subpixelRefinement = true;      // Refine to subpixel accuracy
    int m_maxMatches = 1;                  // Maximum number of matches to find (1 = single best match)

    // Edge detection parameters
    double m_cannyThreshold1 = 50.0;       // Canny low threshold
    double m_cannyThreshold2 = 150.0;      // Canny high threshold
    int m_gaussianBlurSize = 5;            // Gaussian blur kernel size before Canny

    // Configuration widget (not owned)
    QWidget* m_configWidget = nullptr;
    QLabel* m_previewLabel = nullptr;
    QLabel* m_templateInfoLabel = nullptr;
    QLabel* m_templatePathLabel = nullptr;
    QPushButton* m_teachBtn = nullptr;
    QPushButton* m_saveTemplateBtn = nullptr;
    QPushButton* m_loadTemplateBtn = nullptr;
    QPushButton* m_clearTemplateBtn = nullptr;
    QPushButton* m_setAnchorBtn = nullptr;
    QDoubleSpinBox* m_thresholdSpin = nullptr;
    QCheckBox* m_subpixelCheck = nullptr;
    QSpinBox* m_maxMatchesSpin = nullptr;
    QDoubleSpinBox* m_cannyThreshold1Spin = nullptr;
    QDoubleSpinBox* m_cannyThreshold2Spin = nullptr;
    QSpinBox* m_gaussianBlurSizeSpin = nullptr;
};

#endif // HAS_OPENCV

// PatternMatchingPlugin.h
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
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>

/**
 * @brief Pattern matching plugin for finding template patterns in images
 *
 * Uses normalized cross-correlation (NCC) template matching with optional
 * multi-scale and rotation search. Templates are saved as separate PNG files.
 */
class PatternMatchingPlugin : public VisionPluginBase {
public:
    PatternMatchingPlugin();

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
    // Template matching algorithm
    struct MatchResult {
        cv::Point2d location;      // Center of matched region
        double score;              // Match score (0-1)
        double angle;              // Rotation angle (if searched)
        double scale;              // Scale factor (if searched)
    };

    std::vector<MatchResult> findMatches(const cv::Mat& searchImage);
    MatchResult findBestMatch(const cv::Mat& searchImage, double angle = 0.0, double scale = 1.0);
    cv::Mat rotateTemplate(const cv::Mat& templ, double angleDeg);
    cv::Mat scaleTemplate(const cv::Mat& templ, double scaleFactor);

    // Update UI preview
    void updatePreviewLabel();
    void showAnchorPointDialog();

    // Template data
    cv::Mat m_template;                    // The taught template image
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
    int m_maxMatches = 1;                  // Maximum number of matches to find
    bool m_searchRotation = false;         // Search for rotated templates
    double m_rotationRange = 15.0;         // ±degrees to search
    double m_rotationStep = 5.0;           // Degrees per step
    bool m_searchScale = false;            // Search for scaled templates
    double m_scaleMin = 0.9;               // Minimum scale factor
    double m_scaleMax = 1.1;               // Maximum scale factor
    double m_scaleStep = 0.05;             // Scale step
    bool m_subpixelRefinement = true;      // Refine to subpixel accuracy
    int m_matchMethod = 5;                 // cv::TM_CCOEFF_NORMED by default

    // Edge-based matching parameters
    bool m_useEdgeMatching = false;        // Use edge-based matching for speed
    double m_cannyThreshold1 = 50.0;       // Canny low threshold
    double m_cannyThreshold2 = 150.0;      // Canny high threshold
    int m_gaussianBlurSize = 5;            // Gaussian blur kernel size

    // Binary preprocessing parameters
    bool m_useBinaryPreprocess = false;    // Apply binary threshold before edge/matching
    int m_binaryThreshold = 127;           // Binary threshold value (0-255)
    bool m_binaryInvert = false;           // Invert binary (white on black vs black on white)
    std::string m_binaryMethod = "fixed";  // "fixed", "otsu", or "adaptive"

    // Pyramid search parameters
    bool m_usePyramidSearch = false;       // Use coarse-to-fine pyramid search
    int m_pyramidLevels = 3;               // Number of pyramid levels

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
    QSpinBox* m_maxMatchesSpin = nullptr;
    QCheckBox* m_searchRotationCheck = nullptr;
    QDoubleSpinBox* m_rotationRangeSpin = nullptr;
    QDoubleSpinBox* m_rotationStepSpin = nullptr;
    QCheckBox* m_searchScaleCheck = nullptr;
    QDoubleSpinBox* m_scaleMinSpin = nullptr;
    QDoubleSpinBox* m_scaleMaxSpin = nullptr;
    QDoubleSpinBox* m_scaleStepSpin = nullptr;
    QCheckBox* m_subpixelCheck = nullptr;
    QCheckBox* m_useEdgeMatchingCheck = nullptr;
    QDoubleSpinBox* m_cannyThreshold1Spin = nullptr;
    QDoubleSpinBox* m_cannyThreshold2Spin = nullptr;
    QSpinBox* m_gaussianBlurSizeSpin = nullptr;
    QCheckBox* m_useBinaryPreprocessCheck = nullptr;
    QSpinBox* m_binaryThresholdSpin = nullptr;
    QCheckBox* m_binaryInvertCheck = nullptr;
    QComboBox* m_binaryMethodCombo = nullptr;
    QCheckBox* m_usePyramidSearchCheck = nullptr;
    QSpinBox* m_pyramidLevelsSpin = nullptr;
};

#endif // HAS_OPENCV

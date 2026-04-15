// HalconPatternMatchingDescriptorPlugin.h
// Feature-descriptor-based pattern matching using Halcon's uncalibrated descriptor model
//
// ============================================================================
// STATUS: DO NOT USE — KNOWN ISSUES, NOT REGISTERED IN THE UI
// ============================================================================
//
// This plugin is built but is NOT registered with VisionManager (see
// VisionManagerPage::initializePlugins — the registerPlugin block is
// commented out). The code is kept so the work isn't lost, not because
// it's ready for production use.
//
// Unresolved issues observed during bring-up:
//
//   1. Halcon's find_uncalib_descriptor_model returns score=1.0 with
//      coordinates at (-371, -503) for a template cut directly from the
//      search image. A perfect score with the template center mapped
//      hundreds of pixels off-screen indicates the RANSAC fit is
//      degenerate — the homography is projectively singular (bottom
//      row far from [0, 0, 1]) and collapses under perspective divide
//      in ProjectiveTransPoint2d.
//
//   2. Templates that NCC and Edge plugins match correctly (score > 0.8
//      at plausible locations) return either "no match" or the bogus
//      (-371, -503) result here. The plugin does not successfully
//      locate the template on any test image tried so far.
//
//   3. Halcon's MinScore parameter is restricted to [0, 1] regardless of
//      ScoreType (num_points mode rejects raw counts with #1306). This
//      makes it impossible to enforce "require at least N matched
//      points" directly via the Halcon API.
//
// Mitigations already in place (but insufficient to make it usable):
//   - Fix #3125: use ProjectiveTransPoint2d + manual rotation extraction
//     via atan2(a10, a00) instead of HomMat2dToAffinePar
//   - Permissive detector thresholds (lepetit min_score 0.00005) to
//     avoid "too few feature points" error #8977
//   - Sanity checks for out-of-image match positions and near-singular
//     homographies (a20, a21, a22 far from [0, 0, 1])
//
// Next steps if someone picks this up later:
//   - Verify the (row, col) vs (x, y) convention Halcon's descriptor
//     model actually uses — there may be a hidden swap in
//     ProjectiveTransPoint2d semantics we haven't identified
//   - Try get_descriptor_model_results("num_points") after find to
//     enforce a minimum correspondence count before accepting the match
//   - Test with harris_binomial detector on textured templates — it
//     may be more tolerant of boundary effects than lepetit
//   - Verify using a Halcon sample program (hdev) that the same template
//     actually matches there, to confirm whether the bug is in our
//     binding usage or the upstream matcher
//
// For now, USE NCC OR EDGE instead — both tested working on the same
// templates that fail here.
// ============================================================================
#pragma once

#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "TemplateMatchingBase.h"
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <halconcpp/HalconCpp.h>

/**
 * @brief Feature-descriptor-based pattern matching plugin using Halcon's
 *        create_uncalib_descriptor_model / find_uncalib_descriptor_model.
 *
 * Unlike edge or NCC matching, this approach detects interest points on the
 * template (Lepetit, Harris, or Harris-binomial) and finds the same point
 * distribution in the search image. It recovers a full 2D projective
 * transform (HHomMat2D), so it handles:
 *  - Heavy texture
 *  - Scale and limited perspective changes (no explicit angle range needed)
 *  - Partial occlusion / clutter
 *
 * Returns x, y, and theta (rotation) per match by decomposing the homography.
 */
class HalconPatternMatchingDescriptorPlugin : public TemplateMatchingBase {
public:
    HalconPatternMatchingDescriptorPlugin();
    ~HalconPatternMatchingDescriptorPlugin();

    // IVisionPlugin interface
    bool execute(const cv::Mat& input, cv::Mat& output, VisionResult& result) override;
    bool validate() const override;
    QWidget* createConfigWidget(QWidget* parent = nullptr) override;
    nlohmann::json getParameters() const override;
    void setParameters(const nlohmann::json& params) override;
    nlohmann::json getDefaultParameters() const override;
    ParameterSchema getParameterSchema() const override;

    // TemplateMatchingBase interface
    bool teachFromImage(const cv::Mat& templateImage) override;
    bool hasTemplate() const override { return m_hasModel; }
    void clearTemplate() override;

protected:
    std::string getTemplateFileSuffix() const override { return "_halcon_descriptor_template.png"; }

private:
    bool m_hasModel = false;
    HalconCpp::HDescriptorModel m_descriptorModel;
    int m_templateWidth = 0;
    int m_templateHeight = 0;

    // Algorithm parameters
    double m_minScore = 0.25;                      // Min score as inlier ratio (0..1) — Halcon default; 0.10 too permissive (accepts noise), 0.40+ strict
    int m_maxMatches = 1;                          // Max matches to find
    std::string m_detectorType = "lepetit";        // lepetit | harris | harris_binomial
    std::string m_scoreType = "inlier_ratio";      // inlier_ratio | num_points (num_points has [0,1] restriction in some Halcon versions)

    // Widgets (not owned)
    QWidget* m_configWidget = nullptr;
    QDoubleSpinBox* m_minScoreSpin = nullptr;
    QSpinBox* m_maxMatchesSpin = nullptr;
    QComboBox* m_detectorCombo = nullptr;
    QComboBox* m_scoreTypeCombo = nullptr;
};

#endif // HAS_OPENCV && HAS_HALCON

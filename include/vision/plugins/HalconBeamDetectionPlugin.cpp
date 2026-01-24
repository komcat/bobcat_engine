// HalconBeamDetectionPlugin.cpp
// Beam/spot detection using Halcon Aurora beam detection algorithms
#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#include "HalconBeamDetectionPlugin.h"
#include "../VisionManager.h"
#include <opencv2/imgproc.hpp>
#include <halconcpp/HalconCpp.h>
#include <iostream>
#include <cmath>

using namespace HalconCpp;

HalconBeamDetectionPlugin::HalconBeamDetectionPlugin()
    : VisionPluginBase("BeamDetect (Halcon)", "Measurement",
                       "Detect beam spots with position, size, intensity, and FWHM axes")
{
}

HalconBeamDetectionPlugin::~HalconBeamDetectionPlugin()
{
}

bool HalconBeamDetectionPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
{
    if (input.empty()) {
        m_lastError = "Input image is empty";
        return false;
    }

    try {
        // Convert OpenCV Mat to grayscale if needed
        cv::Mat gray;
        if (input.channels() == 3) {
            cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
        } else if (input.channels() == 1) {
            gray = input;
        } else {
            m_lastError = "Unsupported number of channels: " + std::to_string(input.channels());
            return false;
        }

        // Ensure continuous data for Halcon
        if (!gray.isContinuous()) {
            gray = gray.clone();
        }

        // Create Halcon image from OpenCV Mat data
        HImage halconImage;
        halconImage.GenImage1("byte", static_cast<Hlong>(gray.cols),
                              static_cast<Hlong>(gray.rows),
                              static_cast<void*>(gray.data));

        // Step 1: Threshold to find bright regions above minGrayValue
        HRegion brightRegions = halconImage.Threshold(m_minGrayValue, 255);

        // Step 2: Separate individual spots (connected components)
        HRegion connectedRegions = brightRegions.Connection();

        // Step 3: Filter by area
        HRegion beamRegions = connectedRegions.SelectShape("area", "and",
                                                            static_cast<double>(m_minBeamArea),
                                                            static_cast<double>(m_maxBeamArea));

        // Count detected beams
        Hlong numBeams = beamRegions.CountObj();

        std::cout << "[HalconBeamDetection] Found " << numBeams << " beam candidate regions" << std::endl;

        // Prepare output image (convert grayscale to BGR for overlays)
        if (input.channels() == 1) {
            cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
        } else {
            output = input.clone();
        }

        // Get calibration from VisionManager
        double mmPerPixelX = VisionManager::instance().getPixelToMmX();
        double mmPerPixelY = VisionManager::instance().getPixelToMmY();
        double mmPerPixel = (mmPerPixelX + mmPerPixelY) / 2.0;  // Average for area calculation

        int beamCount = 0;

        // Process each detected beam
        for (Hlong i = 1; i <= numBeams && beamCount < m_maxBeams; i++) {
            HRegion singleBeam = beamRegions.SelectObj(i);

            // Get basic area and centroid using global function
            HTuple area, row, col;
            AreaCenter(singleBeam, &area, &row, &col);

            if (area.D() < m_minBeamArea) {
                continue;
            }

            // Get intensity-weighted centroid (photocenter) - more accurate for beam center
            HTuple areaGray, gaussRow, gaussCol;
            AreaCenterGray(singleBeam, halconImage, &areaGray, &gaussRow, &gaussCol);

            // Get intensity statistics
            HTuple minVal, maxVal, rangeVal;
            MinMaxGray(singleBeam, halconImage, 0, &minVal, &maxVal, &rangeVal);

            HTuple meanIntensity, deviation;
            Intensity(singleBeam, halconImage, &meanIntensity, &deviation);

            // Calculate FWHM (Full Width at Half Maximum)
            // Create region for half-max threshold
            double halfMaxThreshold = maxVal.D() / 2.0;
            if (halfMaxThreshold < m_minGrayValue) {
                halfMaxThreshold = m_minGrayValue;
            }

            // Get the intersection of beam region with half-max threshold
            HRegion fwhmRegion;
            try {
                // Create a cropped image of just the beam area
                HTuple beamRow1, beamCol1, beamRow2, beamCol2;
                SmallestRectangle1(singleBeam, &beamRow1, &beamCol1, &beamRow2, &beamCol2);

                // Threshold within the beam region for FWHM
                HRegion halfMaxRegion = halconImage.Threshold(static_cast<Hlong>(halfMaxThreshold), 255);
                fwhmRegion = singleBeam.Intersection(halfMaxRegion);
            }
            catch (HException&) {
                // If FWHM calculation fails, use the original beam region
                fwhmRegion = singleBeam;
            }

            // Fit ellipse to FWHM region to get axes
            HTuple ra, rb, phi;  // ra = semi-major, rb = semi-minor, phi = angle in radians
            try {
                EllipticAxis(fwhmRegion, &ra, &rb, &phi);
            }
            catch (HException&) {
                // Fallback: use beam region if FWHM region ellipse fitting fails
                EllipticAxis(singleBeam, &ra, &rb, &phi);
            }

            // Build BeamResult
            BeamResult beam;
            beam.name = "beam_" + std::to_string(beamCount);

            // Position (use intensity-weighted centroid)
            // Note: Halcon uses (row, col) = (y, x), convert to (x, y)
            beam.centerPx = cv::Point2d(gaussCol.D(), gaussRow.D());
            beam.centerMm = cv::Point2d(gaussCol.D() * mmPerPixelX, gaussRow.D() * mmPerPixelY);

            // Area
            beam.areaPx = area.D();
            beam.areaMm2 = area.D() * mmPerPixel * mmPerPixel;

            // Intensity
            beam.meanIntensity = meanIntensity.D();
            beam.maxIntensity = maxVal.D();
            beam.minIntensity = minVal.D();
            beam.isSaturated = (maxVal.D() >= m_saturationThreshold);

            // FWHM axes - multiply by 2 to get full width (EllipticAxis returns semi-axes)
            beam.fwhmMajorPx = ra.D() * 2.0;
            beam.fwhmMinorPx = rb.D() * 2.0;
            beam.fwhmMajorMm = beam.fwhmMajorPx * mmPerPixel;
            beam.fwhmMinorMm = beam.fwhmMinorPx * mmPerPixel;

            // Orientation
            beam.angleRad = phi.D();
            beam.angleDeg = phi.D() * 180.0 / CV_PI;

            // Ellipticity (1.0 = perfect circle, 0.0 = line)
            if (beam.fwhmMajorPx > 0) {
                beam.ellipticity = beam.fwhmMinorPx / beam.fwhmMajorPx;
            }

            beam.confidence = 1.0;

            // Add to results
            result.addBeam(beam);

            // Draw overlays
            cv::Scalar beamColor = beam.isSaturated ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);  // Red if saturated

            // Draw centroid
            if (m_showCentroid) {
                result.addPointOverlay(beam.centerPx, cv::Scalar(255, 0, 0), 8);  // Blue crosshair
                // Draw crosshair lines
                result.addLineOverlay(cv::Point2d(beam.centerPx.x - 15, beam.centerPx.y),
                                      cv::Point2d(beam.centerPx.x + 15, beam.centerPx.y),
                                      cv::Scalar(255, 0, 0), 1);
                result.addLineOverlay(cv::Point2d(beam.centerPx.x, beam.centerPx.y - 15),
                                      cv::Point2d(beam.centerPx.x, beam.centerPx.y + 15),
                                      cv::Scalar(255, 0, 0), 1);
            }

            // Draw FWHM ellipse
            if (m_showFWHMEllipse) {
                result.addEllipseOverlay(beam.centerPx,
                                         beam.fwhmMajorPx / 2.0,  // Semi-major axis
                                         beam.fwhmMinorPx / 2.0,  // Semi-minor axis
                                         beam.angleDeg,
                                         beamColor, 2);
            }

            // Draw text label
            if (m_showLabels) {
                char label[128];
                snprintf(label, sizeof(label), "B%d (%.1f,%.1f)mm", beamCount,
                         beam.centerMm.x, beam.centerMm.y);
                result.addTextOverlay(label,
                                      cv::Point2d(beam.centerPx.x + 20, beam.centerPx.y - 20),
                                      cv::Scalar(255, 255, 0), 1);

                char sizeLabel[128];
                snprintf(sizeLabel, sizeof(sizeLabel), "FWHM: %.1fx%.1fpx", beam.fwhmMajorPx, beam.fwhmMinorPx);
                result.addTextOverlay(sizeLabel,
                                      cv::Point2d(beam.centerPx.x + 20, beam.centerPx.y),
                                      cv::Scalar(255, 255, 0), 1);

                if (beam.isSaturated) {
                    result.addTextOverlay("[SATURATED]",
                                          cv::Point2d(beam.centerPx.x + 20, beam.centerPx.y + 20),
                                          cv::Scalar(0, 0, 255), 1);
                }
            }

            beamCount++;
        }

        // Add measurements
        result.addMeasurement("beam_count", static_cast<double>(beamCount));

        if (beamCount > 0) {
            // Calculate averages
            double totalIntensity = 0;
            double totalFWHMMajor = 0;
            double totalFWHMMinor = 0;
            int saturatedCount = 0;

            for (const auto& b : result.beams) {
                totalIntensity += b.meanIntensity;
                totalFWHMMajor += b.fwhmMajorPx;
                totalFWHMMinor += b.fwhmMinorPx;
                if (b.isSaturated) saturatedCount++;
            }

            result.addMeasurement("avg_intensity", totalIntensity / beamCount);
            result.addMeasurement("avg_fwhm_major_px", totalFWHMMajor / beamCount);
            result.addMeasurement("avg_fwhm_minor_px", totalFWHMMinor / beamCount);
            result.addMeasurement("avg_fwhm_major_mm", (totalFWHMMajor / beamCount) * mmPerPixel);
            result.addMeasurement("avg_fwhm_minor_mm", (totalFWHMMinor / beamCount) * mmPerPixel);
            result.addMeasurement("saturated_count", static_cast<double>(saturatedCount));

            // For single beam, add direct measurements
            if (beamCount == 1) {
                const auto& b = result.beams[0];
                result.addMeasurement("center_x_mm", b.centerMm.x);
                result.addMeasurement("center_y_mm", b.centerMm.y);
                result.addMeasurement("center_x_px", b.centerPx.x);
                result.addMeasurement("center_y_px", b.centerPx.y);
                result.addMeasurement("fwhm_major_mm", b.fwhmMajorMm);
                result.addMeasurement("fwhm_minor_mm", b.fwhmMinorMm);
                result.addMeasurement("intensity_mean", b.meanIntensity);
                result.addMeasurement("intensity_max", b.maxIntensity);
                result.addMeasurement("angle_deg", b.angleDeg);
                result.addMeasurement("ellipticity", b.ellipticity);
                result.addMeasurement("area_px", b.areaPx);
                result.addMeasurement("area_mm2", b.areaMm2);
            }
        }

        // Add metadata
        result.addMetadata("algorithm", "Halcon_BeamDetection");
        result.addMetadata("min_gray_value", std::to_string(m_minGrayValue));
        result.addMetadata("min_beam_area", std::to_string(m_minBeamArea));
        result.addMetadata("max_beam_area", std::to_string(m_maxBeamArea));
        result.addMetadata("saturation_threshold", std::to_string(m_saturationThreshold));
        result.addMetadata("mm_per_pixel", std::to_string(mmPerPixel));

        std::cout << "[HalconBeamDetection] Detected " << beamCount << " beams" << std::endl;
        return true;
    }
    catch (HException& ex) {
        m_lastError = "Halcon error: " + std::string(ex.ErrorMessage().Text());
        std::cerr << "[HalconBeamDetection] " << m_lastError << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        m_lastError = "Error: " + std::string(e.what());
        std::cerr << "[HalconBeamDetection] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconBeamDetectionPlugin::validate() const
{
    if (m_minGrayValue < 0 || m_minGrayValue > 255) {
        m_lastError = "minGrayValue must be between 0 and 255";
        return false;
    }
    if (m_minBeamArea < 1) {
        m_lastError = "minBeamArea must be at least 1";
        return false;
    }
    if (m_maxBeamArea <= m_minBeamArea) {
        m_lastError = "maxBeamArea must be greater than minBeamArea";
        return false;
    }
    if (m_maxBeams <= 0) {
        m_lastError = "maxBeams must be positive";
        return false;
    }
    if (m_saturationThreshold < 1 || m_saturationThreshold > 255) {
        m_lastError = "saturationThreshold must be between 1 and 255";
        return false;
    }
    return true;
}

QWidget* HalconBeamDetectionPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Detection Parameters Group
    QGroupBox* detectionGroup = new QGroupBox("Detection Parameters");
    QFormLayout* detectionLayout = new QFormLayout(detectionGroup);

    // Min Gray Value (threshold)
    m_minGrayValueSpin = new QSpinBox();
    m_minGrayValueSpin->setRange(0, 255);
    m_minGrayValueSpin->setSingleStep(5);
    m_minGrayValueSpin->setValue(m_minGrayValue);
    m_minGrayValueSpin->setToolTip("Minimum gray value threshold for beam detection.\n"
                                    "Pixels above this value are considered part of a beam.");
    detectionLayout->addRow("Min Gray Value:", m_minGrayValueSpin);

    // Min Beam Area
    m_minBeamAreaSpin = new QSpinBox();
    m_minBeamAreaSpin->setRange(1, 100000);
    m_minBeamAreaSpin->setSingleStep(10);
    m_minBeamAreaSpin->setValue(m_minBeamArea);
    m_minBeamAreaSpin->setToolTip("Minimum beam area in pixels².\n"
                                   "Regions smaller than this are filtered out (noise rejection).");
    detectionLayout->addRow("Min Area (px²):", m_minBeamAreaSpin);

    // Max Beam Area
    m_maxBeamAreaSpin = new QSpinBox();
    m_maxBeamAreaSpin->setRange(1, 10000000);
    m_maxBeamAreaSpin->setSingleStep(1000);
    m_maxBeamAreaSpin->setValue(m_maxBeamArea);
    m_maxBeamAreaSpin->setToolTip("Maximum beam area in pixels².\n"
                                   "Regions larger than this are filtered out.");
    detectionLayout->addRow("Max Area (px²):", m_maxBeamAreaSpin);

    // Max Beams
    m_maxBeamsSpin = new QSpinBox();
    m_maxBeamsSpin->setRange(1, 100);
    m_maxBeamsSpin->setSingleStep(1);
    m_maxBeamsSpin->setValue(m_maxBeams);
    m_maxBeamsSpin->setToolTip("Maximum number of beams to detect and return.");
    detectionLayout->addRow("Max Beams:", m_maxBeamsSpin);

    // Saturation Threshold
    m_saturationThresholdSpin = new QSpinBox();
    m_saturationThresholdSpin->setRange(1, 255);
    m_saturationThresholdSpin->setSingleStep(5);
    m_saturationThresholdSpin->setValue(m_saturationThreshold);
    m_saturationThresholdSpin->setToolTip("Gray value threshold for saturation warning.\n"
                                           "Beams with max intensity >= this value are flagged as saturated.");
    detectionLayout->addRow("Saturation Threshold:", m_saturationThresholdSpin);

    mainLayout->addWidget(detectionGroup);

    // Display Options Group
    QGroupBox* displayGroup = new QGroupBox("Display Options");
    QVBoxLayout* displayLayout = new QVBoxLayout(displayGroup);

    m_showFWHMEllipseCheck = new QCheckBox("Show FWHM ellipse");
    m_showFWHMEllipseCheck->setChecked(m_showFWHMEllipse);
    m_showFWHMEllipseCheck->setToolTip("Draw ellipse representing FWHM (Full Width at Half Maximum) of beam.");
    displayLayout->addWidget(m_showFWHMEllipseCheck);

    m_showCentroidCheck = new QCheckBox("Show centroid marker");
    m_showCentroidCheck->setChecked(m_showCentroid);
    m_showCentroidCheck->setToolTip("Draw crosshair at beam centroid (intensity-weighted center).");
    displayLayout->addWidget(m_showCentroidCheck);

    m_showLabelsCheck = new QCheckBox("Show text labels");
    m_showLabelsCheck->setChecked(m_showLabels);
    m_showLabelsCheck->setToolTip("Display beam ID, position, and FWHM values as text.");
    displayLayout->addWidget(m_showLabelsCheck);

    mainLayout->addWidget(displayGroup);

    // Info label
    QLabel* infoLabel = new QLabel(
        "<b>Output:</b> Position (mm), Area (px²/mm²), Intensity,\n"
        "FWHM axes (major/minor), Angle, Ellipticity, Saturation flag");
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 10px;");
    mainLayout->addWidget(infoLabel);

    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_minGrayValueSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_minGrayValue = val; });
    QObject::connect(m_minBeamAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_minBeamArea = val; });
    QObject::connect(m_maxBeamAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxBeamArea = val; });
    QObject::connect(m_maxBeamsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxBeams = val; });
    QObject::connect(m_saturationThresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_saturationThreshold = val; });
    QObject::connect(m_showFWHMEllipseCheck, &QCheckBox::toggled,
            [this](bool checked) { m_showFWHMEllipse = checked; });
    QObject::connect(m_showCentroidCheck, &QCheckBox::toggled,
            [this](bool checked) { m_showCentroid = checked; });
    QObject::connect(m_showLabelsCheck, &QCheckBox::toggled,
            [this](bool checked) { m_showLabels = checked; });

    return m_configWidget;
}

nlohmann::json HalconBeamDetectionPlugin::getParameters() const
{
    nlohmann::json params;
    params["minGrayValue"] = m_minGrayValue;
    params["minBeamArea"] = m_minBeamArea;
    params["maxBeamArea"] = m_maxBeamArea;
    params["maxBeams"] = m_maxBeams;
    params["saturationThreshold"] = m_saturationThreshold;
    params["showFWHMEllipse"] = m_showFWHMEllipse;
    params["showCentroid"] = m_showCentroid;
    params["showLabels"] = m_showLabels;
    return params;
}

void HalconBeamDetectionPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("minGrayValue")) m_minGrayValue = params["minGrayValue"].get<int>();
    if (params.contains("minBeamArea")) m_minBeamArea = params["minBeamArea"].get<int>();
    if (params.contains("maxBeamArea")) m_maxBeamArea = params["maxBeamArea"].get<int>();
    if (params.contains("maxBeams")) m_maxBeams = params["maxBeams"].get<int>();
    if (params.contains("saturationThreshold")) m_saturationThreshold = params["saturationThreshold"].get<int>();
    if (params.contains("showFWHMEllipse")) m_showFWHMEllipse = params["showFWHMEllipse"].get<bool>();
    if (params.contains("showCentroid")) m_showCentroid = params["showCentroid"].get<bool>();
    if (params.contains("showLabels")) m_showLabels = params["showLabels"].get<bool>();

    // Update UI if widgets exist
    if (m_minGrayValueSpin) m_minGrayValueSpin->setValue(m_minGrayValue);
    if (m_minBeamAreaSpin) m_minBeamAreaSpin->setValue(m_minBeamArea);
    if (m_maxBeamAreaSpin) m_maxBeamAreaSpin->setValue(m_maxBeamArea);
    if (m_maxBeamsSpin) m_maxBeamsSpin->setValue(m_maxBeams);
    if (m_saturationThresholdSpin) m_saturationThresholdSpin->setValue(m_saturationThreshold);
    if (m_showFWHMEllipseCheck) m_showFWHMEllipseCheck->setChecked(m_showFWHMEllipse);
    if (m_showCentroidCheck) m_showCentroidCheck->setChecked(m_showCentroid);
    if (m_showLabelsCheck) m_showLabelsCheck->setChecked(m_showLabels);
}

nlohmann::json HalconBeamDetectionPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["minGrayValue"] = 80;
    params["minBeamArea"] = 20;
    params["maxBeamArea"] = 50000;
    params["maxBeams"] = 10;
    params["saturationThreshold"] = 250;
    params["showFWHMEllipse"] = true;
    params["showCentroid"] = true;
    params["showLabels"] = true;
    return params;
}

ParameterSchema HalconBeamDetectionPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Detection Parameters
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minGrayValue", "Min Gray Value",
        0, 255, 80,
        "Minimum gray value threshold for beam detection. "
        "Pixels above this value are considered part of a beam.",
        "", 5,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minBeamArea", "Min Beam Area",
        1, 100000, 20,
        "Minimum beam area in pixels² for noise rejection.",
        "px²", 10,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxBeamArea", "Max Beam Area",
        1, 10000000, 50000,
        "Maximum beam area in pixels².",
        "px²", 1000,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxBeams", "Max Beams",
        1, 100, 10,
        "Maximum number of beams to detect.",
        "", 1,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "saturationThreshold", "Saturation Threshold",
        1, 255, 250,
        "Gray value threshold for saturation warning. "
        "Beams with max intensity >= this value are flagged as saturated.",
        "", 5,
        "Detection"
    ));

    // Display Options
    schema.parameters.push_back(ParameterDescriptor::createBool(
        "showFWHMEllipse", "Show FWHM Ellipse",
        true,
        "Draw ellipse representing FWHM (Full Width at Half Maximum) of beam.",
        "Display"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "showCentroid", "Show Centroid",
        true,
        "Draw crosshair at beam centroid (intensity-weighted center).",
        "Display"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "showLabels", "Show Labels",
        true,
        "Display beam ID, position, and FWHM values as text.",
        "Display"
    ));

    return schema;
}

#endif // HAS_OPENCV && HAS_HALCON

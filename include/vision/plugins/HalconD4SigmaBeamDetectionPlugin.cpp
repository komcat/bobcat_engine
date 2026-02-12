// HalconD4SigmaBeamDetectionPlugin.cpp
// D4sigma (ISO 11146 second-moment) beam detection using Halcon
#if defined(HAS_OPENCV) && defined(HAS_HALCON)

#include "HalconD4SigmaBeamDetectionPlugin.h"
#include "../VisionManager.h"
#include <opencv2/imgproc.hpp>
#include <halconcpp/HalconCpp.h>
#include <iostream>
#include <cmath>
#include <algorithm>

using namespace HalconCpp;

HalconD4SigmaBeamDetectionPlugin::HalconD4SigmaBeamDetectionPlugin()
    : VisionPluginBase("D4Sigma BeamDetect (Halcon)", "Measurement",
                       "ISO 11146 D4sigma beam width measurement with centroid and saturation detection")
{
}

HalconD4SigmaBeamDetectionPlugin::~HalconD4SigmaBeamDetectionPlugin()
{
}

bool HalconD4SigmaBeamDetectionPlugin::execute(const cv::Mat& input, cv::Mat& output, VisionResult& result)
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

        Hlong W = static_cast<Hlong>(gray.cols);
        Hlong H = static_cast<Hlong>(gray.rows);

        // Step 0: Create Halcon image from OpenCV Mat data
        HImage halconImage;
        halconImage.GenImage1("byte", W, H, static_cast<void*>(gray.data));

        // Step 1: Background estimation from corner regions
        int bgBox = std::min(m_bgBoxSize, static_cast<int>(std::min(W, H) / 2));

        HRegion roiTL, roiBR, roiBg;
        roiTL.GenRectangle1(0, 0, bgBox - 1, bgBox - 1);
        roiBR.GenRectangle1(H - bgBox, W - bgBox, H - 1, W - 1);
        roiBg = roiTL.Union2(roiBR);

        HTuple bgMean, bgDev;
        Intensity(roiBg, halconImage, &bgMean, &bgDev);
        double bgMeanVal = bgMean.D();

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[HalconD4SigmaBeamDetection] Background mean: " << bgMeanVal
                      << " +/- " << bgDev.D() << std::endl;
        }

        // Step 2: Prepare images for D4sigma computation
        // Convert to real (float) for math operations
        HImage imageReal = halconImage.ConvertImageType("real");
        // Signal image = original - background
        HImage signalImage = imageReal.ScaleImage(1.0, -bgMeanVal);

        // Coordinate images: GenImageSurfaceFirstOrder("real", alpha, beta, gamma, row0, col0, width, height)
        // pixel_value = alpha * (row - row0) + beta * (col - col0) + gamma
        // Row image: pixel value = row coordinate
        HImage rowImage, colImage;
        rowImage.GenImageSurfaceFirstOrder("real", 1.0, 0.0, 0.0, 0, 0, W, H);
        // Col image: pixel value = col coordinate
        colImage.GenImageSurfaceFirstOrder("real", 0.0, 1.0, 0.0, 0, 0, W, H);

        // Step 3: Find beam candidates
        Hlong threshVal = static_cast<Hlong>(bgMeanVal + m_minSignalAboveBg);
        if (threshVal > 255) threshVal = 255;

        HRegion brightRegions = halconImage.Threshold(threshVal, 255);
        HRegion connectedRegions = brightRegions.Connection();
        HRegion beamRegions = connectedRegions.SelectShape("area", "and",
                                                            static_cast<double>(m_minBeamArea),
                                                            static_cast<double>(m_maxBeamArea));

        Hlong numBeams = beamRegions.CountObj();

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[HalconD4SigmaBeamDetection] Found " << numBeams << " beam candidate regions" << std::endl;
        }

        // Prepare output image (convert grayscale to BGR for overlays)
        if (input.channels() == 1) {
            cv::cvtColor(input, output, cv::COLOR_GRAY2BGR);
        } else {
            output = input.clone();
        }

        // Get calibration from VisionManager
        double mmPerPixelX = VisionManager::instance().getPixelToMmX();
        double mmPerPixelY = VisionManager::instance().getPixelToMmY();
        double mmPerPixel = (mmPerPixelX + mmPerPixelY) / 2.0;

        int beamCount = 0;

        // Step 4: Process each detected beam with D4sigma
        for (Hlong i = 1; i <= numBeams && beamCount < m_maxBeams; i++) {
            HRegion beam = beamRegions.SelectObj(i);

            // 4a: Get peak signal in beam region
            HTuple minSig, maxSig, rangeSig;
            MinMaxGray(beam, signalImage, 0, &minSig, &maxSig, &rangeSig);
            double peakSignal = maxSig.D();

            if (peakSignal <= 0) {
                continue;  // No signal above background
            }

            // 4b: Expand bounding box for wing capture
            HTuple r1, c1, r2, c2;
            SmallestRectangle1(beam, &r1, &c1, &r2, &c2);

            double beamH = r2.D() - r1.D();
            double beamW = c2.D() - c1.D();

            double expR1 = std::max(0.0, r1.D() - beamH * m_roiExpandFactor);
            double expC1 = std::max(0.0, c1.D() - beamW * m_roiExpandFactor);
            double expR2 = std::min(static_cast<double>(H - 1), r2.D() + beamH * m_roiExpandFactor);
            double expC2 = std::min(static_cast<double>(W - 1), c2.D() + beamW * m_roiExpandFactor);

            HRegion expandedROI;
            expandedROI.GenRectangle1(expR1, expC1, expR2, expC2);

            // 4c: D4sigma integration region (threshold at fraction of peak)
            double d4ThreshVal = m_d4sigmaThresh * peakSignal;
            HImage signalROI = signalImage.ReduceDomain(expandedROI);

            // Threshold: keep pixels with signal >= d4ThreshVal
            // Use a large upper bound for real-valued images
            HRegion d4Region = signalROI.Threshold(d4ThreshVal, 1e9);

            // Check that we have pixels in the D4 region
            HTuple d4Area, d4Row, d4Col;
            AreaCenter(d4Region, &d4Area, &d4Row, &d4Col);
            if (d4Area.I() < 1) {
                continue;
            }

            // 4d: Reduce images to D4 region
            HImage sigD4 = signalImage.ReduceDomain(d4Region);
            HImage rowD4 = rowImage.ReduceDomain(d4Region);
            HImage colD4 = colImage.ReduceDomain(d4Region);

            // 4e: Centroid = mean(I*coord) / mean(I)
            // Weighted row
            HImage wRow = sigD4.MultImage(rowD4, 1.0, 0.0);
            HTuple meanWR, devWR;
            Intensity(d4Region, wRow, &meanWR, &devWR);

            // Weighted col
            HImage wCol = sigD4.MultImage(colD4, 1.0, 0.0);
            HTuple meanWC, devWC;
            Intensity(d4Region, wCol, &meanWC, &devWC);

            // Mean signal
            HTuple meanSig, devSig;
            Intensity(d4Region, sigD4, &meanSig, &devSig);

            if (std::abs(meanSig.D()) < 1e-12) {
                continue;  // Avoid division by zero
            }

            double centroidRow = meanWR.D() / meanSig.D();
            double centroidCol = meanWC.D() / meanSig.D();

            // 4f: D4sigma = 4 * sqrt(mean(I*(coord-center)^2) / mean(I))
            // Row (Y) variance
            HImage rowDiff = rowD4.ScaleImage(1.0, -centroidRow);
            HImage rowDiff2 = rowDiff.MultImage(rowDiff, 1.0, 0.0);
            HImage wDR2 = sigD4.MultImage(rowDiff2, 1.0, 0.0);
            HTuple meanWDR2, devWDR2;
            Intensity(d4Region, wDR2, &meanWDR2, &devWDR2);

            // Col (X) variance
            HImage colDiff = colD4.ScaleImage(1.0, -centroidCol);
            HImage colDiff2 = colDiff.MultImage(colDiff, 1.0, 0.0);
            HImage wDC2 = sigD4.MultImage(colDiff2, 1.0, 0.0);
            HTuple meanWDC2, devWDC2;
            Intensity(d4Region, wDC2, &meanWDC2, &devWDC2);

            double varRow = meanWDR2.D() / meanSig.D();
            double varCol = meanWDC2.D() / meanSig.D();

            if (varRow < 0 || varCol < 0) {
                continue;  // Invalid variance
            }

            double d4sigmaY = 4.0 * std::sqrt(varRow);  // Y direction (rows)
            double d4sigmaX = 4.0 * std::sqrt(varCol);  // X direction (columns)

            // Get intensity statistics from original image (byte)
            HTuple minVal, maxVal, rangeVal;
            MinMaxGray(beam, halconImage, 0, &minVal, &maxVal, &rangeVal);

            HTuple meanIntensity, deviation;
            Intensity(beam, halconImage, &meanIntensity, &deviation);

            // Build BeamResult
            BeamResult beamResult;
            beamResult.name = "beam_" + std::to_string(beamCount);

            // Position: centroid from D4sigma (Halcon row,col -> OpenCV x,y)
            beamResult.centerPx = cv::Point2d(centroidCol, centroidRow);
            beamResult.centerMm = cv::Point2d(centroidCol * mmPerPixelX, centroidRow * mmPerPixelY);

            // Area from thresholded region
            beamResult.areaPx = d4Area.D();
            beamResult.areaMm2 = d4Area.D() * mmPerPixel * mmPerPixel;

            // Intensity
            beamResult.meanIntensity = meanIntensity.D();
            beamResult.maxIntensity = maxVal.D();
            beamResult.minIntensity = minVal.D();
            beamResult.isSaturated = (maxVal.D() >= m_saturationThreshold);

            // Store D4sigma values in FWHM fields for compatibility with BeamResult struct
            // Note: D4sigma != FWHM, but these are the available fields
            beamResult.fwhmMajorPx = std::max(d4sigmaX, d4sigmaY);
            beamResult.fwhmMinorPx = std::min(d4sigmaX, d4sigmaY);
            beamResult.fwhmMajorMm = beamResult.fwhmMajorPx * mmPerPixel;
            beamResult.fwhmMinorMm = beamResult.fwhmMinorPx * mmPerPixel;

            // Orientation: 0 if X > Y, 90 if Y > X (axis-aligned D4sigma)
            beamResult.angleDeg = (d4sigmaX >= d4sigmaY) ? 0.0 : 90.0;
            beamResult.angleRad = beamResult.angleDeg * CV_PI / 180.0;

            // Ellipticity
            if (beamResult.fwhmMajorPx > 0) {
                beamResult.ellipticity = beamResult.fwhmMinorPx / beamResult.fwhmMajorPx;
            }

            beamResult.confidence = 1.0;

            // Add to results
            result.addBeam(beamResult);

            // Add per-beam D4sigma-specific measurements
            std::string prefix = "beam_" + std::to_string(beamCount) + "_";
            result.addMeasurement(prefix + "d4sigma_x_px", d4sigmaX);
            result.addMeasurement(prefix + "d4sigma_y_px", d4sigmaY);
            result.addMeasurement(prefix + "d4sigma_x_mm", d4sigmaX * mmPerPixelX);
            result.addMeasurement(prefix + "d4sigma_y_mm", d4sigmaY * mmPerPixelY);
            result.addMeasurement(prefix + "centroid_x_px", centroidCol);
            result.addMeasurement(prefix + "centroid_y_px", centroidRow);
            result.addMeasurement(prefix + "peak_signal", peakSignal);
            result.addMeasurement(prefix + "bg_mean", bgMeanVal);

            // Draw overlays
            cv::Scalar beamColor = beamResult.isSaturated ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);

            // Draw centroid crosshair (blue)
            if (m_showCentroid) {
                result.addPointOverlay(beamResult.centerPx, cv::Scalar(255, 0, 0), 8);
                result.addLineOverlay(cv::Point2d(centroidCol - 15, centroidRow),
                                      cv::Point2d(centroidCol + 15, centroidRow),
                                      cv::Scalar(255, 0, 0), 1);
                result.addLineOverlay(cv::Point2d(centroidCol, centroidRow - 15),
                                      cv::Point2d(centroidCol, centroidRow + 15),
                                      cv::Scalar(255, 0, 0), 1);
            }

            // Draw D4sigma extent cross-lines with end-ticks
            if (m_showD4SigmaCross) {
                double halfX = d4sigmaX / 2.0;
                double halfY = d4sigmaY / 2.0;
                double tickLen = 5.0;

                // Horizontal D4sigma line (X direction, green or red if saturated)
                result.addLineOverlay(cv::Point2d(centroidCol - halfX, centroidRow),
                                      cv::Point2d(centroidCol + halfX, centroidRow),
                                      beamColor, 2);
                // Left tick
                result.addLineOverlay(cv::Point2d(centroidCol - halfX, centroidRow - tickLen),
                                      cv::Point2d(centroidCol - halfX, centroidRow + tickLen),
                                      beamColor, 2);
                // Right tick
                result.addLineOverlay(cv::Point2d(centroidCol + halfX, centroidRow - tickLen),
                                      cv::Point2d(centroidCol + halfX, centroidRow + tickLen),
                                      beamColor, 2);

                // Vertical D4sigma line (Y direction)
                result.addLineOverlay(cv::Point2d(centroidCol, centroidRow - halfY),
                                      cv::Point2d(centroidCol, centroidRow + halfY),
                                      beamColor, 2);
                // Top tick
                result.addLineOverlay(cv::Point2d(centroidCol - tickLen, centroidRow - halfY),
                                      cv::Point2d(centroidCol + tickLen, centroidRow - halfY),
                                      beamColor, 2);
                // Bottom tick
                result.addLineOverlay(cv::Point2d(centroidCol - tickLen, centroidRow + halfY),
                                      cv::Point2d(centroidCol + tickLen, centroidRow + halfY),
                                      beamColor, 2);
            }

            // Draw expanded ROI bounding box (yellow)
            if (m_showBoundingBox) {
                result.addLineOverlay(cv::Point2d(expC1, expR1), cv::Point2d(expC2, expR1),
                                      cv::Scalar(0, 255, 255), 1);
                result.addLineOverlay(cv::Point2d(expC2, expR1), cv::Point2d(expC2, expR2),
                                      cv::Scalar(0, 255, 255), 1);
                result.addLineOverlay(cv::Point2d(expC2, expR2), cv::Point2d(expC1, expR2),
                                      cv::Scalar(0, 255, 255), 1);
                result.addLineOverlay(cv::Point2d(expC1, expR2), cv::Point2d(expC1, expR1),
                                      cv::Scalar(0, 255, 255), 1);
            }

            // Draw text labels
            if (m_showLabels) {
                char label[128];
                snprintf(label, sizeof(label), "B%d (%.1f,%.1f)mm", beamCount,
                         beamResult.centerMm.x, beamResult.centerMm.y);
                result.addTextOverlay(label,
                                      cv::Point2d(centroidCol + 20, centroidRow - 20),
                                      cv::Scalar(255, 255, 0), 1);

                char sizeLabel[128];
                snprintf(sizeLabel, sizeof(sizeLabel), "D4s: %.1fx%.1fpx", d4sigmaX, d4sigmaY);
                result.addTextOverlay(sizeLabel,
                                      cv::Point2d(centroidCol + 20, centroidRow),
                                      cv::Scalar(255, 255, 0), 1);

                if (beamResult.isSaturated) {
                    result.addTextOverlay("[SATURATED]",
                                          cv::Point2d(centroidCol + 20, centroidRow + 20),
                                          cv::Scalar(0, 0, 255), 1);
                }
            }

            beamCount++;
        }

        // Add summary measurements
        result.addMeasurement("beam_count", static_cast<double>(beamCount));
        result.addMeasurement("background_mean", bgMeanVal);
        result.addMeasurement("background_dev", bgDev.D());

        if (beamCount > 0) {
            double totalIntensity = 0;
            double totalD4X = 0;
            double totalD4Y = 0;
            int saturatedCount = 0;

            for (int b = 0; b < beamCount; b++) {
                const auto& br = result.beams[b];
                totalIntensity += br.meanIntensity;
                if (br.isSaturated) saturatedCount++;

                std::string pfx = "beam_" + std::to_string(b) + "_";
                totalD4X += result.measurements[pfx + "d4sigma_x_px"];
                totalD4Y += result.measurements[pfx + "d4sigma_y_px"];
            }

            result.addMeasurement("avg_intensity", totalIntensity / beamCount);
            result.addMeasurement("avg_d4sigma_x_px", totalD4X / beamCount);
            result.addMeasurement("avg_d4sigma_y_px", totalD4Y / beamCount);
            result.addMeasurement("avg_d4sigma_x_mm", (totalD4X / beamCount) * mmPerPixelX);
            result.addMeasurement("avg_d4sigma_y_mm", (totalD4Y / beamCount) * mmPerPixelY);
            result.addMeasurement("saturated_count", static_cast<double>(saturatedCount));

            // For single beam, add direct convenience measurements
            if (beamCount == 1) {
                const auto& b = result.beams[0];
                result.addMeasurement("center_x_mm", b.centerMm.x);
                result.addMeasurement("center_y_mm", b.centerMm.y);
                result.addMeasurement("center_x_px", b.centerPx.x);
                result.addMeasurement("center_y_px", b.centerPx.y);
                result.addMeasurement("d4sigma_x_px", result.measurements["beam_0_d4sigma_x_px"]);
                result.addMeasurement("d4sigma_y_px", result.measurements["beam_0_d4sigma_y_px"]);
                result.addMeasurement("d4sigma_x_mm", result.measurements["beam_0_d4sigma_x_mm"]);
                result.addMeasurement("d4sigma_y_mm", result.measurements["beam_0_d4sigma_y_mm"]);
                result.addMeasurement("intensity_mean", b.meanIntensity);
                result.addMeasurement("intensity_max", b.maxIntensity);
                result.addMeasurement("area_px", b.areaPx);
                result.addMeasurement("area_mm2", b.areaMm2);
                result.addMeasurement("ellipticity", b.ellipticity);
            }
        }

        // Add metadata
        result.addMetadata("algorithm", "Halcon_D4Sigma_BeamDetection");
        result.addMetadata("min_signal_above_bg", std::to_string(m_minSignalAboveBg));
        result.addMetadata("min_beam_area", std::to_string(m_minBeamArea));
        result.addMetadata("max_beam_area", std::to_string(m_maxBeamArea));
        result.addMetadata("d4sigma_threshold", std::to_string(m_d4sigmaThresh));
        result.addMetadata("roi_expand_factor", std::to_string(m_roiExpandFactor));
        result.addMetadata("bg_box_size", std::to_string(m_bgBoxSize));
        result.addMetadata("saturation_threshold", std::to_string(m_saturationThreshold));
        result.addMetadata("mm_per_pixel_x", std::to_string(mmPerPixelX));
        result.addMetadata("mm_per_pixel_y", std::to_string(mmPerPixelY));

        if (VisionManager::instance().isVerboseLogging()) {
            std::cout << "[HalconD4SigmaBeamDetection] Detected " << beamCount << " beams" << std::endl;
        }
        return true;
    }
    catch (HException& ex) {
        m_lastError = "Halcon error: " + std::string(ex.ErrorMessage().Text());
        std::cerr << "[HalconD4SigmaBeamDetection] " << m_lastError << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        m_lastError = "Error: " + std::string(e.what());
        std::cerr << "[HalconD4SigmaBeamDetection] " << m_lastError << std::endl;
        return false;
    }
}

bool HalconD4SigmaBeamDetectionPlugin::validate() const
{
    if (m_minSignalAboveBg < 1 || m_minSignalAboveBg > 255) {
        m_lastError = "minSignalAboveBg must be between 1 and 255";
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
    if (m_d4sigmaThresh <= 0.0 || m_d4sigmaThresh >= 1.0) {
        m_lastError = "d4sigmaThresh must be between 0 and 1 (exclusive)";
        return false;
    }
    if (m_roiExpandFactor < 0.0 || m_roiExpandFactor > 10.0) {
        m_lastError = "roiExpandFactor must be between 0 and 10";
        return false;
    }
    if (m_bgBoxSize < 5 || m_bgBoxSize > 500) {
        m_lastError = "bgBoxSize must be between 5 and 500";
        return false;
    }
    if (m_saturationThreshold < 1 || m_saturationThreshold > 255) {
        m_lastError = "saturationThreshold must be between 1 and 255";
        return false;
    }
    return true;
}

QWidget* HalconD4SigmaBeamDetectionPlugin::createConfigWidget(QWidget* parent)
{
    m_configWidget = new QWidget(parent);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_configWidget);

    // Detection Parameters Group
    QGroupBox* detectionGroup = new QGroupBox("D4Sigma Detection Parameters");
    QFormLayout* detectionLayout = new QFormLayout(detectionGroup);

    // Min Signal Above Background
    m_minSignalAboveBgSpin = new QSpinBox();
    m_minSignalAboveBgSpin->setRange(1, 255);
    m_minSignalAboveBgSpin->setSingleStep(5);
    m_minSignalAboveBgSpin->setValue(m_minSignalAboveBg);
    m_minSignalAboveBgSpin->setToolTip("Minimum signal above background for beam finding.\n"
                                        "Threshold = background_mean + this value.");
    detectionLayout->addRow("Min Signal Above BG:", m_minSignalAboveBgSpin);

    // Min Beam Area
    m_minBeamAreaSpin = new QSpinBox();
    m_minBeamAreaSpin->setRange(1, 100000);
    m_minBeamAreaSpin->setSingleStep(10);
    m_minBeamAreaSpin->setValue(m_minBeamArea);
    m_minBeamAreaSpin->setToolTip("Minimum beam area in pixels^2.\n"
                                   "Regions smaller than this are filtered out (noise rejection).");
    detectionLayout->addRow("Min Area (px^2):", m_minBeamAreaSpin);

    // Max Beam Area
    m_maxBeamAreaSpin = new QSpinBox();
    m_maxBeamAreaSpin->setRange(1, 10000000);
    m_maxBeamAreaSpin->setSingleStep(1000);
    m_maxBeamAreaSpin->setValue(m_maxBeamArea);
    m_maxBeamAreaSpin->setToolTip("Maximum beam area in pixels^2.\n"
                                   "Regions larger than this are filtered out.");
    detectionLayout->addRow("Max Area (px^2):", m_maxBeamAreaSpin);

    // Max Beams
    m_maxBeamsSpin = new QSpinBox();
    m_maxBeamsSpin->setRange(1, 100);
    m_maxBeamsSpin->setSingleStep(1);
    m_maxBeamsSpin->setValue(m_maxBeams);
    m_maxBeamsSpin->setToolTip("Maximum number of beams to detect and return.");
    detectionLayout->addRow("Max Beams:", m_maxBeamsSpin);

    // D4sigma Threshold
    m_d4sigmaThreshSpin = new QDoubleSpinBox();
    m_d4sigmaThreshSpin->setRange(0.01, 0.99);
    m_d4sigmaThreshSpin->setSingleStep(0.05);
    m_d4sigmaThreshSpin->setDecimals(2);
    m_d4sigmaThreshSpin->setValue(m_d4sigmaThresh);
    m_d4sigmaThreshSpin->setToolTip("Fraction of peak signal for D4sigma integration region.\n"
                                     "Lower values include more of the beam wings.");
    detectionLayout->addRow("D4sigma Threshold:", m_d4sigmaThreshSpin);

    // ROI Expand Factor
    m_roiExpandFactorSpin = new QDoubleSpinBox();
    m_roiExpandFactorSpin->setRange(0.0, 10.0);
    m_roiExpandFactorSpin->setSingleStep(0.1);
    m_roiExpandFactorSpin->setDecimals(1);
    m_roiExpandFactorSpin->setValue(m_roiExpandFactor);
    m_roiExpandFactorSpin->setToolTip("Bounding box expansion factor for wing capture.\n"
                                       "Expands the initial beam bbox by this factor on each side.");
    detectionLayout->addRow("ROI Expand Factor:", m_roiExpandFactorSpin);

    // Background Box Size
    m_bgBoxSizeSpin = new QSpinBox();
    m_bgBoxSizeSpin->setRange(5, 500);
    m_bgBoxSizeSpin->setSingleStep(5);
    m_bgBoxSizeSpin->setValue(m_bgBoxSize);
    m_bgBoxSizeSpin->setToolTip("Corner box size (pixels) for background estimation.\n"
                                 "Background is estimated from top-left and bottom-right corners.");
    detectionLayout->addRow("BG Box Size (px):", m_bgBoxSizeSpin);

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

    m_showD4SigmaCrossCheck = new QCheckBox("Show D4sigma cross-lines");
    m_showD4SigmaCrossCheck->setChecked(m_showD4SigmaCross);
    m_showD4SigmaCrossCheck->setToolTip("Draw D4sigma extent lines with end-ticks at beam center.");
    displayLayout->addWidget(m_showD4SigmaCrossCheck);

    m_showCentroidCheck = new QCheckBox("Show centroid marker");
    m_showCentroidCheck->setChecked(m_showCentroid);
    m_showCentroidCheck->setToolTip("Draw crosshair at beam centroid (intensity-weighted center).");
    displayLayout->addWidget(m_showCentroidCheck);

    m_showLabelsCheck = new QCheckBox("Show text labels");
    m_showLabelsCheck->setChecked(m_showLabels);
    m_showLabelsCheck->setToolTip("Display beam ID, position, and D4sigma values as text.");
    displayLayout->addWidget(m_showLabelsCheck);

    m_showBoundingBoxCheck = new QCheckBox("Show expanded ROI box");
    m_showBoundingBoxCheck->setChecked(m_showBoundingBox);
    m_showBoundingBoxCheck->setToolTip("Draw the expanded bounding box used for D4sigma integration.");
    displayLayout->addWidget(m_showBoundingBoxCheck);

    mainLayout->addWidget(displayGroup);

    // Info label
    QLabel* infoLabel = new QLabel(
        "<b>Output:</b> D4sigma beam widths (ISO 11146), intensity-weighted\n"
        "centroid, peak signal, saturation flag, per-beam measurements");
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 10px;");
    mainLayout->addWidget(infoLabel);

    mainLayout->addStretch();

    // Connect signals
    QObject::connect(m_minSignalAboveBgSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_minSignalAboveBg = val; });
    QObject::connect(m_minBeamAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_minBeamArea = val; });
    QObject::connect(m_maxBeamAreaSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxBeamArea = val; });
    QObject::connect(m_maxBeamsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_maxBeams = val; });
    QObject::connect(m_d4sigmaThreshSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_d4sigmaThresh = val; });
    QObject::connect(m_roiExpandFactorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double val) { m_roiExpandFactor = val; });
    QObject::connect(m_bgBoxSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_bgBoxSize = val; });
    QObject::connect(m_saturationThresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int val) { m_saturationThreshold = val; });
    QObject::connect(m_showD4SigmaCrossCheck, &QCheckBox::toggled,
            [this](bool checked) { m_showD4SigmaCross = checked; });
    QObject::connect(m_showCentroidCheck, &QCheckBox::toggled,
            [this](bool checked) { m_showCentroid = checked; });
    QObject::connect(m_showLabelsCheck, &QCheckBox::toggled,
            [this](bool checked) { m_showLabels = checked; });
    QObject::connect(m_showBoundingBoxCheck, &QCheckBox::toggled,
            [this](bool checked) { m_showBoundingBox = checked; });

    return m_configWidget;
}

nlohmann::json HalconD4SigmaBeamDetectionPlugin::getParameters() const
{
    nlohmann::json params;
    params["minSignalAboveBg"] = m_minSignalAboveBg;
    params["minBeamArea"] = m_minBeamArea;
    params["maxBeamArea"] = m_maxBeamArea;
    params["maxBeams"] = m_maxBeams;
    params["d4sigmaThresh"] = m_d4sigmaThresh;
    params["roiExpandFactor"] = m_roiExpandFactor;
    params["bgBoxSize"] = m_bgBoxSize;
    params["saturationThreshold"] = m_saturationThreshold;
    params["showD4SigmaCross"] = m_showD4SigmaCross;
    params["showCentroid"] = m_showCentroid;
    params["showLabels"] = m_showLabels;
    params["showBoundingBox"] = m_showBoundingBox;
    return params;
}

void HalconD4SigmaBeamDetectionPlugin::setParameters(const nlohmann::json& params)
{
    if (params.contains("minSignalAboveBg")) m_minSignalAboveBg = params["minSignalAboveBg"].get<int>();
    if (params.contains("minBeamArea")) m_minBeamArea = params["minBeamArea"].get<int>();
    if (params.contains("maxBeamArea")) m_maxBeamArea = params["maxBeamArea"].get<int>();
    if (params.contains("maxBeams")) m_maxBeams = params["maxBeams"].get<int>();
    if (params.contains("d4sigmaThresh")) m_d4sigmaThresh = params["d4sigmaThresh"].get<double>();
    if (params.contains("roiExpandFactor")) m_roiExpandFactor = params["roiExpandFactor"].get<double>();
    if (params.contains("bgBoxSize")) m_bgBoxSize = params["bgBoxSize"].get<int>();
    if (params.contains("saturationThreshold")) m_saturationThreshold = params["saturationThreshold"].get<int>();
    if (params.contains("showD4SigmaCross")) m_showD4SigmaCross = params["showD4SigmaCross"].get<bool>();
    if (params.contains("showCentroid")) m_showCentroid = params["showCentroid"].get<bool>();
    if (params.contains("showLabels")) m_showLabels = params["showLabels"].get<bool>();
    if (params.contains("showBoundingBox")) m_showBoundingBox = params["showBoundingBox"].get<bool>();

    // Update UI if widgets exist
    if (m_minSignalAboveBgSpin) m_minSignalAboveBgSpin->setValue(m_minSignalAboveBg);
    if (m_minBeamAreaSpin) m_minBeamAreaSpin->setValue(m_minBeamArea);
    if (m_maxBeamAreaSpin) m_maxBeamAreaSpin->setValue(m_maxBeamArea);
    if (m_maxBeamsSpin) m_maxBeamsSpin->setValue(m_maxBeams);
    if (m_d4sigmaThreshSpin) m_d4sigmaThreshSpin->setValue(m_d4sigmaThresh);
    if (m_roiExpandFactorSpin) m_roiExpandFactorSpin->setValue(m_roiExpandFactor);
    if (m_bgBoxSizeSpin) m_bgBoxSizeSpin->setValue(m_bgBoxSize);
    if (m_saturationThresholdSpin) m_saturationThresholdSpin->setValue(m_saturationThreshold);
    if (m_showD4SigmaCrossCheck) m_showD4SigmaCrossCheck->setChecked(m_showD4SigmaCross);
    if (m_showCentroidCheck) m_showCentroidCheck->setChecked(m_showCentroid);
    if (m_showLabelsCheck) m_showLabelsCheck->setChecked(m_showLabels);
    if (m_showBoundingBoxCheck) m_showBoundingBoxCheck->setChecked(m_showBoundingBox);
}

nlohmann::json HalconD4SigmaBeamDetectionPlugin::getDefaultParameters() const
{
    nlohmann::json params;
    params["minSignalAboveBg"] = 30;
    params["minBeamArea"] = 20;
    params["maxBeamArea"] = 50000;
    params["maxBeams"] = 10;
    params["d4sigmaThresh"] = 0.1;
    params["roiExpandFactor"] = 1.5;
    params["bgBoxSize"] = 50;
    params["saturationThreshold"] = 250;
    params["showD4SigmaCross"] = true;
    params["showCentroid"] = true;
    params["showLabels"] = true;
    params["showBoundingBox"] = false;
    return params;
}

ParameterSchema HalconD4SigmaBeamDetectionPlugin::getParameterSchema() const
{
    ParameterSchema schema;
    schema.pluginName = getName();
    schema.pluginCategory = getCategory();

    // Detection Parameters
    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minSignalAboveBg", "Min Signal Above BG",
        1, 255, 30,
        "Minimum signal above background for beam finding. "
        "Threshold = background_mean + this value.",
        "", 5,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "minBeamArea", "Min Beam Area",
        1, 100000, 20,
        "Minimum beam area in pixels^2 for noise rejection.",
        "px^2", 10,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxBeamArea", "Max Beam Area",
        1, 10000000, 50000,
        "Maximum beam area in pixels^2.",
        "px^2", 1000,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "maxBeams", "Max Beams",
        1, 100, 10,
        "Maximum number of beams to detect.",
        "", 1,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "d4sigmaThresh", "D4sigma Threshold",
        0.01, 0.99, 0.1,
        "Fraction of peak signal for D4sigma integration region. "
        "Lower values include more of the beam wings.",
        "", 0.05,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createDouble(
        "roiExpandFactor", "ROI Expand Factor",
        0.0, 10.0, 1.5,
        "Bounding box expansion factor for wing capture. "
        "Expands the initial beam bbox by this factor on each side.",
        "", 0.1,
        "Detection"
    ));

    schema.parameters.push_back(ParameterDescriptor::createInt(
        "bgBoxSize", "BG Box Size",
        5, 500, 50,
        "Corner box size (pixels) for background estimation.",
        "px", 5,
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
        "showD4SigmaCross", "Show D4sigma Cross",
        true,
        "Draw D4sigma extent cross-lines with end-ticks at beam center.",
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
        "Display beam ID, position, and D4sigma values as text.",
        "Display"
    ));

    schema.parameters.push_back(ParameterDescriptor::createBool(
        "showBoundingBox", "Show Bounding Box",
        false,
        "Draw the expanded bounding box used for D4sigma integration.",
        "Display"
    ));

    return schema;
}

#endif // HAS_OPENCV && HAS_HALCON

// SwirPeakFinder.h
// Port of peak_finder from beam_analyzer_ui_cpp for real-time beam detection
#pragma once

#include <vector>

struct PeakFindParams {
    double height = -1;    // -1 means no height filter
    double width = -1;     // -1 means no width filter
    double distance = -1;  // -1 means no distance filter
};

struct PeakResult {
    std::vector<int> indices;
    std::vector<double> widths;
};

/// Find peaks in a 1D signal, equivalent to scipy.signal.find_peaks
/// with height, distance, and width filtering.
PeakResult find_peaks(const std::vector<double>& data,
                      double height = -1,
                      double distance = -1,
                      double min_width = -1);

/// Float overload - converts to double internally.
PeakResult find_peaks(const std::vector<float>& data,
                      float height = -1,
                      float distance = -1,
                      float min_width = -1);

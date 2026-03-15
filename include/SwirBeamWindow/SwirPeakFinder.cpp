// SwirPeakFinder.cpp
// Port of peak_finder from beam_analyzer_ui_cpp
#include "SwirPeakFinder.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

// Find all local maxima (strictly greater than neighbors)
std::vector<int> find_local_maxima(const std::vector<double>& data) {
    std::vector<int> maxima;
    int n = static_cast<int>(data.size());
    for (int i = 1; i < n - 1; ++i) {
        if (data[i] > data[i - 1] && data[i] > data[i + 1]) {
            maxima.push_back(i);
        }
    }
    return maxima;
}

// Filter by minimum height
void filter_by_height(std::vector<int>& peaks, const std::vector<double>& data, double height) {
    if (height < 0) return;
    std::vector<int> filtered;
    for (int p : peaks) {
        if (data[p] >= height) {
            filtered.push_back(p);
        }
    }
    peaks = std::move(filtered);
}

// Filter by minimum distance: keep tallest peak within each distance window.
void filter_by_distance(std::vector<int>& peaks, const std::vector<double>& data, double distance) {
    if (distance < 0) return;
    int n = static_cast<int>(peaks.size());
    if (n <= 1) return;

    // Sort peak indices by height (descending)
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return data[peaks[a]] > data[peaks[b]];
    });

    std::vector<bool> keep(n, true);
    for (int i = 0; i < n; ++i) {
        int idx = order[i];
        if (!keep[idx]) continue;
        for (int j = 0; j < n; ++j) {
            if (j == idx) continue;
            if (keep[j] && std::abs(peaks[j] - peaks[idx]) < distance) {
                keep[j] = false;
            }
        }
    }

    std::vector<int> filtered;
    for (int i = 0; i < n; ++i) {
        if (keep[i]) filtered.push_back(peaks[i]);
    }
    peaks = std::move(filtered);
}

// Compute prominence for a single peak.
double compute_prominence(int peak, const std::vector<double>& data) {
    int n = static_cast<int>(data.size());
    double peak_val = data[peak];

    double min_left = peak_val;
    for (int i = peak - 1; i >= 0; --i) {
        if (data[i] > peak_val) break;
        min_left = std::min(min_left, data[i]);
    }

    double min_right = peak_val;
    for (int i = peak + 1; i < n; ++i) {
        if (data[i] > peak_val) break;
        min_right = std::min(min_right, data[i]);
    }

    return peak_val - std::max(min_left, min_right);
}

// Compute width at half-prominence for a single peak.
double compute_width(int peak, const std::vector<double>& data, double prominence) {
    int n = static_cast<int>(data.size());
    double half_height = data[peak] - prominence * 0.5;

    double left_ip = 0.0;
    for (int i = peak - 1; i >= 0; --i) {
        if (data[i] <= half_height) {
            double d = data[i + 1] - data[i];
            if (std::abs(d) > 1e-15) {
                left_ip = i + (half_height - data[i]) / d;
            } else {
                left_ip = static_cast<double>(i);
            }
            break;
        }
        if (i == 0) {
            left_ip = 0.0;
        }
    }

    double right_ip = static_cast<double>(n - 1);
    for (int i = peak + 1; i < n; ++i) {
        if (data[i] <= half_height) {
            double d = data[i - 1] - data[i];
            if (std::abs(d) > 1e-15) {
                right_ip = i - (half_height - data[i]) / d;
            } else {
                right_ip = static_cast<double>(i);
            }
            break;
        }
    }

    return right_ip - left_ip;
}

} // anonymous namespace

PeakResult find_peaks(const std::vector<double>& data,
                      double height,
                      double distance,
                      double min_width) {
    PeakResult result;

    std::vector<int> peaks = find_local_maxima(data);
    filter_by_height(peaks, data, height);
    filter_by_distance(peaks, data, distance);

    std::vector<double> widths(peaks.size());
    for (size_t i = 0; i < peaks.size(); ++i) {
        double prom = compute_prominence(peaks[i], data);
        widths[i] = compute_width(peaks[i], data, prom);
    }

    if (min_width >= 0) {
        std::vector<int> filtered_peaks;
        std::vector<double> filtered_widths;
        for (size_t i = 0; i < peaks.size(); ++i) {
            if (widths[i] >= min_width) {
                filtered_peaks.push_back(peaks[i]);
                filtered_widths.push_back(widths[i]);
            }
        }
        result.indices = std::move(filtered_peaks);
        result.widths = std::move(filtered_widths);
    } else {
        result.indices = std::move(peaks);
        result.widths = std::move(widths);
    }

    return result;
}

PeakResult find_peaks(const std::vector<float>& data,
                      float height,
                      float distance,
                      float min_width) {
    std::vector<double> ddata(data.begin(), data.end());
    return find_peaks(ddata,
                      static_cast<double>(height),
                      static_cast<double>(distance),
                      static_cast<double>(min_width));
}

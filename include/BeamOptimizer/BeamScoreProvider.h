// BeamScoreProvider.h
// Thread-safe beam score computation from BeamDetectionResult.
// Reusable by any optimizer that needs a beam quality metric.
#pragma once

#include "SwirBeamWindow/SwirBeamDetector.h"
#include <mutex>
#include <vector>

/// Computes a beam quality score from detection results.
/// Score = average of (CE * EE) across all visible beams.
/// Thread-safe: reads from a shared results vector under mutex.
class BeamScoreProvider {
public:
    /// Bind to the beam results storage (owned by SwirBeamWindow or similar).
    /// The mutex and vector must outlive this object.
    void Bind(std::mutex* resultsMutex, std::vector<BeamDetectionResult>* results) {
        m_mutex = resultsMutex;
        m_results = results;
    }

    /// Compute the current beam score.
    /// Returns negative if no beams are detected.
    double GetScore() const {
        if (!m_mutex || !m_results) return -1.0;

        std::lock_guard<std::mutex> lock(*m_mutex);
        if (m_results->empty()) return -1.0;

        double totalScore = 0.0;
        int numBeams = static_cast<int>(m_results->size());
        if (numBeams > 2) numBeams = 2;  // max 2 beams

        for (int i = 0; i < numBeams; ++i) {
            double ce = (*m_results)[i].circleEnergy;
            double ee = (*m_results)[i].encircledEnergy;  // 0.0 - 1.0
            totalScore += ce * ee;
        }
        return totalScore / numBeams;
    }

private:
    std::mutex* m_mutex = nullptr;
    std::vector<BeamDetectionResult>* m_results = nullptr;
};

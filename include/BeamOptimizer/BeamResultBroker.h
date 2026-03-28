// BeamResultBroker.h
// Thread-safe pub-sub broker for beam detection results.
// Publisher: SwirBeamWindow detection thread
// Subscribers: BeamOptimizerManager (and future consumers)
#pragma once

#include "SwirBeamWindow/SwirBeamDetector.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

class BeamResultBroker {
public:
    using BeamCallback = std::function<void(const std::vector<BeamDetectionResult>&)>;

    BeamResultBroker() = default;
    ~BeamResultBroker() = default;

    // Non-copyable
    BeamResultBroker(const BeamResultBroker&) = delete;
    BeamResultBroker& operator=(const BeamResultBroker&) = delete;

    // Shutdown control
    void BeginShutdown();
    bool IsShuttingDown() const;

    // Publisher side (called from SwirBeamWindow detection thread)
    void Publish(const std::vector<BeamDetectionResult>& results);

    // Subscriber side (returns subscription ID for unsubscribe)
    uint32_t Subscribe(BeamCallback callback);
    void Unsubscribe(uint32_t subscriptionId);

    // Polling access (alternative to callback)
    std::vector<BeamDetectionResult> GetLatestResults() const;
    bool HasResults() const;

private:
    struct Subscription {
        uint32_t id = 0;
        BeamCallback callback;
    };

    mutable std::mutex m_mutex;
    std::atomic<bool> m_shuttingDown{false};
    std::vector<Subscription> m_subscribers;
    uint32_t m_nextSubId = 1;
    std::vector<BeamDetectionResult> m_latestResults;
};

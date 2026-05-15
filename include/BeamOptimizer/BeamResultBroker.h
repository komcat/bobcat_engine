// BeamResultBroker.h
// Thread-safe pub-sub broker for beam detection results.
// Publisher: SwirBeamWindow detection thread
// Subscribers: BeamOptimizerManager (and future consumers)
#pragma once

#include "SwirBeamWindow/SwirBeamDetector.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

class BeamResultBroker {
public:
    // Results are shared as immutable snapshots. Subscribers receive the
    // shared_ptr — copying it is a refcount bump, not a deep copy of the
    // result vector (which includes per-beam profile data).
    using BeamResultsPtr = std::shared_ptr<const std::vector<BeamDetectionResult>>;
    using BeamCallback = std::function<void(const BeamResultsPtr&)>;

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

    // Polling access (alternative to callback). Returns null when no
    // results have been published yet.
    BeamResultsPtr GetLatestResults() const;
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
    BeamResultsPtr m_latestResults;
};

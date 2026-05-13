// BeamResultBroker.cpp
#include "BeamResultBroker.h"

void BeamResultBroker::BeginShutdown()
{
    m_shuttingDown.store(true);
}

bool BeamResultBroker::IsShuttingDown() const
{
    return m_shuttingDown.load();
}

void BeamResultBroker::Publish(const std::vector<BeamDetectionResult>& results)
{
    if (m_shuttingDown.load()) return;

    // Wrap once into an immutable shared snapshot. Subscribers get the same
    // shared_ptr — no per-subscriber deep copy of profile vectors.
    auto snapshot = std::make_shared<const std::vector<BeamDetectionResult>>(results);

    // Copy subscribers under lock, then invoke outside lock
    std::vector<Subscription> subs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestResults = snapshot;
        subs = m_subscribers;
    }

    for (const auto& sub : subs) {
        if (sub.callback) {
            sub.callback(snapshot);
        }
    }
}

uint32_t BeamResultBroker::Subscribe(BeamCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    uint32_t id = m_nextSubId++;
    m_subscribers.push_back({id, std::move(callback)});
    return id;
}

void BeamResultBroker::Unsubscribe(uint32_t subscriptionId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscribers.erase(
        std::remove_if(m_subscribers.begin(), m_subscribers.end(),
            [subscriptionId](const Subscription& s) { return s.id == subscriptionId; }),
        m_subscribers.end());
}

BeamResultBroker::BeamResultsPtr BeamResultBroker::GetLatestResults() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestResults;
}

bool BeamResultBroker::HasResults() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestResults && !m_latestResults->empty();
}

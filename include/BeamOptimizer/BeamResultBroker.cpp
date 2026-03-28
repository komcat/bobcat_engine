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

    // Copy subscribers under lock, then invoke outside lock
    std::vector<Subscription> subs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latestResults = results;
        subs = m_subscribers;
    }

    for (const auto& sub : subs) {
        if (sub.callback) {
            sub.callback(results);
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

std::vector<BeamDetectionResult> BeamResultBroker::GetLatestResults() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestResults;
}

bool BeamResultBroker::HasResults() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_latestResults.empty();
}

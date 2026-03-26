#include "ApiLogBuffer.h"

namespace AutoConsole::Cli
{
    ApiLogBuffer::ApiLogBuffer(size_t maxPerSession)
        : maxPerSession_(maxPerSession)
    {
    }

    void ApiLogBuffer::add(const std::string& sessionId, const std::string& type, const std::string& text, const std::string& timestamp)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& dequeRef = logs_[sessionId];
        dequeRef.push_back(LogEntry{ timestamp, type, text });
        while (dequeRef.size() > maxPerSession_)
        {
            dequeRef.pop_front();
        }
    }

    std::vector<LogEntry> ApiLogBuffer::get(const std::string& sessionId, size_t limit) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<LogEntry> result;
        auto it = logs_.find(sessionId);
        if (it == logs_.end() || limit == 0)
        {
            return result;
        }

        const auto& dequeRef = it->second;
        const size_t startIndex = (dequeRef.size() > limit) ? (dequeRef.size() - limit) : 0;
        result.reserve(dequeRef.size() - startIndex);
        for (size_t i = startIndex; i < dequeRef.size(); ++i)
        {
            result.push_back(dequeRef[i]);
        }

        return result;
    }

    void ApiLogBuffer::set_max_per_session(size_t maxPerSession)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        maxPerSession_ = maxPerSession;
        for (auto& kv : logs_)
        {
            while (kv.second.size() > maxPerSession_)
            {
                kv.second.pop_front();
            }
        }
    }
}

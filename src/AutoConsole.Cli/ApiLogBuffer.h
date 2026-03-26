#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace AutoConsole::Cli
{
    struct LogEntry
    {
        std::string timestamp;
        std::string type;
        std::string text;
    };

    class ApiLogBuffer
    {
    public:
        explicit ApiLogBuffer(size_t maxPerSession = 100);

        void add(const std::string& sessionId, const std::string& type, const std::string& text, const std::string& timestamp);
        std::vector<LogEntry> get(const std::string& sessionId, size_t limit) const;
        void set_max_per_session(size_t maxPerSession);

    private:
        size_t maxPerSession_;
        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::deque<LogEntry>> logs_;
    };
}

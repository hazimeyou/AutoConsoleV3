#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

#include <Windows.h>

#include "AutoConsole/Abstractions/Profile.h"

namespace AutoConsole::Core
{
    class ProcessRunner
    {
    public:
        using ExitedCallback = std::function<void(const std::string&, int)>;

        ~ProcessRunner();

        bool start(
            const std::string& sessionId,
            const AutoConsole::Abstractions::Profile& profile,
            ExitedCallback onExited,
            std::string& errorMessage);

        bool stop(const std::string& sessionId);

    private:
        struct ProcessRecord
        {
            HANDLE processHandle = nullptr;
            HANDLE threadHandle = nullptr;
            HANDLE stdinWrite = nullptr;
            HANDLE stdoutRead = nullptr;
            HANDLE stderrRead = nullptr;
        };

        void close_record_handles(ProcessRecord& record) const;

        std::unordered_map<std::string, std::shared_ptr<ProcessRecord>> processes_;
        std::mutex processesMutex_;
    };
}

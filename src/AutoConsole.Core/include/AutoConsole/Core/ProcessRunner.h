#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <Windows.h>

#include "AutoConsole/Abstractions/Profile.h"

namespace AutoConsole::Core
{
    class ProcessRunner
    {
    public:
        using LineCallback = std::function<void(const std::string&, const std::string&)>;
        using ExitedCallback = std::function<void(const std::string&, int)>;

        ~ProcessRunner();

        bool start(
            const std::string& sessionId,
            const AutoConsole::Abstractions::Profile& profile,
            LineCallback onStdoutLine,
            LineCallback onStderrLine,
            ExitedCallback onExited,
            std::string& errorMessage);

        bool stop(const std::string& sessionId);
        bool send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage);

    private:
        struct ProcessRecord
        {
            HANDLE processHandle = nullptr;
            HANDLE threadHandle = nullptr;
            HANDLE stdinWrite = nullptr;
            HANDLE stdoutRead = nullptr;
            HANDLE stderrRead = nullptr;
            std::thread stdoutThread;
            std::thread stderrThread;
            std::thread waitThread;
        };

        struct SharedState
        {
            std::unordered_map<std::string, std::shared_ptr<ProcessRecord>> processes;
            std::mutex processesMutex;
            std::atomic<bool> shuttingDown{ false };
        };

        static void close_record_handles(ProcessRecord& record);
        static void join_output_threads(ProcessRecord& record);
        std::shared_ptr<SharedState> state_ = std::make_shared<SharedState>();
    };
}

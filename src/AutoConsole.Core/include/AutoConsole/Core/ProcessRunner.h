#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <atomic>

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
        bool write_input(const std::string& sessionId, const std::string& text, bool appendNewline, std::string& errorMessage);
        void cleanup_finished_sessions();

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
            std::atomic<bool> processExited = false;
        };

        struct SharedState
        {
            std::unordered_map<std::string, std::shared_ptr<ProcessRecord>> processes;
            std::mutex processesMutex;
        };

        static void close_record_handles(ProcessRecord& record);
        static void join_output_threads(ProcessRecord& record);
        static bool is_valid_handle(HANDLE handle);
        std::shared_ptr<SharedState> state_ = std::make_shared<SharedState>();
    };
}

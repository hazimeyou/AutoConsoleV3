#include "AutoConsole/Core/ProcessRunner.h"

#include <sstream>
#include <thread>
#include <vector>

namespace
{
    std::string quote_if_needed(const std::string& value)
    {
        if (value.find_first_of(" \t\"") == std::string::npos)
        {
            return value;
        }

        std::string escaped;
        escaped.reserve(value.size() + 2);
        escaped.push_back('"');
        for (char ch : value)
        {
            if (ch == '"')
            {
                escaped.push_back('\\');
            }
            escaped.push_back(ch);
        }
        escaped.push_back('"');
        return escaped;
    }

    std::string build_command_line(const AutoConsole::Abstractions::Profile& profile)
    {
        std::ostringstream oss;
        oss << quote_if_needed(profile.command);
        for (const auto& arg : profile.args)
        {
            oss << ' ' << quote_if_needed(arg);
        }
        return oss.str();
    }

    void close_handle_if_valid(HANDLE handle)
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle);
        }
    }
}

namespace AutoConsole::Core
{
    ProcessRunner::~ProcessRunner()
    {
        std::vector<std::shared_ptr<ProcessRecord>> records;
        {
            std::lock_guard<std::mutex> lock(processesMutex_);
            for (const auto& kv : processes_)
            {
                records.push_back(kv.second);
            }
            processes_.clear();
        }

        for (const auto& record : records)
        {
            if (record->processHandle != nullptr)
            {
                TerminateProcess(record->processHandle, 1);
                WaitForSingleObject(record->processHandle, 2000);
            }
            close_record_handles(*record);
        }
    }

    bool ProcessRunner::start(
        const std::string& sessionId,
        const AutoConsole::Abstractions::Profile& profile,
        ExitedCallback onExited,
        std::string& errorMessage)
    {
        SECURITY_ATTRIBUTES securityAttributes{};
        securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
        securityAttributes.bInheritHandle = TRUE;

        HANDLE childStdInRead = nullptr;
        HANDLE childStdInWrite = nullptr;
        HANDLE childStdOutRead = nullptr;
        HANDLE childStdOutWrite = nullptr;
        HANDLE childStdErrRead = nullptr;
        HANDLE childStdErrWrite = nullptr;

        if (!CreatePipe(&childStdInRead, &childStdInWrite, &securityAttributes, 0))
        {
            errorMessage = "failed to create stdin pipe";
            return false;
        }

        if (!SetHandleInformation(childStdInWrite, HANDLE_FLAG_INHERIT, 0))
        {
            close_handle_if_valid(childStdInRead);
            close_handle_if_valid(childStdInWrite);
            errorMessage = "failed to configure stdin pipe";
            return false;
        }

        if (!CreatePipe(&childStdOutRead, &childStdOutWrite, &securityAttributes, 0))
        {
            close_handle_if_valid(childStdInRead);
            close_handle_if_valid(childStdInWrite);
            errorMessage = "failed to create stdout pipe";
            return false;
        }

        if (!SetHandleInformation(childStdOutRead, HANDLE_FLAG_INHERIT, 0))
        {
            close_handle_if_valid(childStdInRead);
            close_handle_if_valid(childStdInWrite);
            close_handle_if_valid(childStdOutRead);
            close_handle_if_valid(childStdOutWrite);
            errorMessage = "failed to configure stdout pipe";
            return false;
        }

        if (!CreatePipe(&childStdErrRead, &childStdErrWrite, &securityAttributes, 0))
        {
            close_handle_if_valid(childStdInRead);
            close_handle_if_valid(childStdInWrite);
            close_handle_if_valid(childStdOutRead);
            close_handle_if_valid(childStdOutWrite);
            errorMessage = "failed to create stderr pipe";
            return false;
        }

        if (!SetHandleInformation(childStdErrRead, HANDLE_FLAG_INHERIT, 0))
        {
            close_handle_if_valid(childStdInRead);
            close_handle_if_valid(childStdInWrite);
            close_handle_if_valid(childStdOutRead);
            close_handle_if_valid(childStdOutWrite);
            close_handle_if_valid(childStdErrRead);
            close_handle_if_valid(childStdErrWrite);
            errorMessage = "failed to configure stderr pipe";
            return false;
        }

        STARTUPINFOA startupInfo{};
        startupInfo.cb = sizeof(STARTUPINFOA);
        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = childStdInRead;
        startupInfo.hStdOutput = childStdOutWrite;
        startupInfo.hStdError = childStdErrWrite;

        PROCESS_INFORMATION processInfo{};
        std::string commandLine = build_command_line(profile);
        std::vector<char> commandLineBuffer(commandLine.begin(), commandLine.end());
        commandLineBuffer.push_back('\0');

        const char* workingDir = profile.workingDir.empty() ? nullptr : profile.workingDir.c_str();
        const BOOL created = CreateProcessA(
            nullptr,
            commandLineBuffer.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            workingDir,
            &startupInfo,
            &processInfo);

        close_handle_if_valid(childStdInRead);
        close_handle_if_valid(childStdOutWrite);
        close_handle_if_valid(childStdErrWrite);

        if (!created)
        {
            close_handle_if_valid(childStdInWrite);
            close_handle_if_valid(childStdOutRead);
            close_handle_if_valid(childStdErrRead);
            errorMessage = "CreateProcess failed with error code " + std::to_string(GetLastError());
            return false;
        }

        auto record = std::make_shared<ProcessRecord>();
        record->processHandle = processInfo.hProcess;
        record->threadHandle = processInfo.hThread;
        record->stdinWrite = childStdInWrite;
        record->stdoutRead = childStdOutRead;
        record->stderrRead = childStdErrRead;

        {
            std::lock_guard<std::mutex> lock(processesMutex_);
            processes_[sessionId] = record;
        }

        std::thread([this, sessionId, record, onExited]()
        {
            WaitForSingleObject(record->processHandle, INFINITE);

            DWORD exitCode = 1;
            GetExitCodeProcess(record->processHandle, &exitCode);

            if (onExited)
            {
                onExited(sessionId, static_cast<int>(exitCode));
            }

            {
                std::lock_guard<std::mutex> lock(processesMutex_);
                processes_.erase(sessionId);
            }

            close_record_handles(*record);
        }).detach();

        return true;
    }

    bool ProcessRunner::stop(const std::string& sessionId)
    {
        std::shared_ptr<ProcessRecord> record;
        {
            std::lock_guard<std::mutex> lock(processesMutex_);
            const auto it = processes_.find(sessionId);
            if (it == processes_.end())
            {
                return false;
            }

            record = it->second;
        }

        return TerminateProcess(record->processHandle, 1) == TRUE;
    }

    void ProcessRunner::close_record_handles(ProcessRecord& record) const
    {
        close_handle_if_valid(record.stdinWrite);
        close_handle_if_valid(record.stdoutRead);
        close_handle_if_valid(record.stderrRead);
        close_handle_if_valid(record.threadHandle);
        close_handle_if_valid(record.processHandle);

        record.stdinWrite = nullptr;
        record.stdoutRead = nullptr;
        record.stderrRead = nullptr;
        record.threadHandle = nullptr;
        record.processHandle = nullptr;
    }
}

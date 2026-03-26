#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <Windows.h>

#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Abstractions/IPluginActionExecutor.h"
#include "AutoConsole/Abstractions/PluginApiVersion.h"

namespace
{
    struct BridgeHostProcess
    {
        PROCESS_INFORMATION processInfo{};
        HANDLE stdinWrite = nullptr;
        HANDLE stdoutRead = nullptr;
        bool started = false;
    };

    std::filesystem::path resolve_module_directory()
    {
        HMODULE moduleHandle = nullptr;
        if (!::GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&resolve_module_directory),
            &moduleHandle))
        {
            return std::filesystem::current_path();
        }

        char modulePathBuffer[MAX_PATH] = {};
        const DWORD length = ::GetModuleFileNameA(moduleHandle, modulePathBuffer, static_cast<DWORD>(sizeof(modulePathBuffer)));
        if (length == 0 || length >= sizeof(modulePathBuffer))
        {
            return std::filesystem::current_path();
        }

        return std::filesystem::path(modulePathBuffer).parent_path();
    }

    std::string escape_json(const std::string& value)
    {
        std::ostringstream oss;
        for (const char ch : value)
        {
            switch (ch)
            {
            case '\\':
                oss << "\\\\";
                break;
            case '"':
                oss << "\\\"";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                oss << ch;
                break;
            }
        }
        return oss.str();
    }

    bool write_line(HANDLE pipe, const std::string& text)
    {
        const std::string payload = text + "\n";
        DWORD written = 0;
        return ::WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) && written == payload.size();
    }

    bool read_line(HANDLE pipe, std::string& line)
    {
        line.clear();
        char buffer[1] = {};
        DWORD readBytes = 0;
        while (true)
        {
            if (!::ReadFile(pipe, buffer, 1, &readBytes, nullptr) || readBytes == 0)
            {
                return false;
            }

            const char ch = buffer[0];
            if (ch == '\n')
            {
                return true;
            }
            if (ch != '\r')
            {
                line.push_back(ch);
            }
        }
    }
}

namespace AutoConsole::Bridge
{
    class CsBridgePlugin final : public AutoConsole::Abstractions::IPlugin,
                                 public AutoConsole::Abstractions::IPluginActionExecutor
    {
    public:
        CsBridgePlugin()
        {
            resolve_paths();
            start_host_if_needed();
        }

        ~CsBridgePlugin() override
        {
            stop_host();
        }

        AutoConsole::Abstractions::PluginMetadata metadata() const override
        {
            AutoConsole::Abstractions::PluginMetadata metadata{};
            metadata.id = "cs.bridge";
            metadata.name = "CsBridge";
            metadata.displayName = "CsBridge";
            metadata.version = "0.1.0";
            metadata.apiVersion = AutoConsole::Abstractions::kPluginApiVersion;
            metadata.author = "AutoConsole";
            metadata.description = "Bridge plugin that forwards actions to AcSharpHost via JSON over stdio.";
            metadata.capabilities = { "bridge_action" };
            return metadata;
        }

        void on_event(const AutoConsole::Abstractions::Event&, AutoConsole::Abstractions::PluginContext& context) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!pathsLogged_)
            {
                context.log("info", "[cs.bridge] module directory: " + moduleDirectory_.string());
                context.log("info", "[cs.bridge] resolved host path: " + hostPath_.string());
                pathsLogged_ = true;
            }

            if (!host_.started)
            {
                if (start_host_if_needed())
                {
                    context.log("info", "[cs.bridge] AcSharpHost started");
                }
                else
                {
                    context.log("error", "[cs.bridge] failed to start AcSharpHost at: " + hostPath_.string());
                }
            }
        }

        bool execute_action(
            const std::string& action,
            const std::unordered_map<std::string, std::string>& actionArgs,
            AutoConsole::Abstractions::PluginContext& context,
            std::string& errorMessage) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!pathsLogged_)
            {
                context.log("info", "[cs.bridge] module directory: " + moduleDirectory_.string());
                context.log("info", "[cs.bridge] resolved host path: " + hostPath_.string());
                pathsLogged_ = true;
            }

            if (!start_host_if_needed())
            {
                errorMessage = "cs.bridge failed to start AcSharpHost: " + hostPath_.string();
                return false;
            }

            std::ostringstream request;
            request << "{\"action\":\"" << escape_json(action) << "\",\"args\":{";
            bool first = true;
            for (const auto& pair : actionArgs)
            {
                if (!first)
                {
                    request << ",";
                }
                request << "\"" << escape_json(pair.first) << "\":\"" << escape_json(pair.second) << "\"";
                first = false;
            }
            request << "}}";

            if (!write_line(host_.stdinWrite, request.str()))
            {
                stop_host();
                errorMessage = "cs.bridge failed to write JSON request to AcSharpHost";
                return false;
            }

            std::string response;
            if (!read_line(host_.stdoutRead, response))
            {
                stop_host();
                errorMessage = "cs.bridge failed to read JSON response from AcSharpHost";
                return false;
            }

            context.log("debug", "[cs.bridge] host response: " + response);
            if (response.find("\"success\":true") != std::string::npos)
            {
                return true;
            }

            errorMessage = "AcSharpHost response indicates failure: " + response;
            return false;
        }

    private:
        void resolve_paths()
        {
            moduleDirectory_ = resolve_module_directory();
            hostPath_ = (moduleDirectory_.parent_path() / "cs.bridge" / "AcSharpHost.exe").lexically_normal();
        }

        bool start_host_if_needed()
        {
            if (host_.started)
            {
                return true;
            }

            if (!std::filesystem::exists(hostPath_))
            {
                std::cout << "[cs.bridge] AcSharpHost not found: " << hostPath_.string() << "\n";
                return false;
            }

            SECURITY_ATTRIBUTES securityAttributes{};
            securityAttributes.nLength = sizeof(securityAttributes);
            securityAttributes.bInheritHandle = TRUE;
            securityAttributes.lpSecurityDescriptor = nullptr;

            HANDLE childStdoutRead = nullptr;
            HANDLE childStdoutWrite = nullptr;
            if (!::CreatePipe(&childStdoutRead, &childStdoutWrite, &securityAttributes, 0))
            {
                return false;
            }

            if (!::SetHandleInformation(childStdoutRead, HANDLE_FLAG_INHERIT, 0))
            {
                ::CloseHandle(childStdoutRead);
                ::CloseHandle(childStdoutWrite);
                return false;
            }

            HANDLE childStdinRead = nullptr;
            HANDLE childStdinWrite = nullptr;
            if (!::CreatePipe(&childStdinRead, &childStdinWrite, &securityAttributes, 0))
            {
                ::CloseHandle(childStdoutRead);
                ::CloseHandle(childStdoutWrite);
                return false;
            }

            if (!::SetHandleInformation(childStdinWrite, HANDLE_FLAG_INHERIT, 0))
            {
                ::CloseHandle(childStdoutRead);
                ::CloseHandle(childStdoutWrite);
                ::CloseHandle(childStdinRead);
                ::CloseHandle(childStdinWrite);
                return false;
            }

            STARTUPINFOA startupInfo{};
            startupInfo.cb = sizeof(startupInfo);
            startupInfo.dwFlags = STARTF_USESTDHANDLES;
            startupInfo.hStdInput = childStdinRead;
            startupInfo.hStdOutput = childStdoutWrite;
            startupInfo.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);

            PROCESS_INFORMATION processInfo{};
            std::string commandLine = "\"" + hostPath_.string() + "\"";
            std::vector<char> commandLineBuffer(commandLine.begin(), commandLine.end());
            commandLineBuffer.push_back('\0');

            const std::string workingDirectory = hostPath_.parent_path().string();
            const BOOL created = ::CreateProcessA(
                nullptr,
                commandLineBuffer.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                workingDirectory.c_str(),
                &startupInfo,
                &processInfo);

            ::CloseHandle(childStdinRead);
            ::CloseHandle(childStdoutWrite);

            if (!created)
            {
                ::CloseHandle(childStdoutRead);
                ::CloseHandle(childStdinWrite);
                return false;
            }

            host_.processInfo = processInfo;
            host_.stdinWrite = childStdinWrite;
            host_.stdoutRead = childStdoutRead;
            host_.started = true;

            std::cout << "[cs.bridge] started AcSharpHost: " << hostPath_.string() << "\n";
            return true;
        }

        void stop_host()
        {
            if (!host_.started)
            {
                return;
            }

            if (host_.stdinWrite)
            {
                ::CloseHandle(host_.stdinWrite);
                host_.stdinWrite = nullptr;
            }

            if (host_.stdoutRead)
            {
                ::CloseHandle(host_.stdoutRead);
                host_.stdoutRead = nullptr;
            }

            const DWORD waitResult = ::WaitForSingleObject(host_.processInfo.hProcess, 1500);
            if (waitResult == WAIT_TIMEOUT)
            {
                ::TerminateProcess(host_.processInfo.hProcess, 1);
                ::WaitForSingleObject(host_.processInfo.hProcess, 500);
            }

            if (host_.processInfo.hThread)
            {
                ::CloseHandle(host_.processInfo.hThread);
                host_.processInfo.hThread = nullptr;
            }
            if (host_.processInfo.hProcess)
            {
                ::CloseHandle(host_.processInfo.hProcess);
                host_.processInfo.hProcess = nullptr;
            }

            host_.started = false;
        }

    private:
        std::mutex mutex_;
        std::filesystem::path moduleDirectory_;
        std::filesystem::path hostPath_;
        BridgeHostProcess host_{};
        bool pathsLogged_ = false;
    };
}

extern "C" __declspec(dllexport) AutoConsole::Abstractions::IPlugin* create_plugin()
{
    return new AutoConsole::Bridge::CsBridgePlugin();
}

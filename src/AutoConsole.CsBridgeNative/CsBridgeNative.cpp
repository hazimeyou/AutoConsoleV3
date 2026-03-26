#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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

    bool extract_json_string(const std::string& json, const std::string& key, std::string& outValue)
    {
        const std::string pattern = "\"" + key + "\"";
        const auto keyPos = json.find(pattern);
        if (keyPos == std::string::npos)
        {
            return false;
        }

        const auto colonPos = json.find(':', keyPos + pattern.size());
        if (colonPos == std::string::npos)
        {
            return false;
        }

        const auto quotePos = json.find('"', colonPos + 1);
        if (quotePos == std::string::npos)
        {
            return false;
        }

        std::string parsed;
        bool escape = false;
        for (size_t i = quotePos + 1; i < json.size(); ++i)
        {
            const char ch = json[i];
            if (escape)
            {
                switch (ch)
                {
                case 'n':
                    parsed.push_back('\n');
                    break;
                case 'r':
                    parsed.push_back('\r');
                    break;
                case 't':
                    parsed.push_back('\t');
                    break;
                case '\\':
                case '"':
                case '/':
                    parsed.push_back(ch);
                    break;
                default:
                    parsed.push_back(ch);
                    break;
                }
                escape = false;
                continue;
            }

            if (ch == '\\')
            {
                escape = true;
                continue;
            }

            if (ch == '"')
            {
                outValue = parsed;
                return true;
            }

            parsed.push_back(ch);
        }

        return false;
    }

    std::string get_action_arg(
        const std::unordered_map<std::string, std::string>& actionArgs,
        const std::string& key)
    {
        const auto it = actionArgs.find(key);
        if (it == actionArgs.end())
        {
            return {};
        }
        return it->second;
    }

    bool contains_case_insensitive(std::string_view text, std::string_view needle)
    {
        if (needle.empty() || text.size() < needle.size())
        {
            return false;
        }

        auto lower = [](char ch) -> char
            {
                if (ch >= 'A' && ch <= 'Z')
                {
                    return static_cast<char>(ch - 'A' + 'a');
                }
                return ch;
            };

        for (size_t i = 0; i + needle.size() <= text.size(); ++i)
        {
            bool matched = true;
            for (size_t j = 0; j < needle.size(); ++j)
            {
                if (lower(text[i + j]) != lower(needle[j]))
                {
                    matched = false;
                    break;
                }
            }

            if (matched)
            {
                return true;
            }
        }

        return false;
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

        void on_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!pathsLogged_)
            {
                context.log("info", "[cs.bridge] module directory: " + moduleDirectory_.string());
                context.log("info", "[cs.bridge] resolved host path: " + hostPath_.string());
                pathsLogged_ = true;
            }

            if (eventValue.type == "process_started" && !eventValue.sessionId.empty())
            {
                sessionRunning_[eventValue.sessionId] = true;
            }
            else if (eventValue.type == "process_exited" && !eventValue.sessionId.empty())
            {
                sessionRunning_[eventValue.sessionId] = false;
            }
            else if (eventValue.type == "stdout_line" && !eventValue.sessionId.empty())
            {
                const auto textIt = eventValue.data.find("text");
                if (textIt != eventValue.data.end())
                {
                    const auto& line = textIt->second;
                    if (contains_case_insensitive(line, "NO LOG FILE!")
                        || contains_case_insensitive(line, "Server started.")
                        || contains_case_insensitive(line, "Starting Server"))
                    {
                        sessionEdition_[eventValue.sessionId] = "bedrock";
                    }
                    else if (contains_case_insensitive(line, "Starting minecraft server")
                        || contains_case_insensitive(line, "Done ("))
                    {
                        sessionEdition_[eventValue.sessionId] = "java";
                    }

                    forward_stdout_line_to_host(eventValue.sessionId, line);
                }
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
            if (action == "__bridge_list_plugins")
            {
                request << "{\"action\":\"list_plugins\"}";
            }
            else if (action == "__bridge_get_plugin_info")
            {
                const auto pluginIdIt = actionArgs.find("pluginId");
                if (pluginIdIt == actionArgs.end() || pluginIdIt->second.empty())
                {
                    errorMessage = "pluginId is required for __bridge_get_plugin_info";
                    return false;
                }

                request << "{\"action\":\"get_plugin_info\",\"pluginId\":\""
                    << escape_json(pluginIdIt->second)
                    << "\"}";
            }
            else if (action == "__bridge_call")
            {
                const auto pluginIdIt = actionArgs.find("pluginId");
                const auto pluginActionIt = actionArgs.find("pluginAction");
                if (pluginIdIt == actionArgs.end() || pluginIdIt->second.empty() ||
                    pluginActionIt == actionArgs.end() || pluginActionIt->second.empty())
                {
                    errorMessage = "pluginId and pluginAction are required for __bridge_call";
                    return false;
                }

                std::string currentSessionId = get_action_arg(actionArgs, "sessionId");
                if (currentSessionId.empty())
                {
                    currentSessionId = get_action_arg(actionArgs, "fromSession");
                }
                if (currentSessionId.empty())
                {
                    currentSessionId = get_action_arg(actionArgs, "toSession");
                }

                request << "{\"action\":\"execute_plugin\",\"pluginId\":\""
                    << escape_json(pluginIdIt->second)
                    << "\",\"pluginAction\":\""
                    << escape_json(pluginActionIt->second)
                    << "\",\"currentSessionId\":\""
                    << escape_json(currentSessionId)
                    << "\",\"args\":{";

                bool first = true;
                auto append_arg = [&](const std::string& key, const std::string& value)
                    {
                        if (!first)
                        {
                            request << ",";
                        }
                        request << "\"" << escape_json(key) << "\":\"" << escape_json(value) << "\"";
                        first = false;
                    };

                for (const auto& pair : actionArgs)
                {
                    if (pair.first == "pluginId" || pair.first == "pluginAction")
                    {
                        continue;
                    }

                    append_arg(pair.first, pair.second);
                }

                if (actionArgs.find("sekai.mc.runtimeRoot") == actionArgs.end())
                {
                    append_arg("sekai.mc.runtimeRoot", runtimeRoot_.string());
                }

                if (actionArgs.find("sekai.mc.edition") == actionArgs.end())
                {
                    const auto currentEditionIt = sessionEdition_.find(currentSessionId);
                    if (currentEditionIt != sessionEdition_.end())
                    {
                        append_arg("sekai.mc.edition", currentEditionIt->second);
                    }
                }

                const auto fromSession = get_action_arg(actionArgs, "fromSession");
                const auto toSession = get_action_arg(actionArgs, "toSession");
                if (actionArgs.find("fromEdition") == actionArgs.end() && !fromSession.empty())
                {
                    const auto fromEditionIt = sessionEdition_.find(fromSession);
                    if (fromEditionIt != sessionEdition_.end())
                    {
                        append_arg("fromEdition", fromEditionIt->second);
                    }
                }
                if (actionArgs.find("toEdition") == actionArgs.end() && !toSession.empty())
                {
                    const auto toEditionIt = sessionEdition_.find(toSession);
                    if (toEditionIt != sessionEdition_.end())
                    {
                        append_arg("toEdition", toEditionIt->second);
                    }
                }
                request << "}}";
            }
            else
            {
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
            }

            if (!write_line(host_.stdinWrite, request.str()))
            {
                stop_host();
                errorMessage = "cs.bridge failed to write JSON request to AcSharpHost";
                return false;
            }

            std::string response;
            while (true)
            {
                if (!read_line(host_.stdoutRead, response))
                {
                    stop_host();
                    errorMessage = "cs.bridge failed to read JSON response from AcSharpHost";
                    return false;
                }

                std::string bridgeRequestType;
                if (extract_json_string(response, "bridgeRequest", bridgeRequestType) &&
                    bridgeRequestType == "callback")
                {
                    handle_bridge_callback_request(response, context);
                    continue;
                }

                break;
            }

            context.log("debug", "[cs.bridge] host response: " + response);
            if (response.find("\"success\":true") != std::string::npos)
            {
                if (action == "__bridge_list_plugins" || action == "__bridge_get_plugin_info")
                {
                    std::string payload;
                    if (extract_json_string(response, "bridgePayload", payload))
                    {
                        errorMessage = payload;
                    }
                    else
                    {
                        errorMessage.clear();
                    }
                    return true;
                }

                std::string hostMessage;
                if (extract_json_string(response, "message", hostMessage))
                {
                    errorMessage = hostMessage;
                }
                else
                {
                    errorMessage.clear();
                }
                return true;
            }

            std::string hostMessage;
            if (extract_json_string(response, "message", hostMessage) && !hostMessage.empty())
            {
                errorMessage = hostMessage;
            }
            else
            {
                errorMessage = "AcSharpHost response indicates failure: " + response;
            }
            return false;
        }

    private:
        void handle_bridge_callback_request(const std::string& requestJson, AutoConsole::Abstractions::PluginContext& context)
        {
            std::string requestId;
            if (!extract_json_string(requestJson, "requestId", requestId))
            {
                write_callback_response("", false, "", "missing callback requestId");
                return;
            }

            std::string callbackAction;
            if (!extract_json_string(requestJson, "callbackAction", callbackAction) || callbackAction.empty())
            {
                write_callback_response(requestId, false, "", "missing callbackAction");
                return;
            }

            const std::string currentSessionId = [&]()
                {
                    std::string value;
                    extract_json_string(requestJson, "arg_currentSessionId", value);
                    return value;
                }();
            const std::string sessionId = [&]()
                {
                    std::string value;
                    extract_json_string(requestJson, "arg_sessionId", value);
                    return value;
                }();
            const std::string text = [&]()
                {
                    std::string value;
                    extract_json_string(requestJson, "arg_text", value);
                    return value;
                }();
            const std::string level = [&]()
                {
                    std::string value;
                    extract_json_string(requestJson, "arg_level", value);
                    return value;
                }();
            const std::string message = [&]()
                {
                    std::string value;
                    extract_json_string(requestJson, "arg_message", value);
                    return value;
                }();

            if (callbackAction == "has_current_session")
            {
                const bool has = !currentSessionId.empty() && sessionRunning_.find(currentSessionId) != sessionRunning_.end();
                write_callback_response(requestId, true, has ? "true" : "false", "");
                return;
            }

            if (callbackAction == "is_current_session_running")
            {
                const auto it = sessionRunning_.find(currentSessionId);
                const bool running = it != sessionRunning_.end() && it->second;
                write_callback_response(requestId, true, running ? "true" : "false", "");
                return;
            }

            if (callbackAction == "send_input_current_session")
            {
                if (currentSessionId.empty())
                {
                    write_callback_response(requestId, false, "", "currentSessionId is empty");
                    return;
                }

                std::string sendError;
                const bool ok = context.send_input(currentSessionId, text, sendError);
                write_callback_response(requestId, ok, ok ? std::string{} : std::string{}, ok ? std::string{} : sendError);
                return;
            }

            if (callbackAction == "emit_event")
            {
                const std::string logLevel = level.empty() ? "info" : level;
                context.log(logLevel, "[cs.bridge][emit_event] " + message);
                write_callback_response(requestId, true, "", "");
                return;
            }

            if (callbackAction == "restart_current_session")
            {
                write_callback_response(requestId, false, "false", "restart_current_session is not supported in bridge");
                return;
            }

            if (callbackAction == "session_exists")
            {
                const bool exists = !sessionId.empty() && sessionRunning_.find(sessionId) != sessionRunning_.end();
                write_callback_response(requestId, true, exists ? "true" : "false", "");
                return;
            }

            if (callbackAction == "session_running")
            {
                const auto it = sessionRunning_.find(sessionId);
                const bool running = it != sessionRunning_.end() && it->second;
                write_callback_response(requestId, true, running ? "true" : "false", "");
                return;
            }

            if (callbackAction == "send_input_to_session")
            {
                std::string sendError;
                const bool ok = context.send_input(sessionId, text, sendError);
                write_callback_response(requestId, ok, ok ? std::string{} : std::string{}, ok ? std::string{} : sendError);
                return;
            }

            write_callback_response(requestId, false, "", "unknown callbackAction: " + callbackAction);
        }

        void write_callback_response(const std::string& requestId, bool success, const std::string& value, const std::string& message)
        {
            std::ostringstream oss;
            oss << "{\"bridgeResponse\":\"callback\",\"requestId\":\""
                << escape_json(requestId)
                << "\",\"success\":"
                << (success ? "true" : "false");

            if (!value.empty())
            {
                oss << ",\"value\":\"" << escape_json(value) << "\"";
            }
            if (!message.empty())
            {
                oss << ",\"message\":\"" << escape_json(message) << "\"";
            }
            oss << "}";

            write_line(host_.stdinWrite, oss.str());
        }

        void resolve_paths()
        {
            moduleDirectory_ = resolve_module_directory();
            hostPath_ = (moduleDirectory_.parent_path() / "cs.bridge" / "AcSharpHost.exe").lexically_normal();
            runtimeRoot_ = (moduleDirectory_.parent_path().parent_path() / "runtime").lexically_normal();
        }

        void forward_stdout_line_to_host(const std::string& sessionId, const std::string& line)
        {
            if (!host_.started)
            {
                return;
            }

            std::ostringstream request;
            request << "{\"action\":\"notify_stdout_line\",\"sessionId\":\""
                << escape_json(sessionId)
                << "\",\"line\":\""
                << escape_json(line)
                << "\"}";

            if (!write_line(host_.stdinWrite, request.str()))
            {
                stop_host();
                return;
            }

            std::string response;
            while (true)
            {
                if (!read_line(host_.stdoutRead, response))
                {
                    stop_host();
                    return;
                }

                std::string bridgeRequestType;
                if (extract_json_string(response, "bridgeRequest", bridgeRequestType) &&
                    bridgeRequestType == "callback")
                {
                    // notify path does not need PluginContext callback support.
                    write_callback_response("", false, "", "callback is not supported in notify_stdout_line");
                    continue;
                }

                break;
            }
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
        std::unordered_map<std::string, bool> sessionRunning_;
        std::unordered_map<std::string, std::string> sessionEdition_;
        std::filesystem::path moduleDirectory_;
        std::filesystem::path hostPath_;
        std::filesystem::path runtimeRoot_;
        BridgeHostProcess host_{};
        bool pathsLogged_ = false;
    };
}

extern "C" __declspec(dllexport) AutoConsole::Abstractions::IPlugin* create_plugin()
{
    return new AutoConsole::Bridge::CsBridgePlugin();
}

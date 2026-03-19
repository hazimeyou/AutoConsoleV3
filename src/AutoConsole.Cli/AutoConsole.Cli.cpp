#include <iostream>
#include <iomanip>
#include <atomic>
#include <mutex>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <io.h>
#include <conio.h>
#include <fstream>
#include <filesystem>
#include <cctype>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/SessionState.h"
#include "AutoConsole/Core/CoreRuntime.h"
#include "AutoConsole/Core/ProfileLoader.h"
#include "LogPlugin.h"
#include "StandardPluginActions.h"

namespace
{
    class ConsoleOutput
    {
    public:
        void print_line(const std::string& line)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << line << "\n";
        }

        void print_prompt()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "> " << std::flush;
        }

        void render_input_line(const std::string& buffer, std::size_t previousLength)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "\r> " << buffer;
            if (previousLength > buffer.size())
            {
                std::cout << std::string(previousLength - buffer.size(), ' ');
            }
            std::cout << std::flush;
        }

        void finish_input_line()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "\n";
        }

        void print_block(const std::string& text)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << text;
            if (!text.empty() && text.back() != '\n')
            {
                std::cout << "\n";
            }
        }

        void print_async_line(const std::string& line)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << line << "\n";
        }

    private:
        std::mutex mutex_;
    };

    enum class CliLogLevel
    {
        Normal = 0,
        Debug = 1
    };

    std::string help_text()
    {
        return
            "Commands:\n"
            "  help\n"
            "  ping\n"
            "  start <profile-file>\n"
            "  current\n"
            "  sessions\n"
            "  stop [sessionId]\n"
            "  send [sessionId] <text>\n"
            "  plugin send_input [sessionId] <text>\n"
            "  plugin wait_output [sessionId] <contains> <timeoutMs>\n"
            "  plugin delay <durationMs>\n"
            "  plugin stop_process [sessionId]\n"
            "  plugin emit_event <eventType> [sessionId] [payload]\n"
            "  plugin call_plugin <pluginId> <action> [key=value ...]\n"
            "  loglevel\n"
            "  loglevel normal\n"
            "  loglevel debug\n"
            "  exit\n";
    }

    std::string trim_copy(const std::string& value)
    {
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
        {
            return "";
        }

        const auto end = value.find_last_not_of(" \t\r\n");
        return value.substr(begin, end - begin + 1);
    }

    std::string history_file_path()
    {
        return "runtime/cli_history.txt";
    }

    std::vector<std::string> load_history()
    {
        std::vector<std::string> history;

        std::ifstream ifs(history_file_path());
        if (!ifs.is_open())
        {
            return history;
        }

        std::string line;
        while (std::getline(ifs, line))
        {
            const auto trimmed = trim_copy(line);
            if (!trimmed.empty())
            {
                history.push_back(line);
            }
        }

        return history;
    }

    void append_history(const std::string& line)
    {
        const auto trimmed = trim_copy(line);
        if (trimmed.empty())
        {
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories("runtime", ec);

        std::ofstream ofs(history_file_path(), std::ios::app);
        if (!ofs.is_open())
        {
            return;
        }

        ofs << line << "\n";
    }

    void push_history(std::vector<std::string>& history, const std::string& line)
    {
        const auto trimmed = trim_copy(line);
        if (trimmed.empty())
        {
            return;
        }

        if (!history.empty() && history.back() == line)
        {
            return;
        }

        history.push_back(line);
        constexpr std::size_t MaxHistory = 500;
        if (history.size() > MaxHistory)
        {
            history.erase(history.begin());
        }

        append_history(line);
    }

    bool read_line_with_history(
        ConsoleOutput& console,
        std::vector<std::string>& history,
        std::string& line)
    {
        std::string buffer;
        std::string draftBeforeBrowse;
        std::size_t previousRenderLength = 0;
        bool browsingHistory = false;
        std::size_t historyIndex = history.size();

        console.render_input_line(buffer, previousRenderLength);

        while (true)
        {
            const int ch = _getch();
            if (ch == 13)
            {
                line = buffer;
                console.finish_input_line();
                return true;
            }

            if (ch == 8)
            {
                if (!buffer.empty())
                {
                    previousRenderLength = buffer.size();
                    buffer.pop_back();
                    console.render_input_line(buffer, previousRenderLength);
                }
                continue;
            }

            if (ch == 0 || ch == 224)
            {
                const int ext = _getch();
                if (ext == 72)
                {
                    if (history.empty())
                    {
                        continue;
                    }

                    if (!browsingHistory)
                    {
                        draftBeforeBrowse = buffer;
                        browsingHistory = true;
                        historyIndex = history.size();
                    }

                    if (historyIndex > 0)
                    {
                        --historyIndex;
                        previousRenderLength = buffer.size();
                        buffer = history[historyIndex];
                        console.render_input_line(buffer, previousRenderLength);
                    }
                }
                else if (ext == 80)
                {
                    if (!browsingHistory)
                    {
                        continue;
                    }

                    previousRenderLength = buffer.size();
                    if (historyIndex + 1 < history.size())
                    {
                        ++historyIndex;
                        buffer = history[historyIndex];
                    }
                    else
                    {
                        browsingHistory = false;
                        historyIndex = history.size();
                        buffer = draftBeforeBrowse;
                    }

                    console.render_input_line(buffer, previousRenderLength);
                }

                continue;
            }

            if (ch >= 32 && ch <= 126)
            {
                previousRenderLength = buffer.size();
                buffer.push_back(static_cast<char>(ch));
                console.render_input_line(buffer, previousRenderLength);
            }
        }
    }

    bool ends_with_case_insensitive(const std::string& value, const std::string& suffix)
    {
        if (value.size() < suffix.size())
        {
            return false;
        }

        const std::size_t offset = value.size() - suffix.size();
        for (std::size_t i = 0; i < suffix.size(); ++i)
        {
            const unsigned char left = static_cast<unsigned char>(value[offset + i]);
            const unsigned char right = static_cast<unsigned char>(suffix[i]);
            if (std::tolower(left) != std::tolower(right))
            {
                return false;
            }
        }

        return true;
    }

    std::string session_state_to_string(AutoConsole::Abstractions::SessionState state)
    {
        switch (state)
        {
        case AutoConsole::Abstractions::SessionState::Created:
            return "Created";
        case AutoConsole::Abstractions::SessionState::Starting:
            return "Starting";
        case AutoConsole::Abstractions::SessionState::Running:
            return "Running";
        case AutoConsole::Abstractions::SessionState::Stopped:
            return "Stopped";
        case AutoConsole::Abstractions::SessionState::Failed:
            return "Failed";
        default:
            return "Unknown";
        }
    }

    std::string rest_after_first_token(std::istringstream& iss)
    {
        std::string value;
        std::getline(iss >> std::ws, value);
        return trim_copy(value);
    }

    std::string next_token(std::istringstream& iss)
    {
        iss >> std::ws;
        if (!iss.good() || iss.eof())
        {
            return "";
        }

        if (iss.peek() == '"')
        {
            iss.get();
            std::string token;
            bool escaped = false;
            char ch = '\0';
            while (iss.get(ch))
            {
                if (escaped)
                {
                    token.push_back(ch);
                    escaped = false;
                    continue;
                }

                if (ch == '\\')
                {
                    escaped = true;
                    continue;
                }

                if (ch == '"')
                {
                    break;
                }

                token.push_back(ch);
            }

            if (escaped)
            {
                token.push_back('\\');
            }

            return token;
        }

        std::string token;
        iss >> token;
        return trim_copy(token);
    }

    bool session_exists(AutoConsole::Core::CoreRuntime& runtime, const std::string& sessionId)
    {
        if (sessionId.empty())
        {
            return false;
        }

        const auto sessions = runtime.sessions();
        for (const auto& session : sessions)
        {
            if (session.id == sessionId)
            {
                return true;
            }
        }

        return false;
    }

    bool looks_like_session_id(const std::string& value)
    {
        return value.rfind("session-", 0) == 0;
    }

    bool resolve_session_id(
        AutoConsole::Core::CoreRuntime& runtime,
        const std::string& explicitSessionId,
        const std::string& currentSessionId,
        std::string& resolvedSessionId,
        std::string& errorMessage)
    {
        if (!explicitSessionId.empty())
        {
            resolvedSessionId = explicitSessionId;
            return true;
        }

        if (currentSessionId.empty())
        {
            errorMessage = "no current session (start a session first or pass sessionId explicitly)";
            return false;
        }

        if (!session_exists(runtime, currentSessionId))
        {
            errorMessage = "current session is no longer available: " + currentSessionId;
            return false;
        }

        resolvedSessionId = currentSessionId;
        return true;
    }

    void register_standard_actions(AutoConsole::Core::CoreRuntime& runtime)
    {
        constexpr const char* StandardPluginId = "standard";
        const std::vector<std::string> actions = {
            "send_input",
            "wait_output",
            "delay",
            "stop_process",
            "emit_event",
            "call_plugin"
        };

        for (const auto& action : actions)
        {
            runtime.register_plugin_action_handler(
                StandardPluginId,
                action,
                [action](AutoConsole::Abstractions::PluginContext& context, const AutoConsole::Core::CoreRuntime::PluginActionArgs& args, std::string& errorMessage)
                {
                    return AutoConsole::StandardPlugins::StandardPluginActions::execute_action(action, args, context, errorMessage);
                });
        }
    }

}

int main()
{
    auto console = std::make_shared<ConsoleOutput>();
    const bool stdinIsTty = (_isatty(_fileno(stdin)) != 0);
    auto logLevel = std::make_shared<std::atomic<CliLogLevel>>(CliLogLevel::Normal);

    AutoConsole::Core::CoreRuntime runtime;
    runtime.register_plugin(std::make_shared<AutoConsole::StandardPlugins::LogPlugin>());
    register_standard_actions(runtime);
    runtime.set_internal_log_sink([console, logLevel](const std::string& level, const std::string& message)
    {
        if (logLevel->load() == CliLogLevel::Debug)
        {
            console->print_async_line("[" + level + "] " + message);
        }
    });
    runtime.subscribe_events([console](const AutoConsole::Abstractions::Event& eventValue)
    {
        if (eventValue.type == "stdout_line")
        {
            const auto it = eventValue.data.find("text");
            const std::string text = (it != eventValue.data.end()) ? it->second : "";
            console->print_async_line("[stdout][" + eventValue.sessionId + "] " + text);
            return;
        }

        if (eventValue.type == "stderr_line")
        {
            const auto it = eventValue.data.find("text");
            const std::string text = (it != eventValue.data.end()) ? it->second : "";
            console->print_async_line("[stderr][" + eventValue.sessionId + "] " + text);
            return;
        }

        if (eventValue.type == "process_exited")
        {
            const auto it = eventValue.data.find("exitCode");
            const std::string exitCode = (it != eventValue.data.end()) ? it->second : "unknown";
            console->print_async_line("[process][" + eventValue.sessionId + "] exited (code=" + exitCode + ")");
        }
    });

    AutoConsole::Abstractions::Event startupEvent{};
    startupEvent.type = "manual_trigger";
    startupEvent.sessionId = "bootstrap";
    runtime.publish_event(startupEvent);

    console->print_line("AutoConsole v3 started");
    console->print_line("Type 'help' for commands.");

    std::vector<std::string> history = load_history();
    std::string currentSessionId;
    std::string line;
    while (true)
    {
        if (stdinIsTty)
        {
            if (!read_line_with_history(*console, history, line))
            {
                break;
            }
        }
        else if (!std::getline(std::cin, line))
        {
            break;
        }

        const std::string submittedLine = line;
        if (stdinIsTty)
        {
            push_history(history, submittedLine);
        }
        if (!stdinIsTty)
        {
            // In redirected input mode the terminal does not echo user input.
            // Render submitted command explicitly for readable logs.
            console->print_line("> " + submittedLine);
        }
        line = trim_copy(submittedLine);

        std::istringstream iss(line);
        std::string command;
        iss >> command;

        if (command.empty())
        {
            continue;
        }

        if (command == "exit")
        {
            break;
        }

        if (command == "help")
        {
            console->print_block(help_text());
            continue;
        }

        if (command == "ping")
        {
            console->print_line("pong");
            continue;
        }

        if (command == "start")
        {
            const std::string profileFile = rest_after_first_token(iss);
            if (profileFile.empty())
            {
                console->print_line("error: usage: start <profile-file>");
                continue;
            }

            std::string normalizedProfileFile = profileFile;
            if (!ends_with_case_insensitive(normalizedProfileFile, ".json"))
            {
                normalizedProfileFile += ".json";
            }

            const std::string profilePath = "profiles/examples/" + normalizedProfileFile;
            std::string loadError;
            const auto profile = AutoConsole::Core::ProfileLoader::load_from_file(profilePath, loadError);
            if (!profile.has_value())
            {
                console->print_line("error: failed to load profile: " + loadError);
                continue;
            }

            console->print_line("profile loaded: " + profile->name + " (" + profile->id + ")");

            const auto startResult = runtime.start_session(*profile);
            console->print_line("session created: " + startResult.session.id);

            if (startResult.started)
            {
                currentSessionId = startResult.session.id;
                console->print_line("process started: " + startResult.session.id);
            }
            else
            {
                console->print_line("error: process failed: " + startResult.errorMessage);
            }

            continue;
        }

        if (command == "current")
        {
            if (currentSessionId.empty())
            {
                console->print_line("current session: none");
            }
            else
            {
                console->print_line("current session: " + currentSessionId);
            }
            continue;
        }

        if (command == "sessions")
        {
            const auto sessions = runtime.sessions();
            if (sessions.empty())
            {
                console->print_line("no sessions");
                continue;
            }

            {
                std::ostringstream oss;
                oss << std::left
                    << std::setw(14) << "sessionId"
                    << " | " << std::setw(28) << "profile"
                    << " | " << "state";
                console->print_line(oss.str());
            }
            console->print_line("--------------+------------------------------+---------");
            for (const auto& session : sessions)
            {
                std::ostringstream row;
                row << std::left
                    << std::setw(14) << session.id
                    << " | " << std::setw(28) << session.profileName
                    << " | " << session_state_to_string(session.state);
                console->print_line(row.str());
            }
            continue;
        }

        if (command == "stop")
        {
            const std::string explicitSessionId = rest_after_first_token(iss);
            std::string sessionId;
            std::string resolveError;
            if (!resolve_session_id(runtime, explicitSessionId, currentSessionId, sessionId, resolveError))
            {
                console->print_line("error: failed to stop session: " + resolveError);
                continue;
            }

            std::string errorMessage;
            if (runtime.stop_session(sessionId, errorMessage))
            {
                console->print_line("session stopped: " + sessionId);
            }
            else
            {
                console->print_line("error: failed to stop session: " + errorMessage);
            }
            continue;
        }

        if (command == "send")
        {
            const std::string firstToken = next_token(iss);
            const std::string remaining = rest_after_first_token(iss);
            if (firstToken.empty())
            {
                console->print_line("error: usage: send [sessionId] <text>");
                continue;
            }

            std::string explicitSessionId;
            std::string text;
            if (!remaining.empty() && looks_like_session_id(firstToken))
            {
                explicitSessionId = firstToken;
                text = remaining;
            }
            else
            {
                text = firstToken;
                if (!remaining.empty())
                {
                    text += " " + remaining;
                }
            }

            std::string sessionId;
            std::string resolveError;
            if (!resolve_session_id(runtime, explicitSessionId, currentSessionId, sessionId, resolveError))
            {
                console->print_line("error: failed to send input: " + resolveError);
                continue;
            }

            std::string errorMessage;
            if (runtime.send_input(sessionId, text, errorMessage))
            {
                console->print_line("input sent: " + sessionId);
            }
            else
            {
                console->print_line("error: failed to send input: " + errorMessage);
            }
            continue;
        }

        if (command == "plugin")
        {
            const std::string action = next_token(iss);
            if (action == "send_input")
            {
                const std::string firstToken = next_token(iss);
                const std::string remaining = rest_after_first_token(iss);
                if (firstToken.empty())
                {
                    console->print_line("error: usage: plugin send_input [sessionId] <text>");
                    continue;
                }

                std::string explicitSessionId;
                std::string text;
                if (!remaining.empty() && looks_like_session_id(firstToken))
                {
                    explicitSessionId = firstToken;
                    text = remaining;
                }
                else
                {
                    text = firstToken;
                    if (!remaining.empty())
                    {
                        text += " " + remaining;
                    }
                }

                std::string sessionId;
                std::string resolveError;
                if (!resolve_session_id(runtime, explicitSessionId, currentSessionId, sessionId, resolveError))
                {
                    console->print_line("error: plugin send_input failed: " + resolveError);
                    continue;
                }

                std::string errorMessage;
                if (AutoConsole::StandardPlugins::StandardPluginActions::send_input(runtime.plugin_context(), sessionId, text, errorMessage))
                {
                    console->print_line("plugin send_input success");
                }
                else
                {
                    console->print_line("error: plugin send_input failed: " + errorMessage);
                }
                continue;
            }

            if (action == "wait_output")
            {
                const std::string firstToken = next_token(iss);
                const std::string secondToken = next_token(iss);
                const std::string timeoutText = next_token(iss);
                if (firstToken.empty() || secondToken.empty())
                {
                    console->print_line("error: usage: plugin wait_output [sessionId] <contains> <timeoutMs>");
                    continue;
                }

                std::string explicitSessionId;
                std::string contains;
                std::string timeoutToken;
                if (!timeoutText.empty() && looks_like_session_id(firstToken))
                {
                    explicitSessionId = firstToken;
                    contains = secondToken;
                    timeoutToken = timeoutText;
                }
                else
                {
                    contains = firstToken;
                    timeoutToken = secondToken;
                }

                int timeoutMs = 0;
                try
                {
                    timeoutMs = std::stoi(timeoutToken);
                }
                catch (...)
                {
                    console->print_line("error: invalid timeoutMs");
                    continue;
                }

                std::string sessionId;
                std::string resolveError;
                if (!resolve_session_id(runtime, explicitSessionId, currentSessionId, sessionId, resolveError))
                {
                    console->print_line("error: plugin wait_output failed: " + resolveError);
                    continue;
                }

                std::string errorMessage;
                if (AutoConsole::StandardPlugins::StandardPluginActions::wait_output(runtime.plugin_context(), sessionId, contains, timeoutMs, errorMessage))
                {
                    console->print_line("plugin wait_output success");
                }
                else
                {
                    console->print_line("error: plugin wait_output failed: " + errorMessage);
                }
                continue;
            }

            if (action == "delay")
            {
                const std::string durationText = next_token(iss);
                if (durationText.empty())
                {
                    console->print_line("error: usage: plugin delay <durationMs>");
                    continue;
                }

                int durationMs = 0;
                try
                {
                    durationMs = std::stoi(durationText);
                }
                catch (...)
                {
                    console->print_line("error: invalid durationMs");
                    continue;
                }

                std::string errorMessage;
                if (AutoConsole::StandardPlugins::StandardPluginActions::delay(runtime.plugin_context(), durationMs, errorMessage))
                {
                    console->print_line("plugin delay success");
                }
                else
                {
                    console->print_line("error: plugin delay failed: " + errorMessage);
                }
                continue;
            }

            if (action == "stop_process")
            {
                const std::string explicitSessionId = next_token(iss);
                std::string sessionId;
                std::string resolveError;
                if (!resolve_session_id(runtime, explicitSessionId, currentSessionId, sessionId, resolveError))
                {
                    console->print_line("error: plugin stop_process failed: " + resolveError);
                    continue;
                }

                std::string errorMessage;
                if (AutoConsole::StandardPlugins::StandardPluginActions::stop_process(runtime.plugin_context(), sessionId, errorMessage))
                {
                    console->print_line("plugin stop_process success");
                }
                else
                {
                    console->print_line("error: plugin stop_process failed: " + errorMessage);
                }
                continue;
            }

            if (action == "emit_event")
            {
                const std::string eventType = next_token(iss);
                const std::string sessionId = next_token(iss);
                const std::string payload = rest_after_first_token(iss);
                if (eventType.empty())
                {
                    console->print_line("error: usage: plugin emit_event <eventType> [sessionId] [payload]");
                    continue;
                }

                std::string errorMessage;
                if (AutoConsole::StandardPlugins::StandardPluginActions::emit_event(runtime.plugin_context(), eventType, sessionId, payload, errorMessage))
                {
                    console->print_line("plugin emit_event success");
                }
                else
                {
                    console->print_line("error: plugin emit_event failed: " + errorMessage);
                }
                continue;
            }

            if (action == "call_plugin")
            {
                const std::string pluginId = next_token(iss);
                const std::string targetAction = next_token(iss);
                if (pluginId.empty() || targetAction.empty())
                {
                    console->print_line("error: usage: plugin call_plugin <pluginId> <action> [key=value ...]");
                    continue;
                }

                AutoConsole::StandardPlugins::StandardPluginActions::ActionArgs callArgs;
                bool invalidArgs = false;
                while (true)
                {
                    iss >> std::ws;
                    if (!iss.good() || iss.eof())
                    {
                        break;
                    }

                    const std::string kvToken = next_token(iss);
                    const auto delimiter = kvToken.find('=');
                    if (delimiter == std::string::npos || delimiter == 0 || delimiter == kvToken.size() - 1)
                    {
                        console->print_line("error: invalid argument format, expected key=value: " + kvToken);
                        invalidArgs = true;
                        break;
                    }

                    callArgs["arg." + kvToken.substr(0, delimiter)] = kvToken.substr(delimiter + 1);
                }

                if (invalidArgs)
                {
                    continue;
                }

                callArgs["pluginId"] = pluginId;
                callArgs["action"] = targetAction;

                std::string errorMessage;
                if (AutoConsole::StandardPlugins::StandardPluginActions::execute_action("call_plugin", callArgs, runtime.plugin_context(), errorMessage))
                {
                    console->print_line("plugin call_plugin success");
                }
                else
                {
                    console->print_line("error: plugin call_plugin failed: " + errorMessage);
                }
                continue;
            }

            console->print_line("error: unknown plugin action: " + action);
            continue;
        }

        if (command == "loglevel")
        {
            const std::string mode = next_token(iss);
            if (mode.empty())
            {
                const auto current = logLevel->load();
                console->print_line(std::string("loglevel: ") + (current == CliLogLevel::Debug ? "debug" : "normal"));
                continue;
            }

            if (mode == "normal")
            {
                logLevel->store(CliLogLevel::Normal);
                console->print_line("loglevel set to normal");
                continue;
            }

            if (mode == "debug")
            {
                logLevel->store(CliLogLevel::Debug);
                console->print_line("loglevel set to debug");
                continue;
            }

            console->print_line("error: usage: loglevel [normal|debug]");
            continue;
        }

        console->print_line("error: unknown command: " + command + " (type 'help')");
    }

    return 0;
}

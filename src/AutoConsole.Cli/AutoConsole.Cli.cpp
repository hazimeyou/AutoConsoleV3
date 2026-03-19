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
            "  sessions\n"
            "  stop <sessionId>\n"
            "  send <sessionId> <text>\n"
            "  plugin send_input <sessionId> <text>\n"
            "  plugin wait_output <sessionId> <contains> <timeoutMs>\n"
            "  plugin delay <durationMs>\n"
            "  plugin stop_process <sessionId>\n"
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

    std::string line;
    while (true)
    {
        if (stdinIsTty)
        {
            console->print_prompt();
        }
        if (!std::getline(std::cin, line))
        {
            break;
        }
        const std::string submittedLine = line;
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

            const std::string profilePath = "profiles/examples/" + profileFile;
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
                console->print_line("process started: " + startResult.session.id);
            }
            else
            {
                console->print_line("error: process failed: " + startResult.errorMessage);
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
            const std::string sessionId = rest_after_first_token(iss);
            if (sessionId.empty())
            {
                console->print_line("error: usage: stop <sessionId>");
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
            const std::string sessionId = next_token(iss);
            const std::string text = rest_after_first_token(iss);
            if (sessionId.empty() || text.empty())
            {
                console->print_line("error: usage: send <sessionId> <text>");
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
                const std::string sessionId = next_token(iss);
                const std::string text = rest_after_first_token(iss);
                if (sessionId.empty() || text.empty())
                {
                    console->print_line("error: usage: plugin send_input <sessionId> <text>");
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
                const std::string sessionId = next_token(iss);
                const std::string contains = next_token(iss);
                const std::string timeoutText = next_token(iss);
                if (sessionId.empty() || contains.empty() || timeoutText.empty())
                {
                    console->print_line("error: usage: plugin wait_output <sessionId> <contains> <timeoutMs>");
                    continue;
                }

                int timeoutMs = 0;
                try
                {
                    timeoutMs = std::stoi(timeoutText);
                }
                catch (...)
                {
                    console->print_line("error: invalid timeoutMs");
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
                const std::string sessionId = next_token(iss);
                if (sessionId.empty())
                {
                    console->print_line("error: usage: plugin stop_process <sessionId>");
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

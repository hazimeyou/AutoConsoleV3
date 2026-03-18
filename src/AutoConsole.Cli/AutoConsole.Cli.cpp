#include <iostream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/SessionState.h"
#include "AutoConsole/Core/CoreRuntime.h"
#include "AutoConsole/Core/ProfileLoader.h"
#include "LogPlugin.h"
#include "StandardPluginActions.h"

namespace
{
    void print_help()
    {
        std::cout << "Commands:\n";
        std::cout << "  help\n";
        std::cout << "  ping\n";
        std::cout << "  start <profile-file>\n";
        std::cout << "  sessions\n";
        std::cout << "  stop <sessionId>\n";
        std::cout << "  send <sessionId> <text>\n";
        std::cout << "  plugin send_input <sessionId> <text>\n";
        std::cout << "  plugin wait_output <sessionId> <contains> <timeoutMs>\n";
        std::cout << "  exit\n";
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
        std::string token;
        iss >> token;
        return trim_copy(token);
    }

    void print_sessions(const std::vector<AutoConsole::Abstractions::SessionInfo>& sessions)
    {
        if (sessions.empty())
        {
            std::cout << "no sessions\n";
            return;
        }

        std::cout << std::left
            << std::setw(14) << "sessionId"
            << " | " << std::setw(28) << "profile"
            << " | " << "state" << "\n";
        std::cout << "--------------+------------------------------+---------\n";

        for (const auto& session : sessions)
        {
            std::cout << std::left
                << std::setw(14) << session.id
                << " | " << std::setw(28) << session.profileName
                << " | " << session_state_to_string(session.state) << "\n";
        }
    }
}

int main()
{
    AutoConsole::Core::CoreRuntime runtime;
    runtime.register_plugin(std::make_shared<AutoConsole::StandardPlugins::LogPlugin>());
    runtime.subscribe_events([](const AutoConsole::Abstractions::Event& eventValue)
    {
        if (eventValue.type == "stdout_line")
        {
            const auto it = eventValue.data.find("text");
            const std::string text = (it != eventValue.data.end()) ? it->second : "";
            std::cout << "[stdout][" << eventValue.sessionId << "] " << text << "\n";
            return;
        }

        if (eventValue.type == "stderr_line")
        {
            const auto it = eventValue.data.find("text");
            const std::string text = (it != eventValue.data.end()) ? it->second : "";
            std::cout << "[stderr][" << eventValue.sessionId << "] " << text << "\n";
            return;
        }

        if (eventValue.type == "process_exited")
        {
            const auto it = eventValue.data.find("exitCode");
            const std::string exitCode = (it != eventValue.data.end()) ? it->second : "unknown";
            std::cout << "[process][" << eventValue.sessionId << "] exited (code=" << exitCode << ")\n";
        }
    });

    AutoConsole::Abstractions::Event startupEvent{};
    startupEvent.type = "manual_trigger";
    startupEvent.sessionId = "bootstrap";
    runtime.publish_event(startupEvent);

    std::cout << "AutoConsole v3 started\n";
    std::cout << "Type 'help' for commands.\n";

    std::string line;
    while (true)
    {
        std::cout << "> ";
        if (!std::getline(std::cin, line))
        {
            break;
        }
        line = trim_copy(line);

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
            print_help();
            continue;
        }

        if (command == "ping")
        {
            std::cout << "pong\n";
            continue;
        }

        if (command == "start")
        {
            const std::string profileFile = rest_after_first_token(iss);
            if (profileFile.empty())
            {
                std::cout << "error: usage: start <profile-file>\n";
                continue;
            }

            const std::string profilePath = "profiles/examples/" + profileFile;
            std::string loadError;
            const auto profile = AutoConsole::Core::ProfileLoader::load_from_file(profilePath, loadError);
            if (!profile.has_value())
            {
                std::cout << "error: failed to load profile: " << loadError << "\n";
                continue;
            }

            std::cout << "profile loaded: " << profile->name << " (" << profile->id << ")\n";

            const auto startResult = runtime.start_session(*profile);
            std::cout << "session created: " << startResult.session.id << "\n";

            if (startResult.started)
            {
                std::cout << "process started: " << startResult.session.id << "\n";
            }
            else
            {
                std::cout << "error: process failed: " << startResult.errorMessage << "\n";
            }

            continue;
        }

        if (command == "sessions")
        {
            print_sessions(runtime.sessions());
            continue;
        }

        if (command == "stop")
        {
            const std::string sessionId = rest_after_first_token(iss);
            if (sessionId.empty())
            {
                std::cout << "error: usage: stop <sessionId>\n";
                continue;
            }

            std::string errorMessage;
            if (runtime.stop_session(sessionId, errorMessage))
            {
                std::cout << "session stopped: " << sessionId << "\n";
            }
            else
            {
                std::cout << "error: failed to stop session: " << errorMessage << "\n";
            }
            continue;
        }

        if (command == "send")
        {
            const std::string sessionId = next_token(iss);
            const std::string text = rest_after_first_token(iss);
            if (sessionId.empty() || text.empty())
            {
                std::cout << "error: usage: send <sessionId> <text>\n";
                continue;
            }

            std::string errorMessage;
            if (runtime.send_input(sessionId, text, errorMessage))
            {
                std::cout << "input sent: " << sessionId << "\n";
            }
            else
            {
                std::cout << "error: failed to send input: " << errorMessage << "\n";
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
                    std::cout << "error: usage: plugin send_input <sessionId> <text>\n";
                    continue;
                }

                std::string errorMessage;
                if (AutoConsole::StandardPlugins::StandardPluginActions::send_input(runtime.plugin_context(), sessionId, text, errorMessage))
                {
                    std::cout << "plugin send_input success\n";
                }
                else
                {
                    std::cout << "error: plugin send_input failed: " << errorMessage << "\n";
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
                    std::cout << "error: usage: plugin wait_output <sessionId> <contains> <timeoutMs>\n";
                    continue;
                }

                int timeoutMs = 0;
                try
                {
                    timeoutMs = std::stoi(timeoutText);
                }
                catch (...)
                {
                    std::cout << "error: invalid timeoutMs\n";
                    continue;
                }

                std::string errorMessage;
                if (AutoConsole::StandardPlugins::StandardPluginActions::wait_output(runtime.plugin_context(), sessionId, contains, timeoutMs, errorMessage))
                {
                    std::cout << "plugin wait_output success\n";
                }
                else
                {
                    std::cout << "error: plugin wait_output failed: " << errorMessage << "\n";
                }
                continue;
            }

            std::cout << "error: unknown plugin action: " << action << "\n";
            continue;
        }

        std::cout << "error: unknown command: " << command << " (type 'help')\n";
    }

    return 0;
}

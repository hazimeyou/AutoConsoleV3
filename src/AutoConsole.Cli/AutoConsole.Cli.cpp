#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/SessionState.h"
#include "AutoConsole/Core/CoreRuntime.h"
#include "AutoConsole/Core/ProfileLoader.h"
#include "LogPlugin.h"

namespace
{
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
        return value;
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
            std::cout << "Available commands: help, ping, start <profile-file>, sessions, stop <sessionId>, exit\n";
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
                std::cout << "usage: start <profile-file>\n";
                continue;
            }

            const std::string profilePath = "profiles/examples/" + profileFile;
            std::string loadError;
            const auto profile = AutoConsole::Core::ProfileLoader::load_from_file(profilePath, loadError);
            if (!profile.has_value())
            {
                std::cout << "failed to load profile: " << loadError << "\n";
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
                std::cout << "process failed: " << startResult.errorMessage << "\n";
            }

            continue;
        }

        if (command == "sessions")
        {
            const auto sessions = runtime.sessions();
            if (sessions.empty())
            {
                std::cout << "no sessions\n";
                continue;
            }

            for (const auto& session : sessions)
            {
                std::cout << session.id << " | " << session.profileName << " | " << session_state_to_string(session.state) << "\n";
            }
            continue;
        }

        if (command == "stop")
        {
            const std::string sessionId = rest_after_first_token(iss);
            if (sessionId.empty())
            {
                std::cout << "usage: stop <sessionId>\n";
                continue;
            }

            if (runtime.stop_session(sessionId))
            {
                std::cout << "session stopped: " << sessionId << "\n";
            }
            else
            {
                std::cout << "failed to stop session: " << sessionId << "\n";
            }
            continue;
        }

        std::cout << "unknown command\n";
    }

    return 0;
}

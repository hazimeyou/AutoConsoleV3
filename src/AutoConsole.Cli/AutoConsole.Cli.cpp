#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <Windows.h>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/SessionState.h"
#include "AutoConsole/Core/CoreRuntime.h"
#include "AutoConsole/Core/PluginHost.h"
#include "AutoConsole/Core/ProfileLoader.h"
#include "ApiLogBuffer.h"
#include "ApiServer.h"
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
        const auto begin = value.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos)
        {
            return "";
        }
        const auto end = value.find_last_not_of(" \t\r\n");
        value = value.substr(begin, end - begin + 1);
        return value;
    }

    std::string resolve_profile_path(const std::string& profileInput)
    {
        namespace fs = std::filesystem;

        auto executable_directory = []() -> fs::path
        {
            char pathBuffer[MAX_PATH] = {};
            const DWORD length = GetModuleFileNameA(nullptr, pathBuffer, static_cast<DWORD>(sizeof(pathBuffer)));
            if (length == 0 || length >= sizeof(pathBuffer))
            {
                return fs::current_path();
            }

            fs::path exePath(pathBuffer);
            return exePath.parent_path();
        };

        auto absolute_or_empty = [](const fs::path& root, const fs::path& relativePath) -> std::string
        {
            const fs::path candidate = (root / relativePath).lexically_normal();
            if (fs::exists(candidate))
            {
                return candidate.string();
            }
            return "";
        };

        auto collect_self_and_parents = [](const fs::path& startPath) -> std::vector<fs::path>
        {
            std::vector<fs::path> result;
            if (startPath.empty())
            {
                return result;
            }

            fs::path current = startPath.lexically_normal();
            while (!current.empty())
            {
                result.push_back(current);
                const fs::path parent = current.parent_path();
                if (parent == current || parent.empty())
                {
                    break;
                }
                current = parent;
            }
            return result;
        };

        auto dedupe_paths = [](std::vector<fs::path>& paths)
        {
            std::vector<fs::path> unique;
            for (const auto& path : paths)
            {
                const fs::path normalized = path.lexically_normal();
                const bool exists = std::find(unique.begin(), unique.end(), normalized) != unique.end();
                if (!exists)
                {
                    unique.push_back(normalized);
                }
            }
            paths = std::move(unique);
        };

        const fs::path cwd = fs::current_path();
        const fs::path exeDir = executable_directory();

        std::vector<fs::path> roots = collect_self_and_parents(cwd);
        const auto exeRoots = collect_self_and_parents(exeDir);
        roots.insert(roots.end(), exeRoots.begin(), exeRoots.end());
        dedupe_paths(roots);

        auto resolve_against_roots = [&](const fs::path& relativePath) -> std::string
        {
            for (const auto& root : roots)
            {
                const std::string found = absolute_or_empty(root, relativePath);
                if (!found.empty())
                {
                    return found;
                }
            }
            return (cwd / relativePath).lexically_normal().string();
        };

        const bool hasSeparator = (profileInput.find('/') != std::string::npos || profileInput.find('\\') != std::string::npos);

        if (hasSeparator)
        {
            const fs::path providedPath(profileInput);
            if (providedPath.is_absolute())
            {
                return providedPath.lexically_normal().string();
            }
            return resolve_against_roots(providedPath);
        }

        std::string fileName = profileInput;
        if (profileInput.size() >= 5 && profileInput.substr(profileInput.size() - 5) == ".json")
        {
            fileName = profileInput;
        }
        else
        {
            fileName = profileInput + ".json";
        }

        return resolve_against_roots(fs::path("profiles") / "examples" / fileName);
    }

    std::string resolve_plugins_directory()
    {
        namespace fs = std::filesystem;

        auto executable_directory = []() -> fs::path
        {
            char pathBuffer[MAX_PATH] = {};
            const DWORD length = GetModuleFileNameA(nullptr, pathBuffer, static_cast<DWORD>(sizeof(pathBuffer)));
            if (length == 0 || length >= sizeof(pathBuffer))
            {
                return fs::current_path();
            }

            fs::path exePath(pathBuffer);
            return exePath.parent_path();
        };

        auto collect_self_and_parents = [](const fs::path& startPath) -> std::vector<fs::path>
        {
            std::vector<fs::path> result;
            if (startPath.empty())
            {
                return result;
            }

            fs::path current = startPath.lexically_normal();
            while (!current.empty())
            {
                result.push_back(current);
                const fs::path parent = current.parent_path();
                if (parent == current || parent.empty())
                {
                    break;
                }
                current = parent;
            }
            return result;
        };

        auto dedupe_paths = [](std::vector<fs::path>& paths)
        {
            std::vector<fs::path> unique;
            for (const auto& path : paths)
            {
                const fs::path normalized = path.lexically_normal();
                const bool exists = std::find(unique.begin(), unique.end(), normalized) != unique.end();
                if (!exists)
                {
                    unique.push_back(normalized);
                }
            }
            paths = std::move(unique);
        };

        const fs::path cwd = fs::current_path();
        const fs::path exeDir = executable_directory();

        std::vector<fs::path> roots = collect_self_and_parents(cwd);
        const auto exeRoots = collect_self_and_parents(exeDir);
        roots.insert(roots.end(), exeRoots.begin(), exeRoots.end());
        dedupe_paths(roots);

        const fs::path pluginRelativePath = fs::path("plugins") / "installed";
        for (const auto& root : roots)
        {
            const fs::path candidate = (root / pluginRelativePath).lexically_normal();
            if (fs::exists(candidate))
            {
                return candidate.string();
            }
        }

        return pluginRelativePath.string();
    }

    std::string plugin_source_to_string(AutoConsole::Core::PluginSource source)
    {
        switch (source)
        {
        case AutoConsole::Core::PluginSource::External:
            return "external";
        default:
            return "standard";
        }
    }

    std::optional<int> parse_port(const std::string& value)
    {
        try
        {
            int port = std::stoi(value);
            if (port <= 0 || port > 65535)
            {
                return std::nullopt;
            }
            return port;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}

int main(int argc, char** argv)
{
    std::unique_ptr<AutoConsole::Cli::ApiLogBuffer> apiLogBuffer;
    bool apiEnabled = false;
    int apiPort = 5071;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--api")
        {
            apiEnabled = true;
            continue;
        }

        if (arg == "--port")
        {
            if (i + 1 >= argc)
            {
                std::cout << "missing value for --port\n";
                return 1;
            }
            const auto parsed = parse_port(argv[++i]);
            if (!parsed.has_value())
            {
                std::cout << "invalid port\n";
                return 1;
            }
            apiPort = *parsed;
            continue;
        }

        const std::string prefix = "--port=";
        if (arg.rfind(prefix, 0) == 0)
        {
            const auto parsed = parse_port(arg.substr(prefix.size()));
            if (!parsed.has_value())
            {
                std::cout << "invalid port\n";
                return 1;
            }
            apiPort = *parsed;
            continue;
        }
    }

    AutoConsole::Core::CoreRuntime runtime;
    runtime.register_plugin(std::make_shared<AutoConsole::StandardPlugins::LogPlugin>(), AutoConsole::Core::PluginSource::Standard);

    const auto pluginErrors = runtime.load_external_plugins(resolve_plugins_directory());
    for (const auto& error : pluginErrors)
    {
        std::cout << "[plugin] " << error << "\n";
    }

    std::unique_ptr<AutoConsole::Cli::ApiServer> apiServer;
    if (apiEnabled)
    {
        apiLogBuffer = std::make_unique<AutoConsole::Cli::ApiLogBuffer>(100);
        auto resolver = [](const std::string& profileInput, std::string& errorMessage)
        {
            const std::string profilePath = resolve_profile_path(profileInput);
            return AutoConsole::Core::ProfileLoader::load_from_file(profilePath, errorMessage);
        };

        apiServer = std::make_unique<AutoConsole::Cli::ApiServer>(runtime, resolver, apiLogBuffer.get());
        std::string apiError;
        if (!apiServer->start(apiPort, apiError))
        {
            std::cout << "failed to start API server: " << apiError << "\n";
        }
        else
        {
            std::cout << "API server listening on http://127.0.0.1:" << apiPort << "\n";
        }
    }

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

    if (apiLogBuffer)
    {
        runtime.subscribe_events([&apiLogBuffer](const AutoConsole::Abstractions::Event& eventValue)
        {
            if (eventValue.type == "stdout_line" || eventValue.type == "stderr_line")
            {
                const auto textIt = eventValue.data.find("text");
                const auto tsIt = eventValue.data.find("timestamp");
                const std::string text = (textIt != eventValue.data.end()) ? textIt->second : "";
                const std::string timestamp = (tsIt != eventValue.data.end()) ? tsIt->second : "";
                const std::string type = (eventValue.type == "stdout_line") ? "stdout" : "stderr";
                apiLogBuffer->add(eventValue.sessionId, type, text, timestamp);
            }
        });
    }

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
            std::cout << "Available commands: help, ping, start <profile>, sessions, stop <sessionId>, plugins, plugin info <pluginId>, exit\n";
            std::cout << "CLI options: --api, --port <n>\n";
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

            const std::string profilePath = resolve_profile_path(profileFile);
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

        if (command == "plugins")
        {
            const auto plugins = runtime.plugins();
            if (plugins.empty())
            {
                std::cout << "no plugins\n";
                continue;
            }

            for (const auto& plugin : plugins)
            {
                std::cout << plugin.metadata.id << " | " << plugin.metadata.displayName << " | "
                    << plugin.metadata.version << " | " << plugin_source_to_string(plugin.source) << "\n";
            }
            continue;
        }

        if (command == "plugin")
        {
            const std::string subcommand = rest_after_first_token(iss);
            if (subcommand.rfind("info ", 0) == 0)
            {
                const std::string pluginId = subcommand.substr(5);
                if (pluginId.empty())
                {
                    std::cout << "usage: plugin info <pluginId>\n";
                    continue;
                }

                const auto plugins = runtime.plugins();
                const auto it = std::find_if(plugins.begin(), plugins.end(), [&](const AutoConsole::Core::PluginInfo& info)
                {
                    return info.metadata.id == pluginId;
                });

                if (it == plugins.end())
                {
                    std::cout << "plugin not found: " << pluginId << "\n";
                    continue;
                }

                const auto& metadata = it->metadata;
                std::cout << "id: " << metadata.id << "\n";
                std::cout << "displayName: " << metadata.displayName << "\n";
                std::cout << "version: " << metadata.version << "\n";
                std::cout << "apiVersion: " << metadata.apiVersion << "\n";
                std::cout << "author: " << metadata.author << "\n";
                std::cout << "description: " << metadata.description << "\n";
                std::cout << "capabilities: ";
                for (size_t i = 0; i < metadata.capabilities.size(); ++i)
                {
                    if (i > 0)
                    {
                        std::cout << ", ";
                    }
                    std::cout << metadata.capabilities[i];
                }
                std::cout << "\n";
                std::cout << "source: " << plugin_source_to_string(it->source) << "\n";
                continue;
            }

            std::cout << "usage: plugin info <pluginId>\n";
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
                std::cout << session.id << " | " << session.profileName << " | " << session_state_to_string(session.state);
                if (session.hasExitCode)
                {
                    std::cout << " | exitCode=" << session.exitCode;
                }
                std::cout << "\n";
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

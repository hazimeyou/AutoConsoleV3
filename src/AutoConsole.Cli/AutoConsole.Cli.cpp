#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <atomic>
#include <mutex>
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
            "  start <profile>\n"
            "  run <workflow>\n"
            "  plugins\n"
            "  current\n"
            "  sessions\n"
            "  stop [sessionId]\n"
            "  send [sessionId] <text>\n"
            "  plugin send_input [sessionId] <text>\n"
            "  plugin info <pluginId>\n"
            "  plugin wait_output [sessionId] <contains> <timeoutMs>\n"
            "  plugin delay <durationMs>\n"
            "  plugin timer <durationMs>\n"
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

    bool starts_with(const std::string& value, const std::string& prefix)
    {
        if (value.size() < prefix.size())
        {
            return false;
        }

        return value.compare(0, prefix.size(), prefix) == 0;
    }

    std::string common_prefix(const std::vector<std::string>& values)
    {
        if (values.empty())
        {
            return "";
        }

        std::string prefix = values.front();
        for (std::size_t i = 1; i < values.size(); ++i)
        {
            std::size_t j = 0;
            while (j < prefix.size() && j < values[i].size() && prefix[j] == values[i][j])
            {
                ++j;
            }

            prefix.resize(j);
            if (prefix.empty())
            {
                break;
            }
        }

        return prefix;
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
        const std::vector<std::string> topLevelCommands = {
            "help",
            "ping",
            "start",
            "sessions",
            "stop",
            "send",
            "plugins",
            "plugin",
            "current",
            "loglevel",
            "run",
            "exit"
        };

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

            if (ch == 9)
            {
                const std::string trimmed = trim_copy(buffer);
                const bool inFirstToken = trimmed.find_first_of(" \t") == std::string::npos;
                if (!inFirstToken)
                {
                    continue;
                }

                std::vector<std::string> matches;
                for (const auto& command : topLevelCommands)
                {
                    if (starts_with(command, trimmed))
                    {
                        matches.push_back(command);
                    }
                }

                if (matches.empty())
                {
                    continue;
                }

                if (matches.size() == 1)
                {
                    previousRenderLength = buffer.size();
                    buffer = matches.front() + " ";
                    browsingHistory = false;
                    console.render_input_line(buffer, previousRenderLength);
                    continue;
                }

                const std::string prefix = common_prefix(matches);
                if (prefix.size() > trimmed.size())
                {
                    previousRenderLength = buffer.size();
                    buffer = prefix;
                    browsingHistory = false;
                    console.render_input_line(buffer, previousRenderLength);
                    continue;
                }

                std::sort(matches.begin(), matches.end());
                console.finish_input_line();
                std::string candidates = "candidates:";
                for (const auto& match : matches)
                {
                    candidates += " " + match;
                }
                console.print_line(candidates);
                previousRenderLength = 0;
                console.render_input_line(buffer, previousRenderLength);
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

    std::string normalize_json_filename(const std::string& fileName)
    {
        std::string normalized = fileName;
        if (!ends_with_case_insensitive(normalized, ".json"))
        {
            normalized += ".json";
        }

        return normalized;
    }

    std::string resolve_profile_path(const std::string& profileFile)
    {
        return "profiles/examples/" + normalize_json_filename(profileFile);
    }

    bool looks_like_json_object(const std::string& jsonText)
    {
        const std::string trimmed = trim_copy(jsonText);
        if (trimmed.empty())
        {
            return false;
        }

        return trimmed.front() == '{' && trimmed.back() == '}';
    }

    std::optional<std::string> extract_string_value(const std::string& jsonText, const std::string& key)
    {
        const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
        std::smatch match;
        if (!std::regex_search(jsonText, match, pattern) || match.size() < 2)
        {
            return std::nullopt;
        }

        return match[1].str();
    }

    std::optional<int> extract_int_value(const std::string& jsonText, const std::string& key)
    {
        const std::regex pattern("\\\"" + key + "\\\"\\s*:\\s*(-?\\d+)");
        std::smatch match;
        if (!std::regex_search(jsonText, match, pattern) || match.size() < 2)
        {
            return std::nullopt;
        }

        try
        {
            return std::stoi(match[1].str());
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    struct WorkflowStep
    {
        std::string action;
        std::string profile;
        std::string contains;
        std::string text;
        int timeoutMs = -1;
        int durationMs = -1;
    };

    struct WorkflowDefinition
    {
        std::string id;
        std::vector<WorkflowStep> steps;
    };

    bool resolve_session_id(
        AutoConsole::Core::CoreRuntime& runtime,
        const std::string& explicitSessionId,
        const std::string& currentSessionId,
        std::string& resolvedSessionId,
        std::string& errorMessage);

    std::optional<WorkflowDefinition> load_workflow(
        const std::string& filePath,
        std::string& errorMessage)
    {
        std::ifstream file(filePath);
        if (!file)
        {
            errorMessage = "file not found: " + filePath;
            return std::nullopt;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string jsonText = buffer.str();

        if (!looks_like_json_object(jsonText))
        {
            errorMessage = "invalid JSON: expected a JSON object";
            return std::nullopt;
        }

        const std::regex stepsArrayPattern("\\\"steps\\\"\\s*:\\s*\\[([\\s\\S]*?)\\]", std::regex::icase);
        std::smatch stepsArrayMatch;
        if (!std::regex_search(jsonText, stepsArrayMatch, stepsArrayPattern) || stepsArrayMatch.size() < 2)
        {
            errorMessage = "missing required field: steps";
            return std::nullopt;
        }

        WorkflowDefinition workflow{};
        const auto id = extract_string_value(jsonText, "id");
        workflow.id = id.has_value() ? *id : "";

        const std::string stepsContent = stepsArrayMatch[1].str();
        const std::regex stepObjectPattern("\\{([\\s\\S]*?)\\}");
        auto begin = std::sregex_iterator(stepsContent.begin(), stepsContent.end(), stepObjectPattern);
        const auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it)
        {
            WorkflowStep step{};
            const std::string stepBody = (*it)[1].str();
            const auto action = extract_string_value(stepBody, "action");
            if (!action.has_value() || action->empty())
            {
                errorMessage = "invalid workflow step: action is required";
                return std::nullopt;
            }

            step.action = *action;
            step.profile = extract_string_value(stepBody, "profile").value_or("");
            step.contains = extract_string_value(stepBody, "contains").value_or("");
            step.text = extract_string_value(stepBody, "text").value_or("");
            step.timeoutMs = extract_int_value(stepBody, "timeoutMs").value_or(-1);
            step.durationMs = extract_int_value(stepBody, "durationMs").value_or(-1);
            workflow.steps.push_back(step);
        }

        if (workflow.steps.empty())
        {
            errorMessage = "invalid workflow: steps must not be empty";
            return std::nullopt;
        }

        return workflow;
    }

    bool execute_workflow(
        const WorkflowDefinition& workflow,
        AutoConsole::Core::CoreRuntime& runtime,
        std::string& currentSessionId,
        ConsoleOutput& console,
        std::string& errorMessage)
    {
        for (std::size_t i = 0; i < workflow.steps.size(); ++i)
        {
            const auto& step = workflow.steps[i];
            console.print_line(
                "workflow step " + std::to_string(i + 1) + "/" + std::to_string(workflow.steps.size()) + ": " + step.action);

            if (step.action == "start")
            {
                if (step.profile.empty())
                {
                    errorMessage = "workflow start requires profile";
                    return false;
                }

                const std::string profilePath = resolve_profile_path(step.profile);
                std::string loadError;
                const auto profile = AutoConsole::Core::ProfileLoader::load_from_file(profilePath, loadError);
                if (!profile.has_value())
                {
                    errorMessage = "workflow start failed to load profile: " + loadError;
                    return false;
                }

                const auto startResult = runtime.start_session(*profile);
                if (!startResult.started)
                {
                    errorMessage = "workflow start failed: " + startResult.errorMessage;
                    return false;
                }

                currentSessionId = startResult.session.id;
                continue;
            }

            std::string sessionId;
            std::string resolveError;
            if (!resolve_session_id(runtime, "", currentSessionId, sessionId, resolveError))
            {
                errorMessage = resolveError;
                return false;
            }

            if (step.action == "wait_output")
            {
                if (step.contains.empty() || step.timeoutMs < 0)
                {
                    errorMessage = "workflow wait_output requires contains and timeoutMs >= 0";
                    return false;
                }

                if (!AutoConsole::StandardPlugins::StandardPluginActions::wait_output(
                    runtime.plugin_context(),
                    sessionId,
                    step.contains,
                    step.timeoutMs,
                    errorMessage))
                {
                    return false;
                }

                continue;
            }

            if (step.action == "send_input")
            {
                if (step.text.empty())
                {
                    errorMessage = "workflow send_input requires text";
                    return false;
                }

                if (!AutoConsole::StandardPlugins::StandardPluginActions::send_input(
                    runtime.plugin_context(),
                    sessionId,
                    step.text,
                    errorMessage))
                {
                    return false;
                }

                continue;
            }

            if (step.action == "delay")
            {
                if (step.durationMs < 0)
                {
                    errorMessage = "workflow delay requires durationMs >= 0";
                    return false;
                }

                if (!AutoConsole::StandardPlugins::StandardPluginActions::delay(
                    runtime.plugin_context(),
                    step.durationMs,
                    errorMessage))
                {
                    return false;
                }

                continue;
            }

            if (step.action == "stop")
            {
                if (!AutoConsole::StandardPlugins::StandardPluginActions::stop_process(
                    runtime.plugin_context(),
                    sessionId,
                    errorMessage))
                {
                    return false;
                }

                continue;
            }

            errorMessage = "unknown workflow action: " + step.action;
            return false;
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
            std::cout << "Available commands: help, ping, start <profile>, sessions, stop <sessionId>, plugins, plugin info <pluginId>, exit\n";
            std::cout << "CLI options: --api, --port <n>\n";
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
                console->print_line("error: usage: start <profile>");
                continue;
            }

            const std::string profilePath = resolve_profile_path(profileFile);
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

        if (command == "run")
        {
            const std::string workflowFile = rest_after_first_token(iss);
            if (workflowFile.empty())
            {
                console->print_line("error: usage: run <workflow>");
                continue;
            }

            const std::string workflowPath = resolve_profile_path(workflowFile);
            std::string loadError;
            const auto workflow = load_workflow(workflowPath, loadError);
            if (!workflow.has_value())
            {
                console->print_line("error: failed to load workflow: " + loadError);
                continue;
            }

            if (!workflow->id.empty())
            {
                console->print_line("workflow loaded: " + workflow->id);
            }
            else
            {
                console->print_line("workflow loaded");
            }

            std::string runError;
            if (execute_workflow(*workflow, runtime, currentSessionId, *console, runError))
            {
                console->print_line("workflow completed");
            }
            else
            {
                console->print_line("error: workflow failed: " + runError);
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

        if (command == "plugins")
        {
            const auto plugins = runtime.plugins();
            if (plugins.empty())
            {
                console->print_line("no plugins");
                continue;
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
            if (action.empty())
            {
                console->print_line("error: usage: plugin <action> ...");
                continue;
            }

            if (action == "info")
            {
                const std::string pluginId = next_token(iss);
                if (pluginId.empty())
                {
                    console->print_line("error: usage: plugin info <pluginId>");
                    continue;
                }

                const auto plugin = runtime.plugin_info(pluginId);
                if (!plugin.has_value())
                {
                    console->print_line("error: plugin not found: " + pluginId);
                    continue;
                }

                const auto& metadata = plugin->metadata;
                console->print_line("id: " + metadata.id);
                console->print_line("displayName: " + metadata.displayName);
                console->print_line("version: " + metadata.version);
                console->print_line("apiVersion: " + metadata.apiVersion);
                console->print_line("source: " + plugin->source);
                if (!metadata.author.empty())
                {
                    console->print_line("author: " + metadata.author);
                }
                if (!metadata.description.empty())
                {
                    console->print_line("description: " + metadata.description);
                }
                if (!plugin->location.empty())
                {
                    console->print_line("location: " + plugin->location);
                }

                if (!metadata.capabilities.empty())
                {
                    std::string caps = "capabilities:";
                    for (const auto& capability : metadata.capabilities)
                    {
                        caps += " " + capability;
                    }
                    console->print_line(caps);
                }
                continue;
            }

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
                    console->print_line("error: timeoutMs must be an integer");
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
                    console->print_line("error: durationMs must be an integer");
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

            if (action == "timer")
            {
                const std::string durationText = next_token(iss);
                if (durationText.empty())
                {
                    console->print_line("error: usage: plugin timer <durationMs>");
                    continue;
                }

                int durationMs = 0;
                try
                {
                    durationMs = std::stoi(durationText);
                }
                catch (...)
                {
                    console->print_line("error: durationMs must be an integer");
                    continue;
                }

                std::string errorMessage;
                if (AutoConsole::StandardPlugins::StandardPluginActions::timer(runtime.plugin_context(), durationMs, errorMessage))
                {
                    console->print_line("plugin timer success");
                }
                else
                {
                    console->print_line("error: plugin timer failed: " + errorMessage);
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
                    if (errorMessage.empty())
                    {
                        console->print_line("plugin call_plugin success");
                    }
                    else
                    {
                        console->print_line("plugin call_plugin success: " + errorMessage);
                    }
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

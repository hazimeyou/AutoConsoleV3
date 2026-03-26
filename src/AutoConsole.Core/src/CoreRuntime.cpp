#include "AutoConsole/Core/CoreRuntime.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <windows.h>

#include "AutoConsole/Abstractions/PluginApiVersion.h"

namespace
{
    using CreatePluginFn = AutoConsole::Abstractions::IPlugin * (*)();

    std::string now_timestamp_utc()
    {
        const auto now = std::chrono::system_clock::now();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::to_string(millis);
    }

    std::string state_to_string(AutoConsole::Abstractions::SessionState state)
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

    std::vector<std::string> split_tab_line(const std::string& line)
    {
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string token;
        while (std::getline(ss, token, '\t'))
        {
            parts.push_back(token);
        }
        return parts;
    }

    std::vector<std::string> split_caps(const std::string& caps)
    {
        std::vector<std::string> parts;
        std::stringstream ss(caps);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            if (!token.empty())
            {
                parts.push_back(token);
            }
        }
        return parts;
    }

    std::optional<AutoConsole::Core::LoadedPluginInfo> parse_bridge_plugin_line(const std::string& line)
    {
        if (line.empty())
        {
            return std::nullopt;
        }

        const auto parts = split_tab_line(line);
        if (parts.size() < 7)
        {
            return std::nullopt;
        }

        AutoConsole::Abstractions::PluginMetadata metadata{};
        metadata.id = parts[0];
        metadata.name = parts[1];
        metadata.displayName = parts[2];
        metadata.version = parts[3];
        metadata.apiVersion = parts[4];
        metadata.author = parts[5];
        metadata.description = parts[6];
        if (parts.size() >= 8)
        {
            metadata.capabilities = split_caps(parts[7]);
        }

        if (metadata.id.empty())
        {
            return std::nullopt;
        }

        return AutoConsole::Core::LoadedPluginInfo{
            metadata,
            "bridge:csharp",
            "via cs.bridge"
        };
    }
}

namespace AutoConsole::Core
{
    CoreRuntime::CoreRuntime()
        : pluginContext_(
            eventDispatcher_,
            [this](const std::string& sessionId, const std::string& text, std::string& errorMessage)
            {
                return send_input(sessionId, text, errorMessage);
            },
            [this](const std::string& sessionId, std::string& errorMessage)
            {
                return stop_session(sessionId, errorMessage);
            },
            [this](const std::string& sessionId, const std::string& contains, int timeoutMs, std::string& errorMessage)
            {
                return wait_output(sessionId, contains, timeoutMs, errorMessage);
            },
            [this](
                const std::string& pluginId,
                const std::string& action,
                const std::unordered_map<std::string, std::string>& actionArgs,
                std::string& errorMessage)
            {
                return call_plugin_action(pluginId, action, actionArgs, errorMessage);
            },
            [this](const std::string& level, const std::string& message)
            {
                std::function<void(const std::string&, const std::string&)> sink;
                {
                    std::lock_guard<std::mutex> lock(logSinkMutex_);
                    sink = internalLogSink_;
                }

                if (sink)
                {
                    sink(level, message);
                }
            })
    {
        eventDispatcher_.subscribe([this](const AutoConsole::Abstractions::Event& eventValue)
        {
            pluginHost_.dispatch_event(eventValue, pluginContext_);
        });
    }

    void CoreRuntime::register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin)
    {
        std::string errorMessage;
        if (!pluginHost_.register_plugin(std::move(plugin), "standard", "", errorMessage))
        {
            std::function<void(const std::string&, const std::string&)> sink;
            {
                std::lock_guard<std::mutex> lock(logSinkMutex_);
                sink = internalLogSink_;
            }

            if (sink)
            {
                sink("error", "failed to register plugin: " + errorMessage);
            }
        }
    }

    bool CoreRuntime::load_external_plugins(const std::string& directoryPath)
    {
        namespace fs = std::filesystem;

        std::function<void(const std::string&, const std::string&)> sink;
        {
            std::lock_guard<std::mutex> lock(logSinkMutex_);
            sink = internalLogSink_;
        }

        std::error_code ec;
        if (!fs::exists(directoryPath, ec))
        {
            if (sink)
            {
                sink("info", "external plugin directory not found: " + directoryPath);
            }
            return true;
        }

        if (!fs::is_directory(directoryPath, ec))
        {
            if (sink)
            {
                sink("error", "external plugin path is not a directory: " + directoryPath);
            }
            return false;
        }

        std::size_t dllCount = 0;
        std::size_t loadedCount = 0;
        std::size_t failedCount = 0;
        if (sink)
        {
            sink("info", "external plugin scan start: " + directoryPath);
        }

        for (const auto& entry : fs::directory_iterator(directoryPath, ec))
        {
            if (ec)
            {
                if (sink)
                {
                    sink("error", "failed to enumerate plugin directory: " + ec.message());
                }
                return false;
            }

            if (!entry.is_regular_file())
            {
                continue;
            }

            const auto extension = entry.path().extension().string();
            std::string extLower = extension;
            for (char& ch : extLower)
            {
                ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
            }
            if (extLower != ".dll")
            {
                continue;
            }

            const std::string dllPath = entry.path().string();
            ++dllCount;
            if (sink)
            {
                sink("info", "loading external plugin DLL: " + dllPath);
            }
            HMODULE module = ::LoadLibraryA(dllPath.c_str());
            if (!module)
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "failed to load external plugin DLL: " + dllPath);
                }
                continue;
            }

            const auto createPlugin = reinterpret_cast<CreatePluginFn>(::GetProcAddress(module, "create_plugin"));
            if (!createPlugin)
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "missing create_plugin export: " + dllPath);
                }
                ::FreeLibrary(module);
                continue;
            }

            AutoConsole::Abstractions::IPlugin* raw = nullptr;
            try
            {
                raw = createPlugin();
            }
            catch (...)
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "create_plugin threw an exception: " + dllPath);
                }
                ::FreeLibrary(module);
                continue;
            }

            if (!raw)
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "create_plugin returned null: " + dllPath);
                }
                ::FreeLibrary(module);
                continue;
            }

            auto plugin = std::shared_ptr<AutoConsole::Abstractions::IPlugin>(
                raw,
                [module](AutoConsole::Abstractions::IPlugin* ptr)
                {
                    delete ptr;
                    ::FreeLibrary(module);
                });

            AutoConsole::Abstractions::PluginMetadata metadata{};
            try
            {
                metadata = plugin->metadata();
            }
            catch (const std::exception& ex)
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "plugin metadata() threw: " + std::string(ex.what()) + " (" + dllPath + ")");
                }
                continue;
            }
            catch (...)
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "plugin metadata() threw unknown exception: " + dllPath);
                }
                continue;
            }

            if (metadata.id.empty())
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "invalid metadata.id for: " + dllPath);
                }
                continue;
            }

            if (metadata.displayName.empty())
            {
                metadata.displayName = metadata.name;
            }

            if (metadata.name.empty())
            {
                metadata.name = metadata.displayName;
            }

            if (metadata.displayName.empty())
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "invalid metadata.displayName for plugin id: " + metadata.id);
                }
                continue;
            }

            if (metadata.version.empty())
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "invalid metadata.version for plugin id: " + metadata.id);
                }
                continue;
            }

            if (metadata.apiVersion != AutoConsole::Abstractions::kPluginApiVersion)
            {
                ++failedCount;
                if (sink)
                {
                    sink(
                        "error",
                        "incompatible metadata.apiVersion for plugin id: " + metadata.id + " (got " + metadata.apiVersion +
                        ", expected " + AutoConsole::Abstractions::kPluginApiVersion + ")");
                }
                continue;
            }

            std::string registerError;
            if (!pluginHost_.register_plugin(std::move(plugin), "external", dllPath, registerError))
            {
                ++failedCount;
                if (sink)
                {
                    sink("error", "failed to register external plugin from " + dllPath + ": " + registerError);
                }
                continue;
            }

            ++loadedCount;
            if (sink)
            {
                sink("info", "loaded external plugin: " + metadata.id);
            }
        }

        if (sink)
        {
            sink(
                "info",
                "external plugin scan complete: found=" + std::to_string(dllCount) +
                " loaded=" + std::to_string(loadedCount) +
                " failed=" + std::to_string(failedCount));
        }

        return true;
    }

    std::vector<LoadedPluginInfo> CoreRuntime::plugins()
    {
        auto list = pluginHost_.list_plugins();
        std::unordered_set<std::string> existingIds;
        existingIds.reserve(list.size());
        for (const auto& item : list)
        {
            existingIds.insert(item.metadata.id);
        }

        for (const auto& bridgePlugin : list_bridge_virtual_plugins())
        {
            if (existingIds.find(bridgePlugin.metadata.id) != existingIds.end())
            {
                continue;
            }

            list.push_back(bridgePlugin);
            existingIds.insert(bridgePlugin.metadata.id);
        }

        return list;
    }

    std::optional<LoadedPluginInfo> CoreRuntime::plugin_info(const std::string& pluginId)
    {
        auto direct = pluginHost_.find_plugin(pluginId);
        if (direct.has_value())
        {
            return direct;
        }

        return find_bridge_virtual_plugin(pluginId);
    }

    void CoreRuntime::set_internal_log_sink(std::function<void(const std::string&, const std::string&)> sink)
    {
        std::lock_guard<std::mutex> lock(logSinkMutex_);
        internalLogSink_ = std::move(sink);
    }

    void CoreRuntime::subscribe_events(EventDispatcher::Handler handler)
    {
        eventDispatcher_.subscribe(handler);
    }

    void CoreRuntime::publish_event(const AutoConsole::Abstractions::Event& eventValue)
    {
        eventDispatcher_.publish(eventValue);
    }

    AutoConsole::Abstractions::PluginContext& CoreRuntime::plugin_context()
    {
        return pluginContext_;
    }

    StartSessionResult CoreRuntime::start_session(const AutoConsole::Abstractions::Profile& profile)
    {
        processRunner_.cleanup_finished_sessions();

        StartSessionResult result{};
        result.session = sessionManager_.create_session(profile);

        sessionManager_.set_state(result.session.id, AutoConsole::Abstractions::SessionState::Starting);
        result.session.state = AutoConsole::Abstractions::SessionState::Starting;

        std::string errorMessage;
        const bool started = processRunner_.start(
            result.session.id,
            profile,
            [this](const std::string& sessionId, const std::string& text)
            {
                store_output_record(sessionId, text);

                AutoConsole::Abstractions::Event eventValue{};
                eventValue.type = "stdout_line";
                eventValue.sessionId = sessionId;
                eventValue.data["text"] = text;
                eventValue.data["timestamp"] = now_timestamp_utc();
                eventDispatcher_.publish(eventValue);
            },
            [this](const std::string& sessionId, const std::string& text)
            {
                store_output_record(sessionId, text);

                AutoConsole::Abstractions::Event eventValue{};
                eventValue.type = "stderr_line";
                eventValue.sessionId = sessionId;
                eventValue.data["text"] = text;
                eventValue.data["timestamp"] = now_timestamp_utc();
                eventDispatcher_.publish(eventValue);
            },
            [this](const std::string& sessionId, int exitCode)
            {
                sessionManager_.set_state(sessionId, AutoConsole::Abstractions::SessionState::Stopped);
                sessionManager_.set_exit_code(sessionId, exitCode);

                AutoConsole::Abstractions::Event exitEvent{};
                exitEvent.type = "process_exited";
                exitEvent.sessionId = sessionId;
                exitEvent.data["exitCode"] = std::to_string(exitCode);
                exitEvent.data["timestamp"] = now_timestamp_utc();
                eventDispatcher_.publish(exitEvent);
            },
            errorMessage);

        if (!started)
        {
            sessionManager_.set_state(result.session.id, AutoConsole::Abstractions::SessionState::Failed);
            result.session.state = AutoConsole::Abstractions::SessionState::Failed;
            result.started = false;
            result.errorMessage = errorMessage;
            return result;
        }

        sessionManager_.set_state(result.session.id, AutoConsole::Abstractions::SessionState::Running);
        result.session.state = AutoConsole::Abstractions::SessionState::Running;
        result.started = true;

        AutoConsole::Abstractions::Event startedEvent{};
        startedEvent.type = "process_started";
        startedEvent.sessionId = result.session.id;
        startedEvent.data["timestamp"] = now_timestamp_utc();
        eventDispatcher_.publish(startedEvent);

        return result;
    }

    bool CoreRuntime::send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage)
    {
        processRunner_.cleanup_finished_sessions();

        const auto session = sessionManager_.get_session(sessionId);
        if (!session.has_value())
        {
            errorMessage = "invalid sessionId: " + sessionId;
            return false;
        }

        if (session->state != AutoConsole::Abstractions::SessionState::Running)
        {
            errorMessage = "session is not running (state=" + state_to_string(session->state) + ")";
            return false;
        }

        return processRunner_.write_input(sessionId, text, true, errorMessage);
    }

    bool CoreRuntime::stop_session(const std::string& sessionId, std::string& errorMessage)
    {
        processRunner_.cleanup_finished_sessions();

        const auto session = sessionManager_.get_session(sessionId);
        if (!session.has_value())
        {
            errorMessage = "invalid sessionId: " + sessionId;
            return false;
        }

        if (session->state != AutoConsole::Abstractions::SessionState::Running &&
            session->state != AutoConsole::Abstractions::SessionState::Starting)
        {
            errorMessage = "session is not running (state=" + state_to_string(session->state) + ")";
            return false;
        }

        const bool stopped = processRunner_.stop(sessionId);
        if (!stopped)
        {
            errorMessage = "failed to stop process";
            return false;
        }

        sessionManager_.set_state(sessionId, AutoConsole::Abstractions::SessionState::Stopped);
        return true;
    }

    bool CoreRuntime::wait_output(const std::string& sessionId, const std::string& contains, int timeoutMs, std::string& errorMessage)
    {
        processRunner_.cleanup_finished_sessions();

        const auto session = sessionManager_.get_session(sessionId);
        if (!session.has_value())
        {
            errorMessage = "invalid sessionId: " + sessionId;
            return false;
        }

        if (contains.empty())
        {
            errorMessage = "contains must not be empty";
            return false;
        }

        if (timeoutMs < 0)
        {
            errorMessage = "timeoutMs must be >= 0";
            return false;
        }

        std::unique_lock<std::mutex> lock(outputMutex_);

        std::uint64_t lastSeen = 0;
        for (const auto& record : outputRecords_)
        {
            if (record.sessionId == sessionId && record.text.find(contains) != std::string::npos)
            {
                return true;
            }

            lastSeen = record.sequence;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true)
        {
            const bool signaled = outputCv_.wait_until(lock, deadline, [this, lastSeen]()
            {
                return !outputRecords_.empty() && outputRecords_.back().sequence > lastSeen;
            });

            if (!signaled)
            {
                errorMessage = "wait_output timed out (sessionId=" + sessionId + ", contains=\"" + contains + "\", timeoutMs=" + std::to_string(timeoutMs) + ")";
                return false;
            }

            for (const auto& record : outputRecords_)
            {
                if (record.sequence <= lastSeen)
                {
                    continue;
                }

                if (record.sessionId == sessionId && record.text.find(contains) != std::string::npos)
                {
                    return true;
                }
            }

            lastSeen = outputRecords_.back().sequence;
        }
    }

    void CoreRuntime::register_plugin_action_handler(const std::string& pluginId, const std::string& action, PluginActionHandler handler)
    {
        const std::string key = pluginId + "::" + action;
        std::lock_guard<std::mutex> lock(pluginActionMutex_);
        pluginActionHandlers_[key] = std::move(handler);
    }

    bool CoreRuntime::call_plugin_action(
        const std::string& pluginId,
        const std::string& action,
        const PluginActionArgs& actionArgs,
        std::string& errorMessage)
    {
        PluginActionHandler handler{};
        {
            const std::string key = pluginId + "::" + action;
            std::lock_guard<std::mutex> lock(pluginActionMutex_);
            const auto it = pluginActionHandlers_.find(key);
            if (it != pluginActionHandlers_.end())
            {
                handler = it->second;
            }
        }

        if (handler)
        {
            return handler(pluginContext_, actionArgs, errorMessage);
        }

        return pluginHost_.execute_plugin_action(pluginId, action, actionArgs, pluginContext_, errorMessage);
    }

    std::vector<AutoConsole::Abstractions::SessionInfo> CoreRuntime::sessions()
    {
        processRunner_.cleanup_finished_sessions();
        return sessionManager_.list_sessions();
    }

    void CoreRuntime::store_output_record(const std::string& sessionId, const std::string& text)
    {
        {
            std::lock_guard<std::mutex> lock(outputMutex_);
            outputRecords_.push_back(OutputRecord{ ++outputSequence_, sessionId, text });
            constexpr std::size_t MaxRecords = 2048;
            if (outputRecords_.size() > MaxRecords)
            {
                outputRecords_.pop_front();
            }
        }

        outputCv_.notify_all();
    }

    std::vector<LoadedPluginInfo> CoreRuntime::list_bridge_virtual_plugins()
    {
        std::vector<LoadedPluginInfo> result;
        std::string payload;
        std::unordered_map<std::string, std::string> args;
        if (!pluginHost_.execute_plugin_action("cs.bridge", "__bridge_list_plugins", args, pluginContext_, payload))
        {
            return result;
        }

        std::stringstream stream(payload);
        std::string line;
        while (std::getline(stream, line))
        {
            auto parsed = parse_bridge_plugin_line(line);
            if (parsed.has_value())
            {
                result.push_back(*parsed);
            }
        }

        return result;
    }

    std::optional<LoadedPluginInfo> CoreRuntime::find_bridge_virtual_plugin(const std::string& pluginId)
    {
        std::unordered_map<std::string, std::string> args;
        args.emplace("pluginId", pluginId);

        std::string payload;
        if (!pluginHost_.execute_plugin_action("cs.bridge", "__bridge_get_plugin_info", args, pluginContext_, payload))
        {
            return std::nullopt;
        }

        std::stringstream stream(payload);
        std::string line;
        while (std::getline(stream, line))
        {
            auto parsed = parse_bridge_plugin_line(line);
            if (parsed.has_value() && parsed->metadata.id == pluginId)
            {
                return parsed;
            }
        }

        return std::nullopt;
    }

}

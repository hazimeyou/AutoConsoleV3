#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Abstractions/Profile.h"
#include "AutoConsole/Abstractions/SessionInfo.h"
#include "AutoConsole/Core/DummyPluginContext.h"
#include "AutoConsole/Core/EventDispatcher.h"
#include "AutoConsole/Core/PluginHost.h"
#include "AutoConsole/Core/ProcessRunner.h"
#include "AutoConsole/Core/SessionManager.h"

namespace AutoConsole::Core
{
    struct StartSessionResult
    {
        AutoConsole::Abstractions::SessionInfo session;
        bool started = false;
        std::string errorMessage;
    };

    class CoreRuntime
    {
    public:
        using PluginActionArgs = std::unordered_map<std::string, std::string>;
        using PluginActionHandler = std::function<bool(AutoConsole::Abstractions::PluginContext&, const PluginActionArgs&, std::string&)>;

        CoreRuntime();

        void register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin);
        bool load_external_plugins(const std::string& directoryPath);
        std::vector<LoadedPluginInfo> plugins();
        std::optional<LoadedPluginInfo> plugin_info(const std::string& pluginId);
        void set_internal_log_sink(std::function<void(const std::string&, const std::string&)> sink);
        void subscribe_events(EventDispatcher::Handler handler);
        void publish_event(const AutoConsole::Abstractions::Event& eventValue);
        AutoConsole::Abstractions::PluginContext& plugin_context();
        StartSessionResult start_session(const AutoConsole::Abstractions::Profile& profile);
        bool send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage);
        bool stop_session(const std::string& sessionId, std::string& errorMessage);
        bool wait_output(const std::string& sessionId, const std::string& contains, int timeoutMs, std::string& errorMessage);
        void register_plugin_action_handler(const std::string& pluginId, const std::string& action, PluginActionHandler handler);
        bool call_plugin_action(
            const std::string& pluginId,
            const std::string& action,
            const PluginActionArgs& actionArgs,
            std::string& errorMessage);
        std::vector<AutoConsole::Abstractions::SessionInfo> sessions();

    private:
        std::vector<LoadedPluginInfo> list_bridge_virtual_plugins();
        std::optional<LoadedPluginInfo> find_bridge_virtual_plugin(const std::string& pluginId);

        struct OutputRecord
        {
            std::uint64_t sequence = 0;
            std::string sessionId;
            std::string text;
        };

        void store_output_record(const std::string& sessionId, const std::string& text);

        ProcessRunner processRunner_;
        SessionManager sessionManager_;
        EventDispatcher eventDispatcher_;
        PluginHost pluginHost_;
        DummyPluginContext pluginContext_;
        mutable std::mutex outputMutex_;
        std::condition_variable outputCv_;
        std::deque<OutputRecord> outputRecords_;
        std::uint64_t outputSequence_ = 0;
        std::mutex logSinkMutex_;
        std::function<void(const std::string&, const std::string&)> internalLogSink_;
        std::mutex pluginActionMutex_;
        std::unordered_map<std::string, PluginActionHandler> pluginActionHandlers_;
    };
}

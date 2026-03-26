#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/PluginActionResult.h"
#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Abstractions/Profile.h"
#include "AutoConsole/Abstractions/SessionInfo.h"
#include "AutoConsole/Core/DummyPluginContext.h"
#include "AutoConsole/Core/EventDispatcher.h"
#include "AutoConsole/Core/ExternalPluginLoader.h"
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
        CoreRuntime();

        bool register_plugin(
            std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin,
            PluginSource source,
            std::string& errorMessage);
        bool register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin, PluginSource source = PluginSource::Standard);
        void subscribe_events(EventDispatcher::Handler handler);
        void publish_event(const AutoConsole::Abstractions::Event& eventValue);
        AutoConsole::Abstractions::PluginContext& plugin_context();
        StartSessionResult start_session(const AutoConsole::Abstractions::Profile& profile);
        bool stop_session(const std::string& sessionId);
        bool send_input(const std::string& sessionId, const std::string& text, std::string& errorMessage);
        std::vector<AutoConsole::Abstractions::SessionInfo> sessions() const;
        std::vector<PluginInfo> plugins() const;
        std::vector<std::string> load_external_plugins(const std::string& directory);
        bool execute_plugin_action(
            const std::string& pluginId,
            const std::string& action,
            const std::unordered_map<std::string, std::string>& args,
            AutoConsole::Abstractions::PluginActionResult& result,
            std::string& errorMessage);

    private:
        SessionManager sessionManager_;
        EventDispatcher eventDispatcher_;
        PluginHost pluginHost_;
        ProcessRunner processRunner_;
        DummyPluginContext pluginContext_;
    };
}

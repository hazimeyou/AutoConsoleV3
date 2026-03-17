#pragma once

#include <memory>
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
        CoreRuntime();

        void register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin);
        void subscribe_events(EventDispatcher::Handler handler);
        void publish_event(const AutoConsole::Abstractions::Event& eventValue);
        AutoConsole::Abstractions::PluginContext& plugin_context();
        StartSessionResult start_session(const AutoConsole::Abstractions::Profile& profile);
        bool stop_session(const std::string& sessionId);
        std::vector<AutoConsole::Abstractions::SessionInfo> sessions() const;

    private:
        ProcessRunner processRunner_;
        SessionManager sessionManager_;
        EventDispatcher eventDispatcher_;
        PluginHost pluginHost_;
        DummyPluginContext pluginContext_;
    };
}

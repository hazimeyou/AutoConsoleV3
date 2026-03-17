#pragma once

#include <memory>

#include "AutoConsole/Abstractions/Event.h"
#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Core/DummyPluginContext.h"
#include "AutoConsole/Core/EventDispatcher.h"
#include "AutoConsole/Core/PluginHost.h"
#include "AutoConsole/Core/ProcessRunner.h"
#include "AutoConsole/Core/SessionManager.h"

namespace AutoConsole::Core
{
    class CoreRuntime
    {
    public:
        CoreRuntime();

        void register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin);
        void publish_event(const AutoConsole::Abstractions::Event& eventValue);
        AutoConsole::Abstractions::PluginContext& plugin_context();

    private:
        ProcessRunner processRunner_;
        SessionManager sessionManager_;
        EventDispatcher eventDispatcher_;
        PluginHost pluginHost_;
        DummyPluginContext pluginContext_;
    };
}

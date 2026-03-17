#include "AutoConsole/Core/CoreRuntime.h"

namespace AutoConsole::Core
{
    CoreRuntime::CoreRuntime()
        : pluginContext_(eventDispatcher_)
    {
        eventDispatcher_.subscribe([this](const AutoConsole::Abstractions::Event& eventValue)
        {
            pluginHost_.dispatch_event(eventValue, pluginContext_);
        });
    }

    void CoreRuntime::register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin)
    {
        pluginHost_.register_plugin(plugin);
    }

    void CoreRuntime::publish_event(const AutoConsole::Abstractions::Event& eventValue)
    {
        eventDispatcher_.publish(eventValue);
    }

    AutoConsole::Abstractions::PluginContext& CoreRuntime::plugin_context()
    {
        return pluginContext_;
    }
}

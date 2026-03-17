#include "AutoConsole/Core/PluginHost.h"

namespace AutoConsole::Core
{
    void PluginHost::register_plugin(std::shared_ptr<AutoConsole::Abstractions::IPlugin> plugin)
    {
        plugins_.push_back(plugin);
    }

    void PluginHost::dispatch_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) const
    {
        for (const auto& plugin : plugins_)
        {
            plugin->on_event(eventValue, context);
        }
    }
}

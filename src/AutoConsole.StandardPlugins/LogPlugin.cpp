#include "LogPlugin.h"

namespace AutoConsole::StandardPlugins
{
    AutoConsole::Abstractions::PluginMetadata LogPlugin::metadata() const
    {
        return AutoConsole::Abstractions::PluginMetadata{
            "std.log",
            "LogPlugin",
            "0.1.0"
        };
    }

    void LogPlugin::on_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context)
    {
        context.log("info", "LogPlugin received event: " + eventValue.type);
    }
}

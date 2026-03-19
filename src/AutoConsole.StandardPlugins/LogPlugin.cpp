#include "LogPlugin.h"

namespace AutoConsole::StandardPlugins
{
    AutoConsole::Abstractions::PluginMetadata LogPlugin::metadata() const
    {
        AutoConsole::Abstractions::PluginMetadata metadata{};
        metadata.id = "std.log";
        metadata.name = "LogPlugin";
        metadata.displayName = "Log Plugin";
        metadata.version = "0.1.0";
        metadata.author = "AutoConsole";
        metadata.description = "Built-in event logger plugin";
        metadata.capabilities = { "event_listener" };
        return metadata;
    }

    void LogPlugin::on_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context)
    {
        context.log("info", "LogPlugin received event: " + eventValue.type);
    }
}

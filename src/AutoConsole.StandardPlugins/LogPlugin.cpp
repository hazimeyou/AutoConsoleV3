#include "LogPlugin.h"

#include "AutoConsole/Abstractions/ExternalPluginApi.h"

namespace AutoConsole::StandardPlugins
{
    AutoConsole::Abstractions::PluginMetadata LogPlugin::metadata() const
    {
<<<<<<< HEAD
        return AutoConsole::Abstractions::PluginMetadata{
            "std.log",
            "LogPlugin",
            "0.1.0",
            AutoConsole::Abstractions::PluginApiVersion,
            "AutoConsole",
            "Writes a log entry when events are received.",
            { "log" }
        };
=======
        AutoConsole::Abstractions::PluginMetadata metadata{};
        metadata.id = "std.log";
        metadata.name = "LogPlugin";
        metadata.displayName = "Log Plugin";
        metadata.version = "0.1.0";
        metadata.author = "AutoConsole";
        metadata.description = "Built-in event logger plugin";
        metadata.capabilities = { "event_listener" };
        return metadata;
>>>>>>> 3464747bd75e315a5b6ccc25c6e3a2ae3a41e419
    }

    void LogPlugin::on_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context)
    {
        context.log("info", "LogPlugin received event: " + eventValue.type);
    }
}

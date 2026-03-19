#include "pch.h"

#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Abstractions/PluginApiVersion.h"

namespace
{
    class EchoExternalPlugin final : public AutoConsole::Abstractions::IPlugin
    {
    public:
        AutoConsole::Abstractions::PluginMetadata metadata() const override
        {
            AutoConsole::Abstractions::PluginMetadata metadata{};
            metadata.id = "sample.echo";
            metadata.name = "EchoExternalPlugin";
            metadata.displayName = "Echo External Plugin";
            metadata.version = "0.1.0";
            metadata.apiVersion = AutoConsole::Abstractions::kPluginApiVersion;
            metadata.author = "Sample Author";
            metadata.description = "Simple test external plugin";
            metadata.capabilities = { "command_executor" };
            return metadata;
        }

        void on_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) override
        {
            (void)eventValue;
            (void)context;
        }
    };
}

extern "C" __declspec(dllexport) AutoConsole::Abstractions::IPlugin* create_plugin()
{
    return new EchoExternalPlugin();
}

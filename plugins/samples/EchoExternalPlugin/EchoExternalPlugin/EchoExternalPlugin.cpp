#include "pch.h"

#include "AutoConsole/Abstractions/IPlugin.h"

namespace
{
    class EchoExternalPlugin final : public AutoConsole::Abstractions::IPlugin
    {
    public:
        AutoConsole::Abstractions::PluginMetadata metadata() const override
        {
            return AutoConsole::Abstractions::PluginMetadata{
                "sample.echo",
                "Echo External Plugin",
                "0.1.0"
            };
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

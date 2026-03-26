#include <string>
#include <unordered_map>

#include "AutoConsole/Abstractions/ExternalPluginApi.h"
#include "AutoConsole/Abstractions/IActionPlugin.h"

namespace AutoConsole::ExternalPlugins
{
    class EchoExternalPlugin final : public AutoConsole::Abstractions::IPlugin,
                                     public AutoConsole::Abstractions::IActionPlugin
    {
    public:
        AutoConsole::Abstractions::PluginMetadata metadata() const override
        {
            return AutoConsole::Abstractions::PluginMetadata{
                "ext.echo",
                "EchoExternalPlugin",
                "0.1.0",
                AutoConsole::Abstractions::PluginApiVersion,
                "AutoConsole",
                "Echoes text back to the caller.",
                { "echo" }
            };
        }

        void on_event(const AutoConsole::Abstractions::Event& eventValue, AutoConsole::Abstractions::PluginContext& context) override
        {
            (void)eventValue;
            (void)context;
        }

        AutoConsole::Abstractions::PluginActionResult execute_action(
            const std::string& action,
            const std::unordered_map<std::string, std::string>& args,
            AutoConsole::Abstractions::PluginContext& context) override
        {
            (void)context;

            AutoConsole::Abstractions::PluginActionResult result{};
            if (action != "echo")
            {
                result.success = false;
                result.message = "unknown action";
                return result;
            }

            std::string text;
            const auto it = args.find("text");
            if (it != args.end())
            {
                text = it->second;
            }

            result.success = true;
            result.message = "echo: " + text;
            result.data["text"] = text;
            return result;
        }
    };
}

extern "C" __declspec(dllexport) AutoConsole::Abstractions::IPlugin* CreatePlugin()
{
    return new AutoConsole::ExternalPlugins::EchoExternalPlugin();
}

extern "C" __declspec(dllexport) void DestroyPlugin(AutoConsole::Abstractions::IPlugin* plugin)
{
    delete plugin;
}

extern "C" __declspec(dllexport) int GetPluginApiVersion()
{
    return AutoConsole::Abstractions::PluginApiVersion;
}

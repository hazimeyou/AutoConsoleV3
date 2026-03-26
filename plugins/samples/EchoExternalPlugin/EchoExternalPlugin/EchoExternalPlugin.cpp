#include "pch.h"

#include "AutoConsole/Abstractions/ExternalPluginApi.h"
#include "AutoConsole/Abstractions/IPlugin.h"
#include "AutoConsole/Abstractions/IPluginActionExecutor.h"
#include "AutoConsole/Abstractions/PluginApiVersion.h"

namespace
{
    class EchoExternalPlugin final
        : public AutoConsole::Abstractions::IPlugin
        , public AutoConsole::Abstractions::IPluginActionExecutor
    {
    public:
        AutoConsole::Abstractions::PluginMetadata metadata() const override
        {
            AutoConsole::Abstractions::PluginMetadata metadata{};
            metadata.id = "ext.echo";
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
            if (eventValue.type == "process_started" ||
                eventValue.type == "process_exited" ||
                eventValue.type == "stdout_line" ||
                eventValue.type == "stderr_line" ||
                eventValue.type == "timer" ||
                eventValue.type == "manual_trigger")
            {
                context.log("debug", "EchoExternalPlugin received event: " + eventValue.type);
            }
        }

        bool execute_action(
            const std::string& action,
            const std::unordered_map<std::string, std::string>& actionArgs,
            AutoConsole::Abstractions::PluginContext& context,
            std::string& errorMessage) override
        {
            (void)context;
            if (action != "echo")
            {
                errorMessage = "unknown action: " + action;
                return false;
            }

            auto textIt = actionArgs.find("text");
            if (textIt == actionArgs.end())
            {
                errorMessage = "echo requires text";
                return false;
            }

            errorMessage = "echo: " + textIt->second;
            return true;
        }
    };
}

extern "C" __declspec(dllexport) AutoConsole::Abstractions::IPlugin* create_plugin()
{
    return new EchoExternalPlugin();
}

extern "C" __declspec(dllexport) void destroy_plugin(AutoConsole::Abstractions::IPlugin* plugin)
{
    delete plugin;
}

extern "C" __declspec(dllexport) int get_plugin_api_version()
{
    return AutoConsole::Abstractions::PluginApiVersion;
}

extern "C" __declspec(dllexport) void DestroyPlugin(AutoConsole::Abstractions::IPlugin* plugin)
{
    destroy_plugin(plugin);
}

extern "C" __declspec(dllexport) int GetPluginApiVersion()
{
    return get_plugin_api_version();
}
